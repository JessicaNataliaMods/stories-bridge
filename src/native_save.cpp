#include "lcs/native.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace lcs {
namespace {
constexpr uint16_t native_save_version=2;
constexpr size_t native_save_limit=1u<<20;

struct SaveWriter {
    std::vector<uint8_t> bytes;
    void u8(uint8_t v){bytes.push_back(v);}
    void u16(uint16_t v){u8(uint8_t(v));u8(uint8_t(v>>8));}
    void u32(uint32_t v){for(unsigned i=0;i<4;++i)u8(uint8_t(v>>(i*8)));}
    void i32(int32_t v){u32(uint32_t(v));}
    void u64(uint64_t v){for(unsigned i=0;i<8;++i)u8(uint8_t(v>>(i*8)));}
    void f32(float v){uint32_t bits=0;std::memcpy(&bits,&v,4);u32(bits);}
    void boolean(bool v){u8(v?1:0);}
    void text(const std::string& v,size_t max,const char* field){
        if(v.size()>max || v.find('\0')!=std::string::npos)
            throw std::runtime_error(std::string("Invalid ")+field+" in native save state");
        u16(uint16_t(v.size()));bytes.insert(bytes.end(),v.begin(),v.end());
    }
    void count(size_t n,size_t max,const char* field){
        if(n>max || n>std::numeric_limits<uint32_t>::max())
            throw std::runtime_error(std::string(field)+" exceeds native save capacity");
        u32(uint32_t(n));
    }
    void finish(){if(bytes.size()>native_save_limit)throw std::runtime_error("Native save state exceeds 1 MiB");}
};

struct SaveReader {
    const std::vector<uint8_t>& bytes;size_t at=0;
    explicit SaveReader(const std::vector<uint8_t>& b):bytes(b){
        if(b.size()>native_save_limit)throw std::runtime_error("Native save state exceeds 1 MiB");
    }
    uint8_t u8(){if(at>=bytes.size())throw std::runtime_error("Truncated native save state");return bytes[at++];}
    uint16_t u16(){const uint16_t a=u8();return uint16_t(a|(uint16_t(u8())<<8));}
    uint32_t u32(){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(u8())<<(i*8);return v;}
    int32_t i32(){return int32_t(u32());}
    uint64_t u64(){uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(u8())<<(i*8);return v;}
    float f32(){const auto bits=u32();float v=0;std::memcpy(&v,&bits,4);return v;}
    bool boolean(){const auto v=u8();if(v>1)throw std::runtime_error("Invalid boolean in native save state");return v!=0;}
    std::string text(size_t max,const char* field){
        const auto n=u16();if(n>max || n>bytes.size()-at)throw std::runtime_error(std::string("Invalid ")+field+" length in native save state");
        std::string v(bytes.begin()+at,bytes.begin()+at+n);at+=n;
        if(v.find('\0')!=std::string::npos)throw std::runtime_error(std::string("Invalid ")+field+" in native save state");
        return v;
    }
    size_t count(size_t max,const char* field){const auto n=u32();if(n>max)throw std::runtime_error(std::string(field)+" exceeds native save capacity");return n;}
    void expect(const char* value,size_t n){for(size_t i=0;i<n;++i)if(u8()!=uint8_t(value[i]))throw std::runtime_error("Native save state signature mismatch");}
    void finish(){if(at!=bytes.size())throw std::runtime_error("Trailing bytes in native save state");}
};

bool finite(const std::array<float,3>& values){return std::all_of(values.begin(),values.end(),[](float v){return std::isfinite(v);});}
template<size_t N> bool finite(const std::array<float,N>& values){return std::all_of(values.begin(),values.end(),[](float v){return std::isfinite(v);});}
bool valid_label(const std::string& value,size_t max=8){
    return !value.empty() && value.size()<=max && std::all_of(value.begin(),value.end(),[](unsigned char c){return std::isalnum(c)||c=='_';});
}
std::array<uint32_t,3> float_key(const std::array<float,3>& v){
    std::array<uint32_t,3> key{};for(size_t i=0;i<3;++i)std::memcpy(&key[i],&v[i],4);return key;
}
template<class Map> auto ordered_keys(const Map& map){
    using Key=typename Map::key_type;std::vector<Key> keys;keys.reserve(map.size());
    for(const auto& entry:map)keys.push_back(entry.first);std::sort(keys.begin(),keys.end());return keys;
}
template<class Set> auto ordered_values(const Set& set){
    using Value=typename Set::value_type;std::vector<Value> values(set.begin(),set.end());std::sort(values.begin(),values.end());return values;
}
void write_activity(SaveWriter& w,const ActivityStatistics& a){
    const int32_t values[]={a.industrial_passed,a.commercial_passed,a.suburban_passed,a.cars_sold,a.car_sales_money,
        a.bikes_sold,a.bike_sales_money,a.trash_money,a.hitmen_killed,a.guardian_missions,a.guardian_justice,
        a.train_time_left,a.carnage_seconds,a.rc_triad_kills,a.slash_level,a.slash_money,a.slash_kills,
        a.dirt_air_time,a.gogo_time};
    for(auto v:values)w.i32(v);
    for(auto v:a.bandit_laps)w.i32(v);for(auto v:a.bandit_positions)w.i32(v);
    for(auto v:a.street_times)w.i32(v);for(auto v:a.street_laps)w.i32(v);for(auto v:a.street_positions)w.i32(v);
    for(auto v:a.dirt_laps)w.i32(v);for(auto v:a.dirt_times)w.i32(v);for(auto v:a.guardian_levels)w.i32(v);
    w.u16(a.unlocked_costumes);
    const uint32_t bits[]={a.face_plant_distance_bits,a.scrapyard_score_bits,a.mayhem_score_bits,a.scooter_score_bits,
        a.wichita_score_bits,a.pizzas_delivered_bits,a.noodles_delivered_bits,a.tourist_money_bits,a.tourist_spots_bits};
    for(auto v:bits)w.u32(v);
}
ActivityStatistics read_activity(SaveReader& r){
    ActivityStatistics a;
    int32_t* values[]={&a.industrial_passed,&a.commercial_passed,&a.suburban_passed,&a.cars_sold,&a.car_sales_money,
        &a.bikes_sold,&a.bike_sales_money,&a.trash_money,&a.hitmen_killed,&a.guardian_missions,&a.guardian_justice,
        &a.train_time_left,&a.carnage_seconds,&a.rc_triad_kills,&a.slash_level,&a.slash_money,&a.slash_kills,
        &a.dirt_air_time,&a.gogo_time};
    for(auto p:values)*p=r.i32();
    for(auto& v:a.bandit_laps)v=r.i32();for(auto& v:a.bandit_positions)v=r.i32();
    for(auto& v:a.street_times)v=r.i32();for(auto& v:a.street_laps)v=r.i32();for(auto& v:a.street_positions)v=r.i32();
    for(auto& v:a.dirt_laps)v=r.i32();for(auto& v:a.dirt_times)v=r.i32();for(auto& v:a.guardian_levels)v=r.i32();
    a.unlocked_costumes=r.u16();
    uint32_t* bits[]={&a.face_plant_distance_bits,&a.scrapyard_score_bits,&a.mayhem_score_bits,&a.scooter_score_bits,
        &a.wichita_score_bits,&a.pizzas_delivered_bits,&a.noodles_delivered_bits,&a.tourist_money_bits,&a.tourist_spots_bits};
    for(auto p:bits)*p=r.u32();return a;
}
}

std::vector<uint8_t> NativeAdapter::save_state(uint32_t now_ms) const {
    NativeAdapter projected(*this);
    std::set<int32_t> actors;
    const auto collect=[&](const auto& map){for(const auto& item:map)actors.insert(item.first);};
    collect(projected.running_chars_);collect(projected.walking_objectives_);collect(projected.accepted_walk_tasks_);
    collect(projected.attack_policy_);collect(projected.kill_objectives_);collect(projected.pednode_seek_);
    collect(projected.traffic_waits_);collect(projected.companions_);collect(projected.front_seats_);collect(projected.rear_seats_);
    collect(projected.equivalent_weapons_);collect(projected.equivalent_outfits_);collect(projected.worn_outfits_);
    for(auto actor:projected.frozen_followers_)actors.insert(actor);
    if(projected.frenzy_alias_)actors.insert(projected.frenzy_alias_->actor);

    std::optional<int32_t> player;
    if(!actors.empty()){
        const auto result=projected.execute?projected.execute({0x01F5,{0},{},0,1}):NativeResult{};
        if(!result.executed||result.values[0]<0)throw std::runtime_error("Cannot project transient identities without resolving native Player1");
        player=result.values[0];
    }
    const auto erase_non_player=[&](auto& map){
        for(auto it=map.begin();it!=map.end();)if(!player||it->first!=*player)it=map.erase(it);else ++it;
    };
    erase_non_player(projected.running_chars_);erase_non_player(projected.walking_objectives_);
    erase_non_player(projected.accepted_walk_tasks_);erase_non_player(projected.attack_policy_);
    erase_non_player(projected.kill_objectives_);erase_non_player(projected.pednode_seek_);
    erase_non_player(projected.traffic_waits_);erase_non_player(projected.companions_);
    erase_non_player(projected.front_seats_);erase_non_player(projected.rear_seats_);
    erase_non_player(projected.equivalent_weapons_);erase_non_player(projected.equivalent_outfits_);
    erase_non_player(projected.worn_outfits_);
    for(auto it=projected.frozen_followers_.begin();it!=projected.frozen_followers_.end();)
        if(!player||*it!=*player)it=projected.frozen_followers_.erase(it);else ++it;
    if(projected.frenzy_alias_&&(!player||projected.frenzy_alias_->actor!=*player))projected.frenzy_alias_.reset();

    if(player&&(!projected.walking_objectives_.empty()||!projected.accepted_walk_tasks_.empty()||!projected.kill_objectives_.empty())){
        if(!projected.char_has_no_objective)
            throw std::runtime_error("Cannot save Player1 walking-objective cache: native current/suspended objective reader is unavailable");
        const auto no_objective=projected.char_has_no_objective(*player);
        if(!no_objective)
            throw std::runtime_error("Cannot save Player1 walking-objective cache: native current/suspended objective state is unreadable");
        if(!*no_objective)
            throw std::runtime_error("Cannot save Player1 while a walking objective is active; the paired restore does not reissue that task");
        // The native task has already completed or been cancelled. Keep this
        // projection local; the live adapter still owns its callback cache.
        projected.walking_objectives_.erase(*player);
        projected.accepted_walk_tasks_.erase(*player);
        projected.kill_objectives_.erase(*player);
    }
    return projected.save_state_projected(now_ms);
}

std::vector<uint8_t> NativeAdapter::save_state_projected(uint32_t now_ms) const {
    // These records own exact native task epochs, in-flight actor identities,
    // or a cutscene lifetime. A normal out-of-mission save must not capture a
    // partially executing instance of them.
    if(current_cutscene_ || cutscene_started_)throw std::runtime_error("Cannot save NativeAdapter during an active cutscene");
    if(!walking_objectives_.empty())throw std::runtime_error("Cannot save NativeAdapter: unresolved walking-objective cache remains after native task-state check");
    if(!accepted_walk_tasks_.empty())throw std::runtime_error("Cannot save NativeAdapter: accepted walking-task receipt remains after native task-state check");
    if(!kill_objectives_.empty())throw std::runtime_error("Cannot save NativeAdapter: active kill-objective receipt is not restored by the paired save");
    if(!attack_policy_.empty())throw std::runtime_error("Cannot save NativeAdapter: active attack-with-cops policy is not restored by the paired save");
    if(!companions_.empty())throw std::runtime_error("Cannot save NativeAdapter: companion boarding state is not restored by the paired save");
    if(!frozen_followers_.empty())throw std::runtime_error("Cannot save NativeAdapter: frozen-follower state is not restored by the paired save");
    if(!traffic_waits_.empty())throw std::runtime_error("Cannot save NativeAdapter: scripted traffic-wait state is not restored by the paired save");
    if(frenzy_alias_)throw std::runtime_error("Cannot save NativeAdapter: temporary frenzy weapon alias is not restored by the paired save");

    SaveWriter w;w.bytes.insert(w.bytes.end(),{'L','C','N','S'});w.u16(native_save_version);w.u16(0);
    w.i32(unique_jumps_total);w.boolean(fort_staunton_flag_.has_value());if(fort_staunton_flag_)w.u32(*fort_staunton_flag_);
    w.boolean(developer_flag_);w.i32(kill_frenzies_total_);w.i32(export_cars_total_);
    w.f32(jump_records_.distance);w.f32(jump_records_.height);w.i32(jump_records_.flips);w.i32(jump_records_.spins);w.i32(jump_records_.best_stunt);
    write_activity(w,activity_statistics_);
    for(auto v:stored_weather_)w.i32(v);for(auto v:stored_clock_)w.i32(v);
    w.boolean(rubbish_visible_);for(auto v:scene_light_direction_)w.f32(v);
    w.i32(original_radio_channel_);w.i32(original_radio_seek_);w.i32(original_music_mode_);
    w.u32(outfit_changes_);w.text(stored_outfit_,8,"stored outfit");

    w.i32(next_garage_);
    w.count(garages_.size(),50,"Garage bindings");
    for(const auto id:ordered_keys(garages_)){w.i32(id);w.text(garages_.at(id),8,"garage name");}
    w.count(garage_definitions_.size(),50,"Garage declarations");
    for(const auto id:ordered_keys(garage_definitions_)){
        const auto& g=garage_definitions_.at(id);w.i32(id);w.i32(g.type);for(auto v:g.bounds)w.f32(v);
        w.boolean(g.camera_follows_player);w.u8(g.max_stored_cars);
    }

    w.count(phones_.size(),60,"Phone pool");w.u32(uint32_t(claimed_phones_));
    for(const auto& phone:phones_){for(auto v:phone.position)w.f32(v);w.boolean(phone.message_removed);w.boolean(phone.enabled);}
    // Frame receipts and renderer pointers are intentionally frame-local; the
    // native phone phase recomputes visibility from the reloaded world.
    w.count(shortcut_pickups_.size(),16,"Shortcut pickup points");
    for(const auto& p:shortcut_pickups_)for(auto v:p)w.f32(v);
    w.boolean(shortcut_dropoff_.has_value());if(shortcut_dropoff_)for(auto v:*shortcut_dropoff_)w.f32(v);

    w.count(building_groups_.size(),512,"Building groups");
    for(const auto id:ordered_keys(building_groups_)){w.i32(id);w.i32(building_groups_.at(id));}
    w.count(building_swap_models_.size(),4096,"Building swap models");for(auto id:ordered_values(building_swap_models_))w.i32(id);
    w.count(zone_population_.size(),1024,"Zone population entries");
    for(const auto& zone:ordered_keys(zone_population_)){
        const auto& p=zone_population_.at(zone);w.text(zone,8,"zone name");
        const auto pair=[&](const auto& values){for(const auto& day:values){w.boolean(day.has_value());if(day)for(auto v:*day)w.i32(v);}};
        pair(p.pedestrians);pair(p.cars);pair(p.civilians);
        for(const auto& day:p.groups){w.boolean(day.has_value());if(day)w.i32(*day);}
    }
    w.count(gang_models_.size(),9,"Gang model entries");
    for(auto gang:ordered_keys(gang_models_)){w.i32(gang);for(auto model:gang_models_.at(gang))w.i32(model);}
    w.count(gang_cars_.size(),9,"Gang vehicle entries");
    for(auto gang:ordered_keys(gang_cars_)){w.i32(gang);w.i32(gang_cars_.at(gang));}
    w.count(nonped_threats_.size(),64,"Threat policy entries");
    for(auto type:ordered_keys(nonped_threats_)){w.i32(type);w.u32(nonped_threats_.at(type));}

    w.count(weapon_definitions_.size(),37,"Weapon definitions");
    for(const auto& definition:weapon_definitions_){w.boolean(definition.has_value());if(definition){w.i32(definition->model);w.i32(definition->slot);}}
    std::set<int32_t> alias_actors;
    for(const auto& item:equivalent_weapons_)alias_actors.insert(item.first);
    for(const auto& item:equivalent_outfits_)alias_actors.insert(item.first);
    for(const auto& item:worn_outfits_)alias_actors.insert(item.first);
    for(const auto& item:running_chars_)alias_actors.insert(item.first);
    for(const auto& item:pednode_seek_)alias_actors.insert(item.first);
    for(const auto& item:front_seats_)alias_actors.insert(item.first);
    for(const auto& item:rear_seats_)alias_actors.insert(item.first);
    const auto require_ped=[&](int32_t handle,const char* field){
        if(handle<0 || !live_ped_identity)throw std::runtime_error(std::string("Cannot rebase ")+field+" actor handle "+std::to_string(handle));
        const auto live=live_ped_identity(handle);
        if(!live || live->handle!=handle || !live->native_ped)throw std::runtime_error(std::string("Cannot verify ")+field+" actor identity "+std::to_string(handle));
    };
    std::optional<int32_t> saved_player;
    if(!alias_actors.empty()){
        const auto player=execute?execute({0x01F5,{0},{},0,1}):NativeResult{};
        if(!player.executed || player.values[0]<0)throw std::runtime_error("Cannot verify the native saved player");
        saved_player=player.values[0];
    }
    w.count(alias_actors.size(),384,"Actor alias identities");
    for(auto actor:alias_actors){
        // SA SavePedPool persists Player1, not arbitrary script NPCs. Do not
        // publish a companion whose alias cannot exist after native load.
        if(actor!=*saved_player)throw std::runtime_error("Actor alias is not included by native SA SavePedPool");
        require_ped(actor,"alias");
        if(!execute)throw std::runtime_error("Cannot fingerprint an actor alias without the native model/position reader");
        const auto model=execute({0x0665,{actor},{},0,1});
        const auto position=execute({0x00A0,{actor},{},0,3});
        if(!model.executed || model.values[0]<0 || model.values[0]>=20000 || !position.executed)
            throw std::runtime_error("Cannot fingerprint a persistent actor alias in the SA pool");
        w.i32(actor);w.i32(model.values[0]);
        for(size_t axis=0;axis<3;++axis){const auto bits=position.values[axis];float value=0;std::memcpy(&value,&bits,sizeof(value));if(!std::isfinite(value))throw std::runtime_error("Actor alias has a non-finite SA position");w.f32(value);}
    }
    w.count(equivalent_weapons_.size(),128,"Weapon alias actors");
    for(auto actor:ordered_keys(equivalent_weapons_)){
        require_ped(actor,"weapon alias");w.i32(actor);const auto& aliases=equivalent_weapons_.at(actor);
        w.count(aliases.size(),64,"Weapon aliases");for(auto weapon:ordered_keys(aliases)){w.i32(weapon);w.i32(aliases.at(weapon));}
    }
    w.count(equivalent_outfits_.size(),128,"Pending outfit actors");
    for(auto actor:ordered_keys(equivalent_outfits_)){require_ped(actor,"pending outfit");w.i32(actor);w.text(equivalent_outfits_.at(actor),8,"pending outfit name");}
    w.count(worn_outfits_.size(),128,"Worn outfit actors");
    for(auto actor:ordered_keys(worn_outfits_)){require_ped(actor,"worn outfit");const auto& outfit=worn_outfits_.at(actor);w.i32(actor);w.text(outfit.label,8,"worn outfit name");w.i32(outfit.native_model);}

    const auto require_player_policy=[&](int32_t actor,const char* field){
        if(!saved_player||actor!=*saved_player)throw std::runtime_error(std::string("Cannot save non-player ")+field+" after SA SavePedPool projection");
        require_ped(actor,field);
    };
    w.count(running_chars_.size(),1,"Player running policies");
    for(auto actor:ordered_keys(running_chars_)){require_player_policy(actor,"running policy");w.i32(actor);w.boolean(running_chars_.at(actor));}
    w.count(pednode_seek_.size(),1,"Player pednode-seek policies");
    for(auto actor:ordered_keys(pednode_seek_)){require_player_policy(actor,"pednode-seek policy");w.i32(actor);w.boolean(pednode_seek_.at(actor));}
    uint8_t seat_mask=0;const int32_t seat_actor=saved_player.value_or(-1);
    if(!front_seats_.empty()){const auto actor=front_seats_.begin()->first;require_player_policy(actor,"front-seat policy");const auto live=live_ped_identity?live_ped_identity(actor):std::nullopt;if(front_seats_.size()!=1||!live||!(front_seats_.at(actor).actor==*live))throw std::runtime_error("Cannot save Player1 front-seat policy with an unverified live identity");seat_mask|=1;}
    if(!rear_seats_.empty()){const auto actor=rear_seats_.begin()->first;require_player_policy(actor,"rear-seat policy");const auto live=live_ped_identity?live_ped_identity(actor):std::nullopt;if(rear_seats_.size()!=1||!live||!(rear_seats_.at(actor).actor==*live))throw std::runtime_error("Cannot save Player1 rear-seat policy with an unverified live identity");seat_mask|=2;}
    if((seat_mask&1)&&!front_seats_.count(seat_actor))throw std::runtime_error("Player front-seat policy actor does not match saved Player1");
    if((seat_mask&2)&&!rear_seats_.count(seat_actor))throw std::runtime_error("Player rear-seat policy actor does not match saved Player1");
    w.u8(seat_mask);if(seat_mask)w.i32(seat_actor);

    w.count(owned_blips_.size(),512,"Owned blips");for(auto id:ordered_values(owned_blips_))w.i32(id);
    w.count(owned_spheres_.size(),512,"Owned spheres");for(auto id:ordered_values(owned_spheres_))w.i32(id);

    // Vehicle aliases are paired with an exact SA pool generation and position.
    // The entity pointer is read for verification only and never enters the blob.
    const auto lookup_vehicle=[&](int32_t handle)->RouteVehicleLookup{
        if(exclusive_garages.world.vehicle_lookup)return exclusive_garages.world.vehicle_lookup(handle);
        if(crane_service.world.vehicle_lookup)return crane_service.world.vehicle_lookup(handle);
        return {};
    };
    const auto lookup_status_name=[](VehicleLookupStatus status){
        switch(status){case VehicleLookupStatus::unavailable:return "unavailable";case VehicleLookupStatus::absent:return "absent";case VehicleLookupStatus::present:return "present";}
        return "invalid";
    };
    struct SavedVehicleAlias{int32_t handle=-1,original=-1;uint8_t generation=0;std::array<float,3> position{};};
    std::vector<SavedVehicleAlias> saved_vehicle_aliases;saved_vehicle_aliases.reserve(original_vehicle_models_.size());
    for(auto handle:ordered_keys(original_vehicle_models_)){
        const auto original=original_vehicle_models_.at(handle);const auto mapped=binding(original);
        if(handle<0)throw std::runtime_error("Cannot save original vehicle alias handle "+std::to_string(handle)+": invalid handle, lookup status unavailable");
        const auto live=lookup_vehicle(handle);
        if(live.status==VehicleLookupStatus::absent)continue;
        if(live.status==VehicleLookupStatus::unavailable)
            throw std::runtime_error("Cannot save original vehicle alias handle "+std::to_string(handle)+": SA pool lookup status unavailable");
        if(live.status!=VehicleLookupStatus::present || !live.snapshot || live.snapshot->handle!=handle || !live.snapshot->entity ||
           live.snapshot->generation!=uint8_t(handle) || !finite(live.snapshot->position))
            throw std::runtime_error("Cannot save original vehicle alias handle "+std::to_string(handle)+": malformed snapshot, lookup status "+lookup_status_name(live.status));
        if(!mapped || mapped->kind!=ModelKind::vehicle)
            throw std::runtime_error("Cannot save original vehicle alias handle "+std::to_string(handle)+": invalid original model binding, lookup status "+lookup_status_name(live.status));
        if(!execute)throw std::runtime_error("Cannot save original vehicle alias handle "+std::to_string(handle)+": native model reader unavailable, lookup status "+lookup_status_name(live.status));
        const auto native=execute({0x0441,{handle},{},0,1});
        if(!native.executed || native.values[0]!=mapped->id)
            throw std::runtime_error("Cannot save original vehicle alias handle "+std::to_string(handle)+": native model mismatch/unreadable, lookup status "+lookup_status_name(live.status));
        saved_vehicle_aliases.push_back({handle,original,live.snapshot->generation,live.snapshot->position});
    }
    w.count(saved_vehicle_aliases.size(),256,"Original vehicle aliases");
    for(const auto& alias:saved_vehicle_aliases){w.i32(alias.handle);w.i32(alias.original);w.u8(alias.generation);for(auto v:alias.position)w.f32(v);}
    std::vector<int32_t> dynamic_models;
    for(const auto& item:models_)if(item.first>=native_vehicle_model_base)dynamic_models.push_back(item.first);
    std::sort(dynamic_models.begin(),dynamic_models.end());w.count(dynamic_models.size(),256,"Dynamic vehicle model aliases");
    for(auto id:dynamic_models){const auto& model=models_.at(id);if(model.kind!=ModelKind::vehicle || model.id<0 || model.id>=20000 || id!=native_vehicle_model_base+model.id)throw std::runtime_error("Invalid dynamic vehicle model alias");w.i32(model.id);}

    // Native bridge garage declarations and their service phases. Any paid,
    // quarantined or held-car transaction cannot be resumed from a disk save.
    w.count(exclusive_garages.garages_.size(),50,"Exclusive garage service entries");
    std::array<std::optional<uint32_t>,4> collected{};
    for(auto id:ordered_keys(exclusive_garages.garages_)){
        const auto& g=exclusive_garages.garages_.at(id);const auto definition=garage_definitions_.find(id);
        if(definition==garage_definitions_.end() || definition->second.type!=g.type || definition->second.bounds!=g.bounds)
            throw std::runtime_error("Exclusive garage declaration does not match NativeAdapter geometry");
        if(g.finish.started || g.finish.action_started || g.finish.quarantined ||
           g.hold_active || g.held_target.handle>=0 || g.door_quarantined || g.aux_quarantined)
            throw std::runtime_error("Cannot save an in-flight or quarantined exclusive garage transaction");
        if(!std::isfinite(g.door) || !std::isfinite(g.door_height) ||
           (g.door_height_known && !(g.door_height>0.0f)))throw std::runtime_error("Invalid exclusive garage door state");
        const auto target=g.target;
        uint8_t generation=0;
        if(target.handle>=0){
            const auto live=lookup_vehicle(target.handle);
            if(live.status!=VehicleLookupStatus::present || !live.snapshot || !live.snapshot->entity || live.snapshot->generation!=target.generation || target.entity!=live.snapshot->entity)
                throw std::runtime_error("Cannot verify exclusive garage target identity for save");
            generation=live.snapshot->generation;
        }else if(target.entity || target.generation)throw std::runtime_error("Exclusive garage has an unowned target identity");
        uint32_t timer=0;
        if(g.state==ExclusiveGarages::State::waiting_for_car)timer=std::min<uint32_t>(now_ms-g.crane_ms,3001u);
        else if(g.state==ExclusiveGarages::State::after_drop_off)timer=g.action_ms>now_ms?g.action_ms-now_ms:0;
        w.i32(id);w.i32(g.type);w.u8(uint8_t(g.state));w.f32(g.door);w.i32(target.handle);w.u8(generation);
        w.u32(timer);w.f32(g.door_height);w.boolean(g.door_height_known);
        if(ExclusiveGarages::collector(g.type)){
            if(!exclusive_garages.world.collected_read)throw std::runtime_error("Collector save requires the SA saved-list reader");
            const auto index=ExclusiveGarages::table_index(g.type);const auto mask=exclusive_garages.world.collected_read(uint32_t(index));
            if(!mask)throw std::runtime_error("Collector saved list is unavailable");collected[index]=*mask;
        }
    }
    for(const auto& mask:collected){w.boolean(mask.has_value());if(mask)w.u32(*mask);}
    w.u8(exclusive_garages.multiplier_);

    // Crane declarations have stable world coordinates. Only idle cranes can
    // be restored; a carried vehicle or active lift is an in-flight action.
    w.count(cranes_.size(),11,"Crane declarations");
    if(crane_service.cranes_.size()!=cranes_.size())throw std::runtime_error("Crane service and mounted crane declarations differ");
    for(size_t i=0;i<cranes_.size();++i){
        const auto& declaration=cranes_[i];const auto& c=crane_service.cranes_[i];
        if(declaration.origin!=c.origin || !finite(declaration.origin) || c.phase!=CraneService::Phase::idle || c.holding ||
           c.target.handle>=0 || c.target.entity || c.target.generation)
            throw std::runtime_error("Cannot save a crane while a vehicle transition is active");
        if(declaration.activated!=(c.status!=CraneService::Status::none) || declaration.crusher!=c.crusher)
            throw std::runtime_error("Crane activation does not match the mounted declaration");
        w.boolean(declaration.activated);w.boolean(declaration.crusher);w.i32(declaration.model);
        for(auto v:declaration.origin)w.f32(v);for(auto v:declaration.pickup)w.f32(v);
        for(auto v:declaration.dropoff)w.f32(v);w.f32(declaration.heading);
        w.u32(c.seed);w.u8(uint8_t(c.status));for(auto v:c.hook_cur)w.f32(v);
        for(auto v:c.hook_velocity)w.f32(v);w.f32(c.hook_angle);w.f32(c.hook_offset);w.f32(c.hook_height);
        w.u32(c.next_check>now_ms?c.next_check-now_ms:0);w.u8(c.collected);w.u32(c.scan_slice);
    }
    // Message-once entries have no native ownership effect. Stale vehicles are
    // omitted because they cannot trigger a future duplicate message.
    std::vector<CraneService::Target> live_messages;
    for(const auto& target:crane_service.messaged_){
        if(target.handle<0 || !target.entity)throw std::runtime_error("Crane message cache contains an invalid vehicle identity");
        const auto live=lookup_vehicle(target.handle);
        if(live.status==VehicleLookupStatus::unavailable)throw std::runtime_error("Cannot verify a crane message vehicle identity for save");
        if(live.status==VehicleLookupStatus::present && live.snapshot && live.snapshot->generation==target.generation && live.snapshot->entity==target.entity)
            live_messages.push_back(target);
    }
    w.count(live_messages.size(),256,"Crane message identities");
    for(const auto& target:live_messages){const auto live=lookup_vehicle(target.handle);w.i32(target.handle);w.u8(target.generation);for(auto v:live.snapshot->position)w.f32(v);}

    // Ferry track data itself is mounted from the user's private source file;
    // save node identities to reject a mismatched path, then store live clocks.
    for(size_t p=0;p<FerryService::paths;++p){
        w.boolean(ferry_service.have_path_[p]);if(ferry_service.have_path_[p]){
            const auto& path=ferry_service.paths_[p];w.count(path.nodes.size(),10000,"Ferry path nodes");
            for(const auto& node:path.nodes){w.f32(node.x);w.f32(node.y);}
        }
    }
    w.boolean(ferry_service.disabled_);w.boolean(ferry_service.enable_failed_);w.boolean(ferry_service.enable_requested_);
    w.u32(ferry_service.pending_ticks_);
    for(size_t i=0;i<FerryService::max_ferries;++i){
        const auto& f=ferry_service.ferries_[i];w.boolean(f.exists);w.i32(f.exists?f.id:int32_t(i));
        for(auto v:f.pose.position)w.f32(v);for(auto v:f.pose.forward)w.f32(v);for(auto v:f.pose.right)w.f32(v);for(auto v:f.pose.up)w.f32(v);
        w.u32(uint32_t(f.cur_node));w.u32(f.time_along);w.f32(f.position);w.f32(f.speed_units);
        for(auto v:f.move_speed)w.f32(v);w.boolean(f.docked);w.boolean(f.approaching);w.i32(f.skip);
        w.u8(uint8_t(f.door));w.u32(f.door_timer>ferry_service.last_now_?f.door_timer-ferry_service.last_now_:0);
        w.boolean(f.front_door);w.f32(f.door_ratio);w.boolean(f.door_dirty);w.i32(f.collision);
    }
    w.finish();return std::move(w.bytes);
}

bool NativeAdapter::restore_state(const std::vector<uint8_t>& bytes,uint32_t now_ms,std::string& diagnostic) {
    try {
        SaveReader r(bytes);r.expect("LCNS",4);const auto version=r.u16();if(version!=1&&version!=native_save_version)throw std::runtime_error("Unsupported NativeAdapter save version "+std::to_string(version));
        if(r.u16()!=0)throw std::runtime_error("Unknown NativeAdapter save flags");
        const int32_t unique_jumps=r.i32();const bool has_fort=r.boolean();std::optional<uint32_t> fort;if(has_fort)fort=r.u32();
        const bool developer=r.boolean();const int32_t frenzy_total=r.i32(),export_total=r.i32();
        JumpRecords jumps;jumps.distance=r.f32();jumps.height=r.f32();jumps.flips=r.i32();jumps.spins=r.i32();jumps.best_stunt=r.i32();
        auto activity=read_activity(r);std::array<int32_t,4> weather{};std::array<int32_t,3> clock{};
        for(auto& v:weather)v=r.i32();for(auto& v:clock)v=r.i32();const bool rubbish=r.boolean();std::array<float,3> light{};for(auto& v:light)v=r.f32();
        const int32_t radio=r.i32(),seek=r.i32(),music=r.i32();const uint32_t outfit_changes=r.u32();const auto stored_outfit=r.text(8,"stored outfit");
        if(!std::isfinite(jumps.distance)||!std::isfinite(jumps.height)||!finite(light))throw std::runtime_error("Non-finite NativeAdapter statistic or scene state");
        if(radio<0||radio>10||seek< -1||seek>10||music<0||music>3)throw std::runtime_error("Invalid saved LCS radio/music state");

        const int32_t next_garage=r.i32();if(next_garage<0||next_garage>50)throw std::runtime_error("Invalid saved garage cursor");
        std::unordered_map<int32_t,std::string> garages;std::set<std::string> garage_names;
        const auto binding_count=r.count(50,"Garage bindings");
        for(size_t i=0;i<binding_count;++i){const auto id=r.i32();const auto name=r.text(8,"garage name");
            if(id<0||id>=next_garage||!valid_label(name)||!garage_names.insert(name).second||!garages.emplace(id,name).second)
                throw std::runtime_error("Invalid or conflicting saved garage binding");}
        struct GarageDefinitionLoad{std::array<float,8> bounds{};int32_t type=0;bool camera=false;uint8_t capacity=4;};
        std::unordered_map<int32_t,GarageDefinitionLoad> definitions;
        const auto definition_count=r.count(50,"Garage declarations");
        if(definition_count!=size_t(next_garage))throw std::runtime_error("Saved garage declaration count does not match its cursor");
        for(size_t i=0;i<definition_count;++i){const auto id=r.i32();GarageDefinitionLoad d;d.type=r.i32();for(auto& v:d.bounds)v=r.f32();d.camera=r.boolean();d.capacity=r.u8();
            if(id<0||id>=next_garage||d.type<0||d.type>32||!finite(d.bounds)||!definitions.emplace(id,d).second)
                throw std::runtime_error("Invalid saved garage declaration");}
        for(int32_t id=0;id<next_garage;++id)if(!definitions.count(id))throw std::runtime_error("Saved garage declarations contain a gap");
        for(const auto& binding:garages)if(!definitions.count(binding.first))throw std::runtime_error("Saved garage binding has no declaration");

        const auto phone_count=r.count(60,"Phone pool");const auto claimed=r.u32();if(claimed>phone_count)throw std::runtime_error("Saved claimed-phone count exceeds the mounted pool");
        std::vector<Phone> phones;phones.reserve(phone_count);
        for(size_t i=0;i<phone_count;++i){Phone phone{};for(auto& v:phone.position)v=r.f32();phone.message_removed=r.boolean();phone.enabled=r.boolean();
            if(!finite(phone.position)||phone.enabled&&i>=claimed||i<claimed&&!phone.message_removed)
                throw std::runtime_error("Invalid saved phone state");phones.push_back(std::move(phone));}
        std::map<std::array<uint32_t,3>,size_t> saved_phone_positions,mounted_phone_positions;
        for(const auto& p:phones)++saved_phone_positions[float_key(p.position)];
        for(const auto& p:phones_)++mounted_phone_positions[float_key(p.position)];
        if(phone_count!=phones_.size()||saved_phone_positions!=mounted_phone_positions)
            throw std::runtime_error("Saved phone placements do not match the mounted LCS world");
        const auto pickup_count=r.count(16,"Shortcut pickup points");std::vector<std::array<float,4>> shortcut_pickups(pickup_count);
        for(auto& p:shortcut_pickups){for(auto& v:p)v=r.f32();if(!finite(p))throw std::runtime_error("Invalid saved shortcut pickup point");}
        std::optional<std::array<float,4>> shortcut_dropoff;if(r.boolean()){std::array<float,4> p{};for(auto& v:p)v=r.f32();if(!finite(p))throw std::runtime_error("Invalid saved shortcut dropoff point");shortcut_dropoff=p;}

        std::unordered_map<int32_t,int32_t> building_groups;const auto group_count=r.count(512,"Building groups");
        for(size_t i=0;i<group_count;++i){const auto group=r.i32(),state=r.i32();if(!building_groups.emplace(group,state).second)throw std::runtime_error("Duplicate saved building group");}
        std::unordered_set<int32_t> building_models;const auto building_model_count=r.count(4096,"Building swap models");
        for(size_t i=0;i<building_model_count;++i){const auto model=r.i32();if(model<0||model>=20000||!building_models.insert(model).second)throw std::runtime_error("Invalid saved building swap model");}
        std::unordered_map<std::string,ZonePopulation> population;const auto population_count=r.count(1024,"Zone population entries");
        for(size_t i=0;i<population_count;++i){auto zone=r.text(8,"zone name");if(!valid_label(zone)||!population.emplace(zone,ZonePopulation{}).second)throw std::runtime_error("Invalid or duplicate saved zone population name");
            auto& p=population.at(zone);const auto pair=[&](auto& values){for(auto& day:values)if(r.boolean()){std::array<int32_t,12> v{};for(auto& x:v)x=r.i32();day=v;}};
            pair(p.pedestrians);pair(p.cars);pair(p.civilians);for(auto& day:p.groups)if(r.boolean())day=r.i32();}
        std::unordered_map<int32_t,std::array<int32_t,2>> gang_models;const auto gang_model_count=r.count(9,"Gang model entries");
        for(size_t i=0;i<gang_model_count;++i){const auto gang=r.i32();std::array<int32_t,2> pair{};for(auto& model:pair)model=r.i32();if(gang<0||gang>=9||!gang_models.emplace(gang,pair).second)throw std::runtime_error("Invalid saved gang model entry");}
        std::unordered_map<int32_t,int32_t> gang_cars;const auto gang_car_count=r.count(9,"Gang vehicle entries");
        for(size_t i=0;i<gang_car_count;++i){const auto gang=r.i32(),model=r.i32();if(gang<0||gang>=9||!gang_cars.emplace(gang,model).second)throw std::runtime_error("Invalid saved gang vehicle entry");}
        std::unordered_map<int32_t,uint32_t> nonped_threats;const auto threat_count=r.count(64,"Threat policy entries");
        for(size_t i=0;i<threat_count;++i){const auto type=r.i32();const auto mask=r.u32();if(type<0||type>22||!nonped_threats.emplace(type,mask).second)throw std::runtime_error("Invalid saved threat policy");}

        std::array<std::optional<OriginalWeaponDefinition>,37> weapon_definitions{};
        if(r.count(37,"Weapon definitions")!=37)throw std::runtime_error("Saved weapon definition table has the wrong size");
        for(auto& d:weapon_definitions)if(r.boolean()){OriginalWeaponDefinition value;value.model=r.i32();value.slot=r.i32();if(value.model< -1||value.model>=20000||value.slot<0||value.slot>=10)throw std::runtime_error("Invalid saved weapon definition");d=value;}
        struct ActorIdentityLoad{int32_t model=-1;std::array<float,3> position{};};
        std::unordered_map<int32_t,ActorIdentityLoad> actor_identities;const auto actor_identity_count=r.count(384,"Actor alias identities");
        for(size_t i=0;i<actor_identity_count;++i){const auto actor=r.i32();ActorIdentityLoad identity;identity.model=r.i32();for(auto& v:identity.position)v=r.f32();
            if(actor<0||identity.model<0||identity.model>=20000||!finite(identity.position)||!actor_identities.emplace(actor,identity).second)throw std::runtime_error("Invalid saved actor alias identity");}
        std::unordered_map<int32_t,std::unordered_map<int32_t,int32_t>> weapon_aliases;
        const auto actor_count=r.count(128,"Weapon alias actors");for(size_t i=0;i<actor_count;++i){const auto actor=r.i32();if(actor<0||!weapon_aliases.emplace(actor,std::unordered_map<int32_t,int32_t>{}).second)throw std::runtime_error("Invalid weapon alias actor");
            auto& aliases=weapon_aliases.at(actor);const auto count=r.count(64,"Weapon aliases");for(size_t j=0;j<count;++j){const auto native=r.i32(),original=r.i32();if(native<0||native>63||(original<0||original>=37)&&original!=42||!aliases.emplace(native,original).second)throw std::runtime_error("Invalid saved weapon alias");}}
        std::unordered_map<int32_t,std::string> pending_outfits;const auto pending_count=r.count(128,"Pending outfit actors");
        for(size_t i=0;i<pending_count;++i){const auto actor=r.i32();const auto label=r.text(8,"pending outfit name");if(actor<0||!valid_label(label)||!pending_outfits.emplace(actor,label).second)throw std::runtime_error("Invalid pending outfit alias");}
        std::unordered_map<int32_t,WornOutfit> worn_outfits;const auto worn_count=r.count(128,"Worn outfit actors");
        for(size_t i=0;i<worn_count;++i){const auto actor=r.i32();const auto label=r.text(8,"worn outfit name");const auto model=r.i32();if(actor<0||!valid_label(label)||model<0||model>=20000||!worn_outfits.emplace(actor,WornOutfit{label,model}).second)throw std::runtime_error("Invalid worn outfit alias");}
        std::unordered_map<int32_t,bool> running_chars,pednode_seek;
        std::unordered_map<int32_t,uint8_t> seat_policies;
        if(version>=2){
            const auto running_count=r.count(1,"Player running policies");
            for(size_t i=0;i<running_count;++i){const auto actor=r.i32();const auto enabled=r.boolean();if(actor<0||!running_chars.emplace(actor,enabled).second)throw std::runtime_error("Invalid saved Player1 running policy");}
            const auto seek_count=r.count(1,"Player pednode-seek policies");
            for(size_t i=0;i<seek_count;++i){const auto actor=r.i32();const auto enabled=r.boolean();if(actor<0||!pednode_seek.emplace(actor,enabled).second)throw std::runtime_error("Invalid saved Player1 pednode-seek policy");}
            const auto seat_mask=r.u8();if(seat_mask&~uint8_t(3))throw std::runtime_error("Invalid saved Player1 seat policy mask");
            if(seat_mask){const auto actor=r.i32();if(actor<0||!seat_policies.emplace(actor,seat_mask).second)throw std::runtime_error("Invalid saved Player1 seat policy actor");}
        }
        if(!actor_identities.empty()){
            const auto player=execute?execute({0x01F5,{0},{},0,1}):NativeResult{};
            if(!player.executed||player.values[0]<0)throw std::runtime_error("Cannot resolve the loaded native player");
            for(const auto& item:actor_identities)if(item.first!=player.values[0])throw std::runtime_error("Saved player alias differs from the loaded player handle");
        }
        std::set<int32_t> verified_peds;
        const auto validate_peds=[&](const auto& map,const char* field){
            for(const auto& item:map){const auto identity=actor_identities.find(item.first);if(identity==actor_identities.end())throw std::runtime_error(std::string("Saved ")+field+" actor has no persistent identity fingerprint");
                if(!verified_peds.insert(item.first).second)continue;
                if(!live_ped_identity||!execute)throw std::runtime_error(std::string("Cannot restore ")+field+" without the live SA ped identity service");
                const auto live=live_ped_identity(item.first);if(!live||live->handle!=item.first||!live->native_ped)throw std::runtime_error(std::string("Saved ")+field+" actor cannot be rebased: "+std::to_string(item.first));
                const auto model=execute({0x0665,{item.first},{},0,1}),position=execute({0x00A0,{item.first},{},0,3});
                if(!model.executed||!position.executed)throw std::runtime_error(std::string("Cannot read saved ")+field+" actor model/position");
                if(model.values[0]!=identity->second.model){
                    // Toni's two reserved native slots can alternate after LOAD.
                    // A verified current outfit proves this is our player skin,
                    // not an unrelated model that happens to occupy the slot.
                    const auto reserved=[](int32_t value){return value==297||value==298;};
                    const auto outfit=current_outfit?current_outfit(item.first):std::nullopt;
                    auto worn=worn_outfits.find(item.first);
                    if(!reserved(identity->second.model)||!reserved(model.values[0])||!outfit||outfit->empty()||
                       (worn!=worn_outfits.end()&&(worn->second.label!=*outfit||worn->second.native_model!=identity->second.model)))
                        throw std::runtime_error(std::string("Saved ")+field+" actor model does not match the SA save or verified Toni outfit");
                    identity->second.model=model.values[0];
                    if(worn!=worn_outfits.end())worn->second.native_model=model.values[0];
                }
                for(size_t axis=0;axis<3;++axis){float actual=0;std::memcpy(&actual,&position.values[axis],sizeof(actual));if(!std::isfinite(actual)||actual!=identity->second.position[axis])throw std::runtime_error(std::string("Saved ")+field+" actor position does not match the SA save");}}
        };
        validate_peds(weapon_aliases,"weapon alias");validate_peds(pending_outfits,"pending outfit");validate_peds(worn_outfits,"worn outfit");
        validate_peds(running_chars,"running policy");validate_peds(pednode_seek,"pednode-seek policy");validate_peds(seat_policies,"seat policy");
        {std::set<int32_t> alias_actors;for(const auto& item:weapon_aliases)alias_actors.insert(item.first);for(const auto& item:pending_outfits)alias_actors.insert(item.first);for(const auto& item:worn_outfits)alias_actors.insert(item.first);for(const auto& item:running_chars)alias_actors.insert(item.first);for(const auto& item:pednode_seek)alias_actors.insert(item.first);for(const auto& item:seat_policies)alias_actors.insert(item.first);
            if(alias_actors.size()!=actor_identities.size())throw std::runtime_error("Saved actor alias fingerprints do not match the alias tables");for(auto actor:alias_actors)if(!actor_identities.count(actor))throw std::runtime_error("Saved actor alias is missing its identity fingerprint");}
        for(const auto& item:worn_outfits)if(actor_identities.at(item.first).model!=item.second.native_model)throw std::runtime_error("Worn outfit alias and actor model fingerprint disagree");

        std::unordered_map<int32_t,SeatRule> front_seats,rear_seats;
        for(const auto& item:seat_policies){
            if(!live_ped_identity)throw std::runtime_error("Cannot rebase Player1 seat policy without the live SA ped identity service");
            const auto live=live_ped_identity(item.first);if(!live||live->handle!=item.first||!live->native_ped)throw std::runtime_error("Saved Player1 seat policy cannot be rebased");
            if(item.second&1)front_seats.emplace(item.first,SeatRule{*live});
            if(item.second&2)rear_seats.emplace(item.first,SeatRule{*live});
        }

        std::unordered_set<int32_t> blips,spheres;const auto blip_count=r.count(512,"Owned blips");for(size_t i=0;i<blip_count;++i){const auto id=r.i32();if(id<0||!blips.insert(id).second)throw std::runtime_error("Invalid owned blip id");}
        const auto sphere_count=r.count(512,"Owned spheres");for(size_t i=0;i<sphere_count;++i){const auto id=r.i32();if(id<0||!spheres.insert(id).second)throw std::runtime_error("Invalid owned sphere id");}

        const auto lookup_vehicle=[&](int32_t handle)->RouteVehicleLookup{
            if(exclusive_garages.world.vehicle_lookup)return exclusive_garages.world.vehicle_lookup(handle);
            if(crane_service.world.vehicle_lookup)return crane_service.world.vehicle_lookup(handle);
            return {};
        };
        const auto lookup_status_name=[](VehicleLookupStatus status){
            switch(status){case VehicleLookupStatus::unavailable:return "unavailable";case VehicleLookupStatus::absent:return "absent";case VehicleLookupStatus::present:return "present";}
            return "invalid";
        };
        struct VehicleAliasLoad{int32_t original=-1;uint8_t generation=0;std::array<float,3> position{};};
        std::unordered_map<int32_t,VehicleAliasLoad> original_vehicles;std::unordered_set<int32_t> archived_vehicle_handles;const auto vehicle_alias_count=r.count(256,"Original vehicle aliases");
        for(size_t i=0;i<vehicle_alias_count;++i){const auto handle=r.i32();VehicleAliasLoad v;v.original=r.i32();v.generation=r.u8();for(auto& x:v.position)x=r.f32();
            if(handle<0||v.original<0||v.generation!=uint8_t(handle)||!finite(v.position)||!archived_vehicle_handles.insert(handle).second)throw std::runtime_error("Invalid saved original vehicle alias");
            const auto live=lookup_vehicle(handle);
            // A vehicle handle is only a script-pool cache key, not ownership.
            // If the paired SA save proves that the slot is now free or tagged
            // for another entity, consume the stale archive record but do not
            // resurrect its model alias on the new world.
            if(live.status==VehicleLookupStatus::absent)continue;
            const auto identity_error=[&](const char* reason){return std::runtime_error("Saved vehicle alias handle "+std::to_string(handle)+" has lookup status "+lookup_status_name(live.status)+": "+reason);};
            if(live.status!=VehicleLookupStatus::present||!live.snapshot||!live.snapshot->entity||live.snapshot->handle!=handle||
               live.snapshot->generation!=uint8_t(handle)||live.snapshot->generation!=v.generation||live.snapshot->position!=v.position)
                throw identity_error("cannot be rebased to the SA pool");
            const auto mapped=binding(v.original);if(!mapped||mapped->kind!=ModelKind::vehicle)throw identity_error("original model binding is unavailable");
            if(!execute)throw identity_error("native model reader is unavailable");const auto model=execute({0x0441,{handle},{},0,1});if(!model.executed||model.values[0]!=mapped->id)throw identity_error("saved model does not match the SA pool");
            original_vehicles.emplace(handle,v);}
        std::vector<int32_t> dynamic_models;const auto dynamic_count=r.count(256,"Dynamic vehicle model aliases");
        for(size_t i=0;i<dynamic_count;++i){const auto model=r.i32();if(model<0||model>=20000||std::find(dynamic_models.begin(),dynamic_models.end(),model)!=dynamic_models.end())throw std::runtime_error("Invalid dynamic vehicle model alias");dynamic_models.push_back(model);}

        struct ExclusiveGarageLoad{int32_t id=0,type=0;ExclusiveGarages::State state=ExclusiveGarages::State::fully_closed;float door=0;int32_t target=-1;uint8_t generation=0;uint32_t timer=0;bool front=true;float door_height=0;bool height_known=false;};
        std::vector<ExclusiveGarageLoad> exclusive_load;const auto exclusive_count=r.count(50,"Exclusive garage service entries");
        for(size_t i=0;i<exclusive_count;++i){ExclusiveGarageLoad g;g.id=r.i32();g.type=r.i32();const auto state=r.u8();g.door=r.f32();g.target=r.i32();g.generation=r.u8();g.timer=r.u32();g.door_height=r.f32();g.height_known=r.boolean();
            if(g.id<0||!ExclusiveGarages::owns(g.type)||state>uint8_t(ExclusiveGarages::State::waiting_for_car)||!std::isfinite(g.door)||!std::isfinite(g.door_height)||!definitions.count(g.id)||definitions.at(g.id).type!=g.type)
                throw std::runtime_error("Invalid saved exclusive garage state");
            g.state=ExclusiveGarages::State(state);if(g.target< -1)throw std::runtime_error("Invalid saved garage target handle");
            if(g.state==ExclusiveGarages::State::waiting_for_car&&g.timer>3001)throw std::runtime_error("Invalid saved crusher wait age");
            if(g.state!=ExclusiveGarages::State::waiting_for_car&&g.state!=ExclusiveGarages::State::after_drop_off&&g.timer)throw std::runtime_error("Unexpected saved garage timer");
            exclusive_load.push_back(g);}
        std::array<std::optional<uint32_t>,4> collected{};for(auto& mask:collected)if(r.boolean())mask=r.u32();
        const auto reward_multiplier=r.u8();

        struct CraneLoad{bool activated=false,crusher=false;int32_t model=0;std::array<float,3> origin{};std::array<float,4> pickup{};std::array<float,3> dropoff{};float heading=0;uint32_t seed=0;CraneService::Status status=CraneService::Status::none;std::array<float,3> hook_cur{};std::array<float,2> hook_velocity{};float hook_angle=0,hook_offset=0,hook_height=0;uint32_t next_check=0;uint8_t collected=0;uint32_t scan_slice=0;};
        std::vector<CraneLoad> crane_load;const auto crane_count=r.count(11,"Crane declarations");crane_load.reserve(crane_count);
        for(size_t i=0;i<crane_count;++i){CraneLoad c;c.activated=r.boolean();c.crusher=r.boolean();c.model=r.i32();for(auto& v:c.origin)v=r.f32();for(auto& v:c.pickup)v=r.f32();for(auto& v:c.dropoff)v=r.f32();c.heading=r.f32();c.seed=r.u32();const auto status=r.u8();if(status>uint8_t(CraneService::Status::deactivated))throw std::runtime_error("Invalid saved crane status");c.status=CraneService::Status(status);for(auto& v:c.hook_cur)v=r.f32();for(auto& v:c.hook_velocity)v=r.f32();c.hook_angle=r.f32();c.hook_offset=r.f32();c.hook_height=r.f32();c.next_check=r.u32();c.collected=r.u8();c.scan_slice=r.u32();
            if(!finite(c.origin)||!finite(c.pickup)||!finite(c.dropoff)||!finite(c.hook_cur)||!finite(c.hook_velocity)||!std::isfinite(c.heading)||!std::isfinite(c.hook_angle)||!std::isfinite(c.hook_offset)||!std::isfinite(c.hook_height)||c.hook_offset<0||c.hook_height<0||c.scan_slice>100000000u||c.activated!=(c.status!=CraneService::Status::none))throw std::runtime_error("Invalid saved crane declaration/activation");
            crane_load.push_back(c);}
        struct CraneMessageLoad{int32_t handle=-1;uint8_t generation=0;std::array<float,3> position{};};
        std::vector<CraneMessageLoad> crane_messages;const auto crane_message_count=r.count(256,"Crane message identities");
        for(size_t i=0;i<crane_message_count;++i){CraneMessageLoad m;m.handle=r.i32();m.generation=r.u8();for(auto& v:m.position)v=r.f32();if(m.handle<0||!finite(m.position))throw std::runtime_error("Invalid saved crane message identity");crane_messages.push_back(m);}

        std::array<bool,FerryService::paths> have_paths{};std::array<std::vector<std::array<float,2>>,FerryService::paths> path_nodes;
        for(size_t p=0;p<FerryService::paths;++p){have_paths[p]=r.boolean();if(have_paths[p]){const auto count=r.count(10000,"Ferry path nodes");if(count<4)throw std::runtime_error("Saved ferry path is incomplete");path_nodes[p].resize(count);for(auto& n:path_nodes[p]){n[0]=r.f32();n[1]=r.f32();if(!std::isfinite(n[0])||!std::isfinite(n[1]))throw std::runtime_error("Invalid saved ferry path node");}}}
        const bool ferry_disabled=r.boolean(),ferry_failed=r.boolean(),ferry_requested=r.boolean();const auto pending_ticks=r.u32();if(pending_ticks>FerryService::pending_limit+1)throw std::runtime_error("Invalid saved ferry enable wait");
        std::array<FerryService::Ferry,FerryService::max_ferries> ferries{};
        for(size_t i=0;i<ferries.size();++i){auto& f=ferries[i];f.exists=r.boolean();f.id=r.i32();for(auto& v:f.pose.position)v=r.f32();for(auto& v:f.pose.forward)v=r.f32();for(auto& v:f.pose.right)v=r.f32();for(auto& v:f.pose.up)v=r.f32();f.cur_node=r.u32();f.time_along=r.u32();f.position=r.f32();f.speed_units=r.f32();for(auto& v:f.move_speed)v=r.f32();f.docked=r.boolean();f.approaching=r.boolean();f.skip=r.i32();const auto door=r.u8();if(door>uint8_t(FerryService::Door::closing))throw std::runtime_error("Invalid saved ferry door state");f.door=FerryService::Door(door);f.door_timer=r.u32();f.front_door=r.boolean();f.door_ratio=r.f32();f.door_dirty=r.boolean();f.collision=r.i32();
            if((f.exists && f.id!=int32_t(i))||f.skip<0||f.skip>2||f.collision<0||f.collision>1||!finite(f.pose.position)||!finite(f.pose.forward)||!finite(f.pose.right)||!finite(f.pose.up)||!finite(f.move_speed)||!std::isfinite(f.position)||!std::isfinite(f.speed_units)||!std::isfinite(f.door_ratio)||f.door_ratio<0||f.door_ratio>1)
                throw std::runtime_error("Invalid saved ferry state");
            if(!f.exists)f.id=int32_t(i);
            if(f.exists&&!have_paths[size_t(i/2)])throw std::runtime_error("Saved ferry exists without its mounted track");}
        r.finish();

        // The state blob must belong to the world/version that mounted this adapter.
        for(size_t i=0;i<crane_load.size();++i)if(i>=cranes_.size()||cranes_[i].origin!=crane_load[i].origin||cranes_[i].model!=crane_load[i].model)
            throw std::runtime_error("Saved crane identity does not match the mounted LCS world");
        if(crane_load.size()!=cranes_.size()||crane_service.cranes_.size()!=crane_load.size())throw std::runtime_error("Saved crane count does not match the mounted LCS world");
        for(size_t p=0;p<FerryService::paths;++p){
            if(have_paths[p]!=ferry_service.have_path_[p])throw std::runtime_error("Saved ferry track availability does not match the mounted world");
            if(have_paths[p]){const auto& current=ferry_service.paths_[p].nodes;if(current.size()!=path_nodes[p].size())throw std::runtime_error("Saved ferry track node count does not match the mounted world");
                for(size_t i=0;i<current.size();++i)if(current[i].x!=path_nodes[p][i][0]||current[i].y!=path_nodes[p][i][1])throw std::runtime_error("Saved ferry track does not match the mounted world");}
        }
        for(const auto& pair:gang_models){for(auto model:pair.second){const auto mapped=binding(model);if(!mapped||mapped->kind!=ModelKind::ped)throw std::runtime_error("Saved gang ped model is no longer bound");}}
        for(const auto& pair:gang_cars){const auto mapped=binding(pair.second);if(!mapped||mapped->kind!=ModelKind::vehicle)throw std::runtime_error("Saved gang vehicle model is no longer bound");}

        ExclusiveGarages exclusive_candidate;exclusive_candidate.world=exclusive_garages.world;exclusive_candidate.catalog=exclusive_garages.catalog;
        exclusive_candidate.multiplier_=reward_multiplier;
        for(const auto& archived:exclusive_load){
            const auto d=definitions.at(archived.id);exclusive_candidate.declare(archived.id,d.bounds,archived.type);
            auto found=exclusive_candidate.garages_.find(archived.id);if(found==exclusive_candidate.garages_.end())throw std::runtime_error("Saved exclusive garage geometry is degenerate");
            auto& g=found->second;g.state=archived.state;g.door=archived.door;g.door_height=archived.door_height;g.door_height_known=archived.height_known;
            if(archived.target>=0){const auto live=lookup_vehicle(archived.target);if(live.status!=VehicleLookupStatus::present||!live.snapshot||!live.snapshot->entity||live.snapshot->generation!=archived.generation)throw std::runtime_error("Saved garage target cannot be rebased to the SA pool");g.target={archived.target,live.snapshot->entity,live.snapshot->generation};}
            if(archived.state==ExclusiveGarages::State::waiting_for_car)g.crane_ms=now_ms-archived.timer;
            if(archived.state==ExclusiveGarages::State::after_drop_off){if(now_ms>std::numeric_limits<uint32_t>::max()-archived.timer)throw std::runtime_error("Saved garage timer exceeds SA clock range");g.action_ms=now_ms+archived.timer;}
            g.door_applied=false;g.door_quarantined=false;g.aux_quarantined=false;
        }
        for(size_t i=0;i<collected.size();++i)if(collected[i]){
            if(!exclusive_candidate.world.collected_read)throw std::runtime_error("Collector restore requires the SA saved-list reader");
            const auto actual=exclusive_candidate.world.collected_read(uint32_t(i));if(!actual||*actual!=*collected[i])throw std::runtime_error("Collector mask differs from the paired SA save");}

        CraneService crane_candidate=crane_service;crane_candidate.cranes_.clear();crane_candidate.messaged_.clear();
        for(size_t i=0;i<crane_load.size();++i){const auto& a=crane_load[i];const auto index=crane_candidate.declare(a.origin,a.seed);auto& c=crane_candidate.cranes_[index];
            const auto& declaration=cranes_[i];
            if(a.activated){if(!a.crusher)throw std::runtime_error("Saved crane activation is incompatible with its declaration");
                if(!crane_candidate.activate(index,a.pickup[0],a.pickup[1],a.pickup[2],a.pickup[3],a.dropoff,a.heading,a.crusher))throw std::runtime_error("Saved crane activation could not be rebuilt");}
            else if(declaration.activated)throw std::runtime_error("Saved crane state omits its NativeAdapter activation");
            c.status=a.status;c.crusher=a.crusher;c.hook_cur=a.hook_cur;c.hook_velocity=a.hook_velocity;c.hook_angle=a.hook_angle;c.hook_offset=a.hook_offset;c.hook_height=a.hook_height;
            if(now_ms>std::numeric_limits<uint32_t>::max()-a.next_check)throw std::runtime_error("Saved crane timer exceeds SA clock range");c.next_check=now_ms+a.next_check;c.collected=a.collected;c.scan_slice=a.scan_slice;c.rotation_applied=false;
        }
        for(const auto& archived:crane_messages){const auto live=lookup_vehicle(archived.handle);if(live.status==VehicleLookupStatus::present&&live.snapshot&&live.snapshot->generation==archived.generation&&live.snapshot->position==archived.position)
                crane_candidate.messaged_.push_back({archived.handle,live.snapshot->entity,live.snapshot->generation});}

        FerryService ferry_candidate=ferry_service;ferry_candidate.disabled_=ferry_disabled;ferry_candidate.enable_failed_=ferry_failed;ferry_candidate.enable_requested_=ferry_requested;
        ferry_candidate.pending_ticks_=pending_ticks;ferry_candidate.ferries_=ferries;ferry_candidate.last_now_=now_ms;ferry_candidate.diagnostic_.clear();
        for(auto& path:ferry_candidate.paths_)path.heights=false;
        bool any_ferry=false;for(const auto& f:ferries)any_ferry|=f.exists;
        if(any_ferry||ferry_requested){
            if(!ferry_candidate.have_path_[0]||!ferry_candidate.world.apply_pose||!ferry_candidate.producer())throw std::runtime_error("Enabled ferry cannot rebind its physical producer");
            if(!ferry_candidate.resolve_heights(ferry_candidate.paths_[0]))throw std::runtime_error("Enabled ferry track surface could not be re-resolved");
            for(auto& f:ferry_candidate.ferries_)if(f.exists){if(f.cur_node>=ferry_candidate.paths_[0].nodes.size())throw std::runtime_error("Saved ferry node cursor exceeds its track");
                if(f.door_timer>std::numeric_limits<uint32_t>::max()-now_ms)throw std::runtime_error("Saved ferry door timer exceeds SA clock range");f.door_timer=f.door_timer?now_ms+f.door_timer:0;}
            if(!ferry_candidate.tick(now_ms,0))throw std::runtime_error("Ferry pose/door restore failed: "+ferry_candidate.diagnostic());
        }

        // Reapply only absolute settings with real consumers. The service's
        // initialization alone does not make a save incompatible.
        if(!building_groups.empty()&&!world_swap_state)throw std::runtime_error("Building-group save requires the mounted world-swap consumer");
        for(auto group:ordered_keys(building_groups)){
            const auto result=world_swap_state(group,building_groups.at(group));
            if(!result.executed)throw std::runtime_error("Building-group restore failed: "+result.diagnostic);
        }
        if(music!=1){if(!change_music_mode)throw std::runtime_error("Saved LCS music mode has no native restore consumer");const auto result=change_music_mode(music);if(!result.executed)throw std::runtime_error("Saved LCS music mode restore failed: "+result.diagnostic);}
        for(const auto& item:pednode_seek){
            if(!set_pednode_seek||!set_pednode_seek(item.first,item.second))throw std::runtime_error("Cannot reapply saved Player1 pednode-seek policy to the loaded SA ped");
        }

        unique_jumps_total=unique_jumps;fort_staunton_flag_=fort;developer_flag_=developer;kill_frenzies_total_=frenzy_total;export_cars_total_=export_total;
        jump_records_=jumps;activity_statistics_=activity;stored_weather_=weather;stored_clock_=clock;rubbish_visible_=rubbish;scene_light_direction_=light;
        original_radio_channel_=radio;original_radio_seek_=seek;original_music_mode_=music;outfit_changes_=outfit_changes;stored_outfit_=stored_outfit;
        next_garage_=next_garage;garages_=std::move(garages);
        garage_definitions_.clear();for(const auto& item:definitions)garage_definitions_.emplace(item.first,GarageDefinition{item.second.bounds,item.second.type,item.second.camera,item.second.capacity});
        phones_=std::move(phones);claimed_phones_=claimed;shortcut_pickups_=std::move(shortcut_pickups);shortcut_dropoff_=shortcut_dropoff;
        building_groups_=std::move(building_groups);building_swap_models_=std::move(building_models);zone_population_=std::move(population);
        gang_models_=std::move(gang_models);gang_cars_=std::move(gang_cars);nonped_threats_=std::move(nonped_threats);
        weapon_definitions_=weapon_definitions;equivalent_weapons_=std::move(weapon_aliases);equivalent_outfits_=std::move(pending_outfits);worn_outfits_=std::move(worn_outfits);
        running_chars_=std::move(running_chars);pednode_seek_=std::move(pednode_seek);front_seats_=std::move(front_seats);rear_seats_=std::move(rear_seats);
        walking_objectives_.clear();accepted_walk_tasks_.clear();attack_policy_.clear();kill_objectives_.clear();traffic_waits_.clear();companions_.clear();frozen_followers_.clear();frenzy_alias_.reset();
        owned_blips_=std::move(blips);owned_spheres_=std::move(spheres);original_vehicle_models_.clear();
        for(const auto& item:original_vehicles)original_vehicle_models_.emplace(item.first,item.second.original);
        for(auto it=models_.begin();it!=models_.end();)if(it->first>=native_vehicle_model_base)it=models_.erase(it);else ++it;
        for(auto model:dynamic_models)models_.insert_or_assign(native_vehicle_model_base+model,Binding{model,ModelKind::vehicle});
        exclusive_garages=std::move(exclusive_candidate);crane_service=std::move(crane_candidate);cranes_.clear();
        for(const auto& c:crane_load)cranes_.push_back(CraneDeclaration{c.origin,c.model,c.pickup,c.dropoff,c.heading,c.activated,c.crusher});
        ferry_service=std::move(ferry_candidate);last_random_ped_=-1;
        // A completed crusher latch points at a vehicle that no longer exists;
        // clear it so a reused SA pool slot cannot report a false crush.
        exclusive_garages.crushed_id_=-1;exclusive_garages.last_ms_=now_ms;exclusive_garages.frame_=0;
        diagnostic.clear();return true;
    } catch(const std::exception& e){diagnostic=e.what();return false;}
      catch(...){diagnostic="Unknown NativeAdapter restore failure";return false;}
}
}
