#include "lcs/native.hpp"
#include "lcs/route_vehicle_native.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace lcs {
#include "lcs/exclusive_garages.hpp"
}

using namespace lcs;

namespace {
void expect(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}

struct Fixture {
    ExclusiveGarages garages;
    RouteVehicleSnapshot car;
    ExclusiveGaragePlayer player;
    bool gone=false,scan_available=true,release_available=true,door_entered_failure=false;
    std::optional<bool> ready=true;
    std::optional<bool> containment=true;
    uint16_t fail_opcode=0;
    int score_calls=0,ped_delete_calls=0,car_delete_calls=0,door_calls=0,hold_calls=0,release_calls=0,scan_calls=0;
    uint32_t now=1;
    std::function<void(uint16_t)> after_execute;

    Fixture() {
        car.handle=42;car.entity=0x4200;car.generation=42;car.position={0,0,0};car.health=1000;car.alive=true;car.native_class=0;
        player.ped=1;player.ped_entity=0x1000;player.position={100,100,0};player.entering_car=false;
        garages.world.vehicle_lookup=[this](int32_t handle) {
            RouteVehicleLookup result;
            if(handle!=car.handle){result.status=VehicleLookupStatus::absent;return result;}
            if(gone){result.status=VehicleLookupStatus::absent;return result;}
            result.status=VehicleLookupStatus::present;result.snapshot=car;return result;
        };
        garages.world.vehicle_slice_checked=[this](uint32_t,uint32_t,std::vector<int32_t>& out) {
            ++scan_calls;
            if(!scan_available)return false;
            out={car.handle};return true;
        };
        garages.world.ped_identity=[this](int32_t handle)->std::optional<uintptr_t> {
            if(handle==player.ped)return player.ped_entity;
            if(handle==car.driver_handle)return car.driver_ped;
            for(size_t i=0;i<car.passenger_handles.size();++i)
                if(handle==car.passenger_handles[i])return car.passenger_peds[i];
            return uintptr_t(0);
        };
        garages.world.original_model=[](int32_t)->std::optional<int32_t>{return 1001;};
        garages.world.player=[this]()->std::optional<ExclusiveGaragePlayer>{return player;};
        garages.world.crusher_reward_message=[](int32_t){return true;};
        garages.world.entirely_inside=[this](const std::array<float,8>&,const RouteVehicleSnapshot&)->std::optional<bool>{return containment;};
        garages.world.crusher_door=[this](int32_t,const std::array<float,8>&,float) {
            ++door_calls;NativeResult result;result.native_entered=true;result.executed=!door_entered_failure;
            if(door_entered_failure)result.diagnostic="injected entered door failure";
            return result;
        };
        garages.world.crusher_ready=[this](){return ready;};
        garages.world.crusher_hold=[this](const RouteVehicleSnapshot&,float,bool) {
            ++hold_calls;NativeResult result;result.executed=true;result.native_entered=true;return result;
        };
        garages.world.crusher_release=[this](){++release_calls;return release_available;};
        garages.world.execute=[this](const NativeCall& call) {
            NativeResult result;result.native_entered=true;
            if(call.opcode==0x0109)++score_calls;
            if(call.opcode==0x009B)++ped_delete_calls;
            if(call.opcode==0x00A6)++car_delete_calls;
            if(call.opcode==fail_opcode) {
                result.diagnostic="injected entered failure";
                if(after_execute)after_execute(call.opcode);
                return result;
            }
            result.executed=true;
            if(call.opcode==0x00A6)gone=true;
            if(after_execute)after_execute(call.opcode);
            return result;
        };
        const std::array<float,8> bounds{-5,-5,0,5,-5,-5,5,5};
        garages.declare(7,bounds,ExclusiveGarages::crusher_type);
    }

    void tick() {garages.tick(now);now+=200;}
    void approach_finish() {
        tick();
        now=4002;tick();
        for(int i=0;i<40 && garages.state(7)==ExclusiveGarages::State::closing;++i)tick();
    }
    void approach_hold() {
        tick();now=4002;tick();
        for(int i=0;i<30 && garages.state(7)==ExclusiveGarages::State::closing && hold_calls==0;++i)tick();
    }
};

void native_entered_failure_is_quarantined() {
    Fixture f;f.fail_opcode=0x0109;f.approach_finish();
    expect(f.score_calls==1,"entered ADD_SCORE was not invoked exactly once");
    expect(f.car_delete_calls==0,"vehicle deleted after ambiguous ADD_SCORE failure");
    for(int i=0;i<4;++i)f.tick();
    expect(f.score_calls==1,"quarantined reward was replayed");
    expect(!f.garages.car_crushed(f.car.handle),"failed transaction published crusher success");
}

void external_disappearance_is_not_success() {
    Fixture f;f.tick();f.gone=true;f.now=4002;f.tick();
    expect(f.score_calls==0 && f.car_delete_calls==0,"external disappearance caused a native finish action");
    expect(!f.garages.car_crushed(f.car.handle),"external disappearance was counted as a crush");
}

void unavailable_pool_is_not_an_empty_scan() {
    Fixture f;f.scan_available=false;
    expect(!f.garages.tick(f.now),"unavailable checked pool scan reported success");
    expect(f.garages.state(7)==ExclusiveGarages::State::opened,"unavailable pool scan advanced crusher state");
    expect(!f.garages.diagnostic().empty(),"unavailable pool scan omitted its diagnostic");
}

void changed_player_identity_quarantines_followup() {
    Fixture f;
    f.after_execute=[&](uint16_t opcode){if(opcode==0x0109)f.player.ped_entity=0x2000;};
    f.approach_finish();
    expect(f.score_calls==1,"reward was not issued before the player-identity change");
    expect(f.ped_delete_calls==0 && f.car_delete_calls==0,"transaction mutated entities after focused player identity changed");
    for(int i=0;i<3;++i)f.tick();
    expect(f.score_calls==1,"changed-player transaction replayed its reward");
}

void reused_occupant_handle_is_not_deleted() {
    Fixture f;f.car.driver_handle=5;f.car.driver_ped=0x5000;
    f.after_execute=[&](uint16_t opcode){if(opcode==0x0109)f.car.driver_ped=0x6000;};
    f.approach_finish();
    expect(f.score_calls==1,"crusher transaction did not start");
    expect(f.ped_delete_calls==0,"DELETE_CHAR targeted a reused occupant handle");
    expect(f.car_delete_calls==0,"vehicle deleted while a different occupant identity remained");
}

void focused_player_vehicle_is_never_deleted() {
    Fixture f;f.car.driver_handle=f.player.ped;f.car.driver_ped=f.player.ped_entity;f.player.vehicle=f.car.handle;
    f.approach_finish();
    expect(f.score_calls==0 && f.ped_delete_calls==0 && f.car_delete_calls==0,
        "crusher transaction paid for or deleted the focused player vehicle");
}

void missing_physical_consumer_stalls_crusher() {
    Fixture f;f.garages.world.crusher_door={};
    expect(!f.garages.tick(f.now),"crusher advanced without the physical door consumer");
    expect(f.garages.state(7)==ExclusiveGarages::State::opened,"missing physical door consumer changed crusher state");
    expect(f.score_calls==0 && f.car_delete_calls==0,"crusher effects ran without a physical door");
}

void unavailable_collision_query_does_not_use_center_fallback() {
    Fixture f;f.containment=std::nullopt;
    expect(!f.garages.tick(f.now),"unavailable native collision query reported success");
    expect(f.garages.state(7)==ExclusiveGarages::State::opened,"unavailable native collision query advanced crusher state");
    expect(!f.garages.diagnostic().empty(),"unavailable native collision query omitted its diagnostic");
}

void release_failure_blocks_crusher_completion() {
    Fixture f;f.release_available=false;f.approach_finish();
    expect(f.hold_calls>0,"crusher vehicle hold was not established");
    expect(f.car_delete_calls==1,"test did not reach the native vehicle deletion");
    expect(!f.garages.car_crushed(f.car.handle),"crusher published completion despite hold-release failure");
    const auto calls=f.car_delete_calls;
    f.tick();
    expect(f.car_delete_calls==calls,"held deletion was replayed while release remained unavailable");
}

void missing_physical_hold_stalls_before_finish() {
    Fixture f;f.garages.world.crusher_hold={};f.approach_finish();
    expect(f.score_calls==0 && f.car_delete_calls==0,"crusher finished without its physical vehicle-hold consumer");
    expect(f.garages.state(7)==ExclusiveGarages::State::opened,"missing physical hold advanced the transaction");
}

void player_entering_a_held_car_releases_native_hold() {
    Fixture f;f.approach_hold();
    expect(f.hold_calls==1,"crusher did not establish its physical hold");
    f.car.driver_handle=f.player.ped;f.car.driver_ped=f.player.ped_entity;f.player.vehicle=f.car.handle;
    f.tick();
    expect(f.release_calls==1,"player entry did not release the physical vehicle hold");
    expect(f.score_calls==0 && f.ped_delete_calls==0 && f.car_delete_calls==0,
        "crusher mutated a vehicle after the focused player entered it");
}

void streamed_out_crusher_is_a_benign_pause() {
    Fixture f;f.ready=false;
    expect(f.garages.tick(f.now),"streamed-out crusher was reported as a fault");
    expect(f.garages.state(7)==ExclusiveGarages::State::opened,"streamed-out crusher changed state");
    expect(f.door_calls==0 && f.scan_calls==0 && f.score_calls==0 && f.car_delete_calls==0,
        "streamed-out crusher ran a door, scan, or transaction action");
}

void entered_door_failure_survives_streamed_out_pause() {
    Fixture f;f.door_entered_failure=true;
    expect(!f.garages.tick(f.now),"entered door failure reported success");
    f.ready=false;
    expect(!f.garages.tick(f.now+200),"streamed-out pause hid the quarantined door failure");
    expect(f.door_calls==1,"quarantined physical door update was replayed");
}
}

void multiplier_and_message_use_the_real_finish_transaction() {
    for(const auto multiplier:std::array<int32_t,3>{257,-1,0}) {
        Fixture f;f.garages.catalog.values[1001]=10000;
        f.garages.set_reward_multiplier(multiplier);
        int32_t paid=-1,announced=-1;int announcements=0;
        const auto execute=f.garages.world.execute;
        f.garages.world.execute=[&](const NativeCall& call){if(call.opcode==0x0109)paid=call.inputs[1];return execute(call);};
        f.garages.world.crusher_reward_message=[&](int32_t reward){announced=reward;return ++announcements>1;};
        f.approach_finish();
        expect(paid==45*int32_t(uint8_t(multiplier)),"Source multiplier narrowing/formula was not consumed by the native score packet");
        expect(f.score_calls==1 && announcements==2 && announced==paid,"Delayed reward message duplicated payment or lost the exact reward");
        expect(f.garages.car_crushed(f.car.handle) && f.car_delete_calls==1,"Finish did not publish the real removed car after reward/message confirmation");
    }
}
void native_time_step_and_vehicle_class_are_consumed() {
    Fixture excluded;excluded.car.native_class=4;excluded.tick();
    expect(excluded.garages.state(7)==ExclusiveGarages::State::opened,"An aircraft was admitted as a source automobile");
    Fixture f;float applied=-1;
    const auto door=f.garages.world.crusher_door;
    f.garages.world.crusher_door=[&](int32_t id,const std::array<float,8>& b,float angle){applied=angle;return door(id,b,angle);};
    f.tick();f.now=4002;f.tick();
    expect(f.garages.tick(f.now+200,2.0f),"Native frame step refused");
    expect(std::fabs(applied-(ExclusiveGarages::half_pi-0.01f))<0.00001f,
        "Crusher used wall-clock lag instead of SA's actual frame time step");
}

// ---- Import/Export collector (types 8..10, 22) -------------------------------------------------------------------
struct CollectorFixture {
    ExclusiveGarages garages;
    RouteVehicleSnapshot car;
    ExclusiveGaragePlayer player;
    std::array<uint32_t,4> store{};
    bool store_readable=true,store_write_fails=false,gone=false,door_streamed_out=false,door_entered_failure=false;
    bool contained=true,other_car_touches=false,player_outside=true,vehicle_deliverable=true,has_other_car=false;
    std::optional<float> door_height=4.1f;
    float door_position=0.0f;
    int door_calls=0,car_delete_calls=0,ped_delete_calls=0,score_calls=0,writes=0;
    uint32_t store_at_delete=0xFFFFFFFFu;
    int32_t model=1001;
    uint32_t now=1;
    RouteVehicleSnapshot other_car;

    explicit CollectorFixture(int32_t type=8) {
        car.handle=42;car.entity=0x4200;car.generation=42;car.position={0,0,2};car.health=1000;car.alive=true;car.native_class=0;
        player.ped=1;player.ped_entity=0x1000;player.position={0,-8,0};player.entering_car=false;player.vehicle=car.handle;
        garages.catalog.ids["hearse"]=1001;garages.catalog.ids["faggio"]=1002;
        garages.world.vehicle_lookup=[this](int32_t handle) {
            RouteVehicleLookup result;
            if(has_other_car && handle==other_car.handle){result.status=VehicleLookupStatus::present;result.snapshot=other_car;return result;}
            if(handle!=car.handle || gone){result.status=VehicleLookupStatus::absent;return result;}
            result.status=VehicleLookupStatus::present;result.snapshot=car;return result;
        };
        garages.world.vehicle_slice_checked=[this](uint32_t,uint32_t,std::vector<int32_t>& out){out={car.handle};if(has_other_car)out.push_back(other_car.handle);return true;};
        garages.world.ped_identity=[this](int32_t handle)->std::optional<uintptr_t> {
            if(handle==player.ped)return player.ped_entity;
            return uintptr_t(0);
        };
        garages.world.original_model=[this](int32_t)->std::optional<int32_t>{return model;};
        garages.world.player=[this]()->std::optional<ExclusiveGaragePlayer>{return player;};
        garages.world.player_on_mission=[]{return false;};
        garages.world.entirely_inside=[this](const std::array<float,8>&,const RouteVehicleSnapshot&)->std::optional<bool>{return contained;};
        garages.world.collector_vehicle_touches=[this](const std::array<float,8>&,const RouteVehicleSnapshot& vehicle)->std::optional<bool>{
            return has_other_car && vehicle.handle==other_car.handle?other_car_touches:false;
        };
        garages.world.collector_player_outside=[this](const std::array<float,8>&,const ExclusiveGaragePlayer&,float margin)->std::optional<bool>{
            if(margin!=2.0f)return {};
            return player_outside;
        };
        garages.world.collector_vehicle_deliverable=[this](const RouteVehicleSnapshot&)->std::optional<bool>{return vehicle_deliverable;};
        garages.world.collector_door_height=[this](int32_t,const std::array<float,8>&)->std::optional<float>{
            return door_streamed_out?std::nullopt:door_height;
        };
        garages.world.collector_door=[this](int32_t,const std::array<float,8>&,float position) {
            ++door_calls;NativeResult result;
            if(door_streamed_out){result.diagnostic="door streamed out";return result;}
            result.native_entered=true;result.executed=!door_entered_failure;
            if(door_entered_failure){result.diagnostic="injected entered door failure";return result;}
            door_position=position;return result;
        };
        garages.world.collected_read=[this](uint32_t index)->std::optional<uint32_t>{
            if(!store_readable || index>=store.size())return std::nullopt;
            return store[index];
        };
        garages.world.collected_write=[this](uint32_t index,uint32_t mask){
            ++writes;
            if(store_write_fails || index>=store.size())return false;
            store[index]=mask;return true;
        };
        garages.world.execute=[this](const NativeCall& call) {
            NativeResult result;result.native_entered=true;
            if(call.opcode==0x0109)++score_calls;
            if(call.opcode==0x009B)++ped_delete_calls;
            if(call.opcode==0x00A6){++car_delete_calls;store_at_delete=store[0];gone=true;}
            result.executed=true;return result;
        };
        const std::array<float,8> bounds{-5,-5,0,5,-5,-5,5,5};
        garages.declare(7,bounds,type);
    }
    void tick() {garages.tick(now);now+=200;}
    void until(ExclusiveGarages::State wanted,int limit=400) {
        for(int i=0;i<limit && garages.state(7)!=wanted;++i)tick();
    }
    // The player drives the needed car to the door, opens it, leaves the car inside and walks out.
    void deliver() {
        until(ExclusiveGarages::State::opened);
        player.vehicle=std::nullopt;player.position={0,-9,0};
        until(ExclusiveGarages::State::closing,2);
        for(int i=0;i<200 && garages.state(7)==ExclusiveGarages::State::closing;++i)tick();
    }
};

void collector_declares_its_service_and_answers_from_the_saved_list() {
    CollectorFixture f;
    expect(f.garages.has(7) && ExclusiveGarages::owns(8) && ExclusiveGarages::owns(9) && ExclusiveGarages::owns(10) &&
           ExclusiveGarages::owns(22) && !ExclusiveGarages::owns(11),"collector types are owned by the exclusive service");
    expect(f.garages.slot_filled(7,0)==std::optional<bool>(false),"an empty saved list reports a known false slot");
    f.store[0]=0x8001;
    expect(f.garages.slot_filled(7,0)==std::optional<bool>(true) && f.garages.slot_filled(7,15)==std::optional<bool>(true) &&
           f.garages.slot_filled(7,1)==std::optional<bool>(false),"slots are the bits of SA's saved list, read at query time");
    f.store_readable=false;
    expect(!f.garages.slot_filled(7,0),"an unreadable saved list is unavailable, not FALSE");
    f.store_readable=true;
    expect(!f.garages.slot_filled(8,0) && !f.garages.slot_filled(7,-1) && !f.garages.slot_filled(7,32),
           "unknown garages and impossible slots have no answer");
    expect(f.garages.clear() && f.store[0]==0x8001,"world cleanup never erases the saved import list");
}

void collector_moves_the_real_door_by_retail_travel() {
    CollectorFixture f;
    f.until(ExclusiveGarages::State::opened);
    expect(f.garages.state(7)==ExclusiveGarages::State::opened && std::fabs(f.door_position-4.1f)<0.0001f,
           "the needed car opens the door to the real model travel, not a fixed 4.0");
    expect(f.door_calls>2,"the door moves progressively with the native frame time");
}

void collector_saves_the_list_before_destroying_the_delivery() {
    CollectorFixture f;f.deliver();
    expect(f.car_delete_calls==1 && f.gone,"the delivered car is destroyed exactly once");
    expect(f.store_at_delete==1u && f.store[0]==1u,"the hearse bit is saved and confirmed before the car is deleted");
    expect(f.garages.slot_filled(7,0)==std::optional<bool>(true) && f.garages.slot_filled(7,1)==std::optional<bool>(false),
           "only the delivered model's slot is filled");
    expect(f.score_calls==0,"collector deliveries pay nothing");
    expect(std::fabs(f.door_position)<0.0001f && f.garages.state(7)==ExclusiveGarages::State::fully_closed,
           "the door is fully closed after the delivery");
    const auto writes=f.writes;
    for(int i=0;i<10;++i)f.tick();
    expect(f.writes==writes && f.car_delete_calls==1,"a finished delivery is not replayed");
}

void collector_ignores_unneeded_models_and_collected_slots() {
    CollectorFixture other;other.model=999;
    for(int i=0;i<40;++i)other.tick();
    expect(other.garages.state(7)==ExclusiveGarages::State::fully_closed && other.door_calls==0,
           "a model outside the import list never opens the door");
    CollectorFixture collected;collected.store[0]=1;
    for(int i=0;i<40;++i)collected.tick();
    expect(collected.garages.state(7)==ExclusiveGarages::State::fully_closed && collected.door_calls==0,
           "an already filled slot is no longer needed");
    CollectorFixture second;second.model=1002;second.deliver();
    expect(second.store[0]==2u,"a different list model fills its own slot");
}

void collector_never_destroys_a_car_it_could_not_record() {
    CollectorFixture f;f.store_write_fails=true;f.deliver();
    expect(f.car_delete_calls==0 && f.store[0]==0u,"a failed list write leaves the car alive");
    expect(!f.garages.diagnostic().empty(),"the failed list write is diagnosed");
    f.store_write_fails=false;
    for(int i=0;i<10;++i)f.tick();
    expect(f.car_delete_calls==1 && f.store[0]==1u,"the delivery completes once the list can be saved");
    CollectorFixture unreadable;unreadable.store_readable=false;
    expect(!unreadable.garages.tick(unreadable.now) && unreadable.door_calls==0,"an unreadable list blocks every collector decision");
}

void collector_never_destroys_a_car_the_player_still_occupies() {
    CollectorFixture f;
    f.until(ExclusiveGarages::State::opened);
    f.player.position={0,-9,0};f.player.vehicle=std::nullopt;
    f.until(ExclusiveGarages::State::closing,2);
    f.player.vehicle=f.car.handle;
    for(int i=0;i<200 && f.garages.state(7)==ExclusiveGarages::State::closing;++i)f.tick();
    expect(f.car_delete_calls==0 && f.store[0]==0u,"a player who returned to the car is never deleted nor credited");
}

void collector_requires_collision_clearance_and_revalidates_before_commit() {
    CollectorFixture containment;containment.until(ExclusiveGarages::State::opened);
    containment.player.vehicle=std::nullopt;containment.player.position={0,-9,0};containment.contained=false;
    containment.until(ExclusiveGarages::State::closing,2);
    expect(containment.garages.state(7)==ExclusiveGarages::State::opened && containment.car_delete_calls==0,
        "a vehicle center inside the bounds cannot replace native full-collision containment");

    CollectorFixture other;other.has_other_car=true;other.other_car.handle=43;other.other_car.entity=0x4300;
    other.other_car.generation=43;other.other_car.alive=true;other.other_car_touches=true;other.until(ExclusiveGarages::State::opened);
    other.player.vehicle=std::nullopt;other.player.position={0,-9,0};other.until(ExclusiveGarages::State::closing,2);
    expect(other.garages.state(7)==ExclusiveGarages::State::opened && other.car_delete_calls==0,
        "another vehicle touching the collector blocks delivery");

    CollectorFixture player;player.until(ExclusiveGarages::State::opened);
    player.player.vehicle=std::nullopt;player.player.position={0,-9,0};player.player_outside=false;
    player.until(ExclusiveGarages::State::closing,2);
    expect(player.garages.state(7)==ExclusiveGarages::State::opened && player.car_delete_calls==0,
        "the player's full collision body must clear the two-unit collector margin");

    CollectorFixture damage;damage.until(ExclusiveGarages::State::opened);
    damage.player.vehicle=std::nullopt;damage.player.position={0,-9,0};damage.vehicle_deliverable=false;
    damage.until(ExclusiveGarages::State::closing,2);
    expect(damage.garages.state(7)==ExclusiveGarages::State::opened && damage.car_delete_calls==0,
        "damaged, burning, or wrecked vehicles cannot start a collector delivery");

    CollectorFixture recheck;recheck.until(ExclusiveGarages::State::opened);
    recheck.player.vehicle=std::nullopt;recheck.player.position={0,-9,0};
    recheck.until(ExclusiveGarages::State::closing,2);
    expect(recheck.garages.state(7)==ExclusiveGarages::State::closing,"delivery begins closing after initial safety checks");
    recheck.contained=false;
    for(int i=0;i<200 && recheck.garages.state(7)==ExclusiveGarages::State::closing;++i)recheck.tick();
    expect(recheck.garages.state(7)==ExclusiveGarages::State::opening && recheck.store[0]==0u && recheck.car_delete_calls==0,
        "loss of collision containment during door travel cancels before save or deletion");
}

void collector_door_faults_stay_diagnosed() {
    CollectorFixture streamed;streamed.door_streamed_out=true;
    expect(streamed.garages.tick(streamed.now) && streamed.door_calls==0,"an unmounted door is a benign pause");
    CollectorFixture missing;missing.garages.world.collector_door={};
    expect(!missing.garages.tick(missing.now) && !missing.garages.diagnostic().empty(),"no physical door consumer stalls the collector");
    CollectorFixture entered;entered.door_entered_failure=true;
    for(int i=0;i<10;++i)entered.tick();
    expect(entered.door_calls==1,"an entered door failure is never replayed");
    expect(!entered.garages.tick(entered.now),"an entered door failure stays reported");
}

void collector_type_change_retires_the_service() {
    CollectorFixture f;
    expect(!f.garages.change_type(7,13),"another exclusive type cannot replace a collector");
    expect(f.garages.has(7),"a refused change keeps the collector");
    expect(f.garages.change_type(7,0) && !f.garages.has(7),"CHANGE_GARAGE_TYPE to NONE retires the service");
    expect(f.garages.change_type(7,0),"a retired garage ignores later changes");
}
int main() {
    try {
        native_entered_failure_is_quarantined();
        external_disappearance_is_not_success();
        unavailable_pool_is_not_an_empty_scan();
        changed_player_identity_quarantines_followup();
        reused_occupant_handle_is_not_deleted();
        focused_player_vehicle_is_never_deleted();
        missing_physical_consumer_stalls_crusher();
        unavailable_collision_query_does_not_use_center_fallback();
        release_failure_blocks_crusher_completion();
        missing_physical_hold_stalls_before_finish();
        player_entering_a_held_car_releases_native_hold();
        streamed_out_crusher_is_a_benign_pause();
        entered_door_failure_survives_streamed_out_pause();
        multiplier_and_message_use_the_real_finish_transaction();
        native_time_step_and_vehicle_class_are_consumed();
        collector_declares_its_service_and_answers_from_the_saved_list();
        collector_moves_the_real_door_by_retail_travel();
        collector_saves_the_list_before_destroying_the_delivery();
        collector_ignores_unneeded_models_and_collected_slots();
        collector_never_destroys_a_car_it_could_not_record();
        collector_never_destroys_a_car_the_player_still_occupies();
        collector_requires_collision_clearance_and_revalidates_before_commit();
        collector_door_faults_stay_diagnosed();
        collector_type_change_retires_the_service();
        std::cout<<"exclusive garage transaction tests passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
