#include "audio_output_mock.hpp"
#include "lcs/cutscene_audio.hpp"
#include "sa_city_music.hpp"
#include "lcs/ending_tune_envelope.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try {
    lcs::EndingTuneEnvelope fade;
    check(!fade.service(1) && !fade.service(0) && !fade.service(4) && !fade.service(8),"Original frontend fade waits three serviced audio frames at cadence4");
    check(fade.service(12)==6 && !fade.service(12) && fade.service(16)==12,"Original fade advances6 once per serviced frame");
    for(uint32_t frame=20;frame<100;frame+=4)fade.service(frame);
    check(fade.volume()==100,"Original finale envelope saturates at authored100 rather than full127");
    lcs::EndingTuneEnvelope transactional;
    for(uint32_t frame=0;frame<12;frame+=4)check(transactional.service_accepted(frame,[](uint8_t){return true;}),"Latency service commits without output");
    check(!transactional.service_accepted(12,[](uint8_t gain){check(gain==6,"First gain is authored6");return false;}) && transactional.volume()==0,"Rejected output cannot publish fade progress");
    check(transactional.service_accepted(16,[](uint8_t gain){return gain==6;}) && transactional.volume()==6,"Next accepted output commits actual gain");
    lcs::MissionSpeech player;lcs::AudioSample sample;sample.frequency=22050;sample.pcm={0,0,1,0};
    auto invalid=sample;invalid.frequency=0;
    check(!player.play_owned_effect(invalid).entered && audio_mock::opens==0,"Invalid PCM refuses before device entry");
    const auto first=player.play_owned_effect(sample,lcs::AudioMix{80,63});
    check(audio_mock::effects==std::vector<char>{'V','W'},"Initial owned spatial mix is accepted before PCM is submitted, avoiding full-volume start");
    check(first.entered && first.accepted && first.voice->slot==3,"Owned effect begins in dedicated effect slot");
    check(player.owned_effect_finished(*first.voice)==false,"Accepted unfinished buffer is observed by matching generation");
    check(player.stop_owned_effect(*first.voice) && !player.owned_effect_finished(*first.voice),"Stop invalidates generation");
    const auto next=player.play_owned_effect(sample);
    const auto updated=player.update_owned_effect(*next.voice,{80,63},11025,true);
    check(updated.entered && updated.accepted && audio_mock::last_rate==32768,"Owned frequency updates use actual instance and16.16 authored ratio");
    const auto rates=audio_mock::rates;
    check(player.update_owned_effect(*next.voice,{70,53},0,false).accepted && audio_mock::rates==rates,"Unchanged frequency does not issue a fabricated rate write");
    audio_mock::support=WAVECAPS_VOLUME;
    check(!player.update_owned_effect(*next.voice,{80,63},22050,true).entered,"Unsupported stereo/rate device refuses before effects");
    audio_mock::support=WAVECAPS_VOLUME|WAVECAPS_LRVOLUME|WAVECAPS_PLAYBACKRATE;
    audio_mock::fail_rate=true;const auto before_volume=audio_mock::volumes;
    const auto failed_rate=player.update_owned_effect(*next.voice,{80,63},22050,true);
    check(failed_rate.entered && !failed_rate.accepted && audio_mock::volumes==before_volume,"Rate failure stops before later volume effect");audio_mock::fail_rate=false;
    audio_mock::fail_volume=true;const auto partial_mix=player.update_owned_effect(*next.voice,{80,63},22050,true);
    check(partial_mix.entered && !partial_mix.accepted,"Partial rate/volume failure cannot publish successful state");audio_mock::fail_volume=false;
    check(!player.update_owned_effect(*first.voice,{80,63},22050,true).entered,"Stale token cannot modify replacement voice");
    check(next.voice->generation!=first.voice->generation && !player.stop_owned_effect(*first.voice),"Stale token cannot stop reused slot");
    for(int i=1;i<8;++i)check(player.play_owned_effect(sample).accepted,"Eight dedicated slots accept independent effects");
    const auto opens=audio_mock::opens,resets=audio_mock::resets;
    check(!player.play_owned_effect(sample).entered && audio_mock::opens==opens && audio_mock::resets==resets,"Capacity refusal never evicts another effect");
    audio_mock::headers.back()->dwFlags|=WHDR_DONE;
    check(player.play_owned_effect(sample).accepted,"Completed effect can be reused");
    check(player.stop_owned_effect(*next.voice),"Current token retains stop ownership");
    audio_mock::fail_open=true;const auto failed=player.play_owned_effect(sample);
    check(failed.entered && !failed.accepted && !failed.voice,"Device failure retains entry without fabricated receipt");
    audio_mock::fail_open=false;audio_mock::fail_write=true;const auto partial=player.play_owned_effect(sample);
    check(partial.entered && !partial.accepted && !partial.voice,"Prepared buffer write failure cannot publish ownership");
    audio_mock::fail_write=false;audio_mock::support=WAVECAPS_VOLUME;const auto before_write=audio_mock::writes;
    const auto unsupported_start=player.play_owned_effect(sample,lcs::AudioMix{80,63});
    check(unsupported_start.entered && !unsupported_start.accepted && audio_mock::writes==before_write,"Unsupported initial spatial output closes without audible PCM submission");
    audio_mock::support=WAVECAPS_VOLUME|WAVECAPS_LRVOLUME|WAVECAPS_PLAYBACKRATE;audio_mock::fail_volume=true;
    const auto failed_start=player.play_owned_effect(sample,lcs::AudioMix{80,63});
    check(failed_start.entered && !failed_start.accepted && audio_mock::writes==before_write,"Initial mix refusal cannot start at default full volume");audio_mock::fail_volume=false;
    {
        lcs::MissionSpeech music;
        check(!music.play_owned_effect(sample,{},0).entered,"Zero loop count refuses before output");
        const auto loop=music.play_owned_effect(sample,lcs::AudioMix{0,63},7);
        check(loop.accepted && audio_mock::headers.back()->dwLoops==7 &&
            (audio_mock::headers.back()->dwFlags&(WHDR_BEGINLOOP|WHDR_ENDLOOP))==(WHDR_BEGINLOOP|WHDR_ENDLOOP),"Prepared PCM carries both loop boundaries and exact repeat count");
        audio_mock::fail_reset=true;
        check(!music.stop_owned_effect_checked(*loop.voice).accepted && music.owned_effect_finished(*loop.voice).has_value(),"Failed reset preserves owned buffer/token");audio_mock::fail_reset=false;
        audio_mock::fail_unprepare=true;
        check(!music.stop_owned_effect_checked(*loop.voice).accepted && music.owned_effect_finished(*loop.voice).has_value(),"Failed unprepare preserves owned buffer/token");audio_mock::fail_unprepare=false;
        audio_mock::fail_close=true;
        check(!music.stop_owned_effect_checked(*loop.voice).accepted && music.owned_effect_finished(*loop.voice).has_value(),"Failed close preserves owned buffer/token");audio_mock::fail_close=false;
        check(music.stop_owned_effect_checked(*loop.voice).accepted && !music.owned_effect_finished(*loop.voice),"Accepted stop retires token after driver releases buffer");
        check(!music.stop_owned_effect_checked(*loop.voice).entered,"Retired stop cannot enter driver");
    }
    for(auto fault:{&audio_mock::fail_reset,&audio_mock::fail_unprepare,&audio_mock::fail_close}) {
        lcs::MissionSpeech reuse;
        const auto old=reuse.play_owned_effect(sample);
        check(old.accepted,"Reuse fixture owns a real mock buffer");
        auto* header=audio_mock::headers.back();header->dwFlags|=WHDR_DONE;
        const auto data=header->lpData;const auto count=audio_mock::opens;
        *fault=true;
        const auto replacement=reuse.play_owned_effect(sample);
        check(replacement.entered && !replacement.accepted && !replacement.voice &&
            audio_mock::opens==count && header->lpData==data &&
            reuse.owned_effect_finished(*old.voice).has_value(),
            "Failed completed-buffer release must retain PCM/token and refuse replacement before opening output");
        check(!reuse.stop_owned_effect(*old.voice) && reuse.owned_effect_finished(*old.voice).has_value(),
            "Convenience stop must not discard ownership on driver failure");
        *fault=false;
        check(reuse.stop_owned_effect_checked(*old.voice).accepted,"Retained owner can retry actual release");
    }
    for(auto fault:{&audio_mock::fail_reset,&audio_mock::fail_unprepare,&audio_mock::fail_close}) {
        lcs::MissionSpeech ordinary;
        check(ordinary.play(0,sample),"Ordinary speech fixture starts");
        auto* header=audio_mock::headers.back();const auto data=header->lpData;
        const auto count=audio_mock::opens;*fault=true;
        check(!ordinary.play(0,sample) && audio_mock::opens==count && header->lpData==data,
            "Ordinary speech replacement cannot discard a driver-owned buffer on release failure");
        ordinary.stop(0);
        check(header->lpData==data && !ordinary.shutdown(),"Stop and shutdown preserve pending release for retry");
        *fault=false;check(ordinary.shutdown(),"Shutdown retry releases retained ordinary speech");
    }
    {
        lcs::MissionSpeech starting;
        audio_mock::fail_write=true;audio_mock::fail_unprepare=true;
        check(!starting.play_owned_effect(sample).accepted,"Failed write plus failed cleanup refuses ownership receipt");
        auto* header=audio_mock::last_prepared;const auto data=header->lpData;
        check((header->dwFlags&WHDR_PREPARED) && data && header->dwBufferLength==sample.pcm.size(),
            "Failed initial cleanup retains the exact prepared header and PCM");
        audio_mock::fail_write=false;const auto count=audio_mock::opens;
        check(!starting.play(3,sample) && audio_mock::opens==count && header->lpData==data,
            "Later submission cannot overwrite a failed initial cleanup");
        audio_mock::fail_unprepare=false;check(starting.shutdown(),"Unpublished failed start can be released by shutdown retry");
    }
    {
        auto dying=std::make_unique<lcs::MissionSpeech>();
        check(dying->play(0,sample),"Shutdown lifetime fixture starts");
        auto* header=audio_mock::headers.back();const auto data=header->lpData;
        audio_mock::fail_reset=true;dying.reset();
        check(header->lpData==data && header->dwBufferLength==sample.pcm.size() && data[2]==1,
            "Final driver refusal retains stable header and PCM for process lifetime");
        audio_mock::fail_reset=false;
    }
    {
        lcs::MissionSpeech loops;lcs::AudioSample clip;clip.frequency=22050;clip.channels=1;
        clip.pcm={10,11,20,21,30,31,40,41,50,51};
        auto before=audio_mock::writes;
        check(!loops.play_owned_loop(clip,3,8,{80,63}).entered && audio_mock::writes==before,"Misaligned authored loop rejects before driver entry");
        const auto base=audio_mock::headers.size();
        auto loop=loops.play_owned_loop(clip,2,8,{80,63});
        check(loop.accepted && loop.voice && audio_mock::headers.size()==base+3,"Intro plus two stable queue headers start one owned stream");
        check(audio_mock::last_flags==CALLBACK_EVENT && audio_mock::last_callback,"Native output requests actual event-driven completion");
        auto* intro=audio_mock::headers[base];auto* first=audio_mock::headers[base+1];auto* second=audio_mock::headers[base+2];
        check(intro->dwBufferLength==2 && intro->lpData[0]==10 && first!=second && first->lpData==second->lpData,"Intro occurs once; queue headers share immutable authored loop storage");
        check(first->dwBufferLength>=4410 && first->dwBufferLength%6==0 && !first->dwLoops && !(first->dwFlags&(WHDR_BEGINLOOP|WHDR_ENDLOOP)),"Streaming chunks contain complete periods, without a finite-repeat stand-in");
        for(size_t i=0;i<first->dwBufferLength;++i)check(uint8_t(first->lpData[i])==clip.pcm[2+i%6],"Authored range repeated exactly; tail excluded");
        const auto data=first->lpData;intro->dwFlags|=WHDR_DONE;
        check(loops.owned_effect_finished(*loop.voice)==false,"Intro completion does not finish live stream");
        audio_mock::completion_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        check(audio_mock::completion_event!=nullptr,"Owned completion synchronization fixture");
        first->dwFlags|=WHDR_DONE;before=audio_mock::writes;
        SetEvent(reinterpret_cast<HANDLE>(audio_mock::last_callback));
        check(WaitForSingleObject(audio_mock::completion_event,1000)==WAIT_OBJECT_0,"Production worker responds to WinMM completion event");
        check(loops.pump_loop_output() && audio_mock::writes==before+1,"Completion event and manual service cannot duplicate a queued buffer");
        CloseHandle(audio_mock::completion_event);audio_mock::completion_event=nullptr;
        before=audio_mock::writes;
        for(int i=0;i<1000;++i){first->dwFlags|=WHDR_DONE;second->dwFlags|=WHDR_DONE;check(loops.pump_loop_output(),"Stream replenishes completed buffers indefinitely");}
        check(audio_mock::writes==before+2000 && first->lpData==data && audio_mock::headers.back()==second,"Headers and bytes stay stable across repeated completion cycles");
        check(loops.update_owned_effect(*loop.voice,{70,63},22050,false).accepted,"Live loop mix remains owned after intro completion");
        check(loops.update_owned_distance(*loop.voice,90,80,60,63,22050,false).accepted && audio_mock::last_volume==DWORD(4128u|(4128u<<16)),"Actual owned driver mix consumes retail attenuated volume8 instead of mission speech attenuation");
        const auto mix_calls=audio_mock::volumes;
        check(!loops.update_owned_distance(*loop.voice,90,80,-1,63,22050,false).entered && audio_mock::volumes==mix_calls,"Unavailable distance never becomes a fabricated full-volume update");
        const auto source=lcs::continuous_sound_emission(0);
        check(source && loops.update_owned_motion(*loop.voice,*source,{30,0,343.f/30.f,30,false},63,12000).accepted && audio_mock::last_rate==41051,"Owned native output consumes source Doppler frequency13812 and real authored base rate");
        const auto motion_rates=audio_mock::rates;
        check(!loops.update_owned_motion(*loop.voice,*source,{30,0,343.f/30.f,0,false},63,12000).entered && audio_mock::rates==motion_rates,"Missing audio-frame timing refuses before device effects");
        check(loops.update_owned_spatial(*loop.voice,*source,{10,10,343.f/30.f,30,false},10,12000).accepted && (audio_mock::last_volume&65535)>(audio_mock::last_volume>>16),"Original camera-space positive-X pan curve reaches owned native stereo output");
        check(loops.update_owned_spatial(*loop.voice,*source,{10,10,343.f/30.f,30,false},-10,12000).accepted && (audio_mock::last_volume&65535)<(audio_mock::last_volume>>16),"Original negative-X pan curve reaches opposite stereo side");
        auto stale=*loop.voice;++stale.generation;
        const auto pauses=audio_mock::pauses,restarts=audio_mock::restarts;
        const auto pause_resets=audio_mock::resets,pause_writes=audio_mock::writes;
        check(!loops.pause_owned_effect(stale,true).entered && audio_mock::pauses==pauses,"Stale generation never pauses a replacement stream");
        audio_mock::fail_pause=true;
        const auto refused_pause=loops.pause_owned_effect(*loop.voice,true);
        check(refused_pause.entered && !refused_pause.accepted,"Driver pause failure cannot publish successful pause");audio_mock::fail_pause=false;
        check(loops.pause_owned_effect(*loop.voice,true).accepted,"Live stream accepts owned pause");
        const auto repeated_pause=loops.pause_owned_effect(*loop.voice,true);
        check(repeated_pause.accepted && !repeated_pause.entered && audio_mock::pauses==pauses+2,"Repeated state does not reissue native pause");
        audio_mock::fail_restart=true;check(!loops.pause_owned_effect(*loop.voice,false).accepted,"Resume refusal retains paused state");audio_mock::fail_restart=false;
        check(loops.pause_owned_effect(*loop.voice,false).accepted && audio_mock::restarts==restarts+2 && first->lpData==data && audio_mock::resets==pause_resets && audio_mock::writes==pause_writes,"Resume retains queued storage without resetting playback");
        check(loops.pause(loop.voice->slot,true) && loops.pause_owned_effect(*loop.voice,false).entered,"Raw slot pause and owned resume share accepted driver state");
        audio_mock::fail_write=true;first->dwFlags|=WHDR_DONE;
        check(!loops.pump_loop_output() && !loops.owned_effect_finished(*loop.voice),"Entered queue failure becomes observable, never fabricated completion");
        audio_mock::fail_write=false;before=audio_mock::writes;
        check(loops.pump_loop_output() && audio_mock::writes==before,"Faulted stream never replays uncertain write");
        audio_mock::fail_reset=true;check(!loops.stop_owned_effect(*loop.voice) && first->lpData==data && uint8_t(data[0])==20,"Reset refusal retains both published queue headers and PCM");audio_mock::fail_reset=false;
        check(loops.stop_owned_effect(*loop.voice),"Owned release retries safely after driver recovery");
        before=audio_mock::writes;
        auto controlled=loops.play_owned_loop(clip,0,8,{80,63},lcs::ContinuousVoiceStart{12000,0.5f,{false,127}});
        const auto physical_rate=[&](uint32_t hz){return DWORD((uint64_t(hz)*65536u+clip.frequency/2)/clip.frequency);};
        check(controlled.accepted && audio_mock::last_rate==physical_rate(9000) && audio_mock::writes_at_rate==before,
              "Initial unscaled12000 cache applies slow-motion9000 before publishing first PCM");
        auto moving=*lcs::continuous_sound_emission(0);moving.speed=0;
        const lcs::ContinuousSoundMotion stable{10,10,343.f/30.f,30,false};
        check(loops.update_owned_continuous_frame(*controlled.voice,moving,stable,0,30000,0.5f,{false,127}).accepted && audio_mock::last_rate==physical_rate(13500),
              "First frame slews logical12000 to18000, then scales physical13500");
        check(loops.update_owned_continuous_frame(*controlled.voice,moving,stable,0,30000,0.5f,{false,127}).accepted && audio_mock::last_rate==physical_rate(18000),
              "Second frame uses unscaled18000 cache and advances to24000");
        check(loops.update_owned_continuous_frame(*controlled.voice,moving,stable,0,30000,0.5f,{false,127}).accepted && audio_mock::last_rate==physical_rate(22500),
              "Third frame reaches logical30000");
        const auto frame_rates=audio_mock::rates;
        check(!loops.update_owned_effect(*controlled.voice,{80,63},12000,true).entered && audio_mock::rates==frame_rates,
              "Raw pitch primitive cannot bypass continuous unscaled-cache ownership");
        check(loops.update_owned_continuous_frame(*controlled.voice,moving,stable,0,30000,1.f,{false,127}).accepted && audio_mock::rates==frame_rates,
              "Time-scale change alone preserves original unchanged-frequency write gate");
        auto stale_controlled=*controlled.voice;++stale_controlled.generation;
        check(!loops.update_owned_continuous_frame(stale_controlled,moving,stable,0,12000,1.f,{false,127}).entered && audio_mock::rates==frame_rates,
              "Stale controller cannot change a replacement channel");
        audio_mock::fail_volume=true;
        const auto fault=loops.update_owned_continuous_frame(*controlled.voice,moving,stable,0,12000,1.f,{false,127});
        audio_mock::fail_volume=false;
        check(fault.entered && !fault.accepted && !loops.owned_effect_finished(*controlled.voice),"Partial rate/volume failure quarantines continuous channel");
        check(!loops.update_owned_continuous_frame(*controlled.voice,moving,stable,0,12000,1.f,{false,127}).entered && loops.stop_owned_effect(*controlled.voice),
              "Uncertain frame is never replayed and owned release remains available");
        before=audio_mock::writes;audio_mock::fail_rate=true;
        check(!loops.play_owned_loop(clip,0,8,{80,63},lcs::ContinuousVoiceStart{12000,1.f,{false,127}}).accepted && audio_mock::writes==before,
              "Initial pitch failure publishes no PCM");audio_mock::fail_rate=false;
        auto capped_channel=loops.play_owned_loop(clip,0,8,{80,63},lcs::ContinuousVoiceStart{50000,1.f,{false,127}});
        check(capped_channel.accepted && audio_mock::last_rate==physical_rate(44000),"Initial hardware pitch honors retail44000 cap");
        check(loops.update_owned_continuous_frame(*capped_channel.voice,moving,stable,0,50000,0.5f,{false,127}).accepted &&
              audio_mock::last_rate==physical_rate(44000),"Unchanged logical50000 is not confused with hardware44000 cache");
        check(loops.update_owned_continuous_frame(*capped_channel.voice,moving,stable,0,51000,0.5f,{false,127}).accepted &&
              audio_mock::last_rate==physical_rate(38250),"Slow-motion scaling precedes hardware cap");
        check(loops.stop_owned_effect(*capped_channel.voice),"Capped channel releases by generation");
        auto gain_channel=loops.play_owned_loop(clip,0,8,{20,63},lcs::ContinuousVoiceStart{12000,1.f,{true,63}});
        const auto stereo_gain=[](uint32_t volume){return DWORD(65535.0*double(volume)/127.0);};
        check(gain_channel.accepted && (audio_mock::last_volume&65535)==stereo_gain(19),"Initial logical gain20 becomes double/global gain19 before PCM");
        check(loops.update_owned_continuous_frame(*gain_channel.voice,moving,stable,0,12000,1.f,{true,63}).accepted &&
              (audio_mock::last_volume&65535)==stereo_gain(29),"Frame gain slews logical20 to30 before double/global conversion29");
        check(loops.update_owned_continuous_frame(*gain_channel.voice,moving,stable,0,12000,1.f,{false,127}).accepted &&
              (audio_mock::last_volume&65535)==stereo_gain(40),"Next frame retains logical30 instead of previously emitted29");
        const auto gain_writes=audio_mock::volumes;
        check(!loops.update_owned_continuous_frame(*gain_channel.voice,moving,stable,0,12000,1.f,{false,128}).entered &&
              audio_mock::volumes==gain_writes,"Unknown global gain range is refused without invented output");
        check(loops.stop_owned_effect(*gain_channel.voice),"Gain-owned channel releases normally");
        lcs::AudioRecord alarm_record{0,uint32_t(clip.pcm.size()),clip.frequency,0,-1};
        before=audio_mock::writes;
        auto alarm_channel=loops.play_owned_continuous_sample(clip,alarm_record,{80,63},{12000,1.f,{false,127}});
        auto* alarm_header=audio_mock::headers.back();
        check(alarm_channel.accepted && audio_mock::writes==before+1 && alarm_header->dwBufferLength==clip.pcm.size() &&
              !(alarm_header->dwFlags&(WHDR_BEGINLOOP|WHDR_ENDLOOP)),"Retail non-looping VAG alarm stays one actual PCM submission");
        alarm_header->dwFlags|=WHDR_DONE;
        check(loops.owned_effect_finished(*alarm_channel.voice)==true &&
              !loops.update_owned_continuous_frame(*alarm_channel.voice,moving,stable,0,12000,1.f,{false,127}).entered,
              "Actual alarm completion is exposed to source frame reacquisition, not hidden by infinite looping");
        check(loops.stop_owned_effect(*alarm_channel.voice),"Completed alarm receipt is released before reacquiring source request");
        before=audio_mock::writes;
        lcs::AudioRecord authored{0,uint32_t(clip.pcm.size()),clip.frequency,0,int32_t(clip.pcm.size()-2)};
        auto invalid_authored=authored;invalid_authored.loop_end=int32_t(clip.pcm.size());
        check(!loops.play_owned_continuous_sample(clip,invalid_authored,{80,63},{12000,1.f,{false,127}}).entered && audio_mock::writes==before,
              "Malformed inclusive SDT boundary refuses before owned output");
        auto pause_channel=loops.play_owned_continuous_sample(clip,authored,{80,63},{12000,1.f,{false,127}});
        check(pause_channel.accepted,"Source channel exists before user pause");
        const auto driver_pauses=audio_mock::pauses,driver_restarts=audio_mock::restarts;
        audio_mock::fail_reset=true;
        check(!loops.service_continuous_user_pause(true),"Refused pause release remains pending");
        before=audio_mock::writes;
        check(!loops.play_owned_loop(clip,0,8,{80,63},lcs::ContinuousVoiceStart{12000,1.f,{false,127}}).entered &&
              !loops.update_owned_continuous_frame(*pause_channel.voice,moving,stable,0,12000,1.f,{false,127}).entered && audio_mock::writes==before,
              "Paused source cannot queue or update a physical channel");
        check(!loops.service_continuous_user_pause(false),"Unpause cannot bypass an unresolved driver release");
        audio_mock::fail_reset=false;
        check(loops.service_continuous_user_pause(true) && !loops.owned_effect_finished(*pause_channel.voice),
              "Pause release retry retires old receipt instead of preserving playback cursor");
        check(loops.service_continuous_user_pause(false) && audio_mock::pauses==driver_pauses && audio_mock::restarts==driver_restarts,
              "Source user pause stops channels; it never calls waveOutPause/Restart");
        auto resumed_channel=loops.play_owned_loop(clip,0,8,{80,63},lcs::ContinuousVoiceStart{12000,1.f,{false,127}});
        check(resumed_channel.accepted && (resumed_channel.voice->slot!=pause_channel.voice->slot ||
              resumed_channel.voice->generation!=pause_channel.voice->generation),"Unpaused source reacquires a fresh physical receipt");
        check(loops.stop_owned_effect(*resumed_channel.voice),"Resumed channel releases normally");
        std::vector<lcs::OwnedEffectVoice> completed_alarms;
        for(int i=0;i<8;++i){
            auto owned_alarm=loops.play_owned_continuous_sample(clip,alarm_record,{80,63},{12000,1.f,{false,127}});
            check(owned_alarm.accepted,"Completed source alarms retain dedicated owned capacity");
            completed_alarms.push_back(*owned_alarm.voice);audio_mock::headers.back()->dwFlags|=WHDR_DONE;
        }
        before=audio_mock::writes;
        check(!loops.play_effect(clip) && !loops.play_owned_effect(clip).entered &&
              !loops.play_owned_continuous_sample(clip,alarm_record,{80,63},{12000,1.f,{false,127}}).entered && audio_mock::writes==before,
              "DONE continuous alarms remain owned until source queue explicitly retires them; no allocator steals their receipt");
        for(const auto& owned_alarm:completed_alarms)
            check(loops.owned_effect_finished(owned_alarm)==true && loops.stop_owned_effect(owned_alarm),"Original completion receipt survives competing allocation and releases safely");
        for(int i=0;i<8;++i)check(loops.play_owned_loop(clip,0,8,{80,63}).accepted,"Dedicated owned capacity accepts independent streams");
        before=audio_mock::writes;check(!loops.play_effect(clip) && audio_mock::writes==before,"Ordinary effect cannot evict an owned loop");
        check(loops.shutdown(),"All stream headers are released before storage shutdown");
        auto native_music=std::make_shared<lcs::Ps2MusicStream>(std::vector<uint8_t>(0x8000),0);
        auto decoded_music=loops.play_owned_music_stream(native_music,{90,63});
        check(decoded_music.accepted && native_music->encoded_position()==0,"Real ADPCM decoder supplies two stereo blocks to owned output and wraps");
        check(loops.stop_owned_effect(*decoded_music.voice),"Real decoded music channel releases without affecting script sounds");
        size_t decoded_blocks=0;bool decoder_failure=false;
        auto producer=[&](){
            if(decoder_failure)throw std::runtime_error("decoder fault");
            lcs::AudioSample block;block.frequency=32000;block.channels=2;
            block.pcm.assign(57344,uint8_t(++decoded_blocks));return block;
        };
        auto streaming=loops.play_owned_pcm_stream(producer,{90,63});
        check(streaming.accepted && decoded_blocks==2,"Owned music queues two distinct decoded blocks");
        audio_mock::position_samples=37;
        check(loops.owned_stream_position(*streaming.voice)==37 && decoded_blocks==2 && audio_mock::position_handle!=nullptr,
              "Music position comes from owned driver frames, not two decoded/read-ahead blocks");
        audio_mock::position_samples=0xFFFFFFFFu;
        check(loops.owned_stream_position(*streaming.voice)==0xFFFFFFFFu,"Native DWORD sample counter is preserved without guessed wrap extension");
        audio_mock::position_type=TIME_MS;
        check(!loops.owned_stream_position(*streaming.voice),"Driver format substitution cannot be silently treated as sample frames");
        audio_mock::position_type=TIME_SAMPLES;audio_mock::fail_position=true;
        check(!loops.owned_stream_position(*streaming.voice),"Unavailable device playhead stays unavailable");audio_mock::fail_position=false;
        auto wrong_stream=*streaming.voice;++wrong_stream.generation;const auto position_queries=audio_mock::position_queries;
        check(!loops.owned_stream_position(wrong_stream) && audio_mock::position_queries==position_queries,"Stale stream token never queries another channel's playhead");
        auto* streamed_header=audio_mock::headers[audio_mock::headers.size()-2];
        auto* streamed_data=streamed_header->lpData;
        for(int i=0;i<20;++i){
            streamed_header->dwFlags|=WHDR_DONE;
            check(loops.pump_loop_output() && streamed_header->lpData==streamed_data &&
                  uint8_t(streamed_data[0])==uint8_t(decoded_blocks),"Streaming refill decodes into stable driver-owned storage only after DONE");
        }
        decoder_failure=true;streamed_header->dwFlags|=WHDR_DONE;before=audio_mock::writes;
        check(!loops.pump_loop_output() && audio_mock::writes==before && !loops.owned_effect_finished(*streaming.voice),
              "Decoder failure quarantines without replaying old PCM or claiming completion");
        decoder_failure=false;audio_mock::fail_reset=true;
        check(!loops.stop_owned_effect(*streaming.voice) && streamed_header->lpData==streamed_data,
              "Release refusal retains decoder and published stream PCM");
        audio_mock::fail_reset=false;
        check(loops.stop_owned_effect(*streaming.voice),"Decoded stream releases by generation after recovery");
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
        check(dialogue.play(0,sample) && dialogue.set_mix(0,{0,20}),"Distant dialogue can reach its authored silent gain");
        dialogue.stop(0);
        check(dialogue.play(0,sample) && audio_mock::last_volume==0xFFFFFFFFu,
              "A new unpositioned mission line explicitly restores centered full gain before its PCM starts");
    }
    std::cout<<"PASS: owned effect/stream generation, authored loops, stable queue buffers, completion and device faults; no device/game executed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
