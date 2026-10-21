#include <windows.h>
#include "sa_ferry.hpp"
#include "sa_lcs_bridge.hpp"
#include <cmath>
#include <cstring>
#include <string>

#ifdef LCS_FERRY_TEST_BACKEND
namespace lcs {
// Test doubles for the engine entry points that cannot be called outside the game (see tests/ferry_native_tests.cpp).
uintptr_t ferry_test_model_info(int32_t id);
uintptr_t ferry_test_vehicle_pool();
bool ferry_test_world_add(uintptr_t entity);
bool ferry_test_world_remove(uintptr_t entity);
void ferry_test_col_construct(void* block);
bool ferry_test_col_load(const uint8_t* body,void* block);
void ferry_test_col_destroy(void* block);
uintptr_t ferry_test_find_frame(uintptr_t clump,const char* name);
void ferry_test_sync_frames(uintptr_t vehicle);
}
#endif

namespace lcs {
namespace {
constexpr int ferries=2;
// SA 1.0 US image, every address below is pinned by tests/ferry_abi_pin_tests.py:
constexpr size_t model_info_table=0xA9B0C8;      // CModelInfo::ms_modelInfoPtrs
constexpr size_t model_info_col=0x14;            // CBaseModelInfo::m_pColModel (CBaseModelInfo::SetColModel 0x4C4BC0)
constexpr uintptr_t vehicle_pool_global=0xB74494;
constexpr size_t vehicle_pool_stride=0xA18;
constexpr size_t entity_clump=0x18;              // CEntity::m_pRwClump
constexpr size_t vehicle_special_col=0x48B;      // CVehicle::m_nSpecialColModel (0xFF = use the model collision)
constexpr size_t physical_flags=0x40;            // CPhysical flag byte 0
constexpr uint8_t flag_gravity=0x02,flag_turn=0x10,flag_move=0x20,flag_infinite_mass=0x40;
constexpr uint8_t flag_mask=flag_gravity|flag_turn|flag_move|flag_infinite_mass;
constexpr uint8_t flag_frozen=flag_turn|flag_move|flag_infinite_mass;    // gravity is cleared
constexpr size_t colmodel_size=0x30,colmodel_data=0x2C;
constexpr uintptr_t col_ctor=0x40FB60,col_dtor=0x40F700,col_load_v1=0x537580,frame_from_name=0x4C5400,update_rw_frame=0x532B00;
constexpr size_t frame_matrix=0x10;              // RwFrame::modelling
constexpr float deg=0.017453292519943295f;
// Hull node names and retail CFerryDoor open angles (Ferry.cpp: door 0/1 front, 2/3 back).
struct NodeSpec {const char* name;float open_angle;};
constexpr NodeSpec front_nodes[2]={{"door_front_dummy",90.0f*deg},{"ramp_front_dummy",-95.0f*deg}};
constexpr NodeSpec back_nodes[2]={{"door_back_dummy",-90.0f*deg},{"ramp_back_dummy",95.0f*deg}};
// Model-space points of the shipped hull (tropic.dff frames, accumulated from the clump root): Ped_point and the
// car1..car4 dummies. Provenance: logs/tooling/dff_frames.py over vehicles/tropic.dff.
const FerryOffsets hull_offsets{
    {4.6897f,-6.93262f,3.68408f},
    {{{2.58081f,12.7373f,2.75244f},{2.58081f,4.39453f,2.75244f},{2.58081f,-4.40723f,2.75244f},{2.58081f,-12.75f,2.75244f}}}};

struct SavedNode {
    uintptr_t frame=0,clump=0;    // the exact frame and the clump it was found in
    uint32_t matrix[16]{};        // RwMatrix as found (64 bytes)
    bool saved=false;
};
// One physical ferry. The stages complete strictly in order and each is confirmed before the next; a failed stage is
// retried on its own (nothing already done is repeated), and only a slot with every stage done is ready.
struct Slot {
    RouteVehicleSnapshot identity;
    uintptr_t cell=0;
    uint8_t physics_saved=0;
    bool created=false,registered=false,frozen=false,posed=false,flags_saved=false,world_detached=false;
    SavedNode nodes[4];            // front door, front ramp, back door, back ramp
};
const char* const node_names[4]={front_nodes[0].name,front_nodes[1].name,back_nodes[0].name,back_nodes[1].name};
Slot slots[ferries];
int32_t model=-1;
NativeExecutor execute;
bool configured=false;
std::vector<uint8_t> docked_file;

struct Docked {
    alignas(16) uint8_t block[colmodel_size]{};
    bool built=false,installed=false;
    uintptr_t model_info=0,original=0;
} docked;

uintptr_t model_info_of(int32_t id) {
#ifdef LCS_FERRY_TEST_BACKEND
    return ferry_test_model_info(id);
#else
    if(id<=0 || id>=20000)return 0;
    return *reinterpret_cast<const uintptr_t*>(model_info_table+size_t(id)*4);
#endif
}

// SEH leaves: no C++ unwind object lives in these functions.
bool suspend_physics(Slot& slot) {
    __try {
        if(!slot.cell || !sa_vehicle_reference_live(slot.identity,&slot.cell))return false;
        auto* physics=reinterpret_cast<uint8_t*>(slot.cell+physical_flags);
        if(!slot.flags_saved){slot.physics_saved=*physics&flag_mask;slot.flags_saved=true;}
        // Kinematic: no gravity, and no move/turn force or collision response can displace the hull between two poses
        // (the buoyancy and collision code of CBoat apply forces through the same CPhysical entry points).
        *physics=uint8_t((*physics&~flag_mask)|flag_frozen);
        return (*physics&flag_mask)==flag_frozen;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool write_flags_back(uintptr_t entity,uint8_t saved) {
    __try {
        auto* physics=reinterpret_cast<uint8_t*>(entity+physical_flags);
        *physics=uint8_t((*physics&~flag_mask)|saved);
        return (*physics&flag_mask)==saved;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool same(const RouteVehicleSnapshot& a,const RouteVehicleSnapshot& b){return a.handle==b.handle && a.entity==b.entity && a.generation==b.generation;}
bool entity_is_model(uintptr_t entity,int32_t expected) {
    __try {return int32_t(*reinterpret_cast<const int16_t*>(entity+0x22))==expected;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool slot_live(const Slot& slot) {
    if(!slot.created)return false;
    const auto found=sa_route_vehicle_lookup(slot.identity.handle);
    return found.status==VehicleLookupStatus::present && found.snapshot && same(*found.snapshot,slot.identity);
}
// The identity of the vehicle this module created is revalidated before any write: registry lookup (handle, entity,
// generation), the weak reference, and that the entity is still the configured ferry model.
bool slot_owned(Slot& slot) {
    return slot_live(slot) && slot.registered && slot.cell && sa_vehicle_reference_live(slot.identity,&slot.cell) &&
           entity_is_model(slot.identity.entity,model);
}
uintptr_t clump_of(uintptr_t vehicle) {
    __try {return *reinterpret_cast<const uintptr_t*>(vehicle+entity_clump);} __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
}
// Restores only the bits this module changed, and only on the vehicle it changed: a deleted or recycled entity (the
// weak reference no longer matches the identity) belongs to someone else and is never written.
bool restore_physics(Slot& slot) {
    if(!slot.flags_saved)return true;
    if(!slot_owned(slot)){slot.flags_saved=false;return true;}
    return write_flags_back(slot.identity.entity,slot.physics_saved);
}
bool model_registered(int32_t id) {
    __try {
        return model_info_of(id)!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool world_remove(uintptr_t entity) {
#ifdef LCS_FERRY_TEST_BACKEND
    return ferry_test_world_remove(entity);
#else
    return sa_world_entity_remove(entity);
#endif
}
bool world_add(uintptr_t entity) {
#ifdef LCS_FERRY_TEST_BACKEND
    return ferry_test_world_add(entity);
#else
    return sa_world_entity_add(entity);
#endif
}
// The same bridge-pinned sequence the crane uses for a carried vehicle: leave the world sectors (CEntity::Remove
// through the CVehicle vtable), rewrite the basis, republish SetPosn 0x420B80 / UpdateRwMatrix 0x446F90 /
// UpdateRwFrame 0x532B00 and re-enter through virtual Add(). CVehicle overrides both membership calls to maintain its
// physical repeat-sector links. Move speed is the track speed so a rider on the deck inherits it from its contact
// entity; turn speed stays zero.
bool write_pose(Slot& slot,const FerryPose& pose,const std::array<float,3>& speed) {
    __try {
        const auto vehicle=slot.identity.entity;
        const auto matrix=*reinterpret_cast<const uintptr_t*>(vehicle+0x14);
        if(!matrix)return false;
        if(!slot.world_detached) {
            if(!world_remove(vehicle))return false;
            slot.world_detached=true;
        }
        for(int i=0;i<3;++i) {
            *reinterpret_cast<float*>(matrix+i*4)=pose.right[size_t(i)];
            *reinterpret_cast<float*>(matrix+0x10+i*4)=pose.forward[size_t(i)];
            *reinterpret_cast<float*>(matrix+0x20+i*4)=pose.up[size_t(i)];
#ifdef LCS_FERRY_TEST_BACKEND
            *reinterpret_cast<float*>(matrix+0x30+i*4)=pose.position[size_t(i)];
#endif
        }
#ifndef LCS_FERRY_TEST_BACKEND
        reinterpret_cast<void(__thiscall*)(uintptr_t,float,float,float)>(0x420B80)(vehicle,pose.position[0],pose.position[1],pose.position[2]);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x446F90)(vehicle);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(update_rw_frame)(vehicle);
#endif
        if(!world_add(vehicle))return false;
        slot.world_detached=false;
        for(int i=0;i<3;++i) {
            *reinterpret_cast<float*>(vehicle+0x44+i*4)=speed[size_t(i)];   // move speed
            *reinterpret_cast<float*>(vehicle+0x50+i*4)=0.0f;               // turn speed
        }
        const auto updated=*reinterpret_cast<const uintptr_t*>(vehicle+0x14);
        if(!updated)return false;
        for(int i=0;i<3;++i) {
            if(std::fabs(*reinterpret_cast<const float*>(updated+0x30+i*4)-pose.position[size_t(i)])>0.001f)return false;
            if(std::fabs(*reinterpret_cast<const float*>(updated+0x10+i*4)-pose.forward[size_t(i)])>0.001f)return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

// ---- docked collision (per model, as retail SetColModel on the ferry model info) -----------------------------
// Ownership: the 0x30-byte CColModel block lives in this module; the engine allocates the CCollisionData through its
// own loader (CFileLoader::LoadCollisionModel 0x537580, COL1) and releases it through ~CColModel (0x40F700, whose
// non-single-allocation path calls RemoveCollisionVolumes and operator delete). The model info keeps pointing at the
// original COLFILE collision whenever the docked one is not installed; the original is never modified or freed here.
bool col_construct_load(const uint8_t* body) {
    __try {
        std::memset(docked.block,0,sizeof(docked.block));
#ifdef LCS_FERRY_TEST_BACKEND
        ferry_test_col_construct(docked.block);
        return ferry_test_col_load(body,docked.block);
#else
        reinterpret_cast<void*(__thiscall*)(void*)>(col_ctor)(docked.block);
        reinterpret_cast<void(__cdecl*)(const uint8_t*,void*)>(col_load_v1)(body,docked.block);
        return *reinterpret_cast<const uintptr_t*>(docked.block+colmodel_data)!=0;
#endif
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool col_destroy() {
    __try {
#ifdef LCS_FERRY_TEST_BACKEND
        ferry_test_col_destroy(docked.block);
#else
        reinterpret_cast<void(__thiscall*)(void*)>(col_dtor)(docked.block);
#endif
        std::memset(docked.block,0,sizeof(docked.block));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// Raw read/write of CBaseModelInfo::m_pColModel.
bool read_model_col(uintptr_t info,uintptr_t& value) {
    __try {value=*reinterpret_cast<const uintptr_t*>(info+model_info_col);return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool write_model_col(uintptr_t info,uintptr_t value) {
    __try {*reinterpret_cast<uintptr_t*>(info+model_info_col)=value;return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool compare_write_model_col(uintptr_t info,uintptr_t expected,uintptr_t value) {
    uintptr_t current=0;
    return read_model_col(info,current) && current==expected && write_model_col(info,value);
}
bool add_detached_ferries() {
    bool ok=true;
    for(auto& slot:slots) {
        if(!slot.world_detached)continue;
        if(!slot_owned(slot)){slot.world_detached=false;continue;}
        if(world_add(slot.identity.entity))slot.world_detached=false;else ok=false;
    }
    return ok;
}
bool swap_model_col_reindexed(uintptr_t info,uintptr_t expected,uintptr_t replacement) {
    for(auto& slot:slots) {
        if(!slot_owned(slot) || slot.world_detached)continue;
        if(!world_remove(slot.identity.entity)) { (void)add_detached_ferries();return false; }
        slot.world_detached=true;
    }
    if(!compare_write_model_col(info,expected,replacement)) { (void)add_detached_ferries();return false; }
    return add_detached_ferries();
}
// A COL1 file with exactly the shape of the retail docked model: header 'COLL', size == file-8, no spheres, lines,
// vertices or faces, 1..4 boxes. The loader is only ever given a body this function accepted.
bool docked_file_valid(const std::vector<uint8_t>& file) {
    // COL1 body (after the 32-byte file header): bounds (radius, centre, min, max = 40 bytes), then the counted blocks
    // spheres, lines, boxes (28 bytes each: min, max, surface), vertices, faces.
    if(file.size()<32+40+12+28+8 || std::memcmp(file.data(),"COLL",4))return false;
    uint32_t size=0;std::memcpy(&size,file.data()+4,4);
    if(size!=file.size()-8)return false;
    const uint8_t* body=file.data()+32;
    uint32_t spheres=0,lines=0,boxes=0,vertices=0,faces=0;
    std::memcpy(&spheres,body+40,4);std::memcpy(&lines,body+44,4);std::memcpy(&boxes,body+48,4);
    if(spheres||lines||boxes<1||boxes>4)return false;
    const size_t tail=32+40+12+size_t(boxes)*28;
    if(file.size()!=tail+8)return false;
    std::memcpy(&vertices,file.data()+tail,4);std::memcpy(&faces,file.data()+tail+4,4);
    if(vertices||faces)return false;
    float bounds[10];std::memcpy(bounds,body,sizeof(bounds));
    for(const float v:bounds)if(!std::isfinite(v))return false;
    float boxes_data[7*4];std::memcpy(boxes_data,body+52,size_t(boxes)*28);
    for(size_t i=0;i<size_t(boxes)*7;++i)if(i%7<6 && !std::isfinite(boxes_data[i]))return false;
    return true;
}
bool special_collision_in_use(const Slot& slot) {
    __try {
        return *reinterpret_cast<const uint8_t*>(slot.identity.entity+vehicle_special_col)!=0xFF;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return true;}
}
bool ferry_special_collision_free() {
    for(const auto& slot:slots)if(slot_live(slot) && special_collision_in_use(slot))return false;
    return true;
}
bool model_instances_owned_in_pool(uintptr_t pool) {
    // The model collision pointer is shared. Reindexing an unrelated pool vehicle would invent world membership if
    // it had been intentionally removed, so only permit the swap when every live instance is one of our exact ferries.
    __try {
        if(!pool)return false;
        const auto data=*reinterpret_cast<const uintptr_t*>(pool);
        const auto tags=*reinterpret_cast<const uintptr_t*>(pool+sizeof(uintptr_t));
        const auto count=*reinterpret_cast<const int32_t*>(pool+2*sizeof(uintptr_t));
        if(!data || !tags || count<=0 || count>4096)return false;
        for(int32_t i=0;i<count;++i) {
            const auto tag=*reinterpret_cast<const uint8_t*>(tags+size_t(i));
            if(tag&0x80u)continue;
            const auto entity=data+size_t(i)*vehicle_pool_stride;
            if(!entity_is_model(entity,model))continue;
            bool owned=false;
            for(auto& slot:slots)if(slot.identity.entity==entity && slot_live(slot) && slot_owned(slot)) {owned=true;break;}
            if(!owned)return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool model_instances_owned() {
#ifdef LCS_FERRY_TEST_BACKEND
    return model_instances_owned_in_pool(ferry_test_vehicle_pool());
#else
    __try {
        const auto pool=*reinterpret_cast<const uintptr_t*>(vehicle_pool_global);
        return model_instances_owned_in_pool(pool);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#endif
}
bool restore_collision() {
    if(!docked.installed)return add_detached_ferries();
    uintptr_t current=0;
    if(!model_registered(model) || model_info_of(model)!=docked.model_info || !read_model_col(docked.model_info,current))return false;
    if(current==reinterpret_cast<uintptr_t>(docked.block)) {
        if(!swap_model_col_reindexed(docked.model_info,current,docked.original))return false;
    } else {
        // A foreign model-info update owns its new pointer. Repair any of our entities left out of the sectors by a
        // partial swap, using the collision currently installed, then detach our block without overwriting that update.
        if(!add_detached_ferries())return false;
    }
    docked.installed=false;docked.model_info=0;docked.original=0;
    return true;
}

// ---- door / ramp frame nodes ------------------------------------------------------------------------------------
uintptr_t find_frame(uintptr_t vehicle,const char* name) {
    __try {
        const auto clump=*reinterpret_cast<const uintptr_t*>(vehicle+entity_clump);
        if(!clump)return 0;
#ifdef LCS_FERRY_TEST_BACKEND
        return ferry_test_find_frame(clump,name);
#else
        return reinterpret_cast<uintptr_t(__cdecl*)(uintptr_t,const char*)>(frame_from_name)(clump,name);
#endif
    } __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
}
bool write_node(SavedNode& node,uintptr_t frame,uintptr_t clump,float angle,bool closed) {
    __try {
        auto* matrix=reinterpret_cast<uint32_t*>(frame+frame_matrix);
        // A different frame or clump than the one first modified (hull rebuilt) starts a fresh rest-pose record.
        if(!node.saved || node.frame!=frame || node.clump!=clump){std::memcpy(node.matrix,matrix,sizeof(node.matrix));node.frame=frame;node.clump=clump;node.saved=true;}
        if(closed){std::memcpy(matrix,node.matrix,sizeof(node.matrix));return true;}      // back to the shipped rest pose
        auto* f=reinterpret_cast<float*>(matrix);
        const float c=std::cos(angle),s=std::sin(angle);
        // RwMatrix: right 0x00, flags 0x0C, up 0x10, at 0x20, pos 0x30. Rotation about X keeps the node position.
        f[0]=1.0f;f[1]=0.0f;f[2]=0.0f;
        f[4]=0.0f;f[5]=c;f[6]=s;
        f[8]=0.0f;f[9]=-s;f[10]=c;
        matrix[3]=(node.matrix[3]&~0x00020000u)|0x3u;       // rwMATRIXTYPEORTHONORMAL, no longer identity
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool put_node_back(const SavedNode& node) {
    __try {
        std::memcpy(reinterpret_cast<void*>(node.frame+frame_matrix),node.matrix,sizeof(node.matrix));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool sync_frames(uintptr_t vehicle) {
    __try {
#ifdef LCS_FERRY_TEST_BACKEND
        ferry_test_sync_frames(vehicle);
#else
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(update_rw_frame)(vehicle);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool restore_nodes(Slot& slot) {
    if(!slot_owned(slot)){          // the hull is gone with its frames: nothing of ours remains to restore
        for(auto& node:slot.nodes)node=SavedNode{};
        return true;
    }
    const auto clump=clump_of(slot.identity.entity);
    bool ok=true,any=false;
    for(size_t i=0;i<4;++i) {
        auto& node=slot.nodes[i];
        if(!node.saved)continue;
        // The vehicle, its clump and the named frame are re-resolved now: a rebuilt clump or a different frame is not the
        // object that was modified and is never written (SEH only contains faults, it does not prove identity).
        if(!clump || clump!=node.clump || find_frame(slot.identity.entity,node_names[i])!=node.frame){node=SavedNode{};continue;}
        if(put_node_back(node)){node=SavedNode{};any=true;}else ok=false;
    }
    if(ok && any)(void)sync_frames(slot.identity.entity);
    return ok;
}
}

bool sa_ferry_configure(int32_t sa_model,NativeExecutor executor,std::vector<uint8_t> docked_col) {
    if(sa_model<=0 || sa_model>=20000 || !executor || configured)return false;
    model=sa_model;execute=std::move(executor);configured=true;
    docked_file=docked_file_valid(docked_col)?std::move(docked_col):std::vector<uint8_t>{};
    return true;
}

bool sa_ferry_configured(){return configured;}

std::optional<bool> sa_ferry_ready() {
    if(!configured)return false;
    for(auto& slot:slots)if(!slot.posed || !slot_owned(slot))return false;       // existing is not ready: every stage must be done
    return true;
}

std::optional<FerryOffsets> sa_ferry_offsets() {
    if(!configured)return {};
    return hull_offsets;
}

FerryApply sa_ferry_pose(int32_t id,const FerryPose& pose,const std::array<float,3>& speed) {
    FerryApply result;
    if(!configured || id<0 || id>=ferries || !sa_lcs_bridge_profile_matches()){result.diagnostic="Physical ferry is not configured";return result;}
    for(const float v:pose.position)if(!std::isfinite(v)){result.diagnostic="Ferry pose is not finite";return result;}
    for(const float v:speed)if(!std::isfinite(v)){result.diagnostic="Ferry speed is not finite";return result;}
    auto& slot=slots[id];
    if(!slot.created) {
        if(!model_registered(model)){result.diagnostic="Ferry model is not registered in this package";return result;}
        // Residency: ask once per frame through the native streaming command and wait; an unloaded model is never
        // replaced by another vehicle.
        NativeCall loaded;loaded.opcode=0x0248;loaded.inputs={model};loaded.predicate=true;
        const auto has=execute(loaded);
        if(!has.executed){result.diagnostic="Ferry model residency is unreadable: "+has.diagnostic;return result;}
        if(!has.condition) {
            NativeCall request;request.opcode=0x0247;request.inputs={model};
            const auto asked=execute(request);
            result.native_entered=asked.native_entered;
            if(!asked.executed){result.diagnostic="Ferry model request failed: "+asked.diagnostic;return result;}
            result.status=FerryApply::Status::pending;result.diagnostic="Ferry model is streaming";
            return result;
        }
        NativeCall create;create.opcode=0x00A5;create.outputs=1;
        create.inputs={model,0,0,0};
        std::memcpy(&create.inputs[1],&pose.position[0],4);std::memcpy(&create.inputs[2],&pose.position[1],4);std::memcpy(&create.inputs[3],&pose.position[2],4);
        const auto made=execute(create);
        result.native_entered=made.native_entered;
        if(!made.executed || made.values.empty() || made.values[0]<0){result.diagnostic="Ferry vehicle creation failed: "+made.diagnostic;return result;}
        const int32_t handle=made.values[0];
        const auto found=sa_route_vehicle_lookup(handle);
        auto discard=[&](const char* why) {
            NativeCall remove;remove.opcode=0x00A6;remove.inputs={handle};
            (void)execute(remove);
            result.native_entered=true;result.diagnostic=why;
        };
        if(found.status!=VehicleLookupStatus::present || !found.snapshot){discard("Created ferry vehicle is not readable");return result;}
        if(!found.snapshot->is_boat){discard("The ferry model did not create a boat; the package model is not the ferry hull");return result;}
        slot.identity=*found.snapshot;slot.created=true;slot.cell=0;       // creation done: never repeated for this slot
    }
    if(!slot.registered) {
        if(!sa_register_vehicle_reference(slot.identity.handle,&slot.cell)) {
            NativeCall remove;remove.opcode=0x00A6;remove.inputs={slot.identity.handle};
            (void)execute(remove);slot=Slot{};
            result.native_entered=true;result.diagnostic="Ferry weak reference registration failed";return result;
        }
        slot.registered=true;
    }
    if(!slot.frozen) {
        if(!suspend_physics(slot)){result.native_entered=true;result.diagnostic="Ferry physics suspension failed (retried next frame, not recreated)";return result;}
        slot.frozen=true;
    }
    if(!slot_owned(slot)){result.diagnostic="Ferry vehicle changed identity";return result;}
    result.native_entered=true;
    if(write_pose(slot,pose,speed)){slot.posed=true;result.status=FerryApply::Status::applied;}
    else {slot.posed=false;result.diagnostic="Ferry pose write failed";}
    return result;
}

std::optional<float> sa_ferry_surface_z(float x,float y) {
    if(!configured || !execute || !std::isfinite(x) || !std::isfinite(y))return std::nullopt;
    NativeCall ground;ground.opcode=0x02CE;ground.outputs=1;
    const float top=1.0f;int32_t bits[3];
    std::memcpy(bits,&x,4);std::memcpy(bits+1,&y,4);std::memcpy(bits+2,&top,4);
    ground.inputs={bits[0],bits[1],bits[2]};
    const auto result=execute(ground);
    if(!result.executed || result.values.empty())return std::nullopt;
    float z=0;std::memcpy(&z,&result.values[0],4);
    if(!std::isfinite(z))return std::nullopt;
    // Ground search in SA can return a hit below the short original ray.
    // Do not drag the route to a sea bed, or lift it onto an overhead bridge.
    return z>=0.5f && z<=top?z:0.0f;
}

NativeResult sa_ferry_set_collision(int32_t id,int32_t type) {
    NativeResult result;
    if(!configured || id<0 || id>=ferries || (type!=0 && type!=1) || !sa_lcs_bridge_profile_matches()){result.diagnostic="Ferry collision request is invalid";return result;}
    if(type==1) {
        if(docked.installed){result.native_entered=true;result.executed=add_detached_ferries();if(!result.executed)result.diagnostic="A ferry could not be re-added to world sectors";return result;}
        if(docked_file.empty()){result.diagnostic="The packaged docked ferry collision is missing or malformed";return result;}
        if(!model_registered(model)){result.diagnostic="Ferry model is not registered in this package";return result;}
        // The swap is per model; a ferry that carries its own special collision would ignore it, so it is refused.
        if(!model_instances_owned()){result.diagnostic="Another live vehicle uses the shared ferry model; refusing to alter its world membership";return result;}
        if(!ferry_special_collision_free()){result.diagnostic="A ferry uses a special collision model; the per-model swap would not apply";return result;}
        const auto info=model_info_of(model);
        uintptr_t original=0;
        if(!read_model_col(info,original) || !original){result.diagnostic="The ferry model has no collision to replace";return result;}
        if(!docked.built) {
            if(!col_construct_load(docked_file.data()+32)){
                (void)col_destroy();result.native_entered=true;result.diagnostic="The engine could not load the docked ferry collision";return result;
            }
            docked.built=true;
        }
        result.native_entered=true;
        docked.model_info=info;docked.original=original;
        if(!swap_model_col_reindexed(info,original,reinterpret_cast<uintptr_t>(docked.block))) {
            uintptr_t current=0;
            if(read_model_col(info,current) && current==reinterpret_cast<uintptr_t>(docked.block))docked.installed=true;
            else {docked.model_info=0;docked.original=0;}
            result.diagnostic="The ferry model collision could not be replaced and reindexed";return result;
        }
        docked.installed=true;result.executed=true;return result;
    }
    if(!docked.installed){result.executed=true;return result;}
    result.native_entered=true;
    if(!restore_collision()){result.diagnostic="The original ferry collision could not be restored";return result;}
    result.executed=true;return result;
}

NativeResult sa_ferry_set_door(int32_t id,bool front,float ratio) {
    NativeResult result;
    if(!configured || id<0 || id>=ferries || !std::isfinite(ratio) || !sa_lcs_bridge_profile_matches()){result.diagnostic="Ferry door request is invalid";return result;}
    auto& slot=slots[id];
    if(!slot_owned(slot)){result.diagnostic="Ferry vehicle is not live";return result;}
    ratio=ratio<0.0f?0.0f:ratio>1.0f?1.0f:ratio;
    const NodeSpec* specs=front?front_nodes:back_nodes;
    uintptr_t frames[2]{};
    for(int i=0;i<2;++i) {
        frames[i]=find_frame(slot.identity.entity,specs[i].name);
        if(!frames[i]){result.diagnostic=std::string("The ferry hull has no node ")+specs[i].name;return result;}
    }
    result.native_entered=true;
    const auto clump=clump_of(slot.identity.entity);
    if(!clump){result.diagnostic="Ferry clump unavailable";return result;}
    for(int i=0;i<2;++i) {
        auto& node=slot.nodes[(front?0:2)+i];
        if(!write_node(node,frames[i],clump,specs[i].open_angle*ratio,ratio==0.0f)){result.diagnostic="Ferry node write failed";return result;}
    }
    if(!sync_frames(slot.identity.entity)){result.diagnostic="Ferry frame hierarchy sync failed";return result;}
    result.executed=true;return result;
}

bool sa_ferry_collision_installed(){return docked.installed;}

bool sa_ferry_cleanup() {
    if(!restore_collision())return false;
    if(docked.built){if(!col_destroy())return false;docked.built=false;}
    bool ok=true;
    for(auto& slot:slots) {
        if(!slot.created)continue;
        if(!restore_nodes(slot)){ok=false;continue;}
        if(!restore_physics(slot)){ok=false;continue;}
        if(slot.cell && !sa_release_vehicle_reference(slot.identity,&slot.cell)){ok=false;continue;}
        if(execute) {
            NativeCall remove;remove.opcode=0x00A6;remove.inputs={slot.identity.handle};
            if(slot_live(slot) && !execute(remove).executed){ok=false;continue;}
        }
        slot=Slot{};
    }
    if(ok){configured=false;execute=nullptr;docked_file.clear();}
    return ok;
}
}
