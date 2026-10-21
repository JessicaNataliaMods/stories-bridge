#include "../src/sa_ferry.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using namespace lcs;

// ---- fake engine: vehicles, model info, collision loader, RW frames -----------------------------------------------
template<class T>static T get(const unsigned char* at){T v;std::memcpy(&v,at,sizeof(v));return v;}
template<class T>static void put(unsigned char* at,T v){std::memcpy(at,&v,sizeof(v));}
static bool engine_ready=true,model_resident=true,create_fails=false,release_blocked=false;
static std::optional<RouteVehicleSnapshot> vehicles[2];
static uintptr_t* registered_cells[2]={nullptr,nullptr};
alignas(8) static unsigned char entity[3][0xA18];
static uint8_t vehicle_tags[3]={0x80,0x80,0x80};
struct FakeVehiclePool {uintptr_t data,tags;int32_t count;};
static FakeVehiclePool fake_vehicle_pool{reinterpret_cast<uintptr_t>(entity),reinterpret_cast<uintptr_t>(vehicle_tags),3};
alignas(8) static unsigned char matrix[2][0x40];
static int created=0,removed=0,requested=0;static bool not_a_boat=false;
static int world_add_calls=0,world_remove_calls=0,world_add_failures=0,world_remove_failures=0;
static float sampled_ground=0.0f;static bool ground_readable=true;
// model info 454: m_pColModel at +0x14 points at the original (COLFILE) collision
alignas(8) static unsigned char model_info[0x40];
alignas(8) static unsigned char original_col[0x30];
static int col_constructs=0,col_loads=0,col_destroys=0;static bool col_load_fails=false;
alignas(8) static unsigned char col_data_sentinel[16];
// RW frames: clump pointer is entity+0x18; each vehicle owns four door/ramp frames
alignas(8) static unsigned char clump_object[2][16];
alignas(8) static unsigned char frames[2][4][0xA4];
static const char* node_names[4]={"door_front_dummy","ramp_front_dummy","door_back_dummy","ramp_back_dummy"};
static int frame_syncs=0;static bool hull_without_nodes=false,reference_fails_once=false,rename_frames_to_rebuilt=false;
alignas(8) static unsigned char clump_rebuilt[16];
alignas(8) static unsigned char frames_rebuilt[4][0xA4];
static unsigned char rest_pose[0x40];

namespace lcs {
bool sa_lcs_bridge_profile_matches(){return engine_ready;}
RouteVehicleLookup sa_route_vehicle_lookup(int32_t handle){
    RouteVehicleLookup result;
    for(auto& v:vehicles)if(v && v->handle==handle){result.status=VehicleLookupStatus::present;result.snapshot=v;}
    return result;
}
bool sa_register_vehicle_reference(int32_t handle,uintptr_t* cell){
    for(int i=0;i<2;++i)if(vehicles[i] && vehicles[i]->handle==handle){*cell=vehicles[i]->entity;registered_cells[i]=cell;return true;}
    return false;
}
bool sa_vehicle_reference_live(const RouteVehicleSnapshot& e,const uintptr_t* cell){
    if(reference_fails_once){reference_fails_once=false;return false;}
    for(int i=0;i<2;++i)if(vehicles[i] && vehicles[i]->entity==e.entity)return cell==registered_cells[i] && *cell==e.entity;
    return false;
}
bool sa_release_vehicle_reference(const RouteVehicleSnapshot&,uintptr_t* cell){
    if(release_blocked)return false;
    for(auto& c:registered_cells)if(c==cell)c=nullptr;
    *cell=0;return true;
}
uintptr_t ferry_test_model_info(int32_t id){return id==454?reinterpret_cast<uintptr_t>(model_info):0;}
uintptr_t ferry_test_vehicle_pool(){return reinterpret_cast<uintptr_t>(&fake_vehicle_pool);}
bool ferry_test_world_add(uintptr_t){++world_add_calls;if(world_add_failures){--world_add_failures;return false;}return true;}
bool ferry_test_world_remove(uintptr_t){++world_remove_calls;if(world_remove_failures){--world_remove_failures;return false;}return true;}
void ferry_test_col_construct(void* block){++col_constructs;std::memset(block,0,0x30);reinterpret_cast<unsigned char*>(block)[0x29]=4;}
bool ferry_test_col_load(const uint8_t* body,void* block){
    ++col_loads;
    if(col_load_fails)return false;
    float radius;std::memcpy(&radius,body,4);
    if(radius!=35.0f)return false;                                  // the loader is handed the COL1 body, after the 32-byte file header
    const uintptr_t data=reinterpret_cast<uintptr_t>(col_data_sentinel);
    std::memcpy(reinterpret_cast<unsigned char*>(block)+0x2C,&data,sizeof(data));
    return true;
}
void ferry_test_col_destroy(void* block){
    uintptr_t data;std::memcpy(&data,reinterpret_cast<unsigned char*>(block)+0x2C,sizeof(data));
    if(data==reinterpret_cast<uintptr_t>(col_data_sentinel))++col_destroys;
}
uintptr_t ferry_test_find_frame(uintptr_t clump,const char* name){
    if(hull_without_nodes)return 0;
    for(int v=0;v<2;++v)if(clump==reinterpret_cast<uintptr_t>(clump_object[v]))
        for(int n=0;n<4;++n)if(!std::strcmp(name,node_names[n]))return reinterpret_cast<uintptr_t>(rename_frames_to_rebuilt?frames_rebuilt[n]:frames[v][n]);
    if(clump==reinterpret_cast<uintptr_t>(clump_rebuilt))
        for(int n=0;n<4;++n)if(!std::strcmp(name,node_names[n]))return reinterpret_cast<uintptr_t>(frames_rebuilt[n]);
    return 0;
}
void ferry_test_sync_frames(uintptr_t){++frame_syncs;}
}

static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}

// The packaged docked collision (retail ms_colModelFerryDocked): COL1, bounding sphere 35, one box.
static std::vector<uint8_t> docked_file() {
    std::vector<uint8_t> file(120,0);
    std::memcpy(file.data(),"COLL",4);
    put<uint32_t>(file.data()+4,112);
    std::memcpy(file.data()+8,"ferry_docked",12);
    uint8_t* body=file.data()+32;
    const float bounds[10]={35.0f,0,0,0,-6.3f,-22.78f,-2.0f,6.3f,22.78f,2.8f};
    std::memcpy(body,bounds,sizeof(bounds));
    put<uint32_t>(body+48,1);                                       // spheres 0, lines 0, one box, then the box record
    const float box[6]={-6.3f,-22.78f,-2.0f,6.3f,22.78f,2.8f};
    std::memcpy(body+52,box,sizeof(box));
    body[76]=11;                                                    // surface material
    return file;                                                    // vertices and faces counts (body+80, +84) stay zero
}
static void reset_frames() {
    for(int v=0;v<2;++v) {
        std::memset(clump_object[v],0,sizeof(clump_object[v]));
        for(int n=0;n<4;++n) {
            std::memset(frames[v][n],0,sizeof(frames[v][n]));
            float* m=reinterpret_cast<float*>(frames[v][n]+0x10);
            m[0]=1;m[5]=1;m[10]=1;m[12]=0.5f+float(n);m[13]=17.0f;m[14]=4.0f;     // identity rotation, a distinct position per node
            put<uint32_t>(frames[v][n]+0x1C,0x00020003u);                          // identity + orthonormal
        }
    }
    frame_syncs=0;
}

int main() {try {
    check(!sa_ferry_ready().value_or(true) && !sa_ferry_offsets(),"Unconfigured producer is dormant");
    FerryPose pose;pose.position={10,20,0.2f};pose.forward={0,1,0};pose.right={1,0,0};pose.up={0,0,1};
    check(sa_ferry_pose(0,pose,{0,0,0}).status==FerryApply::Status::failed,"Dormant producer creates nothing");
    NativeExecutor execute=[&](const NativeCall& call){
        NativeResult r;r.native_entered=true;
        if(call.opcode==0x02CE){
            check(call.inputs.size()==3 && get<float>(reinterpret_cast<const unsigned char*>(&call.inputs[2]))==1.0f,"Source ferry ground ray starts at one metre, not above the map");
            r.executed=ground_readable;int32_t value;std::memcpy(&value,&sampled_ground,4);r.values={value};return r;
        }
        if(call.opcode==0x0248){r.executed=true;r.condition=model_resident;return r;}
        if(call.opcode==0x00A5){
            if(create_fails)return r;
            const int index=created++;
            std::memset(entity[index],0,sizeof(entity[index]));std::memset(matrix[index],0,sizeof(matrix[index]));
            const uintptr_t address=reinterpret_cast<uintptr_t>(entity[index]);
            const uintptr_t m=reinterpret_cast<uintptr_t>(matrix[index]);std::memcpy(entity[index]+0x14,&m,sizeof(m));
            const uintptr_t clump=reinterpret_cast<uintptr_t>(clump_object[index]);std::memcpy(entity[index]+0x18,&clump,sizeof(clump));
            entity[index][0x40]=0x5B;entity[index][0x48B]=0xFF;
            put<int16_t>(entity[index]+0x22,454);                       // CEntity::m_nModelIndex
            RouteVehicleSnapshot v;v.handle=index<<8;v.entity=address;v.generation=uint8_t(v.handle);v.is_boat=!not_a_boat;
            vehicles[index]=v;vehicle_tags[index]=v.generation;r.executed=true;r.values={v.handle};return r;
        }
        if(call.opcode==0x0247){++requested;r.executed=true;return r;}
        if(call.opcode==0x00A6){
            for(auto& v:vehicles)if(v && v->handle==call.inputs[0]){vehicle_tags[uint32_t(v->handle)>>8]=0x80;v.reset();++removed;}
            r.executed=true;return r;
        }
        return r;
    };
    reset_frames();
    std::memset(model_info,0,sizeof(model_info));
    put<uintptr_t>(model_info+0x14,reinterpret_cast<uintptr_t>(original_col));
    const auto good_docked=docked_file();
    auto truncated=good_docked;truncated.resize(100);
    auto with_faces=good_docked;put<uint32_t>(with_faces.data()+32+80,3);                     // claims vertices: not the retail docked shape
    check(!sa_ferry_configure(0,execute) && !sa_ferry_configure(454,{}) && sa_ferry_configure(454,execute,truncated) && !sa_ferry_configure(454,execute),"Configuration is validated and single-shot");
    check(sa_ferry_cleanup() && !sa_ferry_configured(),"A configured but unused producer cleans up");
    check(sa_ferry_configure(454,execute,with_faces) && !sa_ferry_set_collision(0,1).executed && col_constructs==0,"A docked file that claims vertices is not the retail shape and never reaches the engine loader");
    check(sa_ferry_cleanup(),"Cleanup again");
    check(sa_ferry_configure(454,execute,truncated),"Configured again with the truncated data");
    check(sa_ferry_offsets() && std::fabs(sa_ferry_offsets()->ped_point[0]-4.6897f)<1e-4f && std::fabs(sa_ferry_offsets()->car[3][1]+12.75f)<1e-4f && sa_ferry_ready()==std::optional<bool>(false),"Configured with the shipped hull points, but no ferry exists yet");
    for(float miss:{-20.0f,12.0f,-1000.0f}){sampled_ground=miss;check(sa_ferry_surface_z(10,20)==std::optional<float>(0.0f),"Sea bed, overhead bridge and native miss keep the source base height");}
    sampled_ground=0.75f;check(sa_ferry_surface_z(10,20)==std::optional<float>(0.75f),"Ground inside the original half-metre ray sets the track base");
    ground_readable=false;check(!sa_ferry_surface_z(10,20),"An unavailable ground query stays pending, not a fabricated surface");ground_readable=true;
    check(!sa_ferry_set_collision(0,1).executed && col_constructs==0,"A malformed docked collision file is never handed to the engine loader");
    model_resident=false;
    auto pause=sa_ferry_pose(0,pose,{0,0,0});
    check(pause.status==FerryApply::Status::pending && created==0 && requested==1,"An unloaded model is requested and reported pending, never a substitute vehicle");
    model_resident=true;
    not_a_boat=true;check(sa_ferry_pose(0,pose,{0,0,0}).status==FerryApply::Status::failed && removed==1 && sa_ferry_ready()==std::optional<bool>(false),"A non-boat creation is discarded instead of used as the ferry");not_a_boat=false;removed=0;created=0;
    engine_ready=false;check(sa_ferry_pose(0,pose,{0,0,0}).status==FerryApply::Status::failed && created==0,"Unknown engine profile creates nothing");engine_ready=true;
    create_fails=true;check(sa_ferry_pose(0,pose,{0,0,0}).status==FerryApply::Status::failed && created==0,"A failed creation is not success");create_fails=false;
    const std::array<float,3> speed{0,0.5f,0};
    world_add_calls=world_remove_calls=0;
    auto first=sa_ferry_pose(0,pose,speed);
    check(first.status==FerryApply::Status::applied && created==1 && world_remove_calls==1 && world_add_calls==1,"The first pose creates the vehicle and re-enters through world membership dispatch");
    check(entity[0][0x40]==0x79,"Physics is frozen: gravity off, infinite mass, no move/turn force; unrelated bits kept");
    check(get<float>(matrix[0]+0x30)==10 && get<float>(matrix[0]+0x34)==20 && get<float>(matrix[0]+0x14)==1 && get<float>(entity[0]+0x48)==0.5f,"Pose and speed are written to the exact vehicle");
    check(sa_ferry_pose(0,pose,speed).status==FerryApply::Status::applied && created==1 && entity[0][0x40]==0x79,"Later frames rewrite the same vehicle without touching its frozen flags again");
    check(sa_ferry_ready()==std::optional<bool>(false),"The pair is ready only when both ferries exist");
    check(sa_ferry_pose(1,pose,speed).status==FerryApply::Status::applied && sa_ferry_ready()==std::optional<bool>(true),"Both ferries exist");
    const auto ready_matrix=get<uintptr_t>(entity[0]+0x14);
    put<uintptr_t>(entity[0]+0x14,0);
    check(sa_ferry_pose(0,pose,speed).status==FerryApply::Status::failed && sa_ferry_ready()==std::optional<bool>(false),
          "A later pose failure withdraws readiness even though both registered boats still exist");
    put<uintptr_t>(entity[0]+0x14,ready_matrix);
    check(sa_ferry_pose(0,pose,speed).status==FerryApply::Status::applied && sa_ferry_ready()==std::optional<bool>(true) && created==2,
          "A repaired pose restores readiness without recreating either boat");
    check(sa_ferry_pose(2,pose,speed).status==FerryApply::Status::failed && sa_ferry_pose(-1,pose,speed).status==FerryApply::Status::failed,"Only ferry ids 0 and 1 exist");
    FerryPose bad=pose;bad.position[0]=NAN;check(sa_ferry_pose(0,bad,speed).status==FerryApply::Status::failed,"Non-finite pose is refused");

    // ---- readiness is every stage, not existence; a failed stage is retried alone -----------------------------------
    check(sa_ferry_cleanup() && !sa_ferry_configured(),"Reset before the readiness scenarios");
    created=0;removed=0;reset_frames();
    check(sa_ferry_configure(454,execute,good_docked),"Reconfigured for the readiness scenarios");
    reference_fails_once=true;                                                // the physics freeze cannot confirm the weak reference once
    auto frozen_fail=sa_ferry_pose(0,pose,speed);
    check(frozen_fail.status==FerryApply::Status::failed && created==1 && entity[0][0x40]==0x5B,"A failed physics freeze after creation is a failure and writes nothing");
    check(sa_ferry_ready()==std::optional<bool>(false),"An existing but unfrozen ferry is not ready");
    auto retried=sa_ferry_pose(0,pose,speed);
    check(retried.status==FerryApply::Status::applied && created==1 && removed==0 && entity[0][0x40]==0x79,"The retry completes the freeze and the pose without creating the vehicle again");
    const uintptr_t saved_matrix=get<uintptr_t>(entity[1]+0x14);
    check(sa_ferry_pose(1,pose,speed).status==FerryApply::Status::applied && sa_ferry_ready()==std::optional<bool>(true),"Both ferries ready only after pose");
    check(sa_ferry_cleanup(),"Cleanup of the readiness scenario");
    created=0;removed=0;reset_frames();
    check(sa_ferry_configure(454,execute,good_docked),"Reconfigured again");
    check(sa_ferry_pose(0,pose,speed).status==FerryApply::Status::applied,"Ferry 0 posed");
    put<uintptr_t>(entity[0]+0x14,0);                                         // the matrix cannot be written
    check(sa_ferry_pose(0,pose,speed).status==FerryApply::Status::failed,"A pose that cannot be written is a failure");
    put<uintptr_t>(entity[0]+0x14,reinterpret_cast<uintptr_t>(matrix[0]));
    (void)saved_matrix;
    {   // a recycled entity address whose model is not the ferry is never trusted
        put<int16_t>(entity[0]+0x22,400);
        check(sa_ferry_pose(0,pose,speed).status==FerryApply::Status::failed && sa_ferry_ready()==std::optional<bool>(false),"An entity that is no longer the ferry model is not owned");
        put<int16_t>(entity[0]+0x22,454);
    }
    check(sa_ferry_cleanup(),"Cleanup of the second readiness scenario");
    created=0;removed=0;reset_frames();
    check(sa_ferry_configure(454,execute,truncated) && sa_ferry_pose(0,pose,speed).status==FerryApply::Status::applied && sa_ferry_pose(1,pose,speed).status==FerryApply::Status::applied,
          "Back to the state the collision scenarios expect");

    // ---- docked collision: per-model swap, ownership and restoration ---------------------------------------------
    check(!sa_ferry_set_collision(0,1).executed && col_constructs==0,"Without packaged docked data the switch is refused, not faked");
    check(sa_ferry_cleanup() && removed==2 && !sa_ferry_configured(),"Cleanup of the first scenario");
    created=0;removed=0;reset_frames();
    check(sa_ferry_configure(454,execute,good_docked),"Reconfigured with the valid docked collision");
    check(sa_ferry_pose(0,pose,speed).status==FerryApply::Status::applied && sa_ferry_pose(1,pose,speed).status==FerryApply::Status::applied,"Both ferries exist again");
    check(!sa_ferry_set_collision(0,2).executed && !sa_ferry_set_collision(5,1).executed,"Invalid collision requests are refused");
    check(sa_ferry_set_collision(0,0).executed && col_loads==0,"Restoring the default when nothing was installed is a no-op");
    put<int16_t>(entity[2]+0x22,454);vehicle_tags[2]=2;
    const int remove_calls_before_foreign=world_remove_calls;
    check(!sa_ferry_set_collision(0,1).executed && col_constructs==0 && get<uintptr_t>(model_info+0x14)==reinterpret_cast<uintptr_t>(original_col) && world_remove_calls==remove_calls_before_foreign,
          "A live foreign instance in the fake vehicle pool blocks the shared COL swap before any sector mutation");
    put<int16_t>(entity[2]+0x22,0);vehicle_tags[2]=0x80;
    check(sa_ferry_set_collision(0,1).executed && col_constructs==1 && col_loads==1 && sa_ferry_collision_installed(),"The docked collision is built once through the engine loader and installed");
    check(get<uintptr_t>(model_info+0x14)!=reinterpret_cast<uintptr_t>(original_col) && get<uintptr_t>(model_info+0x14)!=0,"The ferry model info now points at the docked CColModel");
    check(sa_ferry_set_collision(1,1).executed && col_loads==1,"The second ferry sees the already installed per-model swap");
    check(sa_ferry_set_collision(0,0).executed && !sa_ferry_collision_installed() && get<uintptr_t>(model_info+0x14)==reinterpret_cast<uintptr_t>(original_col),"Type 0 puts the original COLFILE collision back untouched");
    check(col_destroys==0,"Switching back keeps the built docked model for the next switch");
    check(sa_ferry_set_collision(0,1).executed && col_loads==1,"Docking again reuses the built collision model");
    entity[1][0x48B]=0;
    check(sa_ferry_set_collision(0,0).executed,"Restore for the special-collision scenario");
    check(!sa_ferry_set_collision(0,1).executed,"A ferry with its own special collision model is refused (the per-model swap would not reach it)");
    entity[1][0x48B]=0xFF;
    col_load_fails=true;
    check(sa_ferry_cleanup() && col_destroys==1,"Cleanup destroys the built docked model once");
    col_load_fails=false;
    created=0;removed=0;col_constructs=0;col_loads=0;col_destroys=0;reset_frames();
    check(sa_ferry_configure(454,execute,good_docked) && sa_ferry_pose(0,pose,speed).status==FerryApply::Status::applied && sa_ferry_pose(1,pose,speed).status==FerryApply::Status::applied,
          "The failed-add recovery scenario mounts two owned ferries");
    world_add_failures=1;
    check(!sa_ferry_set_collision(0,1).executed && sa_ferry_collision_installed() && get<uintptr_t>(model_info+0x14)!=reinterpret_cast<uintptr_t>(original_col),
          "A failed virtual Add leaves the COL swap tracked for recovery");
    const int removed_before_failed_cleanup=removed;
    world_add_failures=2;
    check(!sa_ferry_cleanup() && removed==removed_before_failed_cleanup && get<uintptr_t>(model_info+0x14)==reinterpret_cast<uintptr_t>(original_col) && sa_ferry_configured(),
          "Cleanup restores the original COL but retains ferries while world re-entry is still failing");
    world_add_failures=0;
    check(sa_ferry_cleanup() && removed==removed_before_failed_cleanup+2 && !sa_ferry_configured(),
          "A later cleanup re-adds detached ferries before deleting them");
    created=0;removed=0;col_constructs=0;col_loads=0;col_destroys=0;reset_frames();
    col_load_fails=true;
    check(sa_ferry_configure(454,execute,good_docked) && sa_ferry_pose(0,pose,speed).status==FerryApply::Status::applied && sa_ferry_pose(1,pose,speed).status==FerryApply::Status::applied,"Third scenario mounted");
    check(!sa_ferry_set_collision(0,1).executed && !sa_ferry_collision_installed() && get<uintptr_t>(model_info+0x14)==reinterpret_cast<uintptr_t>(original_col),"A loader failure installs nothing");
    col_load_fails=false;
    check(sa_ferry_set_collision(0,1).executed && sa_ferry_collision_installed(),"Docked collision installed for the third-party scenario");
    const uintptr_t third_party=0x12345670;put<uintptr_t>(model_info+0x14,third_party);
    check(sa_ferry_set_collision(0,0).executed && get<uintptr_t>(model_info+0x14)==third_party,"A pointer someone else installed meanwhile is not overwritten on restore");
    put<uintptr_t>(model_info+0x14,reinterpret_cast<uintptr_t>(original_col));

    // ---- door / ramp nodes -----------------------------------------------------------------------------------------
    std::memcpy(rest_pose,frames[0][0]+0x10,0x40);
    const float rest_door_pos[3]={get<float>(frames[0][0]+0x40),get<float>(frames[0][0]+0x44),get<float>(frames[0][0]+0x48)};
    auto half=sa_ferry_set_door(0,true,0.5f);
    check(half.executed && frame_syncs==1,"The front door and ramp nodes are rotated and the frame hierarchy is synchronised");
    {
        const float* door=reinterpret_cast<const float*>(frames[0][0]+0x10);
        const float angle=90.0f*0.017453292519943295f*0.5f;
        check(std::fabs(door[5]-std::cos(angle))<1e-5f && std::fabs(door[6]-std::sin(angle))<1e-5f && std::fabs(door[9]+std::sin(angle))<1e-5f && std::fabs(door[10]-std::cos(angle))<1e-5f,"The door rotates about X by half its open angle");
        check(get<float>(frames[0][0]+0x40)==rest_door_pos[0] && get<float>(frames[0][0]+0x44)==rest_door_pos[1] && get<float>(frames[0][0]+0x48)==rest_door_pos[2],"The node position is preserved");
        check((get<uint32_t>(frames[0][0]+0x1C)&0x20000u)==0 && (get<uint32_t>(frames[0][0]+0x1C)&3u)==3u,"The matrix is flagged as updated and orthonormal");
        const float* ramp=reinterpret_cast<const float*>(frames[0][1]+0x10);
        const float ramp_angle=-95.0f*0.017453292519943295f*0.5f;
        check(std::fabs(ramp[6]-std::sin(ramp_angle))<1e-5f,"The ramp rotates by its own (opposite) open angle");
        check(get<float>(frames[0][2]+0x24)==1.0f && get<float>(frames[0][2]+0x28)==0.0f,"The back nodes are untouched by a front request");
    }
    check(sa_ferry_set_door(0,false,1.0f).executed && std::fabs(get<float>(frames[0][2]+0x24)-std::cos(-90.0f*0.017453292519943295f))<1e-5f,"The back door uses the back nodes and its own open angle");
    check(sa_ferry_set_door(0,true,0.0f).executed && std::memcmp(rest_pose,frames[0][0]+0x10,0x40)==0,"A closed door returns to the exact shipped rest pose");
    check(sa_ferry_set_door(0,true,7.0f).executed && std::fabs(get<float>(frames[0][0]+0x24)-std::cos(90.0f*0.017453292519943295f))<1e-5f,"Ratios are clamped to the open angle");
    check(!sa_ferry_set_door(0,true,NAN).executed && !sa_ferry_set_door(5,true,0.5f).executed,"Invalid door requests are refused");
    hull_without_nodes=true;check(!sa_ferry_set_door(1,true,0.5f).executed,"A hull without the named nodes is refused, never faked");hull_without_nodes=false;
    check(sa_ferry_set_door(1,true,1.0f).executed,"The second ferry has its own nodes");
    vehicles[1]->entity+=8;
    check(!sa_ferry_set_door(1,true,0.5f).executed,"A replaced vehicle identity is never written");
    vehicles[1]->entity-=8;

    check(!sa_ferry_set_collision(0,2).executed,"Still validated");
    vehicles[0]->entity+=8;
    check(sa_ferry_pose(0,pose,speed).status==FerryApply::Status::failed,"A replaced vehicle identity is never written");
    vehicles[0]->entity-=8;
    release_blocked=true;check(!sa_ferry_cleanup() && sa_ferry_ready()==std::optional<bool>(true),"Blocked reference release keeps the ferry and reports failure");
    release_blocked=false;
    check(sa_ferry_set_collision(0,1).executed,"Docked collision before the final cleanup");
    const int removed_before=removed;
    check(sa_ferry_cleanup() && removed-removed_before==2 && entity[0][0x40]==0x5B && entity[1][0x40]==0x5B && !sa_ferry_offsets(),"Cleanup restores physics, removes both ferries and returns to dormant");
    check(get<uintptr_t>(model_info+0x14)==reinterpret_cast<uintptr_t>(original_col) && !sa_ferry_collision_installed(),"Cleanup also restores the original collision");

    // ---- identity revalidation before restoring physics (a deleted or recycled entity is someone else's) ----------
    for(int scenario=0;scenario<2;++scenario) {
        created=0;removed=0;reset_frames();vehicles[0].reset();vehicles[1].reset();
        check(sa_ferry_configure(454,execute,good_docked) && sa_ferry_pose(0,pose,speed).status==FerryApply::Status::applied && sa_ferry_pose(1,pose,speed).status==FerryApply::Status::applied,"Scenario mounted");
        if(scenario==0) {
            vehicles[0].reset();                                    // the engine deleted the ferry; the memory now belongs to anything
            entity[0][0x40]=0xAA;
        } else {
            RouteVehicleSnapshot foreign=*vehicles[0];foreign.handle=0x777;foreign.generation=0x77;   // same address recycled by another vehicle
            vehicles[0]=foreign;entity[0][0x40]=0xAA;
        }
        check(sa_ferry_cleanup(),"Cleanup completes even though one ferry no longer exists");
        check(entity[0][0x40]==0xAA,scenario==0?"A deleted ferry's memory is never written during cleanup":"A recycled entity address is never written during cleanup");
        check(entity[1][0x40]==0x5B,"The surviving ferry is restored");
    }

    // ---- node restoration re-resolves vehicle, clump and named frame before writing ------------------------------
    for(int scenario=0;scenario<2;++scenario) {
        created=0;removed=0;reset_frames();vehicles[0].reset();vehicles[1].reset();
        check(sa_ferry_configure(454,execute,good_docked) && sa_ferry_pose(0,pose,speed).status==FerryApply::Status::applied && sa_ferry_pose(1,pose,speed).status==FerryApply::Status::applied,"Node scenario mounted");
        check(sa_ferry_set_door(0,true,0.5f).executed && sa_ferry_set_door(0,false,0.5f).executed,"Both ends of ferry 0 are open");
        std::memset(clump_rebuilt,0,sizeof(clump_rebuilt));
        for(int n=0;n<4;++n){std::memset(frames_rebuilt[n],0,sizeof(frames_rebuilt[n]));put<float>(frames_rebuilt[n]+0x20,0.25f);}
        for(int n=0;n<4;++n)std::memset(frames[0][n]+0x10,0xEE,0x40);              // the old frame memory now belongs to somebody else
        if(scenario==0)put<uintptr_t>(entity[0]+0x18,reinterpret_cast<uintptr_t>(clump_rebuilt));    // the hull's clump was rebuilt
        else rename_frames_to_rebuilt=true;                                          // same clump, but the names resolve to other frames
        check(sa_ferry_cleanup(),"Cleanup completes");
        for(int n=0;n<4;++n)check(frames[0][n][0x10]==0xEE && get<float>(frames_rebuilt[n]+0x20)==0.25f,
              scenario==0?"A rebuilt clump's old frames are never written back":"A frame that no longer carries the saved name is never written back");
        rename_frames_to_rebuilt=false;
    }
    std::cout<<"ferry physical producer contracts passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
