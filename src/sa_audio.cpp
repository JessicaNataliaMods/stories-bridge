#include "sa_audio.hpp"
#include "lcs/continuous_sound.hpp"
#include "lcs/cutscene_audio.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#ifdef LCS_ISOLATED_AUDIO_OUTPUT_TEST
#include "audio_output_mock.hpp"
#endif
namespace lcs {
MissionSpeech::MissionSpeech(OwnedVoiceProfile profile)
 :voices_(profile==OwnedVoiceProfile::source_audio?43:11),continuous_volumes_(voices_.size(),0) {
    for(auto& voice:voices_)voice=std::make_unique<Voice>();
}
MissionSpeech::~MissionSpeech(){
    loop_stopping_=true;if(loop_event_)SetEvent(loop_event_);
    if(loop_worker_.joinable())loop_worker_.join();
    bool retained=false;
    for(size_t i=0;i<voices_.size();++i)if(!stop_unlocked(i)){retained=true;voices_[i].release();}
    if(loop_event_ && !retained)CloseHandle(loop_event_);
}
bool MissionSpeech::shutdown(){std::lock_guard<std::mutex> lock(mutex_);bool ok=true;for(size_t i=0;i<voices_.size();++i)ok=stop_unlocked(i)&&ok;return ok;}
WAVEHDR& MissionSpeech::output_header(Voice& voice,size_t index){return index?voice.loop_headers[index-1]:voice.header;}
bool MissionSpeech::finished_unlocked(const Voice& v){
    if(!v.handle || v.loop_fault || !v.source_done)return false;
    if((v.header.dwFlags&WHDR_PREPARED) && !(v.header.dwFlags&WHDR_DONE))return false;
    for(const auto& h:v.loop_headers)if((h.dwFlags&WHDR_PREPARED) && !(h.dwFlags&WHDR_DONE))return false;
    return true;
}
bool MissionSpeech::ensure_loop_worker(){
    if(loop_event_)return true;
    loop_event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!loop_event_)return false;
    try{loop_worker_=std::thread([this]{
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);
        for(;;){const auto wait=WaitForSingleObject(loop_event_,INFINITE);if(loop_stopping_)break;
            if(wait!=WAIT_OBJECT_0){std::lock_guard<std::mutex> lock(mutex_);for(auto& v:voices_)if(v->handle)v->loop_fault=true;break;}
            pump_loop_output();
        }
    });}catch(...){CloseHandle(loop_event_);loop_event_=nullptr;return false;}
    return true;
}
bool MissionSpeech::open_unlocked(size_t slot,const AudioSample& sample,const AudioMix* mix,bool){
    auto& v=*voices_.at(slot);
    if(!sample.frequency || sample.frequency>192000 || sample.channels<1 || sample.channels>2 || sample.pcm.empty() || sample.pcm.size()%(sample.channels*2) || v.generation>=UINT64_MAX-1)return false;
    if(!stop_unlocked(slot) || !ensure_loop_worker())return false;
    v.bytes=sample.pcm;v.frequency=sample.frequency;v.target_frequency=sample.frequency;v.channels=sample.channels;
    v.cursor=0;v.loop_begin=0;v.loop_end=sample.pcm.size()/(sample.channels*2);v.repeats=1;v.source_done=false;v.mix=mix?*mix:AudioMix{};
    WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.wBitsPerSample=16;
    format.nSamplesPerSec=sample.frequency;format.nBlockAlign=4;format.nAvgBytesPerSec=sample.frequency*4;
    if(waveOutOpen(&v.handle,WAVE_MAPPER,&format,reinterpret_cast<DWORD_PTR>(loop_event_),0,CALLBACK_EVENT)!=MMSYSERR_NOERROR){v.handle=nullptr;v.bytes.clear();return false;}
    return true;
}
void MissionSpeech::render_chunk(Voice& v,std::vector<uint8_t>& output){
    // Every submitted block owns its mixed stereo PCM. The driver never sees
    // raw mono data or a volume/rate command shared with other wave streams.
    const size_t frames=std::max<size_t>(1,v.frequency/20); // 50 ms, three queued blocks
    output.clear();output.reserve(frames*4);
    const double gain=double(v.mix.volume)/127.0;
    const double left=gain*(v.mix.pan<=63?1.0:double(127-v.mix.pan)/64.0);
    const double right=gain*(v.mix.pan>=63?1.0:double(v.mix.pan)/63.0);
    const auto append=[&](double value){const int16_t sample=int16_t(std::clamp(std::lround(value),-32768l,32767l));const auto* p=reinterpret_cast<const uint8_t*>(&sample);output.insert(output.end(),p,p+2);};
    for(size_t i=0;i<frames && !v.source_done;++i){
        while(v.cursor>=double(v.loop_end)){
            if(v.pcm_stream){
                auto block=v.pcm_stream->next();
                if(block.frequency!=v.frequency || block.channels!=v.channels || block.pcm.size()!=57344)throw std::runtime_error("Owned stream format changed");
                v.cursor-=double(v.loop_end);v.bytes=std::move(block.pcm);v.loop_end=v.bytes.size()/(v.channels*2);
            }else if(v.looping){
                const auto length=v.loop_end-v.loop_begin;if(!length)throw std::runtime_error("Empty owned loop");
                v.cursor=double(v.loop_begin)+std::fmod(v.cursor-double(v.loop_begin),double(length));
            }else if(v.repeats>1){--v.repeats;v.cursor-=double(v.loop_end);}
            else{v.source_done=true;break;}
        }
        if(v.source_done)break;
        const auto first=size_t(v.cursor);
        const auto second=first+1<v.loop_end?first+1:(v.looping && !v.pcm_stream?v.loop_begin:first);
        const auto read=[&](size_t frame,size_t channel){int16_t value=0;std::memcpy(&value,v.bytes.data()+(frame*v.channels+channel)*2,2);return double(value);};
        const auto interpolate=[&](size_t channel){const auto a=read(first,channel);return a+(read(second,channel)-a)*(v.cursor-double(first));};
        append(interpolate(0)*left);append(interpolate(v.channels==1?0:1)*right);
        v.cursor+=double(v.target_frequency)/double(v.frequency);
        if(!v.looping && !v.pcm_stream && v.repeats==1 && v.cursor>=double(v.loop_end))v.source_done=true;
    }
}
bool MissionSpeech::queue_next_unlocked(Voice& v,size_t index){
    if(v.source_done)return true;
    auto& pcm=v.output_pcm[index];render_chunk(v,pcm);if(pcm.empty())return true;
    auto& header=output_header(v,index);
    header.lpData=reinterpret_cast<LPSTR>(pcm.data());header.dwBufferLength=DWORD(pcm.size());
    if(!(header.dwFlags&WHDR_PREPARED) && waveOutPrepareHeader(v.handle,&header,sizeof(header))!=MMSYSERR_NOERROR)return false;
    header.dwFlags&=~WHDR_DONE;
    return waveOutWrite(v.handle,&header,sizeof(header))==MMSYSERR_NOERROR;
}
bool MissionSpeech::start_output_unlocked(size_t slot){
    auto& v=*voices_[slot];
    try{
        if(waveOutPause(v.handle)!=MMSYSERR_NOERROR){stop_unlocked(slot);return false;}
        for(size_t i=0;i<v.output_pcm.size();++i)if(!queue_next_unlocked(v,i)){stop_unlocked(slot);return false;}
        if(waveOutRestart(v.handle)!=MMSYSERR_NOERROR){stop_unlocked(slot);return false;}
        return true;
    }catch(...){stop_unlocked(slot);return false;}
}
bool MissionSpeech::play_unlocked(size_t slot,const AudioSample& sample,const AudioMix* mix,uint32_t repeats){
    if(!repeats || !open_unlocked(slot,sample,mix,true))return false;
    voices_[slot]->repeats=repeats;return start_output_unlocked(slot);
}
bool MissionSpeech::play(size_t slot,const AudioSample& sample){std::lock_guard<std::mutex> lock(mutex_);return play_unlocked(slot,sample);}
bool MissionSpeech::play_effect(const AudioSample& sample){
    std::lock_guard<std::mutex> lock(mutex_);
    for(size_t slot=3;slot<voices_.size();++slot){auto& v=*voices_[slot];if(!v.continuous_channel && (!v.handle || finished_unlocked(v)))return play_unlocked(slot,sample);}
    for(size_t i=0;i<voices_.size()-3;++i){auto slot=next_effect_;next_effect_=slot+1==voices_.size()?3:slot+1;auto& v=*voices_[slot];if(!v.continuous_channel && !v.looping && !v.loop_fault)return play_unlocked(slot,sample);}
    return false;
}
OwnedEffectReceipt MissionSpeech::play_owned_effect(const AudioSample& sample,std::optional<AudioMix> mix,uint32_t repeats){
    std::lock_guard<std::mutex> lock(mutex_);OwnedEffectReceipt result;
    if(!repeats || !sample.frequency || sample.frequency>192000 || sample.channels<1 || sample.channels>2 || sample.pcm.empty() || sample.pcm.size()%(sample.channels*2) || (mix && (mix->volume>127 || mix->pan>127)))return result;
    for(size_t slot=3;slot<voices_.size();++slot){auto& v=*voices_[slot];
        if(v.generation>=UINT64_MAX-1 || v.continuous_channel || v.looping || v.loop_fault || (v.handle && !finished_unlocked(v)))continue;
        result.entered=true;result.accepted=play_unlocked(slot,sample,mix?&*mix:nullptr,repeats);
        if(result.accepted)result.voice=OwnedEffectVoice{slot,v.generation};return result;
    }return result;
}
OwnedEffectReceipt MissionSpeech::play_owned_continuous_sample(const AudioSample& sample,const AudioRecord& record,AudioMix mix,ContinuousVoiceStart start){
    if(sample.channels!=1 || sample.pcm.size()!=record.size || sample.frequency!=record.frequency)return {};
    const auto span=continuous_pcm_loop_span(record.size,record.loop_start,record.loop_end);if(!span)return {};
    return play_owned_region(sample,span->begin,span->end,mix,start,record.loop_end!=-1);
}
OwnedEffectReceipt MissionSpeech::play_owned_loop(const AudioSample& sample,uint32_t begin,uint32_t end,AudioMix mix,std::optional<ContinuousVoiceStart> start){return play_owned_region(sample,begin,end,mix,start,true);}
OwnedEffectReceipt MissionSpeech::play_owned_region(const AudioSample& sample,uint32_t begin,uint32_t end,AudioMix mix,std::optional<ContinuousVoiceStart> start,bool repeat){
    std::lock_guard<std::mutex> lock(mutex_);OwnedEffectReceipt result;const auto align=sample.channels*2;
    if(start && (continuous_user_paused_ || !start->frequency || start->frequency>192000 || !std::isfinite(start->time_scale)))return result;
    const auto gain=start?continuous_volume_step(mix.volume,mix.volume,start->mix,true):std::optional<ContinuousVolumeStep>{};
    if((start && !gain) || !sample.frequency || sample.frequency>192000 || sample.channels<1 || sample.channels>2 || sample.pcm.empty() || sample.pcm.size()%align || begin>=end || end>sample.pcm.size() || begin%align || end%align || mix.volume>127 || mix.pan>127)return result;
    for(size_t slot=3;slot<voices_.size();++slot){auto& v=*voices_[slot];
        if(v.generation>=UINT64_MAX-1 || v.continuous_channel || v.looping || v.loop_fault || (v.handle && !finished_unlocked(v)))continue;
        result.entered=true;AudioMix initial{gain?gain->output_volume:mix.volume,mix.pan};
        if(!open_unlocked(slot,sample,&initial,true))return result;
        v.looping=repeat;v.loop_begin=begin/align;v.loop_end=repeat?end/align:sample.pcm.size()/align;
        if(start){v.continuous_channel=true;v.continuous_frequency=start->frequency;v.target_frequency=continuous_channel_frequency(start->time_scale<1?uint32_t(float(start->frequency)*0.75f):start->frequency);continuous_volumes_[slot]=mix.volume;}
        if(!v.target_frequency || v.target_frequency>192000){stop_unlocked(slot);return result;}
        result.accepted=start_output_unlocked(slot);if(result.accepted)result.voice=OwnedEffectVoice{slot,v.generation};return result;
    }return result;
}
OwnedEffectReceipt MissionSpeech::play_owned_music_stream(std::shared_ptr<Ps2MusicStream> stream,AudioMix mix){if(!stream)return {};return play_owned_pcm_stream([stream=std::move(stream)]{return stream->next_block();},mix);}
OwnedEffectReceipt MissionSpeech::play_owned_pcm_stream(std::function<AudioSample()> next,AudioMix mix){
    std::lock_guard<std::mutex> lock(mutex_);OwnedEffectReceipt result;if(!next || mix.volume>127 || mix.pan>127)return result;
    for(size_t slot=3;slot<voices_.size();++slot){auto& v=*voices_[slot];if(v.generation>=UINT64_MAX-1 || v.continuous_channel || v.looping || v.loop_fault || (v.handle && !finished_unlocked(v)))continue;
        result.entered=true;try{auto first=next();if(first.frequency!=32000 || first.channels!=2 || first.pcm.size()!=57344)return result;
            if(!open_unlocked(slot,first,&mix,true))return result;
            v.pcm_stream=std::make_unique<PcmStream>();v.pcm_stream->next=std::move(next);v.looping=true;
            result.accepted=start_output_unlocked(slot);if(result.accepted)result.voice=OwnedEffectVoice{slot,v.generation};return result;
        }catch(...){if(v.handle)stop_unlocked(slot);return result;}
    }return result;
}
std::optional<uint32_t> MissionSpeech::owned_stream_position(const OwnedEffectVoice& owned)const{
    std::lock_guard<std::mutex> lock(mutex_);if(owned.slot<3 || owned.slot>=voices_.size() || owned.generation==UINT64_MAX)return {};
    const auto& v=*voices_[owned.slot];if(v.generation!=owned.generation || !v.handle || !v.pcm_stream || !v.looping || v.loop_fault)return {};
    MMTIME position{};position.wType=TIME_SAMPLES;if(waveOutGetPosition(v.handle,&position,sizeof(position))!=MMSYSERR_NOERROR || position.wType!=TIME_SAMPLES)return {};return uint32_t(position.u.sample);
}
bool MissionSpeech::pump_loop_output(){
    std::lock_guard<std::mutex> lock(mutex_);bool ok=true;
    for(auto& p:voices_){auto& v=*p;if(!v.handle || v.loop_fault || v.paused || v.source_done)continue;
        for(size_t i=0;i<v.output_pcm.size();++i){auto& h=output_header(v,i);if(h.dwFlags&WHDR_DONE){try{if(!queue_next_unlocked(v,i))throw std::runtime_error("Owned output write failed");}catch(...){v.loop_fault=true;ok=false;break;}}}
    }return ok;
}
bool MissionSpeech::release_owned_unlocked(size_t slot){
    auto& v=*voices_.at(slot);v.loop_fault=true;v.looping=false;
    if(waveOutReset(v.handle)!=MMSYSERR_NOERROR)return false;
    for(size_t i=0;i<v.output_pcm.size();++i){auto& h=output_header(v,i);if((h.dwFlags&WHDR_PREPARED) && waveOutUnprepareHeader(v.handle,&h,sizeof(h))!=MMSYSERR_NOERROR)return false;}
    if(waveOutClose(v.handle)!=MMSYSERR_NOERROR)return false;
    v.handle=nullptr;v.header={};v.loop_headers={};for(auto& pcm:v.output_pcm)pcm.clear();v.bytes.clear();v.frequency=0;v.target_frequency=0;v.continuous_channel=false;v.continuous_frequency.reset();v.pcm_stream.reset();v.loop_fault=false;v.paused=false;v.source_done=false;
    if(v.generation!=UINT64_MAX)++v.generation;return true;
}
bool MissionSpeech::stop_unlocked(size_t slot){auto& v=*voices_.at(slot);if(v.handle)return release_owned_unlocked(slot);v.continuous_frequency.reset();v.continuous_channel=false;v.pcm_stream.reset();if(v.generation!=UINT64_MAX)++v.generation;return true;}
void MissionSpeech::stop(size_t slot){std::lock_guard<std::mutex> lock(mutex_);stop_unlocked(slot);}
OwnedEffectReceipt MissionSpeech::stop_owned_effect_checked(const OwnedEffectVoice& owned){std::lock_guard<std::mutex> lock(mutex_);OwnedEffectReceipt result;if(owned.slot<3 || owned.slot>=voices_.size() || owned.generation==UINT64_MAX)return result;auto& v=*voices_[owned.slot];if(v.generation!=owned.generation || !v.handle)return result;result.entered=true;result.accepted=release_owned_unlocked(owned.slot);return result;}
bool MissionSpeech::stop_owned_effect(const OwnedEffectVoice& voice){return stop_owned_effect_checked(voice).accepted;}
std::optional<bool> MissionSpeech::owned_effect_finished(const OwnedEffectVoice& owned)const{std::lock_guard<std::mutex> lock(mutex_);if(owned.slot<3 || owned.slot>=voices_.size())return {};const auto& v=*voices_[owned.slot];if(owned.generation==UINT64_MAX || v.generation!=owned.generation || !v.handle || v.loop_fault)return {};return finished_unlocked(v);}
bool MissionSpeech::finished(size_t slot)const{std::lock_guard<std::mutex> lock(mutex_);return finished_unlocked(*voices_.at(slot));}
bool MissionSpeech::pause(size_t slot,bool paused){std::lock_guard<std::mutex> lock(mutex_);auto& v=*voices_.at(slot);if(!v.handle)return true;if(v.paused==paused)return true;if((paused?waveOutPause(v.handle):waveOutRestart(v.handle))!=MMSYSERR_NOERROR)return false;v.paused=paused;return true;}
OwnedEffectReceipt MissionSpeech::pause_owned_effect(const OwnedEffectVoice& owned,bool paused){std::lock_guard<std::mutex> lock(mutex_);OwnedEffectReceipt result;if(owned.slot<3 || owned.slot>=voices_.size() || owned.generation==UINT64_MAX)return result;auto& v=*voices_[owned.slot];if(v.generation!=owned.generation || !v.handle || v.loop_fault)return result;if(v.paused!=paused){result.entered=true;if((paused?waveOutPause(v.handle):waveOutRestart(v.handle))!=MMSYSERR_NOERROR)return result;v.paused=paused;}result.accepted=true;result.voice=owned;return result;}
OwnedEffectReceipt MissionSpeech::update_owned_effect(const OwnedEffectVoice& owned,AudioMix mix,uint32_t frequency,bool write){std::lock_guard<std::mutex> lock(mutex_);if(owned.slot<voices_.size() && voices_[owned.slot]->continuous_channel)return {};return update_owned_effect_unlocked(owned,mix,frequency,write);}
OwnedEffectReceipt MissionSpeech::update_owned_effect_unlocked(const OwnedEffectVoice& owned,AudioMix mix,uint32_t frequency,bool write){
    OwnedEffectReceipt result;if(owned.slot<3 || owned.slot>=voices_.size() || owned.generation==UINT64_MAX || mix.volume>127 || mix.pan>127 || (write && (!frequency || frequency>192000)))return result;
    auto& v=*voices_[owned.slot];if(v.generation!=owned.generation || !v.handle || v.loop_fault || finished_unlocked(v))return result;
    result.entered=true;v.mix=mix;if(write)v.target_frequency=frequency;result.accepted=true;result.voice=owned;return result;
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
       (!voice.looping && finished_unlocked(voice)))return {};
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
bool MissionSpeech::set_mix(size_t slot,AudioMix mix){if(slot>=2 || mix.volume>127 || mix.pan>127)return false;std::lock_guard<std::mutex> lock(mutex_);voices_[slot]->mix=mix;return true;}
}
