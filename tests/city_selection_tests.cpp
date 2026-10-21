#include "sa_city_selection.hpp"
#include "audio_output_mock.hpp"
#include <iostream>
#include <stdexcept>

static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}

int main(int argc,char** argv){try {
    for(int32_t area=0;area<18;++area){
        const auto track=lcs::city_track_for_retail_area(area);
        if(area==5)check(track==17,"Retail area5 selects CITY track17");
        else if(area==17)check(track==18,"Retail area17 selects CITY track18");
        else check(!track,"Other retail areas remain on non-CITY music");
    }
    check(!lcs::city_track_for_retail_area(-1) && !lcs::city_track_for_retail_area(18),
          "Out-of-range source areas are delegated instead of inferred");

    lcs::NativeCityMusic unavailable;
    check(lcs::observe_retail_area_music_selection(unavailable,0)==
              lcs::CityAreaMusicSelection::sa_native,
          "Non-CITY area leaves music selection with SA");
    check(lcs::observe_retail_area_music_selection(unavailable,5)==
              lcs::CityAreaMusicSelection::controller_refused,
          "CITY request fails closed if the owned CITY resource is unavailable");
    check(!unavailable.playing_track() && !unavailable.pending_track(),
          "Area observation never fabricates a current or pending track");
    check(argc==2,"Original CITY.VB argument required");
    lcs::NativeCityMusic music;music.load(argv[1],{0,0,100},{0,0,100});
    const auto before_opens=audio_mock::opens;
    check(music.configure(40,19)==lcs::ContinuousMusicConfiguration::configured && music.track_state(18).position==8659,
          "402 configuration consumes the actual supplied random word before selection");
    check(lcs::observe_retail_area_music_selection(music,17)==lcs::CityAreaMusicSelection::city_track_18_pending &&
          music.pending_track()==18 && !music.playing_track() && audio_mock::opens==before_opens,
          "Source area stages a request without claiming playback or entering output");
    bool start=true;
    check(music.service_selection({127,63},start) && music.playing_track()==18 && !music.pending_track() && !start,
          "Real music service promotes playing only from an owned output receipt");
    check(music.configure(41,90)==lcs::ContinuousMusicConfiguration::playing_gated && music.track_state(18).position==8659,
          "A playing CITY track gates later configuration without resetting its saved position");
    check(music.service_output(true,{80,63}) && audio_mock::pauses>0 && music.service_output(false,{70,63}),
          "Pause and gain changes reach the same owned stream");
    audio_mock::position_samples=160000;
    check(lcs::observe_retail_area_music_selection(music,5)==lcs::CityAreaMusicSelection::city_track_17_pending &&
          music.service_selection({127,63},start) && music.playing_track()==17 && music.track_state(18).position==5000,
          "Track transition retains the observed output playhead instead of decoder read-ahead");
    check(lcs::observe_retail_area_music_selection(music,0)==lcs::CityAreaMusicSelection::sa_native &&
          music.service_selection({127,63},start) && !music.playing_track() && music.shutdown(),
          "Leaving CITY areas retires only the owned CITY stream");
    lcs::NativeCityMusic failing;failing.load(argv[1],{0,0,100},{0,0,100});
    check(failing.request(17) && failing.service_selection({127,63},start),"A second owned controller starts for failure recovery");
    check(failing.request_native_selection(),"Retirement is queued before an injected driver refusal");
    audio_mock::fail_reset=true;
    check(!failing.service_selection({127,63},start) && !failing.ready() && failing.playing_track()==17 && !failing.shutdown(),
          "A refused stop retains its owned playing identity and refuses replacement");
    audio_mock::fail_reset=false;
    check(failing.shutdown() && !failing.playing_track(),"Later checked shutdown retires exactly the retained voice");
    std::cout<<"PASS: retail area-to-CITY selection and owned stream transitions; mocked output, no game/audio device\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
