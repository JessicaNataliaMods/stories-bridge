#include "lcs/native.hpp"
#include "lcs/route_vehicle_native.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace lcs {
#include "lcs/exclusive_garages.hpp"
#include "lcs/crane_service.hpp"
}

using namespace lcs;

namespace {
void expect(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}

struct Fixture {
    CraneService crane;
    VehicleCatalog catalog;
    RouteVehicleSnapshot car;
    std::optional<ExclusiveGaragePlayer> player;
    std::optional<RouteVehicleSnapshot> entry_target;
    std::optional<bool> ready=true;
    bool hold_fails=false,carry_fails=false,rotate_fails=false;
    int model=1001,holds=0,carries=0,releases=0,rotations=0,messages=0;
    std::string last_message;
    float last_angle=-1;
    std::array<float,3> carried_to{};
    uint32_t now=1000;

    Fixture() {
        catalog.ids["firetruk"]=2001;
        car.handle=42;car.entity=0x4200;car.generation=42;car.position={15,0,0.5f};car.health=1000;car.alive=true;
        car.native_class=0;car.move_speed=std::array<float,3>{0,0,0};
        ExclusiveGaragePlayer p;p.ped=1;p.ped_entity=0x1000;p.position={50,50,0};p.entering_car=false;player=p;
        crane.catalog=&catalog;
        crane.world.camera=[]()->std::optional<std::array<float,3>>{return std::array<float,3>{0,0,0};};
        crane.world.vehicle_slice_checked=[this](uint32_t,uint32_t,std::vector<int32_t>& out){out={car.handle};return true;};
        crane.world.vehicle_lookup=[this](int32_t handle) {
            RouteVehicleLookup result;
            if(handle!=car.handle){result.status=VehicleLookupStatus::absent;return result;}
            result.status=VehicleLookupStatus::present;result.snapshot=car;return result;
        };
        crane.world.original_model=[this](int32_t)->std::optional<int32_t>{return model;};
        crane.world.player=[this]{return player;};
        crane.world.player_entry_target=[this]{return entry_target;};
        crane.world.crane_ready=[this](size_t){return ready;};
        crane.world.crane_rotate=[this](size_t,float angle) {
            ++rotations;NativeResult result;result.native_entered=true;result.executed=!rotate_fails;last_angle=angle;return result;
        };
        crane.world.vehicle_top=[](const RouteVehicleSnapshot&)->std::optional<float>{return 1.5f;};
        crane.world.vehicle_forward_angle=[](const RouteVehicleSnapshot&)->std::optional<float>{return 0.0f;};
        crane.world.hold=[this](const RouteVehicleSnapshot&) {
            ++holds;NativeResult result;result.native_entered=true;result.executed=!hold_fails;return result;
        };
        crane.world.carry=[this](const RouteVehicleSnapshot& v,const std::array<float,3>& position,float,const std::array<float,3>&) {
            ++carries;NativeResult result;result.native_entered=true;result.executed=!carry_fails;
            if(result.executed){car.position=position;carried_to=position;(void)v;}
            return result;
        };
        crane.world.release=[this]{++releases;return true;};
        crane.world.message=[this](const std::string& key){++messages;last_message=key;return true;};
        crane.world.time_step=[]{return 1.0f;};
        crane.declare({0,0,10},0);
        // Pickup rectangle x 10..20, y -5..5; drop-off at (-20,0,2) with heading 0 (no car rotation requested).
        expect(crane.activate(0,10,20,-5,5,{-20,0,2},0.0f,true),"crusher activation");
    }
    bool tick() {const bool ok=crane.tick(now);now+=20;return ok;}
    bool until(const std::function<bool()>& done,int limit=30000) {
        for(int i=0;i<limit;++i){if(done())return true;tick();}
        return done();
    }
};

void full_cycle_is_physical_and_exact() {
    Fixture f;
    expect(f.crane.picked_up(15,0,42)==std::optional<bool>(false),"an idle crane has not lifted the car");
    expect(f.until([&]{return f.crane.phase(0)==CraneService::Phase::going_towards_target;}),"the crane never found the parked car");
    expect(f.until([&]{return f.crane.phase(0)==CraneService::Phase::going_towards_target_only_height;}),"the hook never reached the car");
    expect(f.holds==1,"the car is held exactly once, when the hook reaches it");
    expect(f.crane.picked_up(15,0,42)==std::optional<bool>(false),"closing the hook is not yet lifting");
    expect(f.until([&]{return f.crane.phase(0)==CraneService::Phase::lifting_target;}),"the car was never lifted");
    expect(f.crane.picked_up(15,0,42)==std::optional<bool>(true),"the exact car is reported while lifted");
    expect(f.crane.picked_up(1500,0,42)==std::optional<bool>(false),"the report is bound to this crane's neighbourhood");
    expect(f.crane.picked_up(15,0,43)==std::optional<bool>(false),"another handle is never reported");
    expect(f.carries>0 && f.releases==0,"the car follows the hook while collision stays suspended");
    expect(f.until([&]{return f.crane.phase(0)==CraneService::Phase::rotating_target;}),"the car never reached the rotating phase");
    expect(f.crane.picked_up(15,0,42)==std::optional<bool>(true),"rotating still counts as lifting");
    expect(f.until([&]{return f.crane.phase(0)==CraneService::Phase::dropping_target;}),"the car was never dropped");
    expect(f.releases==1 && f.crane.picked_up(15,0,42)==std::optional<bool>(false),"dropping releases the car exactly once and ends the lift");
    expect(std::fabs(f.carried_to[0]+20)<1.0f,"the car was carried to the drop-off");
    expect(f.until([&]{return f.crane.phase(0)==CraneService::Phase::idle;}),"the crane never returned to idle");
    expect(f.rotations>0 && std::isfinite(f.last_angle),"the real crane building turned with the hook");
}

void ineligible_cars_are_never_taken() {
    {   Fixture f;f.model=2001;f.until([&]{return f.messages>0;},3000);
        expect(f.last_message=="CR_1" && f.messages==1,"a refused model shows the crane message once");
        for(int i=0;i<500;++i)f.tick();
        expect(f.messages==1 && f.holds==0 && f.crane.phase(0)==CraneService::Phase::idle,"a refused model is never held or announced again");
    }
    {   Fixture f;f.car.driver_handle=7;for(int i=0;i<3000;++i)f.tick();
        expect(f.holds==0 && f.crane.phase(0)==CraneService::Phase::idle,"a driven car is skipped");
    }
    {   Fixture f;f.car.move_speed=std::array<float,3>{0.02f,0,0};for(int i=0;i<3000;++i)f.tick();
        expect(f.holds==0,"a moving car is skipped");
    }
    {   Fixture f;f.car.health=100;for(int i=0;i<3000;++i)f.tick();
        expect(f.holds==0,"a badly damaged car is skipped");
    }
    {   Fixture f;f.car.native_class=5;for(int i=0;i<3000;++i)f.tick();
        expect(f.holds==0,"a boat is not a car");
    }
    {   Fixture f;f.car.position={30,0,0.5f};for(int i=0;i<3000;++i)f.tick();
        expect(f.holds==0,"a car outside the pickup rectangle is skipped");
    }
}

void interference_cancels_before_and_during_the_carry() {
    {   Fixture f;
        expect(f.until([&]{return f.crane.phase(0)==CraneService::Phase::going_towards_target;}),"target not chosen");
        f.player->entering_car=true;f.player->position={500,500,0};f.entry_target=f.car;
        f.tick();
        expect(f.crane.phase(0)==CraneService::Phase::idle && f.holds==0,"the exact task target cancels pickup regardless of player distance");
    }
    {   Fixture f;
        expect(f.until([&]{return f.crane.phase(0)==CraneService::Phase::going_towards_target;}),"target not chosen");
        f.player->entering_car=true;f.entry_target=f.car;f.entry_target->handle=43;f.entry_target->generation=43;f.entry_target->entity=0x4300;
        f.tick();
        expect(f.crane.phase(0)==CraneService::Phase::going_towards_target && f.holds==0,
               "entering a different exact vehicle does not cancel this car based on proximity");
    }
    {   Fixture f;
        expect(f.until([&]{return f.crane.phase(0)==CraneService::Phase::going_towards_target;}),"target not chosen");
        f.player->entering_car=true;f.entry_target.reset();
        const auto hook=f.crane.hook(0);
        f.tick();
        expect(f.crane.phase(0)==CraneService::Phase::going_towards_target && f.holds==0 && f.crane.hook(0)==hook,
               "unknown entry target pauses steering and never cancels or holds from a distance heuristic");
    }
    {   Fixture f;
        expect(f.until([&]{return f.crane.phase(0)==CraneService::Phase::lifting_target;}),"never lifted");
        f.car.driver_handle=9;
        f.tick();
        expect(f.crane.phase(0)==CraneService::Phase::idle && f.releases==1 && f.crane.picked_up(15,0,42)==std::optional<bool>(false),
               "a driver entering the lifted car releases it and ends the lift");
    }
    {   Fixture f;
        expect(f.until([&]{return f.crane.phase(0)==CraneService::Phase::lifting_target;}),"never lifted");
        f.car.alive=false;f.car.health=0;
        // The car identity is unchanged, so the lift continues; only a gone/driven car ends it.
        f.tick();
        expect(f.crane.phase(0)==CraneService::Phase::lifting_target,"a wrecked car stays on the hook like retail");
    }
}

void failures_are_never_replayed_or_invented() {
    {   Fixture f;f.hold_fails=true;
        bool failed=false;for(int i=0;i<30000 && !failed;++i)failed=!f.tick();
        expect(failed && !f.crane.diagnostic().empty() && f.crane.picked_up(15,0,42)==std::optional<bool>(false),"a refused hold is diagnosed and never reported as a lift");
    }
    {   Fixture f;f.carry_fails=true;
        bool failed=false;for(int i=0;i<30000 && !failed;++i)failed=!f.tick();
        expect(failed && f.releases==1,"a failed carry releases the car and reports the failure");
    }
    {   Fixture f;f.rotate_fails=true;
        expect(!f.tick() && !f.crane.diagnostic().empty(),"a failed crane rotation is reported");
    }
    {   Fixture f;f.ready=false;
        for(int i=0;i<200;++i)expect(f.tick(),"a streamed-out crane is a benign pause");
        expect(f.rotations==0 && f.holds==0,"a streamed-out crane performs no action");
        f.ready=std::nullopt;
        expect(!f.tick() && f.rotations==0,"an unprovable crane building is a fault, not a pause");
    }
}

void service_state_is_scoped() {
    Fixture f;
    expect(!f.crane.activate(1,0,1,0,1,{0,0,0},0,true) && !f.crane.activate(0,0,1,0,1,{0,0,0},0,false),
           "only the declared crusher crane can be activated");
    expect(f.crane.deactivate(0) && !f.crane.deactivate(5),"deactivation only addresses declared cranes");
    expect(f.crane.clear() && f.crane.count()==0 && !f.crane.picked_up(0,0,42),"cleanup removes every site and reports nothing as lifted");
}

void car_heading_follows_the_original_rotation_rule() {
    Fixture f;
    // A nonzero drop-off heading makes the carried car turn toward it at 0.0035 rad per step.
    f.crane.clear();f.crane.declare({0,0,10},0);
    expect(f.crane.activate(0,10,20,-5,5,{-20,0,2},1.0f,true),"activation with a drop-off heading");
    float first=-1,last=-1;int turns=0;
    f.crane.world.carry=[&](const RouteVehicleSnapshot&,const std::array<float,3>& position,float angle,const std::array<float,3>&) {
        if(first<0)first=angle;last=angle;++turns;f.car.position=position;
        NativeResult result;result.native_entered=true;result.executed=true;return result;
    };
    expect(f.until([&]{return f.crane.phase(0)==CraneService::Phase::dropping_target;}),"cycle with a drop-off heading did not finish");
    expect(turns>10 && std::fabs(last-1.0f)<0.01f && std::fabs(first)<0.05f,"the car turned from its own heading to the requested drop-off heading");
}
}

int main() {
    try {
        full_cycle_is_physical_and_exact();
        ineligible_cars_are_never_taken();
        interference_cancels_before_and_during_the_carry();
        failures_are_never_replayed_or_invented();
        service_state_is_scoped();
        car_heading_follows_the_original_rotation_rule();
        std::cout<<"crusher crane state machine tests passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
