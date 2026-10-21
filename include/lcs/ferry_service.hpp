#pragma once
// LCS ferry service (retail CFerry / CFerryInst): a closed track read from DATA\PATHS\FERRY1.DAT, two ferries per
// path that follow one shared timeline (the second half a lap behind), dock at the two station turn points, and
// answer the ferry script commands from that simulation.
//
// This header is the whole behaviour that does not depend on a ferry model: track, timeline, docking, skipping,
// heading, boarding-space maths, door timers and path/collision switches. Everything that needs the ferry model
// (spawning the vehicle, applying a pose to it, door frame nodes, the two collision models and the model-space
// boarding offsets) is a FerryWorld callback that stays unavailable until a ferry model is bound; the service never
// invents a physical result for it.
//
// Included by native.hpp inside namespace lcs, after exclusive_garages.hpp.

struct FerryOffsets {
    // Model-space points the retail CVehicleModelInfo keeps for the ferry (FERRY_POS_*).
    std::array<float,3> ped_point{};
    std::array<std::array<float,3>,4> car{};
};

struct FerryPose {
    std::array<float,3> position{};
    std::array<float,3> forward{},right{},up{};
};

// Result of offering a pose to the physical producer. pending = the boat is being produced (model streaming); the
// service retries on the next frame and no script-visible answer is given until every boat exists.
struct FerryApply {
    enum class Status {applied,pending,failed};
    Status status=Status::failed;
    bool native_entered=false;
    std::string diagnostic;
};

struct FerryWorld {
    // Ground height under (x,y) for the track nodes (retail ProcessVerticalLine: hit height, water level 0 when
    // nothing is hit). nullopt = the surface cannot be read yet; Enable waits instead of using a guess. When the
    // callback is absent (no world) the track stays at the water level.
    std::function<std::optional<float>(float,float)> surface_z;
    // true = every ferry entity exists and is driven by this service; false/absent = no physical ferry. Queries a script
    // can observe are answered only while this is true.
    std::function<std::optional<bool>()> physical_ready;
    // true = a producer can create the entities (hull bound). Poses are offered to it before they exist, so the
    // entities can be created from the first track pose; absent = fall back to physical_ready.
    std::function<std::optional<bool>()> producer_available;
    // Create (once) or rewrite the physical ferry for `id` at this pose; speed is retail m_vecMoveSpeed.
    std::function<FerryApply(int32_t,const FerryPose&,const std::array<float,3>&)> apply_pose;
    // Door ratio 0..1 on the front/back leaf pair.
    std::function<NativeResult(int32_t,bool,float)> apply_door;
    // Retail SwitchFerryCollision: 0 = default model, 1 = docked collision model.
    std::function<NativeResult(int32_t,int32_t)> set_collision;
    std::function<std::optional<FerryOffsets>()> offsets;
    std::function<std::optional<std::array<float,2>>()> player_xy;
};

class FerryService {
public:
    static constexpr int stations=2,paths=1,ferries_per_path=2,max_ferries=paths*2;
    static constexpr float speed=0.1f,slowdown_distance=50.0f,time_stopped=10.0f;
    enum class Kind {stopped,cruising,slowing,accelerating};
    enum class Door {closed,opening,open,closing};
    struct Node {float x=0,y=0,z=0,t=0;};
    struct Line {Kind kind=Kind::cruising;float time=0,position=0,speed=0,acceleration=0;};
    struct Path {
        std::vector<Node> nodes;
        std::array<Line,stations*4+2> lines{};
        float length=0,duration=0;
        bool heights=false;     // node z resolved from world.surface_z (retail does this when the path is enabled)
    };
    FerryWorld world;

    // Reads the text track: first line is the node count, then "x y z" rows (retail ignores the stored z).
    bool load_path(size_t index,std::istream& input) {
        if(index>=paths)return false;
        std::string line;
        if(!std::getline(input,line))return false;
        int count=0;
        if(std::sscanf(line.c_str(),"%d",&count)!=1 || count<4 || count>10000)return false;
        Path path;
        for(int i=0;i<count;++i) {
            if(!std::getline(input,line))return false;
            Node node;
            if(std::sscanf(line.c_str(),"%f %f %f",&node.x,&node.y,&node.z)!=3 || !std::isfinite(node.x) || !std::isfinite(node.y))return false;
            node.z=0.0f;
            path.nodes.push_back(node);
        }
        if(!build(path))return false;
        paths_[index]=std::move(path);have_path_[index]=true;
        return true;
    }
    bool has_path(size_t index)const{return index<paths && have_path_[index];}
    const Path& path(size_t index)const{return paths_.at(index);}

    // Retail EnableFerryPath for the script: success is reported only once the pair of ferries physically exists.
    // ready = both boats exist and are driven; pending = the producer is working (the script thread retries the same
    // instruction; a repeated call never creates anything twice); refused = no producer, or a production that did not
    // finish within pending_limit frames.
    enum class Enable {refused,pending,ready};
    static constexpr uint32_t pending_limit=1800;
    Enable enable_path(int32_t index) {
        if(index<0 || index>=paths || !have_path_[size_t(index)]){diagnostic_="Ferry track is not mounted";return Enable::refused;}
        if(enable_failed_)return Enable::refused;
        if(!world.apply_pose || !producer()){diagnostic_="No physical ferry producer is available";return Enable::refused;}
        enable_requested_=true;       // from here the frame consumer counts waiting frames, with or without an entity yet
        Path& path=paths_[size_t(index)];
        if(!path.heights && !resolve_heights(path)){diagnostic_="Ferry track surface height is not readable yet";return Enable::pending;}
        for(int i=index*2;i<index*2+2;++i) {
            if(ferries_[size_t(i)].exists)continue;
            Ferry f;f.exists=true;f.id=i;
            const float heading=(i&1)?half_pi:3.0f*half_pi;
            f.pose.forward={-std::sin(heading),std::cos(heading),0.0f};
            f.pose.right={std::cos(heading),std::sin(heading),0.0f};f.pose.up={0,0,1};
            ferries_[size_t(i)]=f;
        }
        return physical()?Enable::ready:Enable::pending;
    }
    bool exists(int32_t id)const{return id>=0 && id<max_ferries && ferries_[size_t(id)].exists;}
    void set_disabled(bool value){disabled_=value;}
    bool disabled()const{return disabled_;}
    // Retail SkipFerryToNextDock.
    bool skip_to_next_dock(int32_t id){if(!exists(id))return false;ferries_[size_t(id)].skip=1;return true;}
    bool docked(int32_t id)const{return exists(id) && ferries_[size_t(id)].docked;}
    std::optional<FerryPose> pose(int32_t id)const{return exists(id)?std::optional<FerryPose>(ferries_[size_t(id)].pose):std::nullopt;}
    // Retail GetFerryHeading: Atan2(-forward.x, forward.y).
    std::optional<float> heading(int32_t id)const{
        if(!exists(id))return std::nullopt;
        const auto& f=ferries_[size_t(id)].pose.forward;
        return std::atan2(-f[0],f[1]);
    }
    // Retail GetClosestFerry + IsDocked: id of the closest ferry within 300 units when it is docked, else -1.
    int32_t closest_docked(float x,float y)const {
        int closest=-1;float best=9999.9f;
        for(int i=0;i<max_ferries;++i) {
            if(!ferries_[size_t(i)].exists)continue;
            const auto& p=ferries_[size_t(i)].pose.position;
            const float d=std::hypot(p[0]-x,p[1]-y);
            if(d<300.0f && d<best){best=d;closest=i;}
        }
        return closest>=0 && ferries_[size_t(closest)].docked?closest:-1;
    }
    // Retail GetBoardingSpace(use 0 ped / 1 car, style 0/1, car slot 0..3). nullopt = unavailable.
    std::optional<std::array<float,3>> boarding_space(int32_t id,int32_t use,int32_t style,int32_t slot)const {
        if(!exists(id) || (use!=0 && use!=1) || (style!=0 && style!=1) || (use==1 && (slot<0 || slot>3)))return std::nullopt;
        const auto offsets=world.offsets?world.offsets():std::nullopt;
        if(!offsets)return std::nullopt;
        if(id&1)style=style==0?1:0;
        const auto space=use==0?offsets->ped_point:offsets->car[size_t(slot)];
        const auto& f=ferries_[size_t(id)].pose;
        std::array<float,3> world_point{};
        for(int a=0;a<3;++a)world_point[size_t(a)]=f.right[size_t(a)]*space[0]+f.forward[size_t(a)]*space[1]+f.up[size_t(a)]*space[2]+f.position[size_t(a)];
        if(style==1)for(int a=0;a<3;++a)world_point[size_t(a)]-=2.0f*space[0]*f.right[size_t(a)]+2.0f*space[1]*f.forward[size_t(a)];
        return world_point;
    }
    // Retail SwitchFerryCollision. A ferry whose model collision could not be switched stays unchanged and reports failure.
    bool switch_collision(int32_t type) {
        if(type!=0 && type!=1)return false;
        for(auto& f:ferries_) {
            if(!f.exists || f.collision==type)continue;
            if(physical()) {
                if(!world.set_collision)return false;
                if(!world.set_collision(f.id,type).executed)return false;
            }
            f.collision=type;                 // without a bound model only the logical choice exists
        }
        return true;
    }
    bool physical()const{const auto ready=world.physical_ready?world.physical_ready():std::nullopt;return ready.value_or(false);}
    bool producer()const{
        if(!world.producer_available)return physical();
        return world.producer_available().value_or(false);
    }
    // Door state machine (retail OpenDoor/CloseDoor/CompleteDorrMovement/IsDoorOpen/IsDoorClosed).
    // The script commands run between two frames: the door timers use the time of the last frame tick.
    bool open_door(int32_t id){return open_door(id,last_now_);}
    bool close_door(int32_t id){return close_door(id,last_now_);}
    bool open_door(int32_t id,uint32_t now) {
        if(!exists(id))return false;
        auto& f=ferries_[size_t(id)];
        f.door=Door::opening;f.door_timer=now+uint32_t(door_ms);f.door_dirty=true;
        const auto offsets=world.offsets?world.offsets():std::nullopt;
        const auto player=world.player_xy?world.player_xy():std::nullopt;
        f.front_door=true;
        if(offsets && player) {
            const auto near1=to_world(f.pose,offsets->car[0]),near4=to_world(f.pose,offsets->car[3]);
            const float d1=std::hypot(player->at(0)-near1[0],player->at(1)-near1[1]),d4=std::hypot(player->at(0)-near4[0],player->at(1)-near4[1]);
            if(d4<d1)f.front_door=false;
        }
        return true;
    }
    bool close_door(int32_t id,uint32_t now){if(!exists(id))return false;auto& f=ferries_[size_t(id)];f.door=Door::closing;f.door_timer=now+uint32_t(door_ms);f.door_dirty=true;return true;}
    bool complete_door_movement(int32_t id){if(!exists(id))return false;ferries_[size_t(id)].door_timer=0;return true;}
    std::optional<bool> door_open(int32_t id)const{return exists(id)?std::optional<bool>(!ferries_[size_t(id)].door_dirty && ferries_[size_t(id)].door==Door::open):std::nullopt;}
    std::optional<bool> door_closed(int32_t id)const{return exists(id)?std::optional<bool>(!ferries_[size_t(id)].door_dirty && ferries_[size_t(id)].door==Door::closed):std::nullopt;}

    const std::string& diagnostic()const{return diagnostic_;}
    void clear(){ferries_={};disabled_=false;enable_failed_=false;enable_requested_=false;pending_ticks_=0;diagnostic_.clear();for(auto& p:paths_)p.heights=false;}

    // One frame: retail UpdateFerrys, then ProcessControl for every ferry. step_ms is CTimer::GetTimeStepInMilliseconds.
    bool tick(uint32_t now_ms,uint32_t step_ms) {
        diagnostic_.clear();last_now_=now_ms;
        bool ok=true,waiting=false;
        update_timeline(step_ms);
        for(auto& f:ferries_) {
            if(!f.exists || !have_path_[size_t(f.id/2)])continue;
            process(f,now_ms);
            if(world.apply_pose && producer()) {
                const auto result=world.apply_pose(f.id,f.pose,f.move_speed);
                if(result.status==FerryApply::Status::pending)waiting=true;
                else if(result.status==FerryApply::Status::failed){diagnostic_="Ferry pose could not be applied: "+result.diagnostic;ok=false;waiting=true;}
            }
            // The ratio is written whenever it changed, including the final 0/1 of a finished movement, so the nodes
            // never stay at the last partial angle. A refused write stays dirty and is retried.
            if(f.door_dirty && world.apply_door && physical()) {
                const auto result=world.apply_door(f.id,f.front_door,f.door_ratio);
                if(result.executed)f.door_dirty=f.door==Door::opening || f.door==Door::closing;
                else{diagnostic_="Ferry door could not be applied: "+result.diagnostic;ok=false;}
            }
        }
        // Production that never finishes (surface unreadable, model missing, creation refused) must not hold a script
        // forever: the wait is counted from the Enable request itself, before and after the first entity exists.
        if(enable_requested_ && !enable_failed_) {
            if(physical() && !waiting)pending_ticks_=0;
            else if(++pending_ticks_>pending_limit){enable_failed_=true;if(diagnostic_.empty())diagnostic_="Ferry production did not finish";}
        }
        return ok;
    }
    uint32_t phase_ticks(int32_t id)const{return exists(id)?ferries_[size_t(id)].time_along:0;}

private:
    static constexpr float half_pi=1.57079632679f,pi=3.14159265359f,door_ms=10000.0f;
    struct Ferry {
        bool exists=false;int32_t id=0;
        FerryPose pose;
        size_t cur_node=0;
        uint32_t time_along=0;
        float position=0,speed_units=0;      // CFerryInst::m_afPositions / m_afSpeeds
        std::array<float,3> move_speed{};
        bool docked=false,approaching=false;
        int skip=0;
        Door door=Door::closed;uint32_t door_timer=0;bool front_door=true;float door_ratio=0;bool door_dirty=false;
        int collision=0;
    };
    std::array<Path,paths> paths_;
    std::array<bool,paths> have_path_{};
    std::array<Ferry,max_ferries> ferries_;
    bool disabled_=false,enable_failed_=false,enable_requested_=false;
    uint32_t last_now_=0;
    uint32_t pending_ticks_=0;
    std::string diagnostic_;
    bool any_exists()const{for(const auto& f:ferries_)if(f.exists)return true;return false;}
    bool resolve_heights(Path& p) {
        for(auto& node:p.nodes) {
            float z=0.0f;
            if(world.surface_z) {
                const auto surface=world.surface_z(node.x,node.y);
                if(!surface || !std::isfinite(*surface))return false;
                z=*surface;
            }
            node.z=z+0.2f;
        }
        p.heights=true;return true;
    }

    static std::array<float,3> to_world(const FerryPose& f,const std::array<float,3>& s) {
        std::array<float,3> out{};
        for(size_t a=0;a<3;++a)out[a]=f.right[a]*s[0]+f.forward[a]*s[1]+f.up[a]*s[2]+f.position[a];
        return out;
    }
    bool build(Path& p) const {
        const size_t n=p.nodes.size();
        float t=0.0f;
        for(size_t i=0;i<n;++i) {
            p.nodes[i].t=t;
            const auto& next=p.nodes[(i+1)%n];
            t+=std::sqrt((next.x-p.nodes[i].x)*(next.x-p.nodes[i].x)+(next.y-p.nodes[i].y)*(next.y-p.nodes[i].y));
        }
        p.length=t;
        for(auto& node:p.nodes)node.z=0.2f;      // replaced by resolve_heights() when the path is enabled
        // The two station turn points are the nodes where the x direction reverses.
        std::array<size_t,stations> station_nodes{};
        int found=0;
        for(size_t i=0;i<n;++i) {
            const auto& cur=p.nodes[i];const auto& next=p.nodes[(i+1)%n];const auto& prev=p.nodes[(i+n-1)%n];
            if(cur.x-next.x>0.0f && prev.x-cur.x<0.0f){if(found<stations)station_nodes[size_t(found)]=i;++found;}
            if(cur.x-next.x<0.0f && prev.x-cur.x>0.0f){if(found<stations)station_nodes[size_t(found)]=i;++found;}
        }
        if(found<stations)return false;
        float position=0.0f,time=0.0f;
        size_t j=0;
        for(int s=0;s<stations;++s) {
            const float station=p.nodes[station_nodes[size_t(s)]].t;
            p.lines[j++]={Kind::cruising,time,position,speed,0.0f};
            const float dist=(station-slowdown_distance)-position;
            time+=dist/speed;position+=dist;
            p.lines[j++]={Kind::slowing,time,position,speed,-(speed*speed)/(4*slowdown_distance)};
            time+=2*slowdown_distance/speed;position+=slowdown_distance;
            p.lines[j++]={Kind::stopped,time,position,0.0f,0.0f};
            time+=time_stopped;
            p.lines[j++]={Kind::accelerating,time,position,0.0f,(speed*speed)/(4*slowdown_distance)};
            time+=2*slowdown_distance/speed;position+=slowdown_distance;
        }
        p.lines[j++]={Kind::cruising,time,position,speed,0.0f};
        p.duration=time+(p.length-position)/speed;
        p.lines[j].time=p.duration;
        return std::isfinite(p.duration) && p.duration>0.0f;
    }
    void update_timeline(uint32_t step_ms) {
        if(disabled_)return;
        for(int i=0;i<max_ferries;++i) {
            auto& f=ferries_[size_t(i)];
            if(!f.exists || !have_path_[size_t(i/2)])continue;
            const Path& p=paths_[size_t(i/2)];
            f.time_along+=step_ms;
            float t=p.duration*float((f.time_along+uint32_t(i&1)*0x20000u)&0x3FFFFu)/float(0x40000);
            size_t j=0;
            while(j+1<p.lines.size() && t>p.lines[j+1].time)++j;
            float delta=t-p.lines[j].time;
            auto partner=[&](Ferry& self)->Ferry*{const int other=(self.id&1)?self.id-1:self.id+1;return ferries_[size_t(other)].exists?&ferries_[size_t(other)]:nullptr;};
            if(f.docked && p.lines[j].kind==Kind::slowing) {
                const uint32_t jump=uint32_t(p.lines[j+1].time-p.lines[j].time);
                f.time_along+=jump;
                ++j;
                if(j>stations*4+1)j=0;
                f.approaching=true;
                if(auto* other=partner(f)){other->approaching=true;other->time_along+=jump;}
            }
            if(f.skip==1) {
                float skipped=0.0f;
                f.skip=2;
                while(p.lines[j].kind!=Kind::stopped) {
                    skipped+=p.lines[j+1].time-p.lines[j].time;
                    ++j;
                    if(j>stations*4)j=0;
                }
                f.time_along+=uint32_t(skipped);
                f.approaching=true;
                if(auto* other=partner(f)){other->approaching=true;other->time_along+=uint32_t(skipped);}
            }
            const Line& line=p.lines[j];
            const float rate=p.duration*1000.0f/float(0x40000);
            switch(line.kind) {
            case Kind::stopped:f.position=line.position;f.speed_units=0.0f;break;
            case Kind::cruising:f.position=line.position+line.speed*delta;f.speed_units=rate*line.speed;break;
            case Kind::slowing:case Kind::accelerating:
                f.approaching=line.kind==Kind::slowing;
                f.position=line.position+(line.speed+line.acceleration*delta)*delta;
                f.speed_units=rate*(line.speed+2*line.acceleration*delta);
                break;
            }
        }
    }
    // Position along the closed track -> point (linear between nodes).
    static std::array<float,3> point_at(const Path& p,float along,size_t& cursor) {
        const size_t n=p.nodes.size();
        float lo=p.nodes[cursor].t,hi=cursor+1<n?p.nodes[cursor+1].t:p.length;
        size_t guard=0;
        while((along<lo || along>hi) && guard++<=n) {
            cursor=(cursor+1)%n;
            lo=p.nodes[cursor].t;hi=cursor+1<n?p.nodes[cursor+1].t:p.length;
        }
        const size_t next=(cursor+1)%n;
        float dist=p.nodes[next].t-p.nodes[cursor].t;
        if(dist<0.0f)dist+=p.length;
        const float f=dist!=0.0f?(along-p.nodes[cursor].t)/dist:0.0f;
        return {(1-f)*p.nodes[cursor].x+f*p.nodes[next].x,(1-f)*p.nodes[cursor].y+f*p.nodes[next].y,(1-f)*p.nodes[cursor].z+f*p.nodes[next].z};
    }
    void process(Ferry& f,uint32_t now) {
        const Path& p=paths_[size_t(f.id/2)];
        float rear=f.position;
        if(rear<0.0f)rear+=p.length;
        const auto pos_rear=point_at(p,rear,f.cur_node);
        float front=rear+20.0f;
        if(front>p.length)front-=p.length;
        size_t front_node=f.cur_node;
        const auto pos_front=point_at(p,front,front_node);
        f.pose.position={(pos_rear[0]+pos_front[0])*0.5f,(pos_rear[1]+pos_front[1])*0.5f,(pos_rear[2]+pos_front[2])*0.5f};
        std::array<float,3> fwd{pos_front[0]-pos_rear[0],pos_front[1]-pos_rear[1],pos_front[2]-pos_rear[2]};
        const float length=std::sqrt(fwd[0]*fwd[0]+fwd[1]*fwd[1]+fwd[2]*fwd[2]);
        if(length>0.0f)for(auto& v:fwd)v/=length;
        const float dp=fwd[0]*f.pose.forward[0]+fwd[1]*f.pose.forward[1]+fwd[2]*f.pose.forward[2];
        std::array<float,3> use=f.pose.forward;
        if(std::fabs(dp)>1.001f || std::fabs(dp)<0.999f) {
            const float cross_z=fwd[0]*f.pose.forward[1]-fwd[1]*f.pose.forward[0];
            float angle;
            if(f.skip==2) {
                f.skip=0;
                const float a=std::acos(std::max(-1.0f,std::min(1.0f,dp)));
                angle=(fwd[0]<0.0f && f.pose.forward[0]<0.0f)?pi-a:a;
            } else angle=0.001f;
            if((dp>0.0f && cross_z<0.0f) || (dp<=0.0f && cross_z>0.0f))angle=-angle;
            const float c=std::cos(angle),s=std::sin(angle);
            use={f.pose.forward[0]*c-f.pose.forward[1]*s,f.pose.forward[0]*s+f.pose.forward[1]*c,f.pose.forward[2]};
        }
        std::array<float,3> right{use[1],-use[0],0.0f};            // forward x (0,0,1)
        const float rl=std::hypot(right[0],right[1]);
        if(rl>0.0f){right[0]/=rl;right[1]/=rl;}
        const std::array<float,3> up{right[1]*use[2]-right[2]*use[1],right[2]*use[0]-right[0]*use[2],right[0]*use[1]-right[1]*use[0]};
        f.pose.forward=use;f.pose.right=right;f.pose.up=up;
        const float track_speed=f.speed_units;
        f.move_speed={use[0]*track_speed/60.0f,use[1]*track_speed/60.0f,use[2]*track_speed/60.0f};
        const float speed_sq=f.move_speed[0]*f.move_speed[0]+f.move_speed[1]*f.move_speed[1]+f.move_speed[2]*f.move_speed[2];
        if(speed_sq>0.001f || !f.approaching)f.docked=false;
        else f.docked=true;
        switch(f.door) {
        case Door::opening:
            // Retail ELF 0x13E784..0x13E79C: ratio = 1 - (timer - now) * 0.0001 over the 10000 ms timer set by OpenDoor
            // (0x13F34C); the reLCS source divides by 1000 and would finish the swing in the last second only.
            if(now<f.door_timer)f.door_ratio=clamp01(1.0f-float(f.door_timer-now)/door_ms);
            else{f.door_ratio=1.0f;f.door=Door::open;}
            break;
        case Door::closing:
            if(now<f.door_timer)f.door_ratio=clamp01(float(f.door_timer-now)/door_ms);
            else{f.door_ratio=0.0f;f.door=Door::closed;}
            break;
        default:break;
        }
    }
    static float clamp01(float v){return v<0.0f?0.0f:v>1.0f?1.0f:v;}
};
