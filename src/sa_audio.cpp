#include "sa_audio.hpp"
#include "lcs/continuous_sound.hpp"
#include "lcs/cutscene_audio.hpp"
#include <algorithm>
#include <stdexcept>
#ifdef LCS_ISOLATED_AUDIO_OUTPUT_TEST
#include "audio_output_mock.hpp"
#endif
namespace lcs {
MissionSpeech::MissionSpeech(OwnedVoiceProfile profile)
    : voices_(profile==OwnedVoiceProfile::source_audio?43:11),
      continuous_volumes_(voices_.size(),0) {
    // The source audio service has40 generic channels; the usual owned-effects
    // profile keeps its existing8. Slots0..2 remain reserved in both profiles.
    // Individual Voice allocations keep published WinMM header addresses stable.
    for(auto& voice:voices_)voice=std::make_unique<Voice>();
}
MissionSpeech::~MissionSpeech(){
    loop_stopping_=true;
    if(loop_event_)SetEvent(loop_event_);
    if(loop_worker_.joinable())loop_worker_.join();
    // A failed driver release cannot free a still-published WAVEHDR/PCM block.
    // Retain only that block for process lifetime rather than cause use-after-free.
    bool retained=false;
    for(size_t i=0;i<voices_.size();++i)if(!stop_unlocked(i)){retained=true;voices_[i].release();}
    // A refused streaming driver may still publish this callback event.
    if(loop_event_ && !retained)CloseHandle(loop_event_);
}
bool MissionSpeech::shutdown(){
    std::lock_guard<std::mutex> guard(mutex_);bool accepted=true;
    for(size_t i=0;i<voices_.size();++i)if(!stop_unlocked(i))accepted=false;
    return accepted;
}
bool MissionSpeech::play_effect(const AudioSample& sample) {
    std::lock_guard<std::mutex> guard(mutex_);
    // Dedicated bounded voices never interrupt dialogue or NEWS slots.
    for(size_t slot=3;slot<voices_.size();++slot){
        const auto& voice=*voices_[slot];
        if(!voice.continuous_channel && (!voice.handle || (!voice.looping && !voice.loop_fault && (voice.header.dwFlags&WHDR_DONE))))return play_unlocked(slot,sample);
    }
    for(size_t i=0;i<voices_.size()-3;++i){
        const auto slot=next_effect_;next_effect_=slot+1==voices_.size()?3:slot+1;
        if(!voices_[slot]->continuous_channel && !voices_[slot]->looping && !voices_[slot]->loop_fault)return play_unlocked(slot,sample);
    }
    return false; // Ordinary one-shots cannot evict owned streaming loops.
}
void MissionSpeech::stop(size_t slot) {
    std::lock_guard<std::mutex> guard(mutex_);stop_unlocked(slot);
}
bool MissionSpeech::stop_unlocked(size_t slot) {
    auto& voice=*voices_.at(slot);
    voice.continuous_frequency.reset();
    if(voice.handle)return release_owned_unlocked(slot);
    voice.continuous_channel=false;
    voice.pcm_stream.reset();
    if(voice.generation!=UINT64_MAX)++voice.generation;
    voice.header={};voice.loop_headers={};voice.loop_bytes.clear();voice.looping=false;voice.loop_fault=false;voice.paused=false;voice.bytes.clear();voice.frequency=0;return true;
}
bool MissionSpeech::play(size_t slot,const AudioSample& sample) {
    std::lock_guard<std::mutex> guard(mutex_);
    // Every dialogue/NEWS line starts with an explicit mix. A previous distant
    // line must not leave the next unpositioned line at its old device gain.
    const AudioMix initial{127,63};
    return play_unlocked(slot,sample,slot<3?&initial:nullptr);
}
bool MissionSpeech::open_unlocked(size_t slot,const AudioSample& sample,const AudioMix* initial_mix,bool streaming) {
    auto& voice=*voices_.at(slot);
    if(!sample.frequency || sample.frequency>192000 || sample.channels<1 || sample.channels>2 || sample.pcm.empty() || sample.pcm.size()%(sample.channels*2))return false;
    if(voice.generation>=UINT64_MAX-1 || !stop_unlocked(slot))return false;
    WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=sample.channels;format.wBitsPerSample=16;
    format.nSamplesPerSec=sample.frequency;format.nBlockAlign=sample.channels*2;format.nAvgBytesPerSec=sample.frequency*format.nBlockAlign;
    voice.bytes=sample.pcm;
    if(waveOutOpen(&voice.handle,WAVE_MAPPER,&format,streaming?reinterpret_cast<DWORD_PTR>(loop_event_):0,0,streaming?CALLBACK_EVENT:CALLBACK_NULL)!=MMSYSERR_NOERROR){voice.handle=nullptr;voice.bytes.clear();return false;}
    if(initial_mix) {
        UINT device=0;WAVEOUTCAPSA caps{};
        if(waveOutGetID(voice.handle,&device)!=MMSYSERR_NOERROR ||
            waveOutGetDevCapsA(device,&caps,sizeof(caps))!=MMSYSERR_NOERROR ||
            (caps.dwSupport&(WAVECAPS_VOLUME|WAVECAPS_LRVOLUME))!=
                (WAVECAPS_VOLUME|WAVECAPS_LRVOLUME)) {stop_unlocked(slot);return false;}
        const auto gain=double(initial_mix->volume)/127.0;
        const auto left=uint32_t(65535.0*gain*(initial_mix->pan<=63?1.0:double(127-initial_mix->pan)/64.0));
        const auto right=uint32_t(65535.0*gain*(initial_mix->pan>=63?1.0:double(initial_mix->pan)/63.0));
        if(waveOutSetVolume(voice.handle,DWORD(left|(right<<16)))!=MMSYSERR_NOERROR){stop_unlocked(slot);return false;}
    }
    voice.frequency=sample.frequency;return true;
}
bool MissionSpeech::play_unlocked(size_t slot,const AudioSample& sample,const AudioMix* initial_mix,uint32_t repeats) {
    if(!open_unlocked(slot,sample,initial_mix,false))return false;
    auto& voice=*voices_[slot];
    voice.header.lpData=reinterpret_cast<LPSTR>(voice.bytes.data());voice.header.dwBufferLength=DWORD(voice.bytes.size());
    if(waveOutPrepareHeader(voice.handle,&voice.header,sizeof(voice.header))!=MMSYSERR_NOERROR){stop_unlocked(slot);return false;}
    if(repeats>1){voice.header.dwFlags|=WHDR_BEGINLOOP|WHDR_ENDLOOP;voice.header.dwLoops=repeats;}
    if(waveOutWrite(voice.handle,&voice.header,sizeof(voice.header))!=MMSYSERR_NOERROR){stop_unlocked(slot);return false;}
    voice.frequency=sample.frequency;return true;
}
OwnedEffectReceipt MissionSpeech::play_owned_effect(const AudioSample& sample,std::optional<AudioMix> initial_mix,uint32_t repeats) {
    std::lock_guard<std::mutex> guard(mutex_);
    OwnedEffectReceipt receipt;
    if(!repeats || !sample.frequency || sample.frequency>192000 || sample.channels<1 || sample.channels>2 ||
        sample.pcm.empty() || sample.pcm.size()%(sample.channels*2) ||
        (initial_mix && (initial_mix->volume>127 || initial_mix->pan>127)))return receipt;
    // Owned effects never evict dialogue, NEWS or another live effect.
    for(size_t slot=3;slot<voices_.size();++slot) {
        auto& voice=*voices_[slot];
        if(voice.generation>=UINT64_MAX-1 || voice.continuous_channel || voice.looping || voice.loop_fault || (voice.handle && !(voice.header.dwFlags&WHDR_DONE)))continue;
        receipt.entered=true;
        // DONE means playback finished, not that the driver released its header.
        // Refuse replacement on any release failure, preserving the old owner.
        if(voice.handle && !release_owned_unlocked(slot))return receipt;
        receipt.accepted=play_unlocked(slot,sample,initial_mix?&*initial_mix:nullptr,repeats);
        if(receipt.accepted)receipt.voice=OwnedEffectVoice{slot,voice.generation};
        return receipt;
    }
    return receipt;
}
bool MissionSpeech::ensure_loop_worker(){
    if(loop_event_)return true;
    loop_event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!loop_event_)return false;
    try {loop_worker_=std::thread([this]{
        for(;;){
            const auto wait=WaitForSingleObject(loop_event_,INFINITE);
            if(loop_stopping_)break;
            if(wait!=WAIT_OBJECT_0){
                std::lock_guard<std::mutex> guard(mutex_);
                for(auto& voice:voices_)if(voice->looping){voice->looping=false;voice->loop_fault=true;}
                break;
            }
            pump_loop_output();
        }
    });}catch(...){CloseHandle(loop_event_);loop_event_=nullptr;return false;}
    return true;
}
OwnedEffectReceipt MissionSpeech::play_owned_continuous_sample(const AudioSample& sample,const AudioRecord& record,AudioMix mix,ContinuousVoiceStart start){
    if(sample.channels!=1 || sample.pcm.size()!=record.size || sample.frequency!=record.frequency)return {};
    const auto span=continuous_pcm_loop_span(record.size,record.loop_start,record.loop_end);
    if(!span)return {};
    // Retail sample4 VAG ends with flag1, not loop/repeat flag3. The source
    // frame queue notices channel completion and reacquires it (239FA0..FC8).
    // Do not turn this authored one-shot into a driver-level infinite loop.
    return play_owned_region(sample,span->begin,span->end,mix,start,record.loop_end!=-1);
}
OwnedEffectReceipt MissionSpeech::play_owned_loop(const AudioSample& sample,uint32_t begin,uint32_t end,AudioMix mix,std::optional<ContinuousVoiceStart> start){
    return play_owned_region(sample,begin,end,mix,start,true);
}
OwnedEffectReceipt MissionSpeech::play_owned_region(const AudioSample& sample,uint32_t begin,uint32_t end,AudioMix mix,std::optional<ContinuousVoiceStart> start,bool repeat){
    std::lock_guard<std::mutex> guard(mutex_);OwnedEffectReceipt receipt;
    const auto align=uint32_t(sample.channels)*2;
    if(start && continuous_user_paused_)return receipt;
    if(start && (!start->frequency || start->frequency>192000 || !std::isfinite(start->time_scale)))return receipt;
    const auto initial_volume=start?continuous_volume_step(mix.volume,mix.volume,start->mix,true):std::optional<ContinuousVolumeStep>{};
    if(start && !initial_volume)return receipt;
    if(!sample.frequency || sample.frequency>192000 || sample.channels<1 || sample.channels>2 ||
       sample.pcm.empty() || sample.pcm.size()>MAXDWORD || sample.pcm.size()%align ||
       begin>=end || end>sample.pcm.size() || begin%align || end%align || mix.volume>127 || mix.pan>127)return receipt;
    // Whole authored periods per buffer; short periods get at least100ms of
    // queue time. This is a streaming chunk, never a finite repeat substitute.
    const auto period=size_t(end-begin),minimum=size_t(sample.frequency)*align/10;
    const auto copies=minimum/period+(minimum%period!=0);
    std::vector<uint8_t> loop;
    const auto length=repeat?period*(copies?copies:1):0;if(length>MAXDWORD)return receipt;
    loop.reserve(length);
    while(loop.size()<length)loop.insert(loop.end(),sample.pcm.begin()+begin,sample.pcm.begin()+end);
    for(size_t slot=3;slot<voices_.size();++slot){
        auto& voice=*voices_[slot];
        if(voice.generation>=UINT64_MAX-1 || voice.continuous_channel || voice.looping || voice.loop_fault || (voice.handle && !(voice.header.dwFlags&WHDR_DONE)))continue;
        if(repeat && !ensure_loop_worker())return receipt;
        receipt.entered=true;
        const AudioMix initial_mix{initial_volume?initial_volume->output_volume:mix.volume,mix.pan};
        if(!open_unlocked(slot,sample,&initial_mix,repeat))return receipt;
        voice.continuous_frequency.reset();
        if(start){
            voice.continuous_channel=true;
            // Retail23A4C0 copies the unscaled request to the active channel;
            //23A564..23A5E0 applies slow motion only to physical output.
            const auto output=continuous_channel_frequency(start->time_scale<1.f?uint32_t(float(start->frequency)*0.75f):start->frequency);
            UINT device=0;WAVEOUTCAPSA caps{};
            const auto rate=(uint64_t(output)*65536u+voice.frequency/2)/voice.frequency;
            if(!output || !rate || rate>0x7FFFFFFFu || waveOutGetID(voice.handle,&device)!=MMSYSERR_NOERROR ||
               waveOutGetDevCapsA(device,&caps,sizeof(caps))!=MMSYSERR_NOERROR || !(caps.dwSupport&WAVECAPS_PLAYBACKRATE) ||
               waveOutSetPlaybackRate(voice.handle,DWORD(rate))!=MMSYSERR_NOERROR){stop_unlocked(slot);return receipt;}
            voice.continuous_frequency=start->frequency;
            continuous_volumes_[slot]=mix.volume;
        }
        if(!repeat){
            voice.header.lpData=reinterpret_cast<LPSTR>(voice.bytes.data());voice.header.dwBufferLength=DWORD(sample.pcm.size());
            if(waveOutPrepareHeader(voice.handle,&voice.header,sizeof(WAVEHDR)) || waveOutWrite(voice.handle,&voice.header,sizeof(WAVEHDR))){stop_unlocked(slot);return receipt;}
            receipt.accepted=true;receipt.voice=OwnedEffectVoice{slot,voice.generation};return receipt;
        }
        voice.loop_bytes=std::move(loop);
        if(begin){
            voice.header.lpData=reinterpret_cast<LPSTR>(voice.bytes.data());voice.header.dwBufferLength=begin;
            if(waveOutPrepareHeader(voice.handle,&voice.header,sizeof(WAVEHDR)) || waveOutWrite(voice.handle,&voice.header,sizeof(WAVEHDR))){stop_unlocked(slot);return receipt;}
        }
        for(auto& header:voice.loop_headers){
            header.lpData=reinterpret_cast<LPSTR>(voice.loop_bytes.data());header.dwBufferLength=DWORD(voice.loop_bytes.size());
            if(waveOutPrepareHeader(voice.handle,&header,sizeof(WAVEHDR)) || waveOutWrite(voice.handle,&header,sizeof(WAVEHDR))){stop_unlocked(slot);return receipt;}
        }
        voice.looping=true;receipt.accepted=true;receipt.voice=OwnedEffectVoice{slot,voice.generation};return receipt;
    }
    return receipt;
}
OwnedEffectReceipt MissionSpeech::play_owned_music_stream(std::shared_ptr<Ps2MusicStream> stream,AudioMix mix){
    if(!stream)return {};
    return play_owned_pcm_stream([stream=std::move(stream)](){return stream->next_block();},mix);
}
std::optional<uint32_t> MissionSpeech::owned_stream_position(const OwnedEffectVoice& owned)const{
    std::lock_guard<std::mutex> guard(mutex_);
    if(owned.slot<3 || owned.slot>=voices_.size() || owned.generation==UINT64_MAX)return {};
    const auto& voice=*voices_[owned.slot];
    if(voice.generation!=owned.generation || !voice.handle || !voice.pcm_stream || !voice.looping || voice.loop_fault)return {};
    MMTIME position{};position.wType=TIME_SAMPLES;
    if(waveOutGetPosition(voice.handle,&position,sizeof(position))!=MMSYSERR_NOERROR || position.wType!=TIME_SAMPLES)return {};
    return uint32_t(position.u.sample);
}
OwnedEffectReceipt MissionSpeech::play_owned_pcm_stream(std::function<AudioSample()> next,AudioMix mix){
    std::lock_guard<std::mutex> guard(mutex_);OwnedEffectReceipt receipt;
    if(!next || mix.volume>127 || mix.pan>127)return receipt;
    for(size_t slot=3;slot<voices_.size();++slot){
        auto& voice=*voices_[slot];
        if(voice.generation>=UINT64_MAX-1 || voice.continuous_channel || voice.looping || voice.loop_fault ||
           (voice.handle && !(voice.header.dwFlags&WHDR_DONE)))continue;
        receipt.entered=true;
        try{
            auto first=next();auto second=next();
            if(first.frequency!=32000 || first.channels!=2 || first.pcm.size()!=57344 ||
               second.frequency!=first.frequency || second.channels!=first.channels || second.pcm.size()!=first.pcm.size())return receipt;
            if(!ensure_loop_worker() || !open_unlocked(slot,first,&mix,true))return receipt;
            voice.pcm_stream=std::make_unique<PcmStream>();voice.pcm_stream->next=std::move(next);
            voice.pcm_stream->pcm[0]=std::move(first.pcm);voice.pcm_stream->pcm[1]=std::move(second.pcm);
            for(size_t i=0;i<2;++i){
                auto& header=voice.loop_headers[i];auto& pcm=voice.pcm_stream->pcm[i];
                header.lpData=reinterpret_cast<LPSTR>(pcm.data());header.dwBufferLength=DWORD(pcm.size());
                if(waveOutPrepareHeader(voice.handle,&header,sizeof(WAVEHDR)) || waveOutWrite(voice.handle,&header,sizeof(WAVEHDR))){stop_unlocked(slot);return receipt;}
            }
            voice.looping=true;receipt.accepted=true;receipt.voice=OwnedEffectVoice{slot,voice.generation};return receipt;
        }catch(...){if(voice.handle)stop_unlocked(slot);return receipt;}
    }
    return receipt;
}
bool MissionSpeech::pump_loop_output(){
    std::lock_guard<std::mutex> guard(mutex_);bool accepted=true;
    for(auto& pointer:voices_){
        auto& voice=*pointer;if(!voice.looping)continue;
        for(size_t i=0;i<voice.loop_headers.size();++i){auto& header=voice.loop_headers[i];if(header.dwFlags&WHDR_DONE){
            if(voice.pcm_stream){
                try{
                    auto block=voice.pcm_stream->next();auto& pcm=voice.pcm_stream->pcm[i];
                    if(block.frequency!=voice.frequency || block.channels!=2 || block.pcm.size()!=pcm.size())throw std::runtime_error("Owned stream format changed");
                    std::copy(block.pcm.begin(),block.pcm.end(),pcm.begin());
                }catch(...){voice.loop_fault=true;voice.looping=false;accepted=false;break;}
            }
            header.dwFlags&=~WHDR_DONE;
            if(waveOutWrite(voice.handle,&header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR){
                voice.loop_fault=true;voice.looping=false;accepted=false;break;
            }
        }}
    }
    return accepted;
}
std::optional<bool> MissionSpeech::owned_effect_finished(const OwnedEffectVoice& owned)const {
    std::lock_guard<std::mutex> guard(mutex_);
    if(owned.slot<3 || owned.slot>=voices_.size())return {};
    const auto& voice=*voices_[owned.slot];
    if(owned.generation==UINT64_MAX || voice.generation!=owned.generation || !voice.handle)return {};
    if(voice.loop_fault)return {};
    return !voice.looping && (voice.header.dwFlags&WHDR_DONE)!=0;
}
OwnedEffectReceipt MissionSpeech::stop_owned_effect_checked(const OwnedEffectVoice& owned) {
    std::lock_guard<std::mutex> guard(mutex_);OwnedEffectReceipt receipt;
    if(owned.slot<3 || owned.slot>=voices_.size() || owned.generation==UINT64_MAX)return receipt;
    auto& voice=*voices_[owned.slot];
    if(voice.generation!=owned.generation || !voice.handle)return receipt;
    receipt.entered=true;
    receipt.accepted=release_owned_unlocked(owned.slot);return receipt;
}
OwnedEffectReceipt MissionSpeech::pause_owned_effect(const OwnedEffectVoice& owned,bool paused){
    std::lock_guard<std::mutex> guard(mutex_);OwnedEffectReceipt receipt;
    if(owned.slot<3 || owned.slot>=voices_.size() || owned.generation==UINT64_MAX)return receipt;
    auto& voice=*voices_[owned.slot];
    if(voice.generation!=owned.generation || !voice.handle || voice.loop_fault)return receipt;
    if(voice.paused!=paused){
        receipt.entered=true;
        if((paused?waveOutPause(voice.handle):waveOutRestart(voice.handle))!=MMSYSERR_NOERROR)return receipt;
        voice.paused=paused;
    }
    receipt.accepted=true;receipt.voice=owned;return receipt;
}
bool MissionSpeech::release_owned_unlocked(size_t slot) {
    auto& voice=*voices_.at(slot);
    // Retain the handle, PCM and token on partial failure: never free a buffer
    // still owned by the driver or claim that playback was stopped successfully.
    voice.loop_fault=voice.loop_fault || voice.looping || voice.continuous_channel;
    voice.looping=false; // Never resubmit a buffer after release has begun.
    if(waveOutReset(voice.handle)!=MMSYSERR_NOERROR)return false;
    if((voice.header.dwFlags&WHDR_PREPARED) && waveOutUnprepareHeader(voice.handle,&voice.header,sizeof(voice.header))!=MMSYSERR_NOERROR)return false;
    for(auto& header:voice.loop_headers)
        if((header.dwFlags&WHDR_PREPARED) && waveOutUnprepareHeader(voice.handle,&header,sizeof(header))!=MMSYSERR_NOERROR)return false;
    if(waveOutClose(voice.handle)!=MMSYSERR_NOERROR)return false;
    voice.handle=nullptr;voice.header={};voice.loop_headers={};voice.loop_bytes.clear();voice.loop_fault=false;voice.paused=false;voice.bytes.clear();voice.frequency=0;
    voice.continuous_channel=false;voice.continuous_frequency.reset();
    voice.pcm_stream.reset();
    if(voice.generation!=UINT64_MAX)++voice.generation;
    return true;
}
bool MissionSpeech::stop_owned_effect(const OwnedEffectVoice& owned) {
    return stop_owned_effect_checked(owned).accepted;
}
OwnedEffectReceipt MissionSpeech::update_owned_effect(const OwnedEffectVoice& owned,AudioMix mix,
    uint32_t frequency,bool write_frequency) {
    std::lock_guard<std::mutex> guard(mutex_);
    // Raw gain/pitch updates would desynchronize the source channel's logical
    // caches. Continuous voices must use the atomic frame controller.
    if(owned.slot<voices_.size() && voices_[owned.slot]->continuous_channel)return {};
    auto receipt=update_owned_effect_unlocked(owned,mix,frequency,write_frequency);
    return receipt;
}
OwnedEffectReceipt MissionSpeech::update_owned_effect_unlocked(const OwnedEffectVoice& owned,AudioMix mix,
    uint32_t frequency,bool write_frequency) {
    OwnedEffectReceipt receipt;
    if(owned.slot<3 || owned.slot>=voices_.size() || owned.generation==UINT64_MAX ||
        mix.volume>127 || mix.pan>127 || (write_frequency && (!frequency || frequency>192000)))return receipt;
    const auto& voice=*voices_[owned.slot];
    if(voice.generation!=owned.generation || !voice.handle || voice.loop_fault || (!voice.looping && (voice.header.dwFlags&WHDR_DONE)))return receipt;
    UINT device=0;WAVEOUTCAPSA caps{};
    if(waveOutGetID(voice.handle,&device)!=MMSYSERR_NOERROR ||
        waveOutGetDevCapsA(device,&caps,sizeof(caps))!=MMSYSERR_NOERROR ||
        (caps.dwSupport&(WAVECAPS_VOLUME|WAVECAPS_LRVOLUME))!=(WAVECAPS_VOLUME|WAVECAPS_LRVOLUME) ||
        (write_frequency && !(caps.dwSupport&WAVECAPS_PLAYBACKRATE)))return receipt;
    if(write_frequency && !voice.frequency)return receipt;
    const uint64_t rate=write_frequency?(uint64_t(frequency)*65536u+voice.frequency/2)/voice.frequency:65536u;
    if(write_frequency && (!rate || rate>0x7FFFFFFFu))return receipt;
    const auto gain=double(mix.volume)/127.0;
    const auto left=uint32_t(65535.0*gain*(mix.pan<=63?1.0:double(127-mix.pan)/64.0));
    const auto right=uint32_t(65535.0*gain*(mix.pan>=63?1.0:double(mix.pan)/63.0));
    receipt.entered=true;
    if(write_frequency && waveOutSetPlaybackRate(voice.handle,DWORD(rate))!=MMSYSERR_NOERROR)return receipt;
    if(waveOutSetVolume(voice.handle,DWORD(left|(right<<16)))!=MMSYSERR_NOERROR)return receipt;
    receipt.accepted=true;receipt.voice=owned;return receipt;
}
OwnedEffectReceipt MissionSpeech::update_owned_continuous_frame(const OwnedEffectVoice& owned,const ContinuousSoundEmission& source,
    const ContinuousSoundMotion& frame,float camera_x,uint32_t base_frequency,float time_scale,ContinuousMixContext mix_context){
    const auto desired=continuous_sound_frequency(base_frequency,source.speed,frame);
    const auto volume=continuous_sound_volume(source.volume,source.distance,frame.current_distance);
    const auto pan=continuous_sound_pan(frame.current_distance,camera_x);
    if(!desired || !volume || !pan)return {};
    std::lock_guard<std::mutex> guard(mutex_);
    if(continuous_user_paused_)return {};
    if(owned.slot<3 || owned.slot>=voices_.size())return {};
    auto& voice=*voices_[owned.slot];
    if(voice.generation!=owned.generation || !voice.continuous_channel || !voice.handle || voice.loop_fault || !voice.continuous_frequency ||
       (!voice.looping && (voice.header.dwFlags&WHDR_DONE)))return {};
    const auto step=continuous_frequency_step(*voice.continuous_frequency,*desired,time_scale);
    const auto gain=continuous_volume_step(continuous_volumes_[owned.slot],*volume,mix_context);
    if(!step || !gain)return {};
    auto receipt=update_owned_effect_unlocked(owned,{gain->output_volume,*pan},step->output_frequency,step->write);
    if(receipt.accepted){voice.continuous_frequency=step->cached_frequency;continuous_volumes_[owned.slot]=gain->cached_volume;}
    else if(receipt.entered){voice.loop_fault=true;voice.looping=false;voice.continuous_frequency.reset();}
    return receipt;
}
bool MissionSpeech::service_continuous_user_pause(bool paused){
    std::lock_guard<std::mutex> guard(mutex_);
    if(!paused && !continuous_user_paused_)return true;
    continuous_user_paused_=true;
    // Retail23882C..238914 stops generic channels/clears active queues;
    //19EBEC gates script-object emission while paused. Preserve script objects,
    // but retire physical channel receipts. Resume must create a new channel.
    // Failed releases stay owned and block reopening until a subsequent retry.
    bool accepted=true;
    for(size_t slot=3;slot<voices_.size();++slot)
        if(voices_[slot]->continuous_channel && voices_[slot]->handle && !release_owned_unlocked(slot))accepted=false;
    if(accepted && !paused)continuous_user_paused_=false;
    return accepted;
}
bool MissionSpeech::finished(size_t slot)const {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto& voice=*voices_.at(slot);return voice.handle && !voice.looping && !voice.loop_fault && (voice.header.dwFlags&WHDR_DONE)!=0;
}
bool MissionSpeech::pause(size_t slot,bool paused) {
    std::lock_guard<std::mutex> guard(mutex_);auto& voice=*voices_.at(slot);
    if(!voice.handle)return true;
    if((paused?waveOutPause(voice.handle):waveOutRestart(voice.handle))!=MMSYSERR_NOERROR)return false;
    voice.paused=paused;return true;
}
OwnedEffectReceipt MissionSpeech::update_owned_distance(const OwnedEffectVoice& owned,uint8_t emitting,float maximum,float distance,uint8_t pan,uint32_t frequency,bool write_frequency){
    const auto volume=continuous_sound_volume(emitting,maximum,distance);
    if(!volume || *volume>127 || pan>127)return {};
    return update_owned_effect(owned,{*volume,pan},frequency,write_frequency);
}
OwnedEffectReceipt MissionSpeech::update_owned_motion(const OwnedEffectVoice& owned,const ContinuousSoundEmission& source,const ContinuousSoundMotion& frame,uint8_t pan,uint32_t base_frequency){
    const auto frequency=continuous_sound_frequency(base_frequency,source.speed,frame);
    if(!frequency)return {};
    return update_owned_distance(owned,source.volume,source.distance,frame.current_distance,pan,*frequency,true);
}
OwnedEffectReceipt MissionSpeech::update_owned_spatial(const OwnedEffectVoice& owned,const ContinuousSoundEmission& source,const ContinuousSoundMotion& frame,float camera_x,uint32_t base_frequency){
    const auto pan=continuous_sound_pan(frame.current_distance,camera_x);
    if(!pan)return {};
    return update_owned_motion(owned,source,frame,*pan,base_frequency);
}
bool MissionSpeech::set_mix(size_t slot,AudioMix mix) {
    if(slot>=2 || mix.volume>127 || mix.pan>127)return false;
    std::lock_guard<std::mutex> guard(mutex_);const auto handle=voices_[slot]->handle;
    if(!handle)return true;
    // Use this voice's open handle, never WAVE_MAPPER or a device ID: WinMM
    // scopes volume/pan to the instance when an HWAVEOUT is supplied.
    const auto gain=double(mix.volume)/127.0;
    const auto left=uint32_t(65535.0*gain*(mix.pan<=63?1.0:double(127-mix.pan)/64.0));
    const auto right=uint32_t(65535.0*gain*(mix.pan>=63?1.0:double(mix.pan)/63.0));
    return waveOutSetVolume(handle,DWORD(left|(right<<16)))==MMSYSERR_NOERROR;
}
}
