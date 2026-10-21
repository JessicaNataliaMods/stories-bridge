#include "lcs/native.hpp"
#include "lcs/route_vehicle_native.hpp"

#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace lcs {
#include "lcs/exclusive_garages.hpp"
#include "lcs/ferry_service.hpp"
}
using namespace lcs;

namespace {
void expect(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}

std::string ellipse(int count=40) {
    std::ostringstream text;text<<count<<"\r\n";
    for(int i=0;i<count;++i) {
        const double a=6.283185307179586*i/count+1.6;
        text<<(500.0+300.0*std::cos(a))<<' '<<(100.0*std::sin(a))<<" 0.0\r\n";
    }
    return text.str();
}
FerryApply applied(){FerryApply r;r.status=FerryApply::Status::applied;return r;}

// A fake physical producer with the same contract as sa_ferry: a pose creates the boat (after `stream_frames` pending
// answers) and later poses rewrite it; physical_ready is true only when both boats exist.
struct Fixture {
    FerryService ferry;
    uint32_t now=1000;
    int poses=0,creations=0,collisions=0,stream_frames=0;
    bool created[2]{false,false};
    bool collision_fails=false;
    std::optional<FerryOffsets> offsets;
    std::vector<float> door_ratios;
    std::vector<bool> door_fronts;
    std::optional<std::array<float,2>> player;
    Fixture(const std::string& track=ellipse(),bool producer=true) {
        std::istringstream in(track);
        expect(ferry.load_path(0,in),"track did not load");
        if(producer) {
            ferry.world.physical_ready=[this]()->std::optional<bool>{return created[0] && created[1];};
            ferry.world.producer_available=[]()->std::optional<bool>{return true;};
            ferry.world.apply_pose=[this](int32_t id,const FerryPose&,const std::array<float,3>&){
                ++poses;
                if(!created[id]) {
                    if(stream_frames>0){--stream_frames;FerryApply r;r.status=FerryApply::Status::pending;return r;}
                    created[id]=true;++creations;
                }
                return applied();
            };
        }
        ferry.world.set_collision=[this](int32_t,int32_t){++collisions;NativeResult r;r.executed=!collision_fails;return r;};
        ferry.world.apply_door=[this](int32_t,bool front,float ratio){door_ratios.push_back(ratio);door_fronts.push_back(front);NativeResult r;r.executed=true;return r;};
        ferry.world.offsets=[this]{return offsets;};
        ferry.world.player_xy=[this]{return player;};
    }
    void tick(int n=1){for(int i=0;i<n;++i){ferry.tick(now,20);now+=20;}}
    // Enable and run the producer until both boats exist, exactly like the script thread retrying the instruction.
    void enable(){
        for(int i=0;i<200;++i) {
            const auto status=ferry.enable_path(0);
            if(status==FerryService::Enable::ready)return;
            expect(status==FerryService::Enable::pending,"enable became a refusal while the producer works");
            tick();
        }
        expect(false,"enable never completed");
    }
};

void track_and_timeline_follow_the_original_construction() {
    Fixture f;
    const auto& p=f.ferry.path(0);
    expect(p.nodes.size()==40 && p.length>1200 && p.nodes[0].t==0.0f && p.nodes[1].t>0.0f,"track length/segment distances");
    float last=-1;bool ordered=true;
    for(size_t i=0;i<9;++i){ordered=ordered && p.lines[i].time>=last;last=p.lines[i].time;}
    expect(ordered && p.duration>p.lines[8].time,"keyframes are time ordered and the lap outlasts them");
    expect(p.lines[2].kind==FerryService::Kind::stopped && p.lines[6].kind==FerryService::Kind::stopped && p.lines[1].kind==FerryService::Kind::slowing,
           "each station is cruise, slow, stop, accelerate");
    expect(std::fabs((p.lines[3].time-p.lines[2].time)-FerryService::time_stopped)<1e-3f,"ten stopped units at each station");
    std::istringstream bad("3\n0 0 0\n1 0 0\n2 0 0\n");
    FerryService other;
    expect(!other.load_path(0,bad) && !other.load_path(1,bad),"short or out-of-range tracks are refused");
}

void enable_reports_success_only_when_the_boats_exist() {
    {   // No producer at all: the command is refused, never answered from a virtual ferry.
        Fixture none(ellipse(),false);
        expect(none.ferry.enable_path(0)==FerryService::Enable::refused && !none.ferry.exists(0),"no producer, no ferry");
    }
    Fixture f;
    f.stream_frames=3;
    expect(f.ferry.enable_path(-1)==FerryService::Enable::refused && f.ferry.enable_path(1)==FerryService::Enable::refused,"only the loaded path can be enabled");
    expect(f.ferry.enable_path(0)==FerryService::Enable::pending && !f.ferry.physical(),"the first call only starts production");
    f.tick();expect(f.ferry.enable_path(0)==FerryService::Enable::pending && f.creations==0,"still streaming: still pending, nothing created twice");
    int guard=0;FerryService::Enable status=FerryService::Enable::pending;
    while(status==FerryService::Enable::pending && guard++<50){f.tick();status=f.ferry.enable_path(0);}
    expect(status==FerryService::Enable::ready && f.creations==2 && f.ferry.physical(),"ready exactly when both boats exist");
    // Enable immediately followed by a query is coherent: the answer is read from the live pose.
    const auto heading=f.ferry.heading(0);
    expect(heading && f.ferry.exists(1) && f.ferry.pose(1),"a query right after Enable has a physical ferry behind it");
    expect(f.ferry.enable_path(0)==FerryService::Enable::ready && f.creations==2,"repeating the command replays nothing");
    const auto before=f.ferry.pose(0)->position;
    f.ferry.enable_path(0);
    expect(f.ferry.pose(0)->position==before,"re-enabling does not move an existing ferry");
}

void enable_gives_up_when_production_never_finishes() {
    Fixture f;f.stream_frames=1000000;
    expect(f.ferry.enable_path(0)==FerryService::Enable::pending,"production starts");
    int guard=0;
    while(f.ferry.enable_path(0)==FerryService::Enable::pending && guard++<FerryService::pending_limit+10)f.tick();
    expect(f.ferry.enable_path(0)==FerryService::Enable::refused && !f.ferry.diagnostic().empty(),"a stuck production is refused with a diagnostic, not awaited forever");
}

void enable_gives_up_when_the_surface_is_never_readable() {
    Fixture f;
    f.ferry.world.surface_z=[](float,float)->std::optional<float>{return std::nullopt;};
    expect(f.ferry.enable_path(0)==FerryService::Enable::pending && !f.ferry.exists(0),"an unreadable surface waits without creating anything");
    int guard=0;
    while(f.ferry.enable_path(0)==FerryService::Enable::pending && guard++<FerryService::pending_limit+10)f.tick();
    expect(f.ferry.enable_path(0)==FerryService::Enable::refused && !f.ferry.exists(0) && f.creations==0 && !f.ferry.diagnostic().empty(),
           "the wait is bounded before any entity exists and ends in a coherent refusal (no entity, no handle)");
}

void door_pose_and_state_share_the_ten_second_timer() {
    Fixture f;f.enable();f.tick(2);
    FerryOffsets o;o.car[0]={0,10,0};o.car[3]={0,-10,0};f.offsets=o;f.player=std::array<float,2>{-1000.0f,-1000.0f};
    f.door_ratios.clear();
    expect(f.ferry.open_door(0),"open uses the time of the last frame");
    f.tick(250);                                              // 5000 ms
    expect(f.ferry.door_open(0)==std::optional<bool>(false) && !f.door_ratios.empty() && std::fabs(f.door_ratios.back()-0.5f)<0.02f,
           "halfway through the retail 10 s timer the nodes are half open and the state is still opening");
    f.tick(240);
    expect(f.ferry.door_open(0)==std::optional<bool>(false) && f.door_ratios.back()>0.95f && f.door_ratios.back()<1.0f,"just before 10 s the swing is not finished yet");
    f.tick(20);
    expect(f.ferry.door_open(0)==std::optional<bool>(true) && f.door_ratios.back()==1.0f,"pose and state both finish at 10 s");
    f.door_ratios.clear();
    expect(f.ferry.close_door(0),"close");
    f.tick(250);
    expect(f.ferry.door_closed(0)==std::optional<bool>(false) && std::fabs(f.door_ratios.back()-0.5f)<0.02f,"closing is 1 - elapsed/10 s too");
    f.tick(260);
    expect(f.ferry.door_closed(0)==std::optional<bool>(true) && f.door_ratios.back()==0.0f,"closed after 10 s with the final ratio written");
    expect(f.ferry.complete_door_movement(0),"complete");
    bool refuse_final=true;
    f.ferry.world.apply_door=[&](int32_t,bool,float){NativeResult r;r.executed=!refuse_final;return r;};
    expect(f.ferry.open_door(0) && f.ferry.complete_door_movement(0),"finish an opening on the next frame");
    f.tick();
    expect(f.ferry.door_open(0)==std::optional<bool>(false),"an elapsed timer does not confirm a refused final physical door write");
    refuse_final=false;f.tick();
    expect(f.ferry.door_open(0)==std::optional<bool>(true),"the dirty endpoint is retried before publishing open");
    refuse_final=true;
    expect(f.ferry.close_door(0) && f.ferry.complete_door_movement(0),"finish a closing on the next frame");
    f.tick();expect(f.ferry.door_closed(0)==std::optional<bool>(false),"closed also waits for the physical endpoint");
    refuse_final=false;f.tick();expect(f.ferry.door_closed(0)==std::optional<bool>(true),"closing retries the endpoint too");
}

void track_heights_come_from_the_surface_consumer() {
    Fixture f;
    bool readable=false;int asked=0;
    f.ferry.world.surface_z=[&](float,float)->std::optional<float>{++asked;return readable?std::optional<float>(2.5f):std::nullopt;};
    expect(f.ferry.enable_path(0)==FerryService::Enable::pending && !f.ferry.exists(0),"an unreadable surface keeps Enable waiting without a guessed height");
    readable=true;
    f.enable();
    expect(asked>=40 && std::fabs(f.ferry.path(0).nodes[0].z-2.7f)<1e-5f,"every node sits 0.2 above the sampled surface");
    f.tick(3);
    expect(std::fabs(f.ferry.pose(0)->position[2]-2.7f)<0.05f,"the ferry rides at the sampled height");
}

void ferries_dock_and_are_found_only_while_docked() {
    Fixture f;f.enable();
    int32_t id=-1;
    for(int i=0;i<40000 && id<0;++i){f.tick();if(f.ferry.docked(0))id=0;else if(f.ferry.docked(1))id=1;}
    expect(id>=0,"neither ferry ever docked within a lap");
    const auto pose=f.ferry.pose(id);
    expect(f.ferry.closest_docked(pose->position[0],pose->position[1])==id,"the docked ferry is found near its position");
    expect(f.ferry.closest_docked(pose->position[0]+900,pose->position[1])==-1,"nothing is reported outside 300 units");
    bool moving_seen=false;
    for(int i=0;i<40000 && !moving_seen;++i){f.tick();if(!f.ferry.docked(id))moving_seen=true;}
    expect(moving_seen && f.ferry.closest_docked(f.ferry.pose(id)->position[0],f.ferry.pose(id)->position[1])!=id,"a ferry that left the station is no longer reported docked");
}

void skipping_reaches_the_next_dock_sooner() {
    Fixture normal;normal.enable();normal.tick(100);
    Fixture skipped;skipped.enable();skipped.tick(100);
    expect(skipped.ferry.skip_to_next_dock(0) && !skipped.ferry.skip_to_next_dock(5),"skip addresses an existing ferry");
    int a=-1,b=-1;
    for(int i=0;i<40000 && a<0;++i){normal.tick();if(normal.ferry.docked(0))a=i;}
    for(int i=0;i<40000 && b<0;++i){skipped.tick();if(skipped.ferry.docked(0))b=i;}
    expect(a>0 && b>=0 && b<a,"SkipFerryToNextDock advances the shared timeline to the next stop");
}

// The two ferries of a path share one timeline, the second half a lap behind: whatever a skip or a dock jump adds to
// one of them must be added to the other by the same amount, so their phase difference never changes.
void skips_keep_the_partner_phase(const std::string& track,const char* label) {
    Fixture f(track);f.enable();
    expect(f.ferry.phase_ticks(0)==f.ferry.phase_ticks(1),label);
    int skips=0,docked_jumps=0;
    for(int round=0;round<60;++round) {
        const int32_t id=round&1;
        const bool moving=!f.ferry.docked(id);
        if(f.ferry.skip_to_next_dock(id))++skips;
        const auto before_a=f.ferry.phase_ticks(0),before_b=f.ferry.phase_ticks(1);
        f.tick(1);
        const auto step_a=f.ferry.phase_ticks(0)-before_a,step_b=f.ferry.phase_ticks(1)-before_b;
        expect(step_a==step_b,"a skip advances both ferries by the same accumulated delta");
        if(step_a>1000u)++docked_jumps;
        f.tick(37);
        expect(f.ferry.phase_ticks(0)==f.ferry.phase_ticks(1),"the relative phase survives every skip");
        (void)moving;
    }
    // Long free run including the automatic dock jump of a docked ferry.
    for(int i=0;i<20000;++i) {
        f.tick();
        if(i%997==0)f.ferry.skip_to_next_dock(i&1);
        if(f.ferry.phase_ticks(0)!=f.ferry.phase_ticks(1))expect(false,"the relative phase survives automatic dock jumps");
    }
    expect(skips>=50,label);
    (void)docked_jumps;
}

void disabling_freezes_the_timeline() {
    Fixture f;f.enable();f.tick(300);
    const auto before=f.ferry.pose(0)->position;
    f.ferry.set_disabled(true);f.tick(400);
    const auto frozen=f.ferry.pose(0)->position;
    expect(frozen==before || std::hypot(frozen[0]-before[0],frozen[1]-before[1])<0.5f,"disabled ferries hold their position");
    f.ferry.set_disabled(false);f.tick(400);
    expect(std::hypot(f.ferry.pose(0)->position[0]-before[0],f.ferry.pose(0)->position[1]-before[1])>1.0f,"re-enabled ferries sail again");
}

void heading_and_boarding_space_use_the_pose() {
    Fixture f;f.enable();f.tick(50);
    const auto pose=*f.ferry.pose(0);
    expect(std::fabs(*f.ferry.heading(0)-std::atan2(-pose.forward[0],pose.forward[1]))<1e-6f && !f.ferry.heading(9),"heading is Atan2(-fx,fy)");
    expect(!f.ferry.boarding_space(0,0,0,0),"no model offsets, no boarding space");
    FerryOffsets o;o.ped_point={2,3,1};o.car[0]={1,-4,0.5f};o.car[3]={-1,5,0.5f};f.offsets=o;
    const auto even=f.ferry.boarding_space(0,0,0,0);
    expect(even && std::fabs((*even)[2]-(pose.position[2]+1.0f*pose.up[2]+2*pose.right[2]+3*pose.forward[2]))<1e-4f,"style 0 maps the model point through the ferry matrix");
    const auto mirrored=f.ferry.boarding_space(0,0,1,0);
    expect(mirrored && std::fabs((*mirrored)[0]-((*even)[0]-4*pose.right[0]-6*pose.forward[0]))<1e-3f,"style 1 mirrors across the ferry axes");
    const auto odd=f.ferry.boarding_space(1,0,0,0),odd_flip=f.ferry.boarding_space(1,0,1,0);
    expect(odd && odd_flip && *odd!=*odd_flip,"odd ferries swap the style");
    expect(!f.ferry.boarding_space(0,2,0,0) && !f.ferry.boarding_space(0,1,0,4) && !f.ferry.boarding_space(0,0,2,0),"invalid selectors are refused");
}

void collision_switch_needs_the_physical_ferry() {
    Fixture f;f.enable();
    f.ferry.world.producer_available=[]()->std::optional<bool>{return true;};
    expect(f.ferry.switch_collision(1) && f.collisions==2,"each existing physical ferry is switched");
    f.collision_fails=true;
    expect(!f.ferry.switch_collision(0),"a collision model that cannot be switched is reported");
    f.collision_fails=false;
    expect(f.ferry.switch_collision(0) && !f.ferry.switch_collision(2),"valid switch succeeds, unknown type is refused");
}

void doors_follow_their_timers_and_end_exactly() {
    Fixture f;f.enable();f.tick(2);
    expect(f.ferry.door_closed(0)==std::optional<bool>(true) && f.ferry.door_open(0)==std::optional<bool>(false),"doors start closed");
    FerryOffsets o;o.car[0]={0,10,0};o.car[3]={0,-10,0};f.offsets=o;
    f.player=std::array<float,2>{-1000.0f,-1000.0f};              // far from everything: the first end is kept
    expect(f.ferry.open_door(0,f.now) && !f.ferry.open_door(7,f.now),"open addresses an existing ferry");
    f.tick(10);
    expect(f.ferry.door_open(0)==std::optional<bool>(false) && !f.door_ratios.empty(),"the door is still opening and moving its nodes");
    f.ferry.complete_door_movement(0);f.tick(2);
    expect(f.ferry.door_open(0)==std::optional<bool>(true) && f.door_ratios.back()==1.0f,"the final open ratio is written, not left at the last partial step");
    f.ferry.close_door(0,f.now);f.ferry.complete_door_movement(0);f.tick(2);
    expect(f.ferry.door_closed(0)==std::optional<bool>(true) && f.door_ratios.back()==0.0f,"the final closed ratio is written");
    const size_t written=f.door_ratios.size();
    f.tick(5);
    expect(f.door_ratios.size()==written,"a settled door is not rewritten every frame");
}

void door_end_follows_the_player() {
    Fixture f;f.enable();f.tick(2);
    // Compare the player with the pose-mapped car1 and car4 dummies.
    FerryOffsets o;o.car[0]={0,12,0};o.car[3]={0,-12,0};f.offsets=o;
    const auto pose=*f.ferry.pose(0);
    const float near_car4_x=pose.position[0]-pose.forward[0]*12.0f,near_car4_y=pose.position[1]-pose.forward[1]*12.0f;
    f.player=std::array<float,2>{near_car4_x,near_car4_y};
    f.ferry.open_door(0,f.now);f.tick(2);
    expect(!f.door_fronts.empty() && !f.door_fronts.back(),"a player at the rear dummy opens the rear door");
    const float near_car1_x=pose.position[0]+pose.forward[0]*12.0f,near_car1_y=pose.position[1]+pose.forward[1]*12.0f;
    f.player=std::array<float,2>{near_car1_x,near_car1_y};
    f.ferry.open_door(0,f.now);f.tick(2);
    expect(f.door_fronts.back(),"a player at the front dummy opens the front door");
}

void real_track_when_available(const char* path) {
    if(!path)return;
    std::ifstream file(path);
    std::stringstream whole;whole<<file.rdbuf();
    FerryService ferry;
    std::istringstream again(whole.str());
    expect(whole.str().size()>100 && ferry.load_path(0,again),"original FERRY1.DAT did not load");
    const auto& p=ferry.path(0);
    expect(p.nodes.size()==50 && p.duration>0 && p.length>800 && p.length<900,"original track shape");
    expect(p.lines[2].kind==FerryService::Kind::stopped && p.lines[6].kind==FerryService::Kind::stopped,"original track has two stations");
    skips_keep_the_partner_phase(whole.str(),"the real FERRY1.DAT keeps the partner phase over multiple skips");
}
}

int main(int argc,char** argv) {
    try {
        track_and_timeline_follow_the_original_construction();
        enable_reports_success_only_when_the_boats_exist();
        enable_gives_up_when_production_never_finishes();
        enable_gives_up_when_the_surface_is_never_readable();
        track_heights_come_from_the_surface_consumer();
        door_pose_and_state_share_the_ten_second_timer();
        ferries_dock_and_are_found_only_while_docked();
        skipping_reaches_the_next_dock_sooner();
        skips_keep_the_partner_phase(ellipse(),"synthetic track keeps the partner phase over multiple skips");
        disabling_freezes_the_timeline();
        heading_and_boarding_space_use_the_pose();
        collision_switch_needs_the_physical_ferry();
        doors_follow_their_timers_and_end_exactly();
        door_end_follows_the_player();
        real_track_when_available(argc>1?argv[1]:nullptr);
        std::cout<<"ferry service tests passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
