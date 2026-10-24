#include "sa_vehicle_audio.hpp"
#include "lcs/continuous_sound.hpp"
#include "lcs/sa_executor.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
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
        // Read the actual SA transmission gear, as ProcessPlayerVehicleEngine does.
        // Inferring it from road speed ignores shifts/wheelspin and changes the pitch curve.
        const uint32_t gear=*reinterpret_cast<const uint8_t*>(vehicle+0x4B4);
        if(gear>gears)return false;
        out.inputs.speed=speed;out.inputs.max_velocity=max;
        out.inputs.gear_change_down=table[gear].down;out.inputs.first_gear_change_up=table[1].up;
        out.inputs.gas_pedal=std::fabs(*reinterpret_cast<const float*>(vehicle+0x49C));
        out.inputs.gas_audio=*reinterpret_cast<const float*>(vehicle+(type==0?0x964:0x808)); // m_GasPedalAudioRevs
        out.inputs.is_bike=type==9;
        out.inputs.drive_wheels_on_ground=*reinterpret_cast<const uint8_t*>(vehicle+(type==0?0x961:0x805))!=0;
        out.inputs.player=player;
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

SaVehicleSounds::SaVehicleSounds(const AudioArchive& archive,VehicleSoundConfig config)
    :archive_(archive),mixer_(std::make_shared<SaAudioMixer>()),config_(config) {
    worker_=std::make_unique<AudioWorker>([this]{tick();},std::chrono::milliseconds(16));
}
SaVehicleSounds::SaVehicleSounds(const AudioArchive& archive,VehicleSoundConfig config,std::shared_ptr<SaAudioMixer> mixer)
    :archive_(archive),mixer_(mixer?std::move(mixer):std::make_shared<SaAudioMixer>()),config_(config) {
    worker_=std::make_unique<AudioWorker>([this]{tick();},std::chrono::milliseconds(16));
}
const SaVehicleSounds::Asset* SaVehicleSounds::asset(uint32_t sample,uint32_t assumed_rate) {
    const uint64_t cache_key=(uint64_t(sample)<<32)|assumed_rate;
    std::lock_guard<std::mutex> guard(asset_mutex_);
    const auto found=assets_.find(cache_key);
    if(found!=assets_.end())return &found->second;
    try {
        Asset value;
        auto decoded=assumed_rate?archive_.sample(sample,assumed_rate):archive_.sample(sample);
        const auto record=assumed_rate?archive_.record(sample,assumed_rate):archive_.record(sample);
        if(decoded.pcm.size()>std::numeric_limits<uint32_t>::max())return nullptr;
        const auto size=uint32_t(decoded.pcm.size());
        const auto span=continuous_pcm_loop_span(record.size,record.loop_start,record.loop_end);
        value.begin=span?span->begin:0;value.end=span?std::min(span->end,size):size;
        if(!size || value.begin>=value.end || (decoded.channels!=1 && decoded.channels!=2) ||
           decoded.pcm.size()%(size_t(decoded.channels)*2))return nullptr;
        value.silent=std::all_of(decoded.pcm.begin(),decoded.pcm.end(),[](uint8_t byte){return byte==0;});
        value.pcm=std::make_shared<AudioSample>(std::move(decoded));
        return &assets_.emplace(cache_key,std::move(value)).first->second;   // node-based map: the pointer stays valid
    } catch(const std::exception&) {return nullptr;}
}
uint64_t SaVehicleSounds::next_slot_revision() {
    if(++slot_revision_==0)++slot_revision_;
    return slot_revision_;
}
void SaVehicleSounds::publish(uint64_t id,uint32_t sample,uint32_t assumed_rate,uint32_t frequency,uint8_t volume,uint8_t pan) {
    std::lock_guard<std::mutex> guard(slot_mutex_);
    auto& slot=slots_[id];
    const bool restart=!slot.want;
    const bool sample_changed=slot.sample!=sample || slot.assumed_rate!=assumed_rate;
    if(restart || sample_changed) {
        slot.revision=next_slot_revision();
        slot.ready=false;slot.failed=false;slot.silent=false;
        mixer_->release_loop(id);
    }
    slot.want=true;slot.sample=sample;slot.assumed_rate=assumed_rate;slot.frequency=frequency;slot.volume=volume;slot.pan=pan;slot.touched=GetTickCount();
}
void SaVehicleSounds::release(uint64_t id) {
    std::lock_guard<std::mutex> guard(slot_mutex_);
    const auto found=slots_.find(id);
    if(found==slots_.end() || !found->second.want)return;
    auto& slot=found->second;
    slot.want=false;slot.revision=next_slot_revision();
    slot.ready=false;slot.failed=false;slot.silent=false;
    mixer_->release_loop(id);
}
bool SaVehicleSounds::ready(uint64_t id) {
    std::lock_guard<std::mutex> guard(slot_mutex_);
    const auto found=slots_.find(id);
    return found!=slots_.end() && found->second.ready && !found->second.failed;
}
bool SaVehicleSounds::failed(uint64_t id) {
    std::lock_guard<std::mutex> guard(slot_mutex_);
    const auto found=slots_.find(id);
    return found!=slots_.end() && found->second.failed;
}
// Worker thread: resolves assets and publishes loops to the shared PCM mixer.
void SaVehicleSounds::tick() {
    struct Desired {uint64_t id;uint32_t sample,rate;uint64_t revision;};
    std::vector<Desired> desired;
    const uint32_t now=GetTickCount();
    {
        std::lock_guard<std::mutex> guard(slot_mutex_);
        for(auto it=slots_.begin();it!=slots_.end();) {
            auto& slot=it->second;
            const bool alive=slot.want && now-slot.touched<400;
            if(!alive) {
                mixer_->release_loop(it->first);
                it=slots_.erase(it);continue;
            }
            if(!slot.failed && !slot.silent)
                desired.push_back({it->first,slot.sample,slot.assumed_rate,slot.revision});
            ++it;
        }
    }
    for(const auto& request:desired) {
        const auto* a=asset(request.sample,request.rate);
        std::lock_guard<std::mutex> guard(slot_mutex_);
        const auto found=slots_.find(request.id);
        if(found==slots_.end())continue;
        auto& slot=found->second;
        if(!slot.want || slot.revision!=request.revision || slot.sample!=request.sample || slot.assumed_rate!=request.rate)continue;
        if(GetTickCount()-slot.touched>=400) {
            slot.want=false;slot.revision=next_slot_revision();
            slot.ready=false;slot.failed=false;slot.silent=false;
            mixer_->release_loop(request.id);
            slots_.erase(found);
            continue;
        }
        bool accepted=false,silent=false;
        if(a) {
            silent=a->silent;
            if(silent) mixer_->release_loop(request.id);
            else accepted=mixer_->publish_loop(request.id,a->pcm,a->begin,a->end,slot.frequency,slot.volume,slot.pan);
        }
        if(!a || (!silent && !accepted))mixer_->release_loop(request.id);
        slot.silent=silent;
        slot.ready=silent || accepted;
        slot.failed=!a || (!silent && !accepted);
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
    if((!config_.horns && !config_.sirens) || !mixer_ || !mixer_->available())return;
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
        else {publish(id,row->horn_sample,0,uint32_t(row->horn_frequency),volume,pan);taken_horn=ready(id);if(failed(id))taken_horn=false;}
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
            else {publish(id,sample,0,frequency,volume,pan);taken_siren=ready(id);if(failed(id))taken_siren=false;}
        }
    }
}
void SaVehicleSounds::cancel_engine(const void* audio_entity) {
    const auto entity=reinterpret_cast<uintptr_t>(audio_entity);
    release(key(entity,engine_idle_loop));release(key(entity,engine_rev_loop));
    const auto found=entries_.find(entity);
    if(found!=entries_.end())found->second.engine_owner=Entry::Engine::undecided;
}
bool SaVehicleSounds::engine(const void* audio_entity,const void* params,bool player) {
    if(!config_.engines || !lcs_for_native_ || !mixer_ || !mixer_->available())return false;
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
    if(!engine.engine_on) {stop_engine();return entry->engine_owner==Entry::Engine::lcs;}
    const auto where=spatial_of(v.position);
    if(!where.ok)return false;
    const auto sound=vehicle_engine_sound(engine.inputs);
    // 100*entity%1000: each engine gets its own small pitch offset (ProcessVehicleEngine).
    const uint32_t offset=(100u*uint32_t(entity>>4))%1000u;
    const auto volume=continuous_sound_volume(sound.volume,vehicle_engine_range,where.distance).value_or(0);
    const auto pan=continuous_sound_pan(where.distance,where.camera_x).value_or(63);
    // Both loops stay open. Follow the actual pedal state without an artificial
    // 150 ms hold that changes pitch after the driver has already changed input.
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
        else entry->engine_quiet_since=now?now:std::numeric_limits<uint32_t>::max();
        if(now-entry->engine_quiet_since>2000) {
            stop_engine();
            return entry->engine_owner==Entry::Engine::lcs;
        }
        publish(idle_id,idle_sample,22050,idle_frequency,0,pan);publish(rev_id,rev_sample,22050,rev_frequency,0,pan);
    } else {
        entry->engine_quiet_since=0;
        if(entry->engine_owner==Entry::Engine::undecided) {
            // At most eight vehicle engines at once; the rest stay with SA.
            unsigned active=0;
            for(const auto& item:entries_)if(item.second.engine_owner==Entry::Engine::lcs)++active;
            if(active>=8) {entry->engine_owner=Entry::Engine::sa;return false;}
        }
        publish(idle_id,idle_sample,22050,idle_frequency,sound.idle?volume:uint8_t(0),pan);
        publish(rev_id,rev_sample,22050,rev_frequency,sound.idle?uint8_t(0):volume,pan);
    }
    if(failed(idle_id) || failed(rev_id)) {
        ++refusals_;
        stop_engine();
        entry->engine_owner=Entry::Engine::sa;
        return false;
    }
    if(!ready(idle_id) || !ready(rev_id))return false; // keep SA until both loops have PCM and mixer slots
    entry->engine_owner=Entry::Engine::lcs;
    return true;
}
bool SaVehicleSounds::vehicle_event(const void* audio_entity,int32_t event) {
    const auto sound=sa_vehicle_event_sound(event);
    const bool door=sound==SaVehicleEventSound::door_open || sound==SaVehicleEventSound::door_close;
    if((!(door && config_.doors) && !(sound==SaVehicleEventSound::engine_start && config_.engines)) || !mixer_ || !mixer_->available())return false;
    VehicleRead v;
    if(!read_entity_vehicle(audio_entity,v) || !lcs_for_native_)return false;
    const auto lcs_model=lcs_for_native_(v.model);
    const auto* row=lcs_model?vehicle_sound_row(*lcs_model):nullptr;
    if(!row)return false;
    uint32_t sample;float range;bool open=false;
    if(door) {
        open=sound==SaVehicleEventSound::door_open;
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
        if(!base || base->silent)return;
        unsigned emitting=vehicle_start_volume;
        int32_t frequency=int32_t(base->pcm->frequency);
        if(is_door) {
            emitting=open?random_.table(1)%(127u-vehicle_door_open_volume)+vehicle_door_open_volume
                         :random_.table(2)%(127u-vehicle_door_close_volume)+vehicle_door_close_volume;
            frequency+=random_.displacement(uint32_t(frequency)>>5);
        }
        const auto volume=continuous_sound_volume(uint8_t(std::min(emitting,127u)),range,distance);
        const auto pan=continuous_sound_pan(distance,camera_x);
        if(!volume || !pan || *volume==0 || frequency<=0)return;
        mixer_->play_one_shot(base->pcm,uint32_t(frequency),*volume,*pan);
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
        for(const auto& item:slots_)mixer_->release_loop(item.first);
        slots_.clear();
    }
    entries_.clear();
}
}
