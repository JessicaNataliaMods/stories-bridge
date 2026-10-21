#pragma once
// LCS-owned garage services that have no San Andreas equivalent: the crusher
// (type 13) and the Import/Export "collect cars" garage (types 8..10, 22).
// They follow the retail CGarage::Update state machines for these types and use
// only real native facts: SA vehicle snapshots, SA script opcodes (money, car
// and occupant deletion), a real converted crusher-door consumer, and the LCS
// vehicle catalogue parsed from the converted DEFAULT.IDE/HANDLING.CFG.

// Included by native.hpp inside namespace lcs, right after NativeCall/NativeResult are defined; the standard
// headers and route_vehicle_native.hpp are included at the top of native.hpp.

// LCS model ids by dff name and monetary value by LCS model id. The monetary
// value is token 30 of a HANDLING.CFG vehicle line (retail nMonetaryValue).
struct VehicleCatalog {
    std::unordered_map<std::string,int32_t> ids;
    std::unordered_map<int32_t,int32_t> values;
    static std::string lower(std::string text) {
        std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::tolower(c));});
        return text;
    }
    static std::string upper(std::string text) {
        std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::toupper(c));});
        return text;
    }
    static std::string trim(const std::string& text) {
        const auto first=text.find_first_not_of(" \t\r\n");
        if(first==std::string::npos)return {};
        return text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
    }
    void load(std::istream& ide,std::istream& handling) {
        std::unordered_map<std::string,int32_t> monetary;
        std::string line;
        while(std::getline(handling,line)) {
            const auto text=trim(line);
            if(text.empty() || !std::isalpha(static_cast<unsigned char>(text[0])))continue;
            std::istringstream row(text);std::vector<std::string> tokens;std::string token;
            while(row>>token)tokens.push_back(token);
            if(tokens.size()<31)continue;
            char* end=nullptr;const auto value=std::strtol(tokens[30].c_str(),&end,10);
            if(!end || *end || value<0)continue;
            monetary[upper(tokens[0])]=int32_t(value);
        }
        bool cars=false;
        while(std::getline(ide,line)) {
            const auto text=trim(line);
            if(text.empty() || text[0]=='#')continue;
            if(!cars){cars=text=="cars";continue;}
            if(text=="end"){cars=false;continue;}
            std::vector<std::string> fields;std::string field;std::istringstream row(text);
            while(std::getline(row,field,','))fields.push_back(trim(field));
            if(fields.size()<5)continue;
            char* end=nullptr;const auto id=std::strtol(fields[0].c_str(),&end,10);
            if(!end || *end)continue;
            ids[lower(fields[1])]=int32_t(id);
            const auto found=monetary.find(upper(fields[4]));
            if(found!=monetary.end())values[int32_t(id)]=found->second;
        }
    }
};

struct ExclusiveGaragePlayer {
    int32_t ped=-1;                       // handle of the focused player ped
    uintptr_t ped_entity=0;               // live CPed identity paired with the handle
    std::optional<int32_t> vehicle;       // vehicle the player occupies, if any
    std::array<float,3> position{};
    std::optional<bool> entering_car;     // source PED_ENTER_CAR / PED_CARJACK; nullopt = unknown
};

struct ExclusiveGarageWorld {
    // Legacy vector-only view. Use the checked callback for production scans.
    std::function<std::vector<int32_t>(uint32_t,uint32_t)> vehicle_slice;
    // False means unavailable; an empty successful vector means an empty slice.
    std::function<bool(uint32_t,uint32_t,std::vector<int32_t>&)> vehicle_slice_checked;
    std::function<std::optional<RouteVehicleSnapshot>(int32_t)> vehicle;
    // Current ped-pool identity; nullopt is unavailable and zero is absent.
    std::function<std::optional<uintptr_t>(int32_t)> ped_identity;
    // LCS model id of an SA vehicle handle (nullopt when it has no LCS identity).
    std::function<std::optional<int32_t>(int32_t)> original_model;
    std::function<NativeResult(const NativeCall&)> execute;
    // Optional three-way lookup. `unavailable` (profile/read failure) is never treated as a deleted vehicle.
    std::function<RouteVehicleLookup(int32_t)> vehicle_lookup;
    // Explicit focused-player facts; nullopt means the facts are UNAVAILABLE (never "player not in a car").
    std::function<std::optional<ExclusiveGaragePlayer>()> player;
    std::function<bool()> player_on_mission;
    std::function<bool(int32_t)> crusher_reward_message;
    // Apply an absolute angle to the real converted crusher doors for a garage.
    std::function<NativeResult(int32_t,const std::array<float,8>&,float)> crusher_door;
    // true=door/consumer ready, false=benign streamed-out pause, nullopt=profile/ownership failure.
    std::function<std::optional<bool>()> crusher_ready;
    // Native CGarage collision-sphere containment; nullopt means unavailable.
    std::function<std::optional<bool>(const std::array<float,8>&,const RouteVehicleSnapshot&)> entirely_inside;
    // Retain/release the real converted crusher's vehicle hold (collision/gravity/position).
    std::function<NativeResult(const RouteVehicleSnapshot&,float,bool)> crusher_hold;
    std::function<bool()> crusher_release;
    // Import/Export collector. Height is retail FindDoorHeightForMI of the real converted door;
    // nullopt is a benign unavailable/streamed-out pause, never a guessed 4.0.
    std::function<std::optional<float>(int32_t,const std::array<float,8>&)> collector_door_height;
    // Retail UpdateDoorsHeight: translate the real door to its IPL height plus `position`.
    std::function<NativeResult(int32_t,const std::array<float,8>&,float)> collector_door;
    // The saved import list: SA's own CGarages::CarTypesCollected[index] (nullopt/false = unavailable).
    std::function<std::optional<uint32_t>(uint32_t)> collected_read;
    std::function<bool(uint32_t,uint32_t)> collected_write;
    // Collector-specific retail guards, backed by SA collision/status consumers.
    std::function<std::optional<bool>(const std::array<float,8>&,const RouteVehicleSnapshot&)> collector_vehicle_touches;
    std::function<std::optional<bool>(const std::array<float,8>&,const ExclusiveGaragePlayer&,float)> collector_player_outside;
    std::function<std::optional<bool>(const RouteVehicleSnapshot&)> collector_vehicle_deliverable;
};

class ExclusiveGarages {
public:
    enum class State {fully_closed,opened,closing,opening,after_drop_off,waiting_for_car};
    static constexpr int32_t crusher_type=13,collect_first=8,collect_last=10,collect_fourth=22;
    static constexpr int32_t import_slots=16;
    static constexpr float half_pi=1.57079632679f;
    static bool crusher(int32_t type){return type==crusher_type;}
    static bool collector(int32_t type){return (type>=collect_first && type<=collect_last) || type==collect_fourth;}
    static bool owns(int32_t type){return crusher(type) || collector(type);}

    ExclusiveGarageWorld world;
    VehicleCatalog catalog;

    void declare(int32_t id,const std::array<float,8>& b,int32_t type) {
        if(!owns(type))return;
        Garage g;g.id=id;g.bounds=b;g.type=type;
        // CGarages::AddOne(X1,Y1,Z1,X2,Y2,X3,Y3,Z2). Retail takes the minimum over the fourth corner D=B+C-A but not the maximum,
        g.inf_x=std::min({b[0],b[3],b[5],b[3]+b[5]-b[0]});g.sup_x=std::max({b[0],b[3],b[5],b[3]+b[5]-b[0]});
        g.inf_y=std::min({b[1],b[4],b[6],b[4]+b[6]-b[1]});g.sup_y=std::max({b[1],b[4],b[6],b[4]+b[6]-b[1]});
        g.corner={b[0],b[1]};g.inf_z=b[2];g.sup_z=b[7];
        g.dir1={b[3]-b[0],b[4]-b[1]};g.dir2={b[5]-b[0],b[6]-b[1]};
        g.len1=std::hypot(g.dir1[0],g.dir1[1]);g.len2=std::hypot(g.dir2[0],g.dir2[1]);
        if(!(g.len1>0.0f) || !(g.len2>0.0f) || !std::isfinite(g.len1) || !std::isfinite(g.len2))return;
        g.dir1={g.dir1[0]/g.len1,g.dir1[1]/g.len1};g.dir2={g.dir2[0]/g.len2,g.dir2[1]/g.len2};
        if(crusher(type)){g.state=State::opened;g.door=half_pi;}
        else {g.state=State::fully_closed;g.door=0.0f;}
        const auto previous=garages_.find(id);
        if(previous!=garages_.end() && !release_crusher_hold(previous->second))return;
        garages_[id]=g;
    }
    bool has(int32_t id)const{return garages_.count(id)!=0;}
    bool forget(int32_t id){
        const auto found=garages_.find(id);if(found==garages_.end())return true;
        if(!release_crusher_hold(found->second))return false;
        garages_.erase(found);return true;
    }
    bool clear(){
        for(auto& entry:garages_)if(!release_crusher_hold(entry.second))return false;
        garages_.clear();crushed_id_=-1;multiplier_=1;return true;
    }
    void set_reward_multiplier(int32_t value){multiplier_=uint8_t(value);} // retail uint8 wraparound
    uint8_t reward_multiplier()const{return multiplier_;}
    // CGarages::HasCarBeenCrushed: only the last crushed car handle is remembered.
    bool car_crushed(int32_t handle)const{return crushed_id_==handle;}
    // CGarages::HasImportExportGarageCollectedThisCar(garage, car-1) for a known collector garage.
    // The answer is read from SA's saved CGarages::CarTypesCollected on every call, so a loaded game is
    // visible immediately; an unreadable list is unavailable, never an invented FALSE.
    std::optional<bool> slot_filled(int32_t id,int32_t slot)const {
        const auto found=garages_.find(id);
        if(found==garages_.end() || !collector(found->second.type) || slot<0 || slot>=32)return std::nullopt;
        const auto mask=collected_mask(found->second.type);
        if(!mask)return std::nullopt;
        return (*mask&(1u<<slot))!=0;
    }
    std::optional<uint32_t> collected_mask(int32_t type)const {
        if(!collector(type) || !world.collected_read)return std::nullopt;
        return world.collected_read(uint32_t(table_index(type)));
    }
    // CGarages::ChangeGarageType for a garage this service owns. A type that no exclusive service owns
    // retires the service (retail NONE does nothing); the door keeps its last retail position.
    bool change_type(int32_t id,int32_t type) {
        const auto found=garages_.find(id);
        if(found==garages_.end())return true;
        if(owns(type)) {
            if(type!=found->second.type)return false;
            found->second.state=State::fully_closed;return true;
        }
        return forget(id);
    }
    std::optional<State> state(int32_t id)const{const auto found=garages_.find(id);return found==garages_.end()?std::nullopt:std::optional<State>(found->second.state);}

    // Advances every owned garage. Returns false when a required native fact or
    // action failed so the caller can report a diagnostic; state is never invented.
    bool tick(uint32_t now_ms,std::optional<float> engine_step={}) {
        if(engine_step && (!std::isfinite(*engine_step) || *engine_step<0 || *engine_step>10))return false;
        const uint32_t elapsed=last_ms_?std::min<uint32_t>(now_ms-last_ms_,200):0;
        last_ms_=now_ms;++frame_;run_failed_=false;diagnostic_.clear();
        bool ok=true;
        for(auto& entry:garages_) {
            auto& g=entry.second;
            if((!world.vehicle && !world.vehicle_lookup) || !world.execute){ok=false;continue;}
            const bool result=crusher(g.type)?update_crusher(g,now_ms,engine_step.value_or(float(elapsed)/20.0f)):update_collector(g,now_ms,elapsed);
            ok=ok && result;
        }
        return ok;
    }
    const std::string& diagnostic()const{return diagnostic_;}

    // The converted crane mesh has no verified movable-hook consumer. Keep the
    // source opcodes unavailable rather than claiming a lift by moving a car.
    size_t declare_crane(const std::array<float,3>&){return 0;}
    bool activate_crane(size_t,const std::array<float,4>&,const std::array<float,3>&,float){return false;}
    std::optional<bool> crane_lifting(float,float,int32_t)const{return std::nullopt;}
    size_t crane_count()const{return 0;}
    int crane_state(size_t)const{return -1;}

private:
    friend class NativeAdapter;
    struct Garage {
        int32_t id=0;
        std::array<float,8> bounds{};
        int32_t type=0;
        float inf_x=0,sup_x=0,inf_y=0,sup_y=0,inf_z=0,sup_z=0;
        std::array<float,2> corner{},dir1{},dir2{};float len1=0,len2=0;
        State state=State::fully_closed;float door=0.0f;
        struct Target {int32_t handle=-1;uintptr_t entity=0;uint8_t generation=0;} target;
        uint32_t crane_ms=0,action_ms=0;
        // Idempotent destroy/pay transaction (see run_finish).
        struct Finish {
            bool started=false,paid=false,announced=false,car_deleted=false,quarantined=false,action_started=false;
            bool collection_marked=false;
            int32_t reward=0,player_ped=-1,model=-1;uintptr_t player_entity=0;
            bool model_known=false;
            struct Occupant {int32_t handle=-1;uintptr_t entity=0;};
            std::vector<Occupant> occupants;size_t next=0;
            Target car;
        } finish;
        bool door_applied=false,door_quarantined=false,aux_quarantined=false,hold_active=false;
        Target held_target;
        // Collector door travel (retail m_fDoorHeight) from the real converted door model.
        float door_height=0.0f;bool door_height_known=false;
    };
    std::unordered_map<int32_t,Garage> garages_;
    int32_t crushed_id_=-1;
    uint8_t multiplier_=1;
    uint32_t last_ms_=0,frame_=0;
    bool run_failed_=false;
    std::string diagnostic_;

    static size_t table_index(int32_t type) {
        switch(type){case 8:return 0;case 9:return 1;case 10:return 2;default:return 3;}
    }
    // Retail only gives COLLECTCARS_1 a car list (total 16); the others have none.
    static uint32_t table_total(int32_t type){return type==8?uint32_t(import_slots):0;}

    // CGarage::IsPointInsideGarage(pos, margin)
    static bool inside(const Garage& g,const std::array<float,3>& p,float margin) {
        if(p[2]<g.inf_z-margin || p[2]>g.sup_z+margin)return false;
        const float vx=p[0]-g.corner[0],vy=p[1]-g.corner[1];
        float dp=g.dir1[0]*vx+g.dir1[1]*vy;
        if(dp<-margin || g.len1+margin<dp)return false;
        dp=g.dir2[0]*vx+g.dir2[1]*vy;
        return !(dp<-margin || g.len2+margin<dp);
    }
    static bool same(const Garage::Target& t,const RouteVehicleSnapshot& v) {
        return t.handle==v.handle && t.entity==v.entity && t.generation==v.generation && v.entity!=0;
    }
    static Garage::Target remember(const RouteVehicleSnapshot& v){return {v.handle,v.entity,v.generation};}

    RouteVehicleLookup lookup(int32_t handle) const {
        if(world.vehicle_lookup)return world.vehicle_lookup(handle);
        RouteVehicleLookup result;
        if(!world.vehicle)return result;
        result.snapshot=world.vehicle(handle);
        result.status=result.snapshot?VehicleLookupStatus::present:VehicleLookupStatus::absent;
        return result;
    }
    // unavailable: no decision this frame. gone: the exact identity ended (deleted, other generation, or dead when
    // liveness is required). live: current snapshot of that exact vehicle.
    enum class TargetState {unavailable,gone,live};
    struct TargetView {TargetState state=TargetState::unavailable;std::optional<RouteVehicleSnapshot> car;};
    TargetView target_view(const Garage::Target& t,bool require_alive) const {
        TargetView view;
        if(t.handle<0){view.state=TargetState::gone;return view;}
        const auto found=lookup(t.handle);
        if(found.status==VehicleLookupStatus::unavailable)return view;
        if(found.status==VehicleLookupStatus::absent || !found.snapshot || !same(t,*found.snapshot) ||
           (require_alive && !found.snapshot->alive)){view.state=TargetState::gone;return view;}
        view.state=TargetState::live;view.car=found.snapshot;return view;
    }

    std::optional<std::vector<int32_t>> pool_slice(uint32_t slice,uint32_t slices) const {
        if(world.vehicle_slice_checked) {
            std::vector<int32_t> result;
            if(!world.vehicle_slice_checked(slice,slices,result))return std::nullopt;
            return result;
        }
        // A vector-only callback cannot distinguish an empty pool from a failed
        // read. Treat it as unavailable instead of advancing on an invented empty scan.
        return std::nullopt;
    }
    bool run(Garage::Finish& finish,uint16_t opcode,std::vector<int32_t> inputs) {
        NativeCall call;call.opcode=opcode;call.inputs=std::move(inputs);
        const auto result=world.execute(call);
        if(!result.executed){
            run_failed_=true;
            diagnostic_="Native garage action "+std::to_string(opcode)+
                (result.native_entered?" failed after native entry; transaction quarantined: ":" refused before native entry: ")+result.diagnostic;
            if(result.native_entered)finish.quarantined=true;
        }
        return result.executed;
    }
    bool release_crusher_hold(Garage& g) {
        if(!g.hold_active)return true;
        if(!world.crusher_release || !world.crusher_release()) {
            run_failed_=true;g.aux_quarantined=true;
            diagnostic_="Crusher hold release failed; garage state quarantined";
            return false;
        }
        g.hold_active=false;g.held_target={};return true;
    }
    bool set_crusher_door(Garage& g,float angle) {
        if(g.door_quarantined){run_failed_=true;diagnostic_="Crusher door is quarantined after an entered native failure";return false;}
        if(g.door_applied && angle==g.door)return true;
        if(!world.crusher_door){run_failed_=true;diagnostic_="Physical crusher door consumer is unavailable";return false;}
        const auto result=world.crusher_door(g.id,g.bounds,angle);
        if(!result.executed) {
            run_failed_=true;
            diagnostic_="Physical crusher door update"+
                std::string(result.native_entered?" failed after native entry; replay quarantined: ":" refused before native entry: ")+result.diagnostic;
            if(result.native_entered)g.door_quarantined=true;
            return false;
        }
        g.door=angle;g.door_applied=true;return true;
    }
    std::optional<bool> crusher_entirely_inside(const Garage& g,const RouteVehicleSnapshot& car) {
        if(!world.entirely_inside){run_failed_=true;diagnostic_="Native crusher collision-containment consumer is unavailable";return std::nullopt;}
        auto result=world.entirely_inside(g.bounds,car);
        if(!result){run_failed_=true;diagnostic_="Native crusher collision-containment query is unavailable";}
        return result;
    }
    static bool player_occupies_target(const RouteVehicleSnapshot& car,const ExclusiveGaragePlayer& player) {
        if(player.ped<0 || (player.vehicle && *player.vehicle==car.handle) || occupies(car,player.ped))return true;
        if(player.entering_car==std::optional<bool>(true)) {
            const float dx=player.position[0]-car.position[0],dy=player.position[1]-car.position[1];
            if(std::isfinite(dx) && std::isfinite(dy) && dx*dx+dy*dy<64.0f)return true;
        }
        return false;
    }
    bool hold_crusher_target(Garage& g,float step,bool locked) {
        if(g.aux_quarantined){
            if(g.hold_active)release_crusher_hold(g);
            run_failed_=true;diagnostic_="Crusher vehicle action is quarantined after an entered native failure";return false;
        }
        if(g.hold_active && (g.held_target.handle!=g.target.handle || g.held_target.entity!=g.target.entity ||
           g.held_target.generation!=g.target.generation) && !release_crusher_hold(g))return false;
        const auto current=target_view(g.target,false);
        if(current.state==TargetState::unavailable){run_failed_=true;diagnostic_="Crusher target identity unavailable before hold-still action";return false;}
        if(current.state==TargetState::gone)return true;
        const auto player=world.player?world.player():std::nullopt;
        if(!player || player->ped<0 || player->ped_entity==0 || !player_identity_live(*player)){run_failed_=true;diagnostic_="Focused player identity unavailable before crusher action";return false;}
        if(player_occupies_target(*current.car,*player))return true;
        // Recheck both identities immediately before the native vehicle mutation.
        const auto confirm=target_view(g.target,false);
        const auto player_confirm=world.player?world.player():std::nullopt;
        if(confirm.state!=TargetState::live || !confirm.car || !player_confirm || player_confirm->ped!=player->ped ||
           player_confirm->ped_entity!=player->ped_entity || !player_identity_live(*player_confirm) ||
           player_occupies_target(*confirm.car,*player_confirm)) {
            run_failed_=true;diagnostic_="Crusher target or player identity changed before hold-still action";return false;
        }
        if(!world.crusher_hold){run_failed_=true;diagnostic_="Physical crusher vehicle-hold consumer is unavailable";return false;}
        const auto result=world.crusher_hold(*confirm.car,step,locked);
        if(!result.executed) {
            run_failed_=true;
            diagnostic_="Crusher hold-still action"+std::string(result.native_entered?" failed after native entry; replay quarantined: ":" refused before native entry: ")+result.diagnostic;
            if(result.native_entered){g.aux_quarantined=true;g.hold_active=true;g.held_target=g.target;release_crusher_hold(g);}
            return false;
        }
        g.hold_active=true;g.held_target=g.target;
        return true;
    }
    static bool same_player(const Garage::Finish& finish,const ExclusiveGaragePlayer& player) {
        return player.ped>=0 && player.ped_entity!=0 && player.ped==finish.player_ped &&
            player.ped_entity==finish.player_entity;
    }
    bool player_identity_live(const ExclusiveGaragePlayer& player) const {
        if(player.ped<0 || player.ped_entity==0 || !world.ped_identity)return false;
        const auto identity=world.ped_identity(player.ped);
        return identity && *identity==player.ped_entity;
    }
    bool finish_player(Garage::Finish& finish,std::optional<ExclusiveGaragePlayer>& player) {
        player=world.player?world.player():std::nullopt;
        if(!player || player->ped<0 || player->ped_entity==0)return false;
        if(!player_identity_live(*player))return false;
        if(!same_player(finish,*player)) {
            finish.quarantined=true;run_failed_=true;
            diagnostic_="Focused player identity changed during garage transaction; transaction quarantined";
            return false;
        }
        return true;
    }
    static bool capture_occupants(Garage::Finish& finish,const RouteVehicleSnapshot& car) {
        finish.occupants.clear();
        if(car.driver_handle>=0) {
            if(!car.driver_ped)return false;
            finish.occupants.push_back({car.driver_handle,car.driver_ped});
        }
        for(size_t i=0;i<car.passenger_handles.size();++i)if(car.passenger_handles[i]>=0) {
            if(!car.passenger_peds[i])return false;
            finish.occupants.push_back({car.passenger_handles[i],car.passenger_peds[i]});
        }
        return true;
    }
    static bool occupies(const RouteVehicleSnapshot& car,int32_t ped) {
        if(ped<0)return false;
        if(car.driver_handle==ped)return true;
        for(const auto passenger:car.passenger_handles)if(passenger==ped)return true;
        return false;
    }
    // Retail DestroyVehicleAndDriverAndPassengers as an idempotent transaction. Nothing is published until every
    // native action succeeded; a retry only repeats the actions that did not succeed (no duplicate payment). The
    // focused player is never deleted: a vehicle holding the player is not destroyed at all (aborted).
    enum class FinishResult {done,retry,aborted,vanished,quarantined};
    FinishResult run_finish(Garage& g,const std::optional<RouteVehicleSnapshot>& car,bool pay) {
        auto& f=g.finish;
        if(f.quarantined){run_failed_=true;diagnostic_="Garage transaction is quarantined; native action will not be replayed";return FinishResult::quarantined;}
        if(!f.started) {
            if(!car)return FinishResult::retry;
            const auto player=world.player?world.player():std::nullopt;
            if(!player || player->ped<0 || player->ped_entity==0 || !player_identity_live(*player))return FinishResult::retry; // unavailable: never mutate on unknown identity
            if(player_occupies_target(*car,*player))return FinishResult::aborted;
            const auto model=world.original_model?world.original_model(car->handle):std::nullopt;
            if(!pay && !model)return FinishResult::retry;
            if(!capture_occupants(f,*car))return FinishResult::retry;
            for(const auto& occupant:f.occupants) {
                const auto identity=world.ped_identity?world.ped_identity(occupant.handle):std::nullopt;
                if(!identity || *identity!=occupant.entity)return FinishResult::retry;
            }
            f.started=true;f.car=remember(*car);f.paid=!pay;f.announced=!pay;f.car_deleted=false;f.next=0;
            f.player_ped=player->ped;f.player_entity=player->ped_entity;
            if(model){f.model=*model;f.model_known=true;}
            f.reward=pay?crusher_reward(*car):0;
        }
        auto current=target_view(f.car,false);
        if(current.state==TargetState::unavailable)return FinishResult::retry;
        if(current.state==TargetState::gone)return f.car_deleted?FinishResult::done:FinishResult::vanished;
        std::optional<ExclusiveGaragePlayer> player;
        if(!finish_player(f,player))return f.quarantined?FinishResult::quarantined:FinishResult::retry;
        if(player_occupies_target(*current.car,*player))return f.action_started?FinishResult::retry:FinishResult::aborted;
        if(!pay && collector(g.type) && !f.collection_marked) {
            // Retail marks the list and then destroys the car in the same frame. Here the bit is saved (and read
            // back) first and only once, so a destroyed delivery can never be missing from the saved game.
            const auto list=collected_mask(g.type);
            if(!list){run_failed_=true;diagnostic_="Import list store is unavailable";return FinishResult::retry;}
            const auto slot=f.model_known?slot_for(g,f.model,false,*list):std::nullopt;
            if(slot && !mark_collected(g,*slot))return FinishResult::retry;
            f.collection_marked=true;f.action_started=true;
        }
        if(!f.paid) {
            if(!run(f,0x0109,{0,f.reward}))return f.quarantined?FinishResult::quarantined:FinishResult::retry;
            f.paid=true;
            f.action_started=true;
        }
        if(!f.announced) {
            if(!world.crusher_reward_message || !world.crusher_reward_message(f.reward))return FinishResult::retry;
            f.announced=true;
        }
        while(f.next<f.occupants.size()) {
            current=target_view(f.car,false);
            if(current.state==TargetState::unavailable)return FinishResult::retry;
            if(current.state==TargetState::gone)return f.car_deleted?FinishResult::done:FinishResult::vanished;
            if(!finish_player(f,player))return f.quarantined?FinishResult::quarantined:FinishResult::retry;
            if(player_occupies_target(*current.car,*player))return FinishResult::retry;
            const auto occupant=f.occupants[f.next];
            int32_t handle=-1;uintptr_t entity=0;
            if(current.car->driver_handle==occupant.handle){handle=current.car->driver_handle;entity=current.car->driver_ped;}
            for(size_t i=0;i<current.car->passenger_handles.size();++i)
                if(current.car->passenger_handles[i]==occupant.handle){handle=current.car->passenger_handles[i];entity=current.car->passenger_peds[i];break;}
            if(handle<0 || entity!=occupant.entity){++f.next;continue;} // left, or the handle now names a different ped
            const auto live_identity=world.ped_identity?world.ped_identity(occupant.handle):std::nullopt;
            if(!live_identity)return FinishResult::retry;
            if(*live_identity!=occupant.entity)return FinishResult::retry; // never delete a reused or unavailable ped handle
            if(!run(f,0x009B,{occupant.handle}))return f.quarantined?FinishResult::quarantined:FinishResult::retry;
            ++f.next;f.action_started=true;
        }
        current=target_view(f.car,false);
        if(current.state==TargetState::unavailable)return FinishResult::retry;
        if(current.state==TargetState::gone)return f.car_deleted?FinishResult::done:FinishResult::vanished;
        if(!finish_player(f,player))return f.quarantined?FinishResult::quarantined:FinishResult::retry;
        if(player_occupies_target(*current.car,*player))return FinishResult::retry;
        if(current.car->driver_handle>=0 || std::any_of(current.car->passenger_handles.begin(),current.car->passenger_handles.end(),[](int32_t handle){return handle>=0;}))
            return FinishResult::retry; // do not delete a car containing a new/unvalidated occupant
        if(!run(f,0x00A6,{f.car.handle}))return f.quarantined?FinishResult::quarantined:FinishResult::retry;
        f.car_deleted=true;f.action_started=true;
        current=target_view(f.car,false);
        if(current.state==TargetState::gone)return FinishResult::done;
        if(current.state==TargetState::unavailable)return FinishResult::retry;
        f.quarantined=true;run_failed_=true;
        diagnostic_="DELETE_CAR returned success but the exact target remains live; transaction quarantined";
        return FinishResult::quarantined;
    }
    int32_t crusher_reward(const RouteVehicleSnapshot& car) const {
        const auto model=world.original_model?world.original_model(car.handle):std::nullopt;
        const auto value=model?catalog.values.find(*model):catalog.values.end();
        const float monetary=value==catalog.values.end()?0.0f:float(value->second);
        const float reward=std::min(125.0f,25.0f+monetary*car.health*(1.0f/500000.0f));
        return int32_t(reward*float(multiplier_));
    }

    bool update_crusher(Garage& g,uint32_t now,float step) {
        if(g.door_quarantined){release_crusher_hold(g);run_failed_=true;diagnostic_="Crusher door is quarantined after an entered native failure";return false;}
        if(!world.crusher_hold || !world.crusher_release){run_failed_=true;diagnostic_="Physical crusher vehicle consumer unavailable";return false;}
        if(!world.crusher_ready){run_failed_=true;diagnostic_="Crusher readiness consumer is unavailable";return false;}
        const auto ready=world.crusher_ready();
        if(!ready){run_failed_=true;diagnostic_="Crusher readiness/profile/ownership check is unavailable";return false;}
        if(!*ready) {
            if(g.hold_active) {
                const auto held=target_view(g.held_target,false);
                const auto player=world.player?world.player():std::nullopt;
                if(held.state!=TargetState::live || !player || !player_identity_live(*player) || player_occupies_target(*held.car,*player))
                    if(!release_crusher_hold(g))return false;
            }
            return true;
        }
        if(!set_crusher_door(g,g.door))return false;
        switch(g.state) {
        case State::opened: {
            const auto handles=pool_slice(frame_%8,8);
            if(!handles){run_failed_=true;diagnostic_="Crusher vehicle-pool slice unavailable";return false;}
            for(const auto handle:*handles) {
                const auto found=lookup(handle);
                if(found.status!=VehicleLookupStatus::present || !found.snapshot)continue;
                const auto& v=*found.snapshot;
                if(!((v.native_class>=0 && v.native_class<=2) || v.is_bike))continue;
                const auto contained=crusher_entirely_inside(g,v);
                if(!contained)return false;
                if(!*contained)continue;
                g.state=State::waiting_for_car;g.target=remember(v);g.crane_ms=now;g.finish=Garage::Finish{};g.aux_quarantined=false;
                break;
            }
            break;
        }
        case State::waiting_for_car: {
            const auto view=target_view(g.target,false);
            if(view.state==TargetState::unavailable)break;
            if(view.state==TargetState::live){if(now-g.crane_ms>3000)g.state=State::closing;}
            else {g.target={};g.state=State::opened;} // retail would stay here forever; SA port reopens
            break;
        }
        case State::closing: {
            const auto view=target_view(g.target,false);
            if(view.state==TargetState::unavailable) {
                if(g.hold_active && !release_crusher_hold(g))return false;
                return true;
            }
            if(view.state==TargetState::live) {
                const auto player=world.player?world.player():std::nullopt;
                if(!player || !player_identity_live(*player)) {
                    if(!release_crusher_hold(g))return false;
                    run_failed_=true;diagnostic_="Focused player identity unavailable while crusher vehicle was held";return false;
                }
                if(player_occupies_target(*view.car,*player)) {
                    // Free the player through the actual lid before restoring
                    // vehicle collision. No payment/destruction may follow.
                    if(!set_crusher_door(g,half_pi) || !release_crusher_hold(g))return false;
                    if(g.finish.action_started) {
                        g.finish.quarantined=true;g.aux_quarantined=true;
                        run_failed_=true;diagnostic_="Player entered a crusher transaction after a native action; replay quarantined";return false;
                    }
                    g.target={};g.finish=Garage::Finish{};g.state=State::opened;return true;
                }
            }
            if(view.state==TargetState::live && g.aux_quarantined) {
                if(g.hold_active && !release_crusher_hold(g))return false;
                run_failed_=true;diagnostic_="Crusher vehicle hold is quarantined; target must leave before the garage can recover";return false;
            }
            if(view.state==TargetState::gone) {
                if(!release_crusher_hold(g))return false;
                if(!g.finish.started){g.target={};g.state=State::opening;g.aux_quarantined=false;break;}
            }
            const float next_door=std::max(0.0f,g.door-0.005f*step);
            if(view.state==TargetState::live && !hold_crusher_target(g,step,next_door<6.28318530718f/5.0f))return false;
            if(next_door!=g.door && !set_crusher_door(g,next_door)){release_crusher_hold(g);return false;}
            if(g.door==0.0f) {
                switch(run_finish(g,view.car,true)) {
                case FinishResult::done:
                    if(!release_crusher_hold(g))return false;
                    crushed_id_=g.target.handle; // published only after payment and deletion succeeded
                    g.target={};g.finish=Garage::Finish{};g.state=State::after_drop_off;g.action_ms=now+3000;
                    break;
                case FinishResult::aborted:   // the player is inside: nothing is paid, nothing deleted
                    if(!release_crusher_hold(g))return false;
                    g.target={};g.finish=Garage::Finish{};g.state=State::opening;g.aux_quarantined=false;break;
                case FinishResult::retry:
                    if(run_failed_)return false;
                    break;
                case FinishResult::vanished:
                    // Disappearance without our verified DELETE_CAR is not crusher success.
                    if(!release_crusher_hold(g))return false;
                    g.target={};g.finish=Garage::Finish{};g.state=State::opening;g.aux_quarantined=false;break;
                case FinishResult::quarantined:
                    if(!release_crusher_hold(g))return false;
                    return false;
                }
            }
            return true;
        }
        case State::after_drop_off:
            if(now>g.action_ms)g.state=State::opening;
            break;
        case State::opening:
            if(!release_crusher_hold(g))return false;
            {
                const float next_door=std::min(half_pi,g.door+0.005f*step);
                if(next_door!=g.door && !set_crusher_door(g,next_door))return false;
            }
            if(g.door==half_pi)g.state=State::opened;
            break;
        default:break;
        }
        return true;
    }

    // DoesCraigNeedThisCar / MarkThisCarAsCollectedForCraig over the LCS model id. `mask` is the saved list.
    std::optional<uint32_t> slot_for(const Garage& g,int32_t model,bool need_free,uint32_t mask) const {
        if(g.type!=8)return std::nullopt;
        static const char* const names[import_slots]={"hearse","faggio","freeway","spider","manana","shelby","pontiac","esprit",
            "hotrod","pcj600","sentinel","infernus","banshee","patriot","bfinject","landstal"};
        for(uint32_t i=0;i<table_total(g.type);++i) {
            const auto id=catalog.ids.find(names[i]);
            if(id==catalog.ids.end() || id->second!=model)continue;
            if(need_free && (mask&(1u<<i)))return std::nullopt;
            return i;
        }
        return std::nullopt;
    }
    bool needed(const Garage& g,const RouteVehicleSnapshot& v,uint32_t mask) const {
        const auto model=world.original_model?world.original_model(v.handle):std::nullopt;
        return model && slot_for(g,*model,true,mask);
    }
    // Retail MarkThisCarAsCollectedForCraig: set the slot bit in SA's saved list and confirm it by reading back.
    bool mark_collected(const Garage& g,uint32_t slot) {
        const auto index=uint32_t(table_index(g.type));
        if(!world.collected_read || !world.collected_write){run_failed_=true;diagnostic_="Import list store is unavailable";return false;}
        const auto before=world.collected_read(index);
        if(!before){run_failed_=true;diagnostic_="Import list could not be read before marking a delivery";return false;}
        if((*before&(1u<<slot))==0 && !world.collected_write(index,*before|(1u<<slot))) {
            run_failed_=true;diagnostic_="Import list could not be saved; the delivered car is not destroyed";return false;
        }
        const auto after=world.collected_read(index);
        if(!after || (*after&(1u<<slot))==0){run_failed_=true;diagnostic_="Import list write could not be confirmed";return false;}
        return true;
    }
    // Retail CGarage::UpdateDoorsHeight. An unmounted door is a pause; an entered failure quarantines the door.
    enum class DoorStep {done,pause,failed};
    DoorStep set_collector_door(Garage& g,float position) {
        if(g.door_quarantined){run_failed_=true;diagnostic_="Collector door is quarantined after an entered native failure";return DoorStep::failed;}
        if(g.door_applied && position==g.door)return DoorStep::done;
        if(!world.collector_door){run_failed_=true;diagnostic_="Physical collector door consumer is unavailable";return DoorStep::failed;}
        const auto result=world.collector_door(g.id,g.bounds,position);
        if(!result.executed) {
            if(result.native_entered) {
                run_failed_=true;g.door_quarantined=true;
                diagnostic_="Collector door update failed after native entry; replay quarantined: "+result.diagnostic;
                return DoorStep::failed;
            }
            // Refused before any mutation (door streamed out or ownership not yet provable): re-resolve next frame.
            g.door_height_known=false;diagnostic_="Collector door update refused before native entry: "+result.diagnostic;
            return DoorStep::pause;
        }
        g.door=position;g.door_applied=true;return DoorStep::done;
    }
    bool update_collector(Garage& g,uint32_t now,uint32_t elapsed) {
        const float step=float(elapsed)/20.0f;
        const auto player=world.player?world.player():std::nullopt;
        if(!player)return true;                                  // unavailable facts: no decision, no mutation
        std::optional<RouteVehicleSnapshot> player_vehicle;
        if(player->vehicle) {
            const auto found=lookup(*player->vehicle);
            if(found.status==VehicleLookupStatus::unavailable)return true;
            if(found.status==VehicleLookupStatus::present)player_vehicle=found.snapshot;
        }
        // The list SA saves is the only record of what was delivered; without it nothing may be decided.
        const auto list=collected_mask(g.type);
        if(!list){run_failed_=true;diagnostic_="Import list store is unavailable";return false;}
        if(!g.door_height_known) {
            if(!world.collector_door_height || !world.collector_door){run_failed_=true;diagnostic_="Physical collector door consumer is unavailable";return false;}
            const auto height=world.collector_door_height(g.id,g.bounds);
            if(!height || !std::isfinite(*height) || !(*height>0.0f))return true; // door not mounted yet: benign pause
            g.door_height=*height;g.door_height_known=true;
        }
        const bool player_in_car=player->vehicle.has_value();
        const auto center=std::array<float,2>{(g.inf_x+g.sup_x)*0.5f,(g.inf_y+g.sup_y)*0.5f};
        bool ok=true;
        switch(g.state) {
        case State::opened: {
            if(player_vehicle && needed(g,*player_vehicle,*list))g.target=remember(*player_vehicle);
            if(std::fabs(player->position[0]-center[0])>40.0f || std::fabs(player->position[1]-center[1])>40.0f) {
                g.state=State::closing;g.target={};g.finish=Garage::Finish{};break;
            }
            const auto view=target_view(g.target,true);
            if(view.state==TargetState::live && !player_in_car) {
                const auto ready=collector_ready_to_close(g,*view.car,*player);
                if(!ready){ok=false;break;}
                if(*ready)g.state=State::closing;
            }
            break;
        }
        case State::closing: {
            const float next=std::max(0.0f,g.door-0.04f*step);
            const auto moved=set_collector_door(g,next);
            if(moved==DoorStep::failed){ok=false;break;}
            if(moved==DoorStep::pause || g.door>0.0f)break;
            if(g.target.handle<0){g.state=State::fully_closed;g.finish=Garage::Finish{};break;}
            const auto view=target_view(g.target,false);
            if(view.state==TargetState::unavailable)break;
            if(view.state==TargetState::gone && !g.finish.started){g.target={};g.state=State::fully_closed;break;}
            // The door took many frames to close. Revalidate the exact car's collision,
            // surrounding vehicles, player clearance, and damage immediately before
            // the saved bit or any occupant/car deletion can be committed.
            if(view.state==TargetState::live && !g.finish.action_started) {
                const auto commit_player=world.player?world.player():std::nullopt;
                const auto ready=commit_player?collector_ready_to_close(g,*view.car,*commit_player):std::nullopt;
                if(!ready || !*ready) {
                    if(!ready){run_failed_=true;if(diagnostic_.empty())diagnostic_="Collector delivery safety could not be revalidated";ok=false;}
                    g.target={};g.finish=Garage::Finish{};g.state=State::opening;
                    break;
                }
            }
            switch(run_finish(g,view.car,false)) {
            case FinishResult::done:
                // The list bit was saved before the first destructive action; the delivery ends here.
                g.target={};g.finish=Garage::Finish{};g.state=State::fully_closed;
                break;
            case FinishResult::aborted:
                g.target={};g.finish=Garage::Finish{};g.state=State::opening;break;
            case FinishResult::retry:
                if(run_failed_)ok=false;
                break;
            case FinishResult::vanished:
                // Disappearance by another actor does not fill the export list.
                g.target={};g.finish=Garage::Finish{};g.state=State::fully_closed;
                break;
            case FinishResult::quarantined:
                ok=false;break;
            }
            break;
        }
        case State::fully_closed:
            if(world.player_on_mission && world.player_on_mission()){g.target={};break;}
            if(inside(g,player->position,0.0f)){g.state=State::opening;}
            if(player_vehicle) {
                const float dx=std::max({g.inf_x-player_vehicle->position[0],0.0f,player_vehicle->position[0]-g.sup_x});
                const float dy=std::max({g.inf_y-player_vehicle->position[1],0.0f,player_vehicle->position[1]-g.sup_y});
                if(dx*dx+dy*dy<64.0f && needed(g,*player_vehicle,*list))g.state=State::opening;
            }
            g.target={};
            break;
        case State::opening: {
            if(player_vehicle && needed(g,*player_vehicle,*list))g.target=remember(*player_vehicle);
            const float next=std::min(g.door_height,g.door+0.035f*step);
            const auto moved=set_collector_door(g,next);
            if(moved==DoorStep::failed){ok=false;break;}
            if(moved==DoorStep::done && g.door==g.door_height)g.state=State::opened;
            break;
        }
        default:break;
        }
        (void)now;
        return ok;
    }
    enum class OtherCars {none,some,unavailable};
    OtherCars other_car_inside(const Garage& g,int32_t except) {
        for(uint32_t slice=0;slice<8;++slice) {
            const auto handles=pool_slice(slice,8);
            if(!handles){run_failed_=true;diagnostic_="Collector vehicle-pool slice unavailable";return OtherCars::unavailable;}
            for(const auto handle:*handles) {
                if(handle==except)continue;
                const auto found=lookup(handle);
                if(found.status==VehicleLookupStatus::unavailable)return OtherCars::unavailable;
                if(found.status!=VehicleLookupStatus::present || !found.snapshot || !found.snapshot->alive)continue;
                if(!world.collector_vehicle_touches){
                    run_failed_=true;diagnostic_="Native collector vehicle-touching query is unavailable";return OtherCars::unavailable;
                }
                const auto touches=world.collector_vehicle_touches(g.bounds,*found.snapshot);
                if(!touches){run_failed_=true;diagnostic_="Native collector vehicle-touching query failed";return OtherCars::unavailable;}
                if(*touches)return OtherCars::some;
            }
        }
        return OtherCars::none;
    }
    std::optional<bool> collector_ready_to_close(const Garage& g,const RouteVehicleSnapshot& car,
                                                  const ExclusiveGaragePlayer& player) {
        if(!world.entirely_inside){run_failed_=true;diagnostic_="Native collector collision-containment query is unavailable";return {};}
        const auto contained=world.entirely_inside(g.bounds,car);
        if(!contained){run_failed_=true;diagnostic_="Native collector collision-containment query failed";return {};}
        if(!*contained)return false;
        const auto others=other_car_inside(g,car.handle);
        if(others==OtherCars::unavailable)return {};
        if(others==OtherCars::some)return false;
        if(player.vehicle || player.entering_car==std::optional<bool>(true))return false;
        if(!world.collector_player_outside){run_failed_=true;diagnostic_="Native collector player-boundary query is unavailable";return {};}
        const auto outside=world.collector_player_outside(g.bounds,player,2.0f);
        if(!outside){run_failed_=true;diagnostic_="Native collector player-boundary query failed";return {};}
        if(!*outside)return false;
        if(!world.collector_vehicle_deliverable){run_failed_=true;diagnostic_="Native collector vehicle damage query is unavailable";return {};}
        const auto deliverable=world.collector_vehicle_deliverable(car);
        if(!deliverable){run_failed_=true;diagnostic_="Native collector vehicle damage query failed";return {};}
        return *deliverable;
    }
};
