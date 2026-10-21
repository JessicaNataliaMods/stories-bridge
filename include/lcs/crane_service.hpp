#pragma once
// LCS crane service (retail CCranes/CCrane::Update) for the mounted crusher crane.
//
// The crane is the real converted building: its arm turns about Z with the hook angle (the original rotates
// the building, m_pHook stays nil in the source). A carried car is held by a real SA vehicle reference:
// collision/gravity are suspended, it follows the hook coordinates with the original sway, and the original
// drop rules release it. Nothing is concluded from a handle that merely existed earlier.
//
// Included by native.hpp inside namespace lcs, after exclusive_garages.hpp (ExclusiveGaragePlayer,
// VehicleCatalog); the standard headers and route_vehicle_native.hpp are included at the top of native.hpp.

struct CraneWorld {
    // Position of the active game camera / focus; nullopt = unavailable (no decision this frame).
    std::function<std::optional<std::array<float,3>>()> camera;
    std::function<bool(uint32_t,uint32_t,std::vector<int32_t>&)> vehicle_slice_checked;
    std::function<RouteVehicleLookup(int32_t)> vehicle_lookup;
    std::function<std::optional<int32_t>(int32_t)> original_model;
    std::function<std::optional<ExclusiveGaragePlayer>()> player;
    // Exact vehicle in the focused player's live enter-car task. Used only
    // while entering_car is true; missing means the target is unknown.
    std::function<std::optional<RouteVehicleSnapshot>()> player_entry_target;
    // true = crane building mounted and exactly owned, false = streamed out (benign pause), nullopt = fault.
    std::function<std::optional<bool>(size_t)> crane_ready;
    // Turn the real crane building to this absolute angle (radians about Z).
    std::function<NativeResult(size_t,float)> crane_rotate;
    // Collision bounding box max Z of the vehicle's collision model (retail boundingBox.max.z).
    std::function<std::optional<float>(const RouteVehicleSnapshot&)> vehicle_top;
    // Angle of the vehicle forward vector in XY, atan2(y,x) in [0,2pi).
    std::function<std::optional<float>(const RouteVehicleSnapshot&)> vehicle_forward_angle;
    // Suspend collision/gravity of the exact vehicle and zero its speed (registers a weak reference).
    std::function<NativeResult(const RouteVehicleSnapshot&)> hold;
    // Pose of the held vehicle: position, forward XY angle and up vector.
    std::function<NativeResult(const RouteVehicleSnapshot&,const std::array<float,3>&,float,const std::array<float,3>&)> carry;
    // Restore the vehicle's own collision/gravity and drop the weak reference.
    std::function<bool()> release;
    // Mission message (retail CGarages::TriggerMessage) by text key.
    std::function<bool(const std::string&)> message;
    std::function<float(float,float)> ground_z;
    std::function<float()> time_step;
};

class CraneService {
public:
    enum class Phase {idle,going_towards_target,lifting_target,going_towards_target_only_height,rotating_target,dropping_target};
    enum class Status {none,activated,deactivated};
    static constexpr float max_distance_to_find_crane=100.0f,movement_processing_radius=150.0f,crusher_z=-0.951f;
    static constexpr float hook_to_vehicle_to_collect=0.5f,car_moving_speed=0.01f,slowdown=0.3f;
    static constexpr float oscillation_speed=0.002f,car_rotation_speed=0.0035f,crane_movement_speed=0.001f;
    static constexpr float hook_angle_speed=0.004f,hook_offset_speed=0.1f,hook_height_speed=0.06f;
    static constexpr float two_pi=6.28318530718f,pi=3.14159265359f,half_pi=1.57079632679f,default_offset=20.0f;
    static constexpr uint32_t recheck_ms=10000;

    CraneWorld world;
    const VehicleCatalog* catalog=nullptr;

    size_t declare(const std::array<float,3>& origin,uint32_t seed=0) {
        Crane c;c.origin=origin;c.seed=seed;c.hook_angle=float(cranes_.size());
        while(c.hook_angle>two_pi)c.hook_angle-=two_pi;
        cranes_.push_back(c);return cranes_.size()-1;
    }
    size_t count()const{return cranes_.size();}
    // Retail CCranes::ActivateCrane for the crusher variant (military cranes are not used by this campaign).
    bool activate(size_t index,float inf_x,float sup_x,float inf_y,float sup_y,const std::array<float,3>& dropoff,
                  float heading,bool crusher) {
        if(index>=cranes_.size() || !crusher)return false;
        auto& c=cranes_[index];
        if(!release_target(c))return false;
        c.pickup={inf_x,sup_x,inf_y,sup_y};c.dropoff=dropoff;c.status=Status::activated;c.target={};c.collected=0;
        c.dropoff_heading=heading;c.crusher=true;c.next_check=0;c.phase=Phase::idle;
        find_parameters(c,(inf_x+sup_x)*0.5f,(inf_y+sup_y)*0.5f,crusher_z,c.pickup_angle,c.pickup_distance,c.pickup_height);
        find_parameters(c,dropoff[0],dropoff[1],dropoff[2],c.dropoff_angle,c.dropoff_distance,c.dropoff_height);
        return true;
    }
    bool deactivate(size_t index) {
        if(index>=cranes_.size())return false;
        auto& c=cranes_[index];
        if(!release_target(c))return false;
        c.status=Status::deactivated;c.phase=Phase::idle;return true;
    }
    // Retail CCranes::IsThisCarPickedUp: this exact vehicle, lifted or rotating, near (x,y).
    std::optional<bool> picked_up(float x,float y,int32_t handle) const {
        if(!std::isfinite(x) || !std::isfinite(y) || cranes_.empty())return std::nullopt;
        const auto found=lookup(handle);
        if(found.status==VehicleLookupStatus::unavailable)return std::nullopt;
        for(const auto& c:cranes_) {
            const float distance=std::hypot(x-c.origin[0],y-c.origin[1]);
            if(distance>=max_distance_to_find_crane || c.target.handle!=handle)continue;
            if(c.phase!=Phase::lifting_target && c.phase!=Phase::rotating_target)continue;
            if(found.status!=VehicleLookupStatus::present || !found.snapshot || !same(c.target,*found.snapshot))continue;
            return true;
        }
        return false;
    }
    bool carried(int32_t handle)const {
        for(const auto& c:cranes_)
            if(c.target.handle==handle && (c.phase==Phase::going_towards_target_only_height || c.phase==Phase::lifting_target ||
               c.phase==Phase::rotating_target))return true;
        return false;
    }
    std::optional<Phase> phase(size_t index)const{return index<cranes_.size()?std::optional<Phase>(cranes_[index].phase):std::nullopt;}
    std::array<float,3> hook(size_t index)const{return index<cranes_.size()?cranes_[index].hook_cur:std::array<float,3>{};}
    float hook_angle(size_t index)const{return index<cranes_.size()?cranes_[index].hook_angle:0.0f;}

    bool clear() {
        for(auto& c:cranes_)if(!release_target(c))return false;
        cranes_.clear();messaged_.clear();return true;
    }
    // Retires only the held vehicle (world cleanup) while keeping the declared sites.
    bool release_all() {
        for(auto& c:cranes_) {
            if(!release_target(c))return false;
            c.phase=Phase::idle;
        }
        return true;
    }
    const std::string& diagnostic()const{return diagnostic_;}

    // Advances every active crane by one frame. false = a required native fact/action failed (state is never invented).
    bool tick(uint32_t now_ms) {
        diagnostic_.clear();
        bool ok=true;
        const auto camera=world.camera?world.camera():std::nullopt;
        if(!camera)return true;                                         // unavailable focus: no decision, no mutation
        const float step=world.time_step?world.time_step():1.0f;
        if(!std::isfinite(step) || step<0 || step>10){diagnostic_="Crane frame step is invalid";return false;}
        for(size_t i=0;i<cranes_.size();++i) {
            auto& c=cranes_[i];
            if(c.status==Status::none)continue;
            if(!world.crane_ready || !world.crane_rotate){diagnostic_="Physical crane consumer is unavailable";ok=false;continue;}
            const auto ready=world.crane_ready(i);
            if(!ready){diagnostic_="Crane building ownership/profile check is unavailable";ok=false;continue;}
            if(!*ready){                                                  // streamed out: benign pause
                if(c.holding && !release_target(c)){ok=false;continue;}
                c.phase=Phase::idle;continue;
            }
            ok=update(i,c,*camera,now_ms,step) && ok;
        }
        return ok;
    }

private:
    struct Target {int32_t handle=-1;uintptr_t entity=0;uint8_t generation=0;};
    struct Crane {
        std::array<float,3> origin{};uint32_t seed=0;
        std::array<float,4> pickup{};std::array<float,3> dropoff{};
        float dropoff_heading=0,pickup_angle=0,dropoff_angle=0,pickup_distance=0,dropoff_distance=0,pickup_height=0,dropoff_height=0;
        float hook_angle=0,hook_offset=default_offset,hook_height=default_offset;
        std::array<float,3> hook_cur{};std::array<float,2> hook_velocity{};
        Target target;float top=0,car_angle=0;bool holding=false,have_pose=false;
        uint32_t next_check=0;uint8_t collected=0;
        Status status=Status::none;Phase phase=Phase::idle;bool crusher=false;
        bool rotation_applied=false;float applied_angle=0;
        uint32_t scan_slice=0;
    };
    std::vector<Crane> cranes_;
    std::vector<Target> messaged_;
    std::string diagnostic_;

    static bool same(const Target& t,const RouteVehicleSnapshot& v){return t.handle==v.handle && t.entity==v.entity && t.generation==v.generation && v.entity!=0;}
    static Target remember(const RouteVehicleSnapshot& v){return {v.handle,v.entity,v.generation};}
    RouteVehicleLookup lookup(int32_t handle)const {
        if(world.vehicle_lookup)return world.vehicle_lookup(handle);
        return {};
    }
    // gone: this exact identity ended (deleted or another vehicle). live: current snapshot.
    enum class View {unavailable,gone,live};
    std::pair<View,std::optional<RouteVehicleSnapshot>> view(const Target& t)const {
        if(t.handle<0)return {View::gone,std::nullopt};
        const auto found=lookup(t.handle);
        if(found.status==VehicleLookupStatus::unavailable)return {View::unavailable,std::nullopt};
        if(found.status==VehicleLookupStatus::absent || !found.snapshot || !same(t,*found.snapshot))return {View::gone,std::nullopt};
        return {View::live,found.snapshot};
    }
    static float atan_xy(float x,float y){float a=std::atan2(y,x);if(a<0.0f)a+=two_pi;return a;}
    static void find_parameters(const Crane& c,float x,float y,float z,float& angle,float& distance,float& height) {
        angle=atan_xy(x-c.origin[0],y-c.origin[1]);distance=std::hypot(x-c.origin[0],y-c.origin[1]);height=z;
    }
    float height_to_pickup(const Crane& c)const{return 4.0f+c.pickup_height+(c.crusher?4.5f:0.0f);}
    float height_to_dropoff(const Crane& c)const{return c.crusher?(2.0f+c.dropoff_height+3.0f):(2.0f+c.dropoff_height);}
    float height_to_pickup_height(const Crane& c)const{return c.pickup_height+(c.crusher?7.0f:4.0f);}
    float height_to_dropoff_height(const Crane& c)const{return c.dropoff_height+(c.crusher?7.0f:2.0f);}

    bool does_pick_up(int32_t model,bool crusher)const {
        if(!crusher)return true;
        if(!catalog)return false;
        for(const char* name:{"firetruk","trash","coach","securica","bus","dodo","rhino"}) {
            const auto id=catalog->ids.find(name);
            if(id!=catalog->ids.end() && id->second==model)return false;
        }
        return true;
    }
    bool release_target(Crane& c) {
        if(c.holding) {
            if(!world.release || !world.release()){diagnostic_="Crane vehicle release is unavailable";return false;}
            c.holding=false;
        }
        c.target={};c.have_pose=false;return true;
    }
    bool drop_target(Crane& c){c.phase=Phase::idle;return release_target(c);}

    bool go_towards(Crane& c,float angle,float distance,float height,float step,float multiplier=1.0f) {
        float angle_delta=angle-c.hook_angle;
        while(angle_delta>pi)angle_delta-=two_pi;
        while(angle_delta<-pi)angle_delta+=two_pi;
        bool angle_done,offset_done,height_done;
        const float angle_change=multiplier*step*hook_angle_speed;
        if(std::fabs(angle_delta)<angle_change){c.hook_angle=angle;angle_done=true;}
        else {
            if(angle_delta<0.0f){c.hook_angle-=angle_change;if(c.hook_angle<0.0f)c.hook_angle+=two_pi;}
            else {c.hook_angle+=angle_change;if(c.hook_angle>two_pi)c.hook_angle-=two_pi;}
            angle_done=false;
        }
        const float offset_delta=distance-c.hook_offset,offset_change=multiplier*step*hook_offset_speed;
        if(std::fabs(offset_delta)<offset_change){c.hook_offset=distance;offset_done=true;}
        else {c.hook_offset+=offset_delta<0.0f?-offset_change:offset_change;offset_done=false;}
        height_done=go_height(c,height,step,multiplier);
        return angle_done && offset_done && height_done;
    }
    bool go_height(Crane& c,float height,float step,float multiplier=1.0f) {
        const float delta=height-c.hook_height,change=multiplier*step*hook_height_speed;
        if(std::fabs(delta)<change){c.hook_height=height;return true;}
        c.hook_height+=delta<0.0f?-change:change;return false;
    }
    std::array<float,3> hook_coordinates(const Crane& c)const {
        return {std::cos(c.hook_angle)*c.hook_offset+c.origin[0],std::sin(c.hook_angle)*c.hook_offset+c.origin[1],c.hook_height};
    }
    // Retail RotateCarriedCarProperly over the tracked forward angle of the carried car.
    bool rotate_carried(Crane& c,float step) {
        if(c.dropoff_heading<=0.0f || c.target.handle<0)return true;
        float delta=c.dropoff_heading-c.car_angle;
        while(delta<-half_pi)delta+=pi;
        while(delta>half_pi)delta-=pi;
        const float change=car_rotation_speed*step;
        if(std::fabs(delta)<=change)return true;
        c.car_angle+=delta<0?-change:change;
        while(c.car_angle<0.0f)c.car_angle+=two_pi;
        while(c.car_angle>=two_pi)c.car_angle-=two_pi;
        return false;
    }
    std::optional<bool> player_entering_target(const RouteVehicleSnapshot& car)const {
        const auto player=world.player?world.player():std::nullopt;
        if(!player)return std::nullopt;
        if(player->vehicle)return *player->vehicle==car.handle;
        if(player->entering_car!=std::optional<bool>(true))
            return player->entering_car==std::optional<bool>(false)?std::optional<bool>(false):std::nullopt;
        const auto target=world.player_entry_target?world.player_entry_target():std::nullopt;
        if(!target)return std::nullopt;
        return target->handle==car.handle && target->entity==car.entity && target->generation==car.generation;
    }
    bool moving(const RouteVehicleSnapshot& car,bool at_least)const {
        if(!car.move_speed)return true;                                  // unknown speed is never "still"
        for(const float axis:*car.move_speed) {
            if(!std::isfinite(axis))return true;
            if(at_least?std::fabs(axis)>=car_moving_speed:std::fabs(axis)>car_moving_speed)return true;
        }
        return false;
    }
    bool is_automobile(const RouteVehicleSnapshot& car)const{return car.native_class>=0 && car.native_class<=2 && !car.is_bike;}
    bool message_once(const RouteVehicleSnapshot& car,const char* key) {
        for(const auto& t:messaged_)if(same(t,car))return true;
        if(world.message && !world.message(key))return false;
        messaged_.push_back(remember(car));return true;
    }
    // FindCarInSectorList over one pool slice per frame; the first eligible car becomes the target.
    bool find_car(Crane& c) {
        std::vector<int32_t> handles;
        if(!world.vehicle_slice_checked || !world.vehicle_slice_checked(c.scan_slice%8,8,handles)){
            diagnostic_="Crane vehicle-pool slice unavailable";return false;
        }
        ++c.scan_slice;
        for(const auto handle:handles) {
            const auto found=lookup(handle);
            if(found.status==VehicleLookupStatus::unavailable){diagnostic_="Crane vehicle lookup unavailable";return false;}
            if(found.status!=VehicleLookupStatus::present || !found.snapshot)continue;
            const auto& v=*found.snapshot;
            if(v.position[0]<c.pickup[0] || v.position[0]>c.pickup[1] || v.position[1]<c.pickup[2] || v.position[1]>c.pickup[3])continue;
            if(v.driver_handle>=0)continue;
            if(moving(v,true))continue;
            if(!is_automobile(v) || !v.alive || v.health<250.0f)continue;
            const auto model=world.original_model?world.original_model(handle):std::nullopt;
            if(!model)continue;                                           // no LCS identity: source cannot judge it
            if(!does_pick_up(*model,c.crusher)) {
                if(!message_once(v,"CR_1"))return false;                // Crane cannot lift this vehicle.
                continue;
            }
            c.target=remember(v);c.phase=Phase::going_towards_target;c.have_pose=false;
            return true;
        }
        return true;
    }

    bool update(size_t index,Crane& c,const std::array<float,3>& camera,uint32_t now,float step) {
        const bool focused=std::fabs(camera[0]-c.origin[0])<movement_processing_radius && std::fabs(camera[1]-c.origin[1])<movement_processing_radius;
        if(((c.status==Status::activated || c.status==Status::deactivated) && focused) || c.phase!=Phase::idle) {
            switch(c.phase) {
            case Phase::idle:
                if(go_towards(c,c.pickup_angle,c.pickup_distance,height_to_pickup(c),step) && now>c.next_check) {
                    if(!find_car(c))return false;
                }
                break;
            case Phase::going_towards_target: {
                const auto seen=view(c.target);
                if(seen.first==View::unavailable)return true;
                if(seen.first==View::gone){c.phase=Phase::idle;c.target={};break;}
                const auto& car=*seen.second;
                if(car.position[0]<c.pickup[0] || car.position[0]>c.pickup[1] || car.position[1]<c.pickup[2] || car.position[1]>c.pickup[3] ||
                   car.driver_handle>=0 || moving(car,false)) {
                    c.target={};c.phase=Phase::idle;break;
                }
                const auto entering=player_entering_target(car);
                if(!entering)return true;                              // No target fact: pause before steering or holding.
                if(*entering){c.target={};c.phase=Phase::idle;break;}
                const auto top=world.vehicle_top?world.vehicle_top(car):std::nullopt;
                if(!top){diagnostic_="Crane vehicle collision height is unavailable";return false;}
                c.top=*top;
                float angle,offset,height;
                find_parameters(c,car.position[0],car.position[1],car.position[2]+*top,angle,offset,height);
                if(go_towards(c,angle,offset,height,step)) {
                    const float dx=car.position[0]-c.hook_cur[0],dy=car.position[1]-c.hook_cur[1],dz=car.position[2]-c.hook_cur[2]+*top;
                    if(dx*dx+dy*dy+dz*dz<hook_to_vehicle_to_collect*hook_to_vehicle_to_collect) {
                        const auto forward=world.vehicle_forward_angle?world.vehicle_forward_angle(car):std::nullopt;
                        if(!forward || !world.hold){diagnostic_="Crane vehicle hold consumer is unavailable";return false;}
                        const auto held=world.hold(car);
                        if(!held.executed){
                            diagnostic_="Crane vehicle hold refused: "+held.diagnostic;
                            if(held.native_entered){c.holding=true;(void)release_target(c);c.phase=Phase::idle;}
                            return false;
                        }
                        c.holding=true;c.car_angle=*forward;c.have_pose=true;
                        c.phase=Phase::going_towards_target_only_height;
                        c.hook_velocity={c.hook_velocity[0]*0.4f,c.hook_velocity[1]*0.4f};
                    }
                }
                break;
            }
            case Phase::lifting_target: {
                rotate_carried(c,step);
                if(go_towards(c,c.dropoff_angle,c.dropoff_distance,height_to_dropoff(c),step,slowdown))c.phase=Phase::rotating_target;
                if(!carried_alive(c)){if(!drop_target(c))return false;}
                break;
            }
            case Phase::going_towards_target_only_height:
                rotate_carried(c,step);
                if(go_height(c,height_to_pickup_height(c),step,slowdown))c.phase=Phase::lifting_target;
                if(!carried_alive(c)){if(!drop_target(c))return false;}
                break;
            case Phase::rotating_target: {
                const bool rotated=rotate_carried(c,step);
                const bool moved=go_towards(c,c.dropoff_angle,c.dropoff_distance,c.dropoff_height,step,0.3f);
                if(moved && rotated) {
                    c.phase=Phase::dropping_target;
                    if(c.target.handle>=0) {
                        ++c.collected;
                        if(!release_target(c))return false;               // retail: bUsesCollision=true, the car falls
                    }
                }
                break;
            }
            case Phase::dropping_target:
                if(go_towards(c,c.dropoff_angle,c.dropoff_distance,height_to_dropoff_height(c),step,slowdown)){c.phase=Phase::idle;c.next_check=now+recheck_ms;}
                break;
            }
            // Spring-damper hook: x/y lag behind the ideal coordinates, Z follows exactly.
            const auto ideal=hook_coordinates(c);
            c.hook_velocity[0]+=(ideal[0]-c.hook_cur[0])*step*crane_movement_speed;
            c.hook_velocity[1]+=(ideal[1]-c.hook_cur[1])*step*crane_movement_speed;
            const float damping=std::pow(0.98f,step);
            c.hook_velocity[0]*=damping;c.hook_velocity[1]*=damping;
            c.hook_cur[0]+=c.hook_velocity[0]*step;c.hook_cur[1]+=c.hook_velocity[1]*step;c.hook_cur[2]=ideal[2];
            if((c.phase==Phase::lifting_target || c.phase==Phase::going_towards_target_only_height || c.phase==Phase::rotating_target) &&
               c.target.handle>=0 && c.have_pose) {
                const auto seen=view(c.target);
                if(seen.first==View::live) {
                    std::array<float,3> up{ideal[0]-c.hook_cur[0],ideal[1]-c.hook_cur[1],20.0f};
                    const float length=std::sqrt(up[0]*up[0]+up[1]*up[1]+up[2]*up[2]);
                    for(auto& v:up)v/=length;
                    const auto pose=world.carry?world.carry(*seen.second,{c.hook_cur[0],c.hook_cur[1],c.hook_cur[2]-c.top},c.car_angle,up):NativeResult{};
                    if(!pose.executed){
                        diagnostic_="Crane vehicle carry failed: "+pose.diagnostic;
                        if(pose.native_entered){(void)drop_target(c);}
                        return false;
                    }
                }
            }
        } else {
            // Idle sway of a crane nobody uses: 16 two-second windows seeded per crane.
            const int rnd=int((c.seed+(now>>11))&0xF);
            if(rnd<4){c.hook_angle-=oscillation_speed*step;if(c.hook_angle<0.0f)c.hook_angle+=two_pi;}
            else if(rnd>5 && rnd<12){c.hook_angle+=oscillation_speed*step;if(c.hook_angle>two_pi)c.hook_angle-=two_pi;}
            c.hook_cur=hook_coordinates(c);c.hook_velocity={0,0};
        }
        // CraneEntity matrix: SetRotateZOnly(hook angle), then the RW refresh (done by the native consumer).
        if(!c.rotation_applied || std::fabs(c.applied_angle-c.hook_angle)>1e-5f) {
            const auto turned=world.crane_rotate(index,c.hook_angle);
            if(!turned.executed){
                diagnostic_="Crane building rotation failed"+std::string(turned.native_entered?" after native entry":" before native entry")+": "+turned.diagnostic;
                return false;
            }
            c.rotation_applied=true;c.applied_angle=c.hook_angle;
        }
        return true;
    }
    // A car that is gone, driven, or no longer unambiguously ours ends the carry (retail: not pickedUp or pDriver).
    bool carried_alive(Crane& c) {
        if(c.target.handle<0)return false;
        const auto seen=view(c.target);
        if(seen.first==View::unavailable)return true;
        if(seen.first==View::gone)return false;
        return seen.second->driver_handle<0;
    }
};
