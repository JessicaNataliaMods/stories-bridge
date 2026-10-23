#include "sa_vehicle_audio.hpp"
#include "lcs/continuous_sound.hpp"
#include "lcs/sa_executor.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace lcs {
namespace {
struct VehicleRead {
    uintptr_t vehicle=0;uint16_t model=0;std::array<float,3> position{};
};
// Plain-data readers: every dereference of SA memory is guarded.
bool read_vehicle(uintptr_t vehicle,VehicleRead& out) {
    if(!vehicle)return false;
    __try {
        out.vehicle=vehicle;out.model=*reinterpret_cast<const uint16_t*>(vehicle+0x22);
        const auto matrix=*reinterpret_cast<const uintptr_t*>(vehicle+0x14);
        std::memcpy(out.position.data(),reinterpret_cast<const void*>(matrix?matrix+0x30:vehicle+4),12);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    for(const auto value:out.position)if(!std::isfinite(value))return false;
    return true;
}
bool read_entity_vehicle(const void* audio_entity,VehicleRead& out) {
    if(!audio_entity)return false;
    uintptr_t vehicle=0;
    __try {vehicle=*reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(audio_entity)+4);}   // CAEAudioEntity::m_Entity
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    return read_vehicle(vehicle,out);
}
bool read_params_vehicle(const void* params,VehicleRead& out) {
    if(!params)return false;
    uintptr_t vehicle=0;
    __try {vehicle=*reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(params)+0x10);}  // tVehicleParams::Vehicle
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    return read_vehicle(vehicle,out);
}
struct EngineRead {VehicleEngineInputs inputs{};bool engine_on=false;};
bool read_engine(const void* params,uintptr_t vehicle,bool player,EngineRead& out) {
    __try {
        const auto base=reinterpret_cast<uintptr_t>(params);
        const int type=*reinterpret_cast<const int*>(base+4);                       // BaseVehicleType: 0 automobile, 9 bike
        const float speed=*reinterpret_cast<const float*>(base+0x1C);
        const auto transmission=*reinterpret_cast<const uintptr_t*>(base+0x14);
        if(!transmission || (type!=0 && type!=9))return false;
        const float max=*reinterpret_cast<const float*>(transmission+0x58);         // cTransmission::m_MaxVelocity
        const uint8_t gears=*reinterpret_cast<const uint8_t*>(transmission+0x4A);   // m_nNumberOfGears
        if(!gears || gears>5)return false;
        struct Gear {float max,up,down;};
        const auto* table=reinterpret_cast<const Gear*>(transmission);
        uint32_t gear=gears;
        for(uint32_t i=1;i<=gears;++i)if(std::fabs(speed)<=table[i].max){gear=i;break;}
        out.inputs.speed=speed;out.inputs.max_velocity=max;
        out.inputs.gear_change_down=table[gear].down;out.inputs.first_gear_change_up=table[1].up;
        out.inputs.gas_pedal=std::fabs(*reinterpret_cast<const float*>(vehicle+0x49C));
        out.inputs.gas_audio=*reinterpret_cast<const float*>(vehicle+(type==0?0x964:0x808)); // m_GasPedalAudioRevs
        out.inputs.is_bike=type==9;out.inputs.drive_wheels_on_ground=true;out.inputs.player=player;
        out.engine_on=(*reinterpret_cast<const uint32_t*>(vehicle+0x428)&0x10)!=0;     // vehicleFlags.bEngineOn
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    const auto& in=out.inputs;
    return std::isfinite(in.speed) && std::isfinite(in.max_velocity) && std::isfinite(in.gear_change_down) &&
           std::isfinite(in.first_gear_change_up) && std::isfinite(in.gas_pedal) && std::isfinite(in.gas_audio);
}
struct Spatial {float distance,camera_x;bool ok;};
Spatial spatial_of(const std::array<float,3>& position) {
    const auto listener=sa_audio_listener();
    if(!listener)return {0,0,false};
    float squared=0,right=0;
    for(size_t i=0;i<3;++i){const float d=position[i]-listener->position[i];squared+=d*d;right+=d*listener->right[i];}
    return {std::sqrt(squared),right,true};
}
uint8_t scaled(uint8_t volume,float effects_scale) {
    return uint8_t(std::clamp(float(volume)*std::clamp(effects_scale,0.f,1.f),0.f,127.f)+0.5f);
}
}
bool sa_vehicle_audio_profile_matches() {
    constexpr uint8_t horn[]={0x83,0xEC,0x0C,0xA1,0xF8,0x00,0xBD,0x00,0x0F,0xBE,0x50,0x60};   // PlayHornOrSiren
    constexpr uint8_t player[]={0x51,0x56,0x8B,0xF1,0x33,0xC9,0x66,0x8B,0x8E,0xE0,0x00,0x00};  // ProcessPlayerVehicleEngine
    constexpr uint8_t dummy[]={0x56,0x8B,0xF1,0x66,0x83,0xBE,0xE0,0x00,0x00,0x00,0xFF,0x75};   // ProcessDummyVehicleEngine
    __try {
        if(std::memcmp(reinterpret_cast<const void*>(SaVehicleAudioProfile::horn_siren),horn,sizeof(horn)) ||
           std::memcmp(reinterpret_cast<const void*>(SaVehicleAudioProfile::player_engine),player,sizeof(player)) ||
           std::memcmp(reinterpret_cast<const void*>(SaVehicleAudioProfile::dummy_engine),dummy,sizeof(dummy)))return false;
        const auto targets=[](uintptr_t site,uintptr_t target){
            const auto* code=reinterpret_cast<const uint8_t*>(site);
            int32_t relative=0;std::memcpy(&relative,code+1,4);
            return code[0]==0xE8 && uintptr_t(int64_t(site+5)+relative)==target;
        };
        if(!targets(SaVehicleAudioProfile::horn_siren_call,SaVehicleAudioProfile::horn_siren))return false;
        for(const auto site:SaVehicleAudioProfile::player_engine_calls)if(!targets(site,SaVehicleAudioProfile::player_engine))return false;
        for(const auto site:SaVehicleAudioProfile::dummy_engine_calls)if(!targets(site,SaVehicleAudioProfile::dummy_engine))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool sa_vehicle_event_calls(uintptr_t hook) {
    constexpr uintptr_t target=SaVehicleAudioProfile::add_event;
    for(const auto site:SaVehicleAudioProfile::add_event_calls) {
        bool ok=false;
        __try {
            const auto* code=reinterpret_cast<const uint8_t*>(site);
            int32_t relative=0;std::memcpy(&relative,code+1,4);
            ok=code[0]==0xE8 && uintptr_t(int64_t(site+5)+relative)==target;
        } __except(EXCEPTION_EXECUTE_HANDLER) {ok=false;}
        if(!ok)return false;
    }
    for(const auto site:SaVehicleAudioProfile::add_event_calls) {
        auto* code=reinterpret_cast<uint8_t*>(site);
        DWORD old=0;
        if(!VirtualProtect(code,5,PAGE_EXECUTE_READWRITE,&old))return false;
        const int32_t relative=int32_t(int64_t(hook)-int64_t(site+5));
        std::memcpy(code+1,&relative,4);
        FlushInstructionCache(GetCurrentProcess(),code,5);
        DWORD ignored=0;VirtualProtect(code,5,old,&ignored);
    }
    return true;
}

SaVehicleSounds::SaVehicleSounds(const AudioArchive& archive,VehicleSoundConfig config):archive_(archive),config_(config) {
    worker_=std::make_unique<AudioWorker>([this]{tick();},std::chrono::milliseconds(16));
}
const SaVehicleSounds::Asset* SaVehicleSounds::asset(uint32_t sample,uint32_t assumed_rate) {
    const uint64_t cache_key=(uint64_t(sample)<<32)|assumed_rate;
    std::lock_guard<std::mutex> guard(asset_mutex_);
    const auto found=assets_.find(cache_key);
    if(found!=assets_.end())return &found->second;
    try {
        Asset value;
        value.pcm=assumed_rate?archive_.sample(sample,assumed_rate):archive_.sample(sample);
        const auto record=assumed_rate?archive_.record(sample,assumed_rate):archive_.record(sample);
        const auto size=uint32_t(value.pcm.pcm.size());
        const auto span=continuous_pcm_loop_span(record.size,record.loop_start,record.loop_end);
        value.begin=span?span->begin:0;value.end=span?std::min(span->end,size):size;
        if(!size || value.begin>=value.end || value.pcm.channels!=1)return nullptr;
        value.silent=std::all_of(value.pcm.pcm.begin(),value.pcm.pcm.end(),[](uint8_t byte){return byte==0;});
        return &assets_.emplace(cache_key,std::move(value)).first->second;   // node-based map: the pointer stays valid
    } catch(const std::exception&) {return nullptr;}
}
void SaVehicleSounds::publish(uint64_t id,uint32_t sample,uint32_t assumed_rate,uint32_t frequency,uint8_t volume,uint8_t pan) {
    std::lock_guard<std::mutex> guard(slot_mutex_);
    auto& slot=slots_[id];
    if(slot.sample!=sample){slot.failed=false;slot.silent=false;}
    slot.want=true;slot.sample=sample;slot.assumed_rate=assumed_rate;slot.frequency=frequency;slot.volume=volume;slot.pan=pan;slot.touched=GetTickCount();
}
void SaVehicleSounds::release(uint64_t id) {
    std::lock_guard<std::mutex> guard(slot_mutex_);
    const auto found=slots_.find(id);
    if(found!=slots_.end())found->second.want=false;
}
bool SaVehicleSounds::failed(uint64_t id) {
    std::lock_guard<std::mutex> guard(slot_mutex_);
    const auto found=slots_.find(id);
    return found!=slots_.end() && found->second.failed;
}
// Worker thread: starts, retunes and stops the looping voices the game thread asked for.
void SaVehicleSounds::tick() {
    struct Job {enum class Action{start,stop,update} action;uint64_t id;uint32_t sample,rate,frequency;uint8_t volume,pan;std::optional<OwnedEffectVoice> voice;bool write_frequency;};
    std::vector<Job> jobs;
    const uint32_t now=GetTickCount();
    const float scale=effects_scale_;
    {
        std::lock_guard<std::mutex> guard(slot_mutex_);
        for(auto it=slots_.begin();it!=slots_.end();) {
            auto& slot=it->second;
            const bool alive=slot.want && now-slot.touched<400;
            if(!alive) {
                if(slot.voice){jobs.push_back({Job::Action::stop,it->first,0,0,0,0,0,slot.voice,false});slot.voice.reset();}
                it=slots_.erase(it);continue;
            }
            if(slot.voice && slot.voice_sample!=slot.sample) {
                jobs.push_back({Job::Action::stop,it->first,0,0,0,0,0,slot.voice,false});slot.voice.reset();
            }
            if(!slot.voice) {
                if(!slot.failed && !slot.silent)
                    jobs.push_back({Job::Action::start,it->first,slot.sample,slot.assumed_rate,slot.frequency,slot.volume,slot.pan,std::nullopt,false});
            } else {
                // At most ~16 driver writes a second per loop, and only for an audible change.
                const int volume_step=std::abs(int(slot.volume)-int(slot.voice_volume)),pan_step=std::abs(int(slot.pan)-int(slot.voice_pan));
                const int64_t rate_step=slot.frequency?std::llabs(int64_t(slot.frequency)-int64_t(slot.voice_frequency)):0;
                const bool audible=volume_step>=3 || pan_step>=4 || rate_step*100>=int64_t(slot.voice_frequency);
                const bool due=(now-slot.sent>=60 || volume_step>=20) && (audible || now-slot.sent>=250);
                if(due)jobs.push_back({Job::Action::update,it->first,slot.sample,0,slot.frequency,slot.volume,slot.pan,slot.voice,rate_step*100>=int64_t(slot.voice_frequency) && slot.frequency!=0});
            }
            ++it;
        }
    }
    for(const auto& job:jobs) {
        switch(job.action) {
        case Job::Action::stop:speech_.stop_owned_effect(*job.voice);break;
        case Job::Action::start: {
            const auto* a=asset(job.sample,job.rate);
            bool ok=false,silent=false;std::optional<OwnedEffectVoice> voice;uint32_t frequency=job.frequency;
            if(a) {
                if(a->silent)silent=true;
                else {
                    if(!frequency)frequency=a->pcm.frequency;
                    AudioSample sample=a->pcm;sample.frequency=frequency;
                    const auto receipt=speech_.play_owned_loop(sample,a->begin,a->end,{scaled(job.volume,scale),job.pan},std::nullopt);
                    ok=receipt.accepted && receipt.voice;if(ok)voice=receipt.voice;
                }
            }
            std::lock_guard<std::mutex> guard(slot_mutex_);
            const auto found=slots_.find(job.id);
            if(found==slots_.end()) {if(voice)speech_.stop_owned_effect(*voice);break;}
            if(silent)found->second.silent=true;
            else if(!ok)found->second.failed=true;
            else {
                auto& slot=found->second;
                slot.voice=voice;slot.voice_sample=job.sample;slot.voice_frequency=frequency;slot.voice_volume=job.volume;slot.voice_pan=job.pan;slot.sent=GetTickCount();
            }
            break;}
        case Job::Action::update: {
            const auto receipt=speech_.update_owned_effect(*job.voice,{scaled(job.volume,scale),job.pan},job.write_frequency?job.frequency:0,job.write_frequency);
            std::lock_guard<std::mutex> guard(slot_mutex_);
            const auto found=slots_.find(job.id);
            if(found==slots_.end())break;
            auto& slot=found->second;
            if(!receipt.accepted) {speech_.stop_owned_effect(*job.voice);slot.voice.reset();break;}   // never leak a voice
            slot.voice_volume=job.volume;slot.voice_pan=job.pan;slot.sent=GetTickCount();
            if(job.write_frequency)slot.voice_frequency=job.frequency;
            break;}
        }
    }
}
SaVehicleSounds::Entry* SaVehicleSounds::entry_for(uintptr_t entity,uint16_t model,uint32_t now) {
    auto& entry=entries_[entity];
    if(entry.model!=model) {release_entry(entity);entry=Entry{};entry.model=model;}
    entry.touched=now;
    return &entry;
}
void SaVehicleSounds::release_entry(uintptr_t entity) {
    for(const auto kind:{horn_loop,siren_loop,engine_idle_loop,engine_rev_loop})release(key(entity,kind));
}
void SaVehicleSounds::horn_siren(const void* audio_entity,bool horn,bool siren,bool fast,const void* params,bool& taken_horn,bool& taken_siren) {
    taken_horn=taken_siren=false;
    if(!config_.horns && !config_.sirens)return;
    VehicleRead v;
    if(!read_params_vehicle(params,v) || !lcs_for_native_)return;
    const auto lcs_model=lcs_for_native_(v.model);
    const auto* row=lcs_model?vehicle_sound_row(*lcs_model):nullptr;
    if(!row)return;
    const auto where=spatial_of(v.position);
    if(!where.ok)return;
    const auto entity=reinterpret_cast<uintptr_t>(audio_entity);
    entry_for(entity,v.model,GetTickCount());
    const auto pan=continuous_sound_pan(where.distance,where.camera_x).value_or(63);
    if(config_.horns) {
        taken_horn=true;
        const auto id=key(entity,horn_loop);
        const auto volume=continuous_sound_volume(vehicle_horn_volume,vehicle_horn_range,where.distance).value_or(0);
        if(!horn || volume==0)release(id);
        else {publish(id,row->horn_sample,0,uint32_t(row->horn_frequency),volume,pan);taken_horn=!failed(id);}
    }
    if(config_.sirens) {
        taken_siren=true;
        const auto id=key(entity,siren_loop);
        if(!siren)release(id);
        else {
            // ProcessVehicleSirenOrAlarm: the fast siren replaces the long one except on the fire truck and Mr. Whoopee.
            const bool switching=*lcs_model!=138 && *lcs_model!=154;
            uint32_t sample=row->siren_sample,frequency=uint32_t(row->siren_frequency);
            if(fast && switching) {sample=vehicle_fast_siren_sample();frequency=*lcs_model==148?12668u:0u;}   // 0: the sample's own rate; the FBI car uses a fixed one
            const auto volume=continuous_sound_volume(vehicle_siren_volume,vehicle_siren_range,where.distance).value_or(0);
            if(volume==0)release(id);
            else {publish(id,sample,0,frequency,volume,pan);taken_siren=!failed(id);}
        }
    }
}
bool SaVehicleSounds::engine(const void* audio_entity,const void* params,bool player) {
    if(!config_.engines || !lcs_for_native_)return false;
    VehicleRead v;
    if(!read_params_vehicle(params,v))return false;
    const auto lcs_model=lcs_for_native_(v.model);
    const auto* row=lcs_model?vehicle_sound_row(*lcs_model):nullptr;
    if(!row)return false;
    const auto samples=vehicle_engine_samples(row->bank);
    if(!samples)return false;               // helicopters, planes and boats keep SA's engine
    EngineRead engine;
    if(!read_engine(params,v.vehicle,player,engine))return false;
    const uint32_t now=GetTickCount();
    const auto entity=reinterpret_cast<uintptr_t>(audio_entity);
    auto* entry=entry_for(entity,v.model,now);
    if(entry->engine_owner==Entry::Engine::sa)return false;
    const auto idle_id=key(entity,engine_idle_loop),rev_id=key(entity,engine_rev_loop);
    const auto stop_engine=[&]{release(idle_id);release(rev_id);};
    if(!engine.engine_on) {stop_engine();if(entry->engine_owner==Entry::Engine::undecided)entry->engine_owner=Entry::Engine::lcs;return true;}
    const auto where=spatial_of(v.position);
    if(!where.ok)return false;
    const auto sound=vehicle_engine_sound(engine.inputs);
    // 100*entity%1000: each engine gets its own small pitch offset (ProcessVehicleEngine).
    const uint32_t offset=(100u*uint32_t(entity>>4))%1000u;
    const auto volume=continuous_sound_volume(sound.volume,vehicle_engine_range,where.distance).value_or(0);
    const auto pan=continuous_sound_pan(where.distance,where.camera_x).value_or(63);
    // Idle <-> acceleration is a change of volume between two loops that stay open; hold each kind 150 ms.
    if(sound.idle!=entry->engine_idle && now-entry->engine_switch>=150) {entry->engine_idle=sound.idle;entry->engine_switch=now;}
    const uint32_t idle_sample=samples->idle,rev_sample=row->accel_sample;
    const auto frequency_for=[&](uint32_t sample,uint32_t base){
        uint32_t frequency=base+offset;
        if(vehicle_engine_halves_frequency(sample))frequency>>=1;
        return frequency;
    };
    const uint32_t idle_frequency=frequency_for(idle_sample,uint32_t(10000.f*sound.modificator+22050.f));
    const uint32_t rev_frequency=frequency_for(rev_sample,engine.inputs.is_bike?22050u:uint32_t(13000.f*sound.modificator+14000.f));
    if(volume==0) {
        // Out of range: keep the loops muted for 2 s before releasing them, so the edge of the range does not churn.
        if(entry->engine_quiet_since) {if(now-entry->engine_quiet_since>2000)stop_engine();}
        else entry->engine_quiet_since=now?now:1;
        if(now-entry->engine_quiet_since<=2000) {
            publish(idle_id,idle_sample,22050,idle_frequency,0,pan);publish(rev_id,rev_sample,22050,rev_frequency,0,pan);
        }
        if(entry->engine_owner==Entry::Engine::undecided)entry->engine_owner=Entry::Engine::lcs;
        return true;
    }
    entry->engine_quiet_since=0;
    if(entry->engine_owner==Entry::Engine::undecided) {
        // At most eight vehicle engines at once; the rest stay with SA.
        unsigned active=0;
        for(const auto& item:entries_)if(item.second.engine_owner==Entry::Engine::lcs)++active;
        if(active>=8) {entry->engine_owner=Entry::Engine::sa;return false;}
    }
    publish(idle_id,idle_sample,22050,idle_frequency,entry->engine_idle?volume:uint8_t(0),pan);
    publish(rev_id,rev_sample,22050,rev_frequency,entry->engine_idle?uint8_t(0):volume,pan);
    if(failed(idle_id) || failed(rev_id)) {
        ++refusals_;
        stop_engine();
        // Never leave a vehicle silent from the start: SA's engine keeps it until the vehicle is gone.
        if(entry->engine_owner==Entry::Engine::undecided)entry->engine_owner=Entry::Engine::sa;
        return entry->engine_owner==Entry::Engine::lcs;
    }
    entry->engine_owner=Entry::Engine::lcs;
    return true;
}
bool SaVehicleSounds::vehicle_event(const void* audio_entity,int32_t event) {
    const bool door=event>=door_open_first && event<=door_close_last;
    if(!(door && config_.doors) && !(event==start_engine_event && config_.engines))return false;
    VehicleRead v;
    if(!read_entity_vehicle(audio_entity,v) || !lcs_for_native_)return false;
    const auto lcs_model=lcs_for_native_(v.model);
    const auto* row=lcs_model?vehicle_sound_row(*lcs_model):nullptr;
    if(!row)return false;
    uint32_t sample;float range;bool open=false;
    if(door) {
        open=event<door_open_first+6;
        sample=vehicle_door_sample(row->door,open);range=vehicle_door_range*range_scale_;
    } else {
        const auto index=int(row->bank)-4;   // cars only: no starter on bikes, golf carts or aircraft
        if(index<0 || index>13 || index==11)return false;
        sample=vehicle_engine_start_sample();range=vehicle_start_range*range_scale_;
    }
    const auto where=spatial_of(v.position);
    if(!where.ok)return false;
    if(log_ && door_logged_<60) {   // diagnostic: which vehicles raise door / starter events, and how far away
        ++door_logged_;
        log_("Vehicle event "+std::to_string(event)+" SA model "+std::to_string(v.model)+" (LCS "+std::to_string(*lcs_model)+") at "+std::to_string(int(where.distance))+" m");
    }
    if(!(where.distance<range))return true;
    const float distance=where.distance,camera_x=where.camera_x;
    const bool is_door=door;
    worker_->post([this,sample,range,open,is_door,distance,camera_x]{
        random_.regenerate();
        const auto* base=asset(sample,0);
        if(!base)return;
        unsigned emitting=vehicle_start_volume;
        int32_t frequency=int32_t(base->pcm.frequency);
        if(is_door) {
            emitting=open?random_.table(1)%(127u-vehicle_door_open_volume)+vehicle_door_open_volume
                         :random_.table(2)%(127u-vehicle_door_close_volume)+vehicle_door_close_volume;
            frequency+=random_.displacement(uint32_t(frequency)>>5);
        }
        const auto volume=continuous_sound_volume(uint8_t(std::min(emitting,127u)),range,distance);
        const auto pan=continuous_sound_pan(distance,camera_x);
        if(!volume || !pan || *volume==0 || frequency<=0)return;
        AudioSample voice=base->pcm;voice.frequency=uint32_t(frequency);
        speech_.play_owned_effect(voice,AudioMix{scaled(*volume,effects_scale_),*pan});
    });
    return true;
}
void SaVehicleSounds::sweep(uint32_t now) {
    for(auto it=entries_.begin();it!=entries_.end();) {
        if(now-it->second.touched>300) {release_entry(it->first);it=entries_.erase(it);}
        else ++it;
    }
}
void SaVehicleSounds::shutdown() {
    if(!worker_)return;
    worker_->stop();
    worker_.reset();
    {
        std::lock_guard<std::mutex> guard(slot_mutex_);
        for(auto& item:slots_)if(item.second.voice)speech_.stop_owned_effect(*item.second.voice);
        slots_.clear();
    }
    entries_.clear();
    speech_.shutdown();
}
}
