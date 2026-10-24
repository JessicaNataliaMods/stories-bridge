#include "audio_output_mock.hpp"
#include "lcs/cutscene_audio.hpp"
#include "sa_city_music.hpp"
#include "lcs/ending_tune_envelope.hpp"
#include <iostream>
#include <stdexcept>
#include <set>
#include <cstring>
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static lcs::AudioSample clip(size_t frames=4){lcs::AudioSample s;s.frequency=20000;s.channels=1;for(size_t i=0;i<frames;++i){int16_t v=int16_t(1000+(i%4)*1000);auto* p=reinterpret_cast<uint8_t*>(&v);s.pcm.insert(s.pcm.end(),p,p+2);}return s;}
static int16_t pcm(const std::vector<uint8_t>& bytes,size_t frame,size_t channel=0){int16_t v;std::memcpy(&v,bytes.data()+(frame*2+channel)*2,2);return v;}
static std::vector<WAVEHDR*> queued_since(size_t start){std::set<WAVEHDR*> unique;std::vector<WAVEHDR*> result;for(size_t i=start;i<audio_mock::headers.size();++i)if(unique.insert(audio_mock::headers[i]).second)result.push_back(audio_mock::headers[i]);return result;}
static void complete(const std::vector<WAVEHDR*>& headers){for(auto* h:headers)h->dwFlags|=WHDR_DONE;}
int main(int argc,char** argv){try{
    lcs::EndingTuneEnvelope fade;
    check(!fade.service(1) && !fade.service(0) && !fade.service(4) && !fade.service(8),"Original frontend fade waits three serviced audio frames at cadence4");
    check(fade.service(12)==6 && !fade.service(12) && fade.service(16)==12,"Original fade advances6 once per serviced frame");
    for(uint32_t frame=20;frame<100;frame+=4)fade.service(frame);
    check(fade.volume()==100,"Original finale envelope saturates at authored100 rather than full127");
    lcs::EndingTuneEnvelope transactional;
    for(uint32_t frame=0;frame<12;frame+=4)check(transactional.service_accepted(frame,[](uint8_t){return true;}),"Latency service commits without output");
    check(!transactional.service_accepted(12,[](uint8_t gain){check(gain==6,"First gain is authored6");return false;}) && transactional.volume()==0,"Rejected output cannot publish fade progress");
    check(transactional.service_accepted(16,[](uint8_t gain){return gain==6;}) && transactional.volume()==6,"Next accepted output commits actual gain");

    auto sample=clip();
    {
        lcs::MissionSpeech output;
        auto invalid=sample;invalid.frequency=0;const auto opens=audio_mock::opens;
        check(!output.play_owned_effect(invalid).entered && audio_mock::opens==opens,"Invalid PCM refuses before driver entry");
        const auto first=output.play_owned_effect(sample,lcs::AudioMix{63,0});
        check(first.accepted && first.voice->slot==3,"Owned effect begins in dedicated slot");
        check(pcm(audio_mock::pcm_writes.back(),0)==496 && pcm(audio_mock::pcm_writes.back(),0,1)==0,"Initial volume and pan are applied to PCM before submission");
        check(output.owned_effect_finished(*first.voice)==false,"Queued PCM is not reported complete before driver completion");
        check(output.stop_owned_effect(*first.voice) && !output.owned_effect_finished(*first.voice),"Release invalidates generation");
        const auto next=output.play_owned_effect(sample);check(next.accepted && next.voice->generation!=first.voice->generation,"Reused slot has new generation");
        check(!output.stop_owned_effect(*first.voice) && !output.update_owned_effect(*first.voice,{50,63},10000,true).entered,"Stale owner cannot stop or retune new voice");
        audio_mock::support=0;audio_mock::fail_volume=true;audio_mock::fail_rate=true;
        check(output.update_owned_effect(*next.voice,{70,20},10000,true).accepted,"Software gain/pitch works without driver gain/rate capabilities");
        check(audio_mock::rates==0 && audio_mock::volumes==0,"Neither driver gain nor playback-rate controls are used");
        audio_mock::support=WAVECAPS_VOLUME|WAVECAPS_LRVOLUME|WAVECAPS_PLAYBACKRATE;audio_mock::fail_volume=false;audio_mock::fail_rate=false;
        for(int i=1;i<8;++i)check(output.play_owned_effect(sample).accepted,"Dedicated slots accept independent voices");
        const auto count=audio_mock::opens;
        check(!output.play_owned_effect(sample).entered && audio_mock::opens==count,"Capacity refusal never evicts a live voice");
        audio_mock::headers.back()->dwFlags|=WHDR_DONE;
        check(output.play_owned_effect(sample).accepted,"Observed completion permits slot reuse");
    }
    {
        lcs::MissionSpeech output;
        audio_mock::fail_open=true;auto open=output.play_owned_effect(sample);check(open.entered && !open.accepted && !open.voice,"Open failure publishes no receipt");audio_mock::fail_open=false;
        audio_mock::fail_write=true;auto write=output.play_owned_effect(sample);check(write.entered && !write.accepted && !write.voice,"Write failure publishes no receipt");audio_mock::fail_write=false;
        audio_mock::fail_restart=true;auto restart=output.play_owned_effect(sample);check(restart.entered && !restart.accepted,"Prefill restart failure refuses ownership");audio_mock::fail_restart=false;
        const auto owned=output.play_owned_effect(sample);check(owned.accepted,"Driver recovers");
        audio_mock::fail_reset=true;check(!output.stop_owned_effect(*owned.voice) && !output.owned_effect_finished(*owned.voice),"Reset refusal preserves uncertain driver ownership");audio_mock::fail_reset=false;
        audio_mock::fail_unprepare=true;check(!output.stop_owned_effect(*owned.voice),"Unprepare refusal cannot free published PCM");audio_mock::fail_unprepare=false;
        audio_mock::fail_close=true;check(!output.stop_owned_effect(*owned.voice),"Close refusal retains handle/token");audio_mock::fail_close=false;
        check(output.stop_owned_effect(*owned.voice),"Release retries the same token after driver recovery");
    }
    {
        lcs::MissionSpeech output;auto long_clip=clip(10000);const auto start=audio_mock::headers.size();
        auto owned=output.play_owned_effect(long_clip);check(owned.accepted,"Finite dialogue stream starts");auto headers=queued_since(start);check(headers.size()==3,"Finite speech is queued in bounded blocks");
        const auto data=headers.front()->lpData;
        for(int i=0;i<2;++i){complete(headers);check(output.pump_loop_output(),"Finite source refills completed blocks");check(!*output.owned_effect_finished(*owned.voice),"Refilling does not invent completion");}
        check(headers.front()->lpData==data,"Published PCM allocation stays stable across refill");
        for(int i=0;i<5 && !*output.owned_effect_finished(*owned.voice);++i){complete(headers);check(output.pump_loop_output(),"Finite source drains");}
        check(*output.owned_effect_finished(*owned.voice),"Completion requires final queued data to finish");
    }
    {
        lcs::MissionSpeech output;const auto start=audio_mock::headers.size();
        auto loop=output.play_owned_loop(sample,2,8,{127,63});check(loop.accepted,"Authored region with prefix starts");auto headers=queued_since(start);
        check(pcm(audio_mock::pcm_writes[audio_mock::pcm_writes.size()-3],0)==1000 && pcm(audio_mock::pcm_writes[audio_mock::pcm_writes.size()-3],4)==2000,"Prefix plays once then authored loop begins");
        check(output.update_owned_effect(*loop.voice,{127,63},10000,true).accepted,"Pitch changes in software without resetting phase");
        headers.front()->dwFlags|=WHDR_DONE;check(output.pump_loop_output(),"Completed loop block refills");
        check(pcm(audio_mock::pcm_writes.back(),0)==4000 && pcm(audio_mock::pcm_writes.back(),1)==3000,"Resampling preserves loop cursor and interpolates across loop wrap");
        check(output.update_owned_effect(*loop.voice,{0,63},0,false).accepted,"One owned loop may be muted");headers[1]->dwFlags|=WHDR_DONE;check(output.pump_loop_output(),"Muted loop refills");
        check(pcm(audio_mock::pcm_writes.back(),0)==0,"Mute is applied only to that loop PCM");
        const auto other=output.play_owned_effect(sample);check(other.accepted && pcm(audio_mock::pcm_writes.back(),0)==1000,"Another voice remains audible while first loop is muted");
        check(output.pause_owned_effect(*loop.voice,true).accepted,"Owned pause is applied to matching handle");const auto writes=audio_mock::writes;complete(headers);check(output.pump_loop_output() && audio_mock::writes==writes,"Paused voice cannot consume queued source");
        audio_mock::fail_restart=true;check(!output.pause_owned_effect(*loop.voice,false).accepted,"Restart refusal does not publish resumed state");audio_mock::fail_restart=false;
        check(output.pause_owned_effect(*loop.voice,false).accepted && output.pump_loop_output(),"Resume refills retained PCM");
        check(output.stop_owned_effect(*loop.voice) && output.stop_owned_effect(*other.voice),"Independent voice release");
    }
    {
        lcs::MissionSpeech output(lcs::OwnedVoiceProfile::source_audio);lcs::AudioRecord record;record.size=uint32_t(sample.pcm.size());record.frequency=sample.frequency;record.loop_start=0;record.loop_end=int32_t(sample.pcm.size()-2);
        auto continuous=output.play_owned_continuous_sample(sample,record,{80,63},{12000,1.f,{false,127}});check(continuous.accepted,"Source continuous sample uses authored loop span");
        check(!output.update_owned_effect(*continuous.voice,{70,63},10000,true).entered,"Raw update cannot desynchronize continuous channel caches");
        auto emission=*lcs::continuous_sound_emission(2);lcs::ContinuousSoundMotion motion{10,10,343.f/30.f,30,false};
        check(output.update_owned_continuous_frame(*continuous.voice,emission,motion,0,12000,1.f,{false,127}).accepted,"Atomic source frequency/volume controller remains accepted");
        check(output.service_continuous_user_pause(true) && !output.owned_effect_finished(*continuous.voice),"User pause releases physical source channel generation");
        check(!output.play_owned_continuous_sample(sample,record,{80,63},{12000,1.f,{false,127}}).entered,"Paused source cannot reacquire");
        check(output.service_continuous_user_pause(false),"Source resumes without fabricating a channel");
        record.loop_end=-1;std::vector<lcs::OwnedEffectVoice> finite;
        for(int i=0;i<40;++i){auto one=output.play_owned_continuous_sample(sample,record,{80,63},{12000,1.f,{false,127}});check(one.accepted,"Source generic channels retain bounded independent ownership");finite.push_back(*one.voice);audio_mock::headers.back()->dwFlags|=WHDR_DONE;}
        check(!output.play_owned_continuous_sample(sample,record,{80,63},{12000,1.f,{false,127}}).entered,"Completed source sentinel clips remain reserved until owner releases");
        for(const auto& voice:finite)check(output.stop_owned_effect(voice),"Sentinel clip ownership releases explicitly");
    }
    {
        lcs::MissionSpeech output;size_t blocks=0;bool fail=false;const auto start=audio_mock::headers.size();
        auto stream=output.play_owned_pcm_stream([&]{if(fail)throw std::runtime_error("decoder failure");++blocks;lcs::AudioSample block;block.frequency=32000;block.channels=2;block.pcm.resize(57344);for(size_t i=0;i<block.pcm.size();i+=2){int16_t v=int16_t(blocks*1000);std::memcpy(block.pcm.data()+i,&v,2);}return block;},{127,63});
        check(stream.accepted && blocks==1,"Stream owns one decoder block without inventing playback position");auto headers=queued_since(start);
        audio_mock::position_type=TIME_SAMPLES;audio_mock::position_samples=32000;
        check(output.owned_stream_position(*stream.voice)==32000,"Music position uses observed driver samples, not decoder read-ahead");
        audio_mock::position_type=TIME_BYTES;check(!output.owned_stream_position(*stream.voice),"Invalid position type refuses a fabricated clock");audio_mock::position_type=TIME_SAMPLES;
        for(int i=0;i<3;++i){complete(headers);check(output.pump_loop_output(),"Source decoder feeds bounded mixed blocks");}
        check(blocks>=2 && pcm(audio_mock::pcm_writes.back(),0)>=2000,"PCM stream crosses source block boundary correctly");
        fail=true;bool accepted=true;for(int i=0;i<8 && accepted;++i){complete(headers);accepted=output.pump_loop_output();}
        check(!accepted && !output.owned_effect_finished(*stream.voice),"Decoder fault never pretends stream completed successfully");
        check(output.stop_owned_effect(*stream.voice),"Decoder fault retains releasable generation");
    }
    {
        lcs::NativeCityMusic city;
        const auto source=argc>1?std::filesystem::path(argv[1]):std::filesystem::path(__FILE__).parent_path().parent_path()/"local/originals/lcs-ps2/AUDIO/MUSIC/CITY.VB";
        lcs::ContinuousSourceRandom shared(180371);
        const auto table=shared.audio_table();
        std::array<uint32_t,10> lengths;lengths.fill(600320);
        const auto initial=lcs::capture_continuous_music_initialization(shared,table,lengths);
        city.load(source,*initial);
        check(city.track_state(17).position==56 && city.track_state(18).position==68 &&
              city.track_state(17).service_age==0 && city.track_state(18).service_age==0,
              "Original initialization arithmetic reaches actual CITY controller without fabricated zero positions");
        check(city.track_state(17).length==100 && city.track_state(18).length==100,
              "Retail non-radio manager length100 is preserved independently of encoded duration600320");
        check(city.ready() && !city.playing_track() && !city.pending_track(),"CITY loader requires explicit observed source state and owns no playing track before a request");
        check(city.configure(41,255)==lcs::ContinuousMusicConfiguration::configured && city.track_state(18).position==286847,
              "Configuration mutates real controller position state without starting an audio track");
        check(city.request(18) && city.pending_track()==18 && !city.playing_track(),"Actual source request is pending before output transition");
        check(city.configure(40,0)==lcs::ContinuousMusicConfiguration::next_gated && city.track_state(18).position==286847,
              "Actual pending controller track gates script configuration");
        bool source_marker=true;
        check(city.service_selection({80,63},source_marker) && city.playing_track()==18 && !city.pending_track() && !source_marker,
              "Pending source CITY track reaches original ADPCM resource and owned driver queue; observed source marker cleared");
        check(city.configure(40,0)==lcs::ContinuousMusicConfiguration::playing_gated && city.track_state(18).position==286847,
              "Actual owned playing track gates configuration without fabricated never-playing state");
        check(city.configure(43,5)==lcs::ContinuousMusicConfiguration::configured && city.track_state(17).position==5,
              "Inactive strip track remains independently configurable");
        audio_mock::position_samples=32000;audio_mock::position_type=TIME_SAMPLES;
        check(city.request(17) && city.service_selection({80,63},source_marker) && city.playing_track()==17 && city.track_state(18).position==1000,
              "Owned track transition saves observed driver position, releases old receipt and consumes next source track");
        city.advance_service_ages();check(city.track_state(17).service_age==1 && city.track_state(18).service_age==1,
              "Explicit source music service increments both real track ages once");
        audio_mock::fail_reset=true;
        check(!city.shutdown(),"Refused CITY release remains owned");audio_mock::fail_reset=false;
        check(city.shutdown() && !city.playing_track(),"CITY release retries without losing published decoder storage");
    }

    {
        lcs::MissionSpeech dialogue;
        check(dialogue.play(0,clip(10000)) && dialogue.set_mix(0,{0,20}),"Positioned speech can request silence independently");dialogue.stop(0);
        check(dialogue.play(0,sample) && pcm(audio_mock::pcm_writes.back(),0)==1000,"New unpositioned line starts at its own full PCM gain");
    }
    check(audio_mock::volumes==0 && audio_mock::rates==0,"All dialogue, CITY, finite, continuous and streamed tests avoid driver gain/rate writes");
    std::cout<<"PASS: owned software PCM mixing/resampling, independent gains, finite completion, authored loops, stream clock, generation and driver faults; no device/game executed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
