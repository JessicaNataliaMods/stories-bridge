#include <windows.h>
#include "sa_garage_door.hpp"
#include "sa_lcs_bridge.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace lcs {
namespace {
// SA 1.0 US CGarages::CarTypesCollected[4] and the three SA functions that are its
// only users: Init zeroes it, Save and Load stream the four words.
constexpr uintptr_t collected_address=0x96BFEC;
constexpr uint32_t collected_slots=4;
constexpr unsigned char init_profile[]={0x89,0x1D,0xEC,0xBF,0x96,0x00,0x89,0x1D,0xF0,0xBF,0x96,0x00,
    0x89,0x1D,0xF4,0xBF,0x96,0x00,0x89,0x1D,0xF8,0xBF,0x96,0x00};
constexpr unsigned char stream_profile[]={0xBE,0xEC,0xBF,0x96,0x00,0x6A,0x04,0x56};
constexpr unsigned char stream_end_profile[]={0x81,0xFE,0xFC,0xBF,0x96,0x00,0x7C,0xEA};
// SA's own CGarages::FindDoorHeightForMI (0x449C30): model info -> collision model (+0x14) -> box max.z - min.z
// minus the float at 0x858B1C. It is the same formula as the original LCS door height, so it is read, not guessed.
constexpr unsigned char height_profile[]={0x8B,0x44,0x24,0x04,0x8B,0x0C,0x85,0xC8,0xB0,0xA9,0x00,0x8B,0x41,0x14,
    0xD9,0x40,0x14,0xD8,0x60,0x08,0xD8,0x25,0x1C,0x8B,0x85,0x00,0xC3};
constexpr float placement_tolerance=0.25f;
constexpr float retail_search_radius=20.0f;
constexpr float max_door_height=12.0f;

struct DoorPlacement {
    uint16_t model=0;
    std::array<float,3> at{};
    SaGarageDoorEntityPool pool=SaGarageDoorEntityPool::building;
    bool touched=false;
};
std::vector<DoorPlacement> doors;

#ifdef LCS_GARAGE_DOOR_TEST_BACKEND
uintptr_t test_collected=0;
uintptr_t test_model_info=0;
uintptr_t test_pool_root=0;
uintptr_t test_object_pool_root=0;
uintptr_t test_dummy_pool_root=0;
uintptr_t test_native_garages=0;
#endif
// SA CPools roots. Static IPL instances use CBuilding (0xB74498); after
// native promotion the physical CObject uses ms_pObjectPool (0xB7449C).
// CDummyObject has a separate pool and must never be scanned as CObject.
uintptr_t pool_root(SaGarageDoorEntityPool pool) {
#ifdef LCS_GARAGE_DOOR_TEST_BACKEND
    return pool==SaGarageDoorEntityPool::object?test_object_pool_root:test_pool_root;
#else
    return pool==SaGarageDoorEntityPool::object?0xB7449C:0xB74498;
#endif
}
uintptr_t dummy_pool_root() {
#ifdef LCS_GARAGE_DOOR_TEST_BACKEND
    return test_dummy_pool_root;
#else
    return 0xB744A0;
#endif
}
uintptr_t native_garages_base() {
#ifdef LCS_GARAGE_DOOR_TEST_BACKEND
    return test_native_garages;
#else
    return 0x96C048;
#endif
}

bool close(float value,float expected,float tolerance) {
    return std::isfinite(value) && std::fabs(value-expected)<=tolerance;
}
bool profile() {
    // Reuse the approved world/RW consumer profile (SetPosn, UpdateRwMatrix,
    // Remove/Add entity, UpdateRwFrame) that the converted bridge relies on.
    return sa_lcs_bridge_profile_matches();
}
// Pure POD SEH leaf: no C++ unwind object crosses this boundary.
bool collected_profile_matches() {
#ifdef LCS_GARAGE_DOOR_TEST_BACKEND
    return test_collected!=0;
#else
    __try {
        return !std::memcmp(reinterpret_cast<void*>(0x447156),init_profile,sizeof(init_profile)) &&
               !std::memcmp(reinterpret_cast<void*>(0x5D31C1),stream_profile,sizeof(stream_profile)) &&
               !std::memcmp(reinterpret_cast<void*>(0x5D31D4),stream_end_profile,sizeof(stream_end_profile)) &&
               !std::memcmp(reinterpret_cast<void*>(0x5D32D2),stream_profile,sizeof(stream_profile)) &&
               !std::memcmp(reinterpret_cast<void*>(0x5D32E5),stream_end_profile,sizeof(stream_end_profile));
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#endif
}
uintptr_t collected_base() {
#ifdef LCS_GARAGE_DOOR_TEST_BACKEND
    return test_collected;
#else
    return collected_address;
#endif
}
bool read_collected(uint32_t index,uint32_t* value) {
    __try {
        *value=*reinterpret_cast<const volatile uint32_t*>(collected_base()+index*4);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool write_collected(uint32_t index,uint32_t value) {
    __try {
        auto* slot=reinterpret_cast<volatile uint32_t*>(collected_base()+index*4);
        *slot=value;
        return *slot==value;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool garage_center(const std::array<float,8>& b,float* x,float* y) {
    for(const float value:b)if(!std::isfinite(value))return false;
    *x=(std::min({b[0],b[3],b[5],b[3]+b[5]-b[0]})+std::max({b[0],b[3],b[5],b[3]+b[5]-b[0]}))*0.5f;
    *y=(std::min({b[1],b[4],b[6],b[4]+b[6]-b[1]})+std::max({b[1],b[4],b[6],b[4]+b[6]-b[1]}))*0.5f;
    return true;
}
// Retail CGarage::FindDoorsEntities only accepts doors within 20 units of the
// garage centre; the converted placement must lie in that same window.
DoorPlacement* door_for(const std::array<float,8>& bounds) {
    float cx=0,cy=0;
    if(!garage_center(bounds,&cx,&cy))return nullptr;
    DoorPlacement* found=nullptr;
    for(auto& door:doors) {
        if(std::fabs(door.at[0]-cx)>=retail_search_radius || std::fabs(door.at[1]-cy)>=retail_search_radius)continue;
        // A garage may only claim a door when the converted placement is unique
        // inside Retail's own 20-unit search window.
        if(found)return nullptr;
        found=&door;
    }
    return found;
}

SaGarageDoorResolve scan(uintptr_t pool_root,uint16_t model,const std::array<float,3>& at,
                         SaGarageDoorEntityPool pool_kind,uintptr_t* out) {
    __try {
        const auto pool=*reinterpret_cast<const uintptr_t*>(pool_root);
        if(!pool)return SaGarageDoorResolve::streamed_out;
        const auto data=*reinterpret_cast<const uintptr_t*>(pool);
        const auto flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        if(!data || !flags || count<0 || count>200000)return SaGarageDoorResolve::unavailable;
        uintptr_t found=0;
        const uintptr_t stride=pool_kind==SaGarageDoorEntityPool::object?0x19C:0x38;
        const uint8_t expected_type=pool_kind==SaGarageDoorEntityPool::object?4:1;
        for(int32_t slot=0;slot<count;++slot) {
            if(*reinterpret_cast<const uint8_t*>(flags+slot)&0x80)continue;
            const uintptr_t entity=data+uintptr_t(slot)*stride;
            if(*reinterpret_cast<const uint16_t*>(entity+0x22)!=model)continue;
            const auto matrix=*reinterpret_cast<const uintptr_t*>(entity+0x14);
            const auto position=matrix?matrix+0x30:entity+4;
            const float x=*reinterpret_cast<const float*>(position);
            const float y=*reinterpret_cast<const float*>(position+4);
            const float z=*reinterpret_cast<const float*>(position+8);
            // A model may have many legitimate map placements. Ignore other
            // placements, then prove exact identity before checking its class.
            if(!close(x,at[0],placement_tolerance) || !close(y,at[1],placement_tolerance) ||
               !std::isfinite(z) || z<at[2]-placement_tolerance || z>at[2]+max_door_height+placement_tolerance)continue;
            // SA eEntityType building=1; a different class at this exact placement
            // is an identity mismatch, never permission to mutate a lookalike.
            if((*reinterpret_cast<const uint8_t*>(entity+0x36)&7)!=expected_type)return SaGarageDoorResolve::unavailable;
            if(found)return SaGarageDoorResolve::unavailable;
            found=entity;
        }
        *out=found;
        return found?SaGarageDoorResolve::ready:SaGarageDoorResolve::streamed_out;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return SaGarageDoorResolve::unavailable;}
}

struct RuntimeObjectSample {
    bool object_sample_available=false;
    bool object_z_available=false;
    float object_z=0.0f;
    bool has_dummy_known=false;
    bool has_dummy=false;
    bool dummy_z_available=false;
    float dummy_z=0.0f;
    bool control_code_known=false;
    bool control_code_linked=false;
};

bool read_runtime_object_sample(uintptr_t object,float z,RuntimeObjectSample* sample) {
    *sample=RuntimeObjectSample{};
    __try {
        sample->object_sample_available=true;
        sample->object_z=z;
        sample->object_z_available=std::isfinite(z);
        const auto control_code=*reinterpret_cast<const uintptr_t*>(object+0x138);
        sample->control_code_linked=control_code!=0;
        sample->control_code_known=true;
        const auto dummy=*reinterpret_cast<const uintptr_t*>(object+0x170);
        sample->has_dummy=dummy!=0;
        sample->has_dummy_known=true;
        if(dummy) {
            const auto dummy_matrix=*reinterpret_cast<const uintptr_t*>(dummy+0x14);
            const auto dummy_position=dummy_matrix?dummy_matrix+0x30:dummy+4;
            const float dummy_z=*reinterpret_cast<const float*>(dummy_position+8);
            if(std::isfinite(dummy_z)) {
                sample->dummy_z=dummy_z;
                sample->dummy_z_available=true;
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        *sample=RuntimeObjectSample{};
        return false;
    }
}

// Read-only diagnostic scan. Unlike the resolver, it samples all three SA
// entity pools so a static placement, an unpromoted dummy, and a native object
// remain distinguishable. The caller only supplies placements already owned by
// this module; no entity pointer escapes this function.
bool count_runtime_pool(uintptr_t pool_root,uint16_t model,const std::array<float,3>& at,
        uintptr_t stride,uint32_t* matches,int8_t* native_garage_index,
        RuntimeObjectSample* object_sample=nullptr) {
    *matches=0;
    *native_garage_index=-128;
    if(object_sample)*object_sample=RuntimeObjectSample{};
    if(!pool_root || (stride!=0x38 && stride!=0x19C))return false;
    __try {
        const auto pool=*reinterpret_cast<const uintptr_t*>(pool_root);
        if(!pool)return false;
        const auto data=*reinterpret_cast<const uintptr_t*>(pool);
        const auto flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        if(!data || !flags || count<0 || count>200000 ||
           data>std::numeric_limits<uintptr_t>::max()-uintptr_t(count)*stride)return false;
        uint32_t found=0;
        int8_t garage_index=-128;
        for(int32_t slot=0;slot<count;++slot) {
            if(*reinterpret_cast<const uint8_t*>(flags+slot)&0x80)continue;
            const uintptr_t entity=data+uintptr_t(slot)*stride;
            if(*reinterpret_cast<const uint16_t*>(entity+0x22)!=model)continue;
            const auto matrix=*reinterpret_cast<const uintptr_t*>(entity+0x14);
            const auto position=matrix?matrix+0x30:entity+4;
            const float x=*reinterpret_cast<const float*>(position);
            const float y=*reinterpret_cast<const float*>(position+4);
            const float z=*reinterpret_cast<const float*>(position+8);
            if(!close(x,at[0],placement_tolerance) || !close(y,at[1],placement_tolerance) ||
               !std::isfinite(z) || z<at[2]-placement_tolerance ||
               z>at[2]+max_door_height+placement_tolerance)continue;
            ++found;
            if(stride==0x19C) {
                garage_index=found==1?*reinterpret_cast<const int8_t*>(entity+0x147):int8_t(-2);
                if(object_sample) {
                    if(found==1)(void)read_runtime_object_sample(entity,z,object_sample);
                    else
                        *object_sample=RuntimeObjectSample{};
                }
            }
        }
        *matches=found;
        *native_garage_index=found==1 && stride==0x19C?garage_index:(found>1 && stride==0x19C?int8_t(-2):int8_t(-128));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool read_native_garage_status(int8_t index,SaGarageDoorRuntimeStatus* status) {
    if(index<0 || index>=50)return false;
    const auto base=native_garages_base();
    if(!base)return false;
    const uintptr_t offset=uintptr_t(index)*0xD8;
    if(base>std::numeric_limits<uintptr_t>::max()-offset-0x4F)return false;
    __try {
        const auto garage=base+offset;
        const float progress=*reinterpret_cast<const float*>(garage+0x38);
        if(!std::isfinite(progress))return false;
        status->native_garage_state=*reinterpret_cast<const uint8_t*>(garage+0x4D);
        status->native_garage_progress=progress;
        status->native_garage_flags=*reinterpret_cast<const uint8_t*>(garage+0x4E);
        status->native_garage_state_available=true;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool read_runtime_model_status(uint16_t model,SaGarageDoorRuntimeStatus* status) {
    __try {
#ifdef LCS_GARAGE_DOOR_TEST_BACKEND
        const auto info=test_model_info;
#else
        const auto info=*reinterpret_cast<const uintptr_t*>(0xA9B0C8+uintptr_t(model)*4);
#endif
        if(!info)return false;
        status->object_info_index=*reinterpret_cast<const int16_t*>(info+0x10);
        status->model_flags=*reinterpret_cast<const uint16_t*>(info+0x12);
        status->model_info_available=true;
        status->garage_door_model=(status->model_flags&0x7800)==0x3800;
        const auto col=*reinterpret_cast<const uintptr_t*>(info+0x14);
        if(!col)return true;
        status->collision_model_available=true;
        status->collision_ready=(*reinterpret_cast<const uint8_t*>(col+0x29)&1)!=0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

// CGarages::FindDoorHeightForMI: collision bounding box height minus 0.1.
bool read_height(uint16_t model,float* height) {
    __try {
#ifdef LCS_GARAGE_DOOR_TEST_BACKEND
        const auto info=test_model_info;
#else
        const auto info=*reinterpret_cast<const uintptr_t*>(0xA9B0C8+uintptr_t(model)*4);
#endif
        if(!info)return false;
        const auto col=*reinterpret_cast<const uintptr_t*>(info+0x14);
        if(!col)return false;
        const float min_z=*reinterpret_cast<const float*>(col+8);
        const float max_z=*reinterpret_cast<const float*>(col+0x14);
#ifdef LCS_GARAGE_DOOR_TEST_BACKEND
        const float subtracted=0.1f;
#else
        if(std::memcmp(reinterpret_cast<void*>(0x449C30),height_profile,sizeof(height_profile)))return false;
        const float subtracted=*reinterpret_cast<const float*>(0x858B1C);
#endif
        if(!std::isfinite(min_z) || !std::isfinite(max_z) || !(max_z>min_z) || !close(subtracted,0.1f,0.0001f))return false;
        *height=max_z-min_z-subtracted;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

// Same ownership/transform sequence as the converted bridge: leave the physical
// sector lists, set the new position, refresh RenderWare, and rejoin the sectors.
bool move_entity(uintptr_t entity,float x,float y,float z,SaGarageDoorEntityPool pool_kind,bool* entered) {
    __try {
        *entered=true;
#ifdef LCS_GARAGE_DOOR_TEST_BACKEND
        const auto matrix=*reinterpret_cast<const uintptr_t*>(entity+0x14);
        const auto position=matrix?matrix+0x30:entity+4;
        *reinterpret_cast<float*>(position)=x;*reinterpret_cast<float*>(position+4)=y;*reinterpret_cast<float*>(position+8)=z;
        return true;
#else
        // CPhysical/CObject override these virtual methods to remove and restore
        // collision-sector links. Calling the CEntity base Add/Remove bypasses
        // those overrides and can leave a stale collision list behind.
        if(!sa_world_entity_remove(entity))return false;
        reinterpret_cast<void(__thiscall*)(uintptr_t,float,float,float)>(0x420B80)(entity,x,y,z);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x446F90)(entity);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x532B00)(entity);
        if(!sa_world_entity_add(entity))return false;
        const auto after=*reinterpret_cast<const uintptr_t*>(entity+0x14);
        const auto position=after?after+0x30:entity+4;
        return close(*reinterpret_cast<const float*>(position+8),z,0.01f);
#endif
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
}

#ifdef LCS_GARAGE_DOOR_TEST_BACKEND
void sa_garage_door_test_collected(uintptr_t address){test_collected=address;}
void sa_garage_door_test_model_info(uintptr_t address){test_model_info=address;}
void sa_garage_door_test_pool_root(uintptr_t address){test_pool_root=address;}
void sa_garage_door_test_object_pool_root(uintptr_t address){test_object_pool_root=address;}
void sa_garage_door_test_dummy_pool_root(uintptr_t address){test_dummy_pool_root=address;}
void sa_garage_door_test_native_garages(uintptr_t address){test_native_garages=address;}
#endif

bool sa_garage_door_configure(uint16_t model,const std::array<float,3>& placement,SaGarageDoorEntityPool pool) {
    if(!model || model>=20000)return false;
    for(const float value:placement)if(!std::isfinite(value))return false;
    const auto existing=std::find_if(doors.begin(),doors.end(),[&](const DoorPlacement& door){
        return door.model==model && door.at==placement && door.pool==pool;
    });
    if(existing!=doors.end())return true;
    doors.push_back({model,placement,pool,false});
    return true;
}

std::vector<SaGarageDoorRuntimeStatus> sa_garage_door_runtime_statuses() {
    std::vector<SaGarageDoorRuntimeStatus> statuses;
    statuses.reserve(doors.size());
    for(const auto& door:doors) {
        SaGarageDoorRuntimeStatus status;
        status.model=door.model;
        status.placement=door.at;
        uint32_t buildings=0,dummies=0,objects=0;
        int8_t unused=-128,native_index=-128;
        RuntimeObjectSample object_sample;
        const bool building_ok=count_runtime_pool(pool_root(SaGarageDoorEntityPool::building),
            door.model,door.at,0x38,&buildings,&unused);
        const bool dummy_ok=count_runtime_pool(dummy_pool_root(),door.model,door.at,0x38,&dummies,&unused);
        const bool object_ok=count_runtime_pool(pool_root(SaGarageDoorEntityPool::object),
            door.model,door.at,0x19C,&objects,&native_index,&object_sample);
        status.buildings=buildings;
        status.dummies=dummies;
        status.objects=objects;
        status.native_garage_index=native_index;
        status.building_pool_available=building_ok;
        status.dummy_pool_available=dummy_ok;
        status.object_pool_available=object_ok;
        // Keep the sample tied to a unique exact CObject match. Duplicate model
        // placements intentionally yield counts/index only, never ambiguous detail.
        if(object_ok && objects==1) {
            if(object_sample.object_sample_available) {
                status.object_sample_available=object_sample.object_sample_available;
                status.object_z_available=object_sample.object_z_available;
                status.object_z=object_sample.object_z;
                status.has_dummy_known=object_sample.has_dummy_known;
                status.has_dummy=object_sample.has_dummy;
                status.dummy_z_available=object_sample.dummy_z_available;
                status.dummy_z=object_sample.dummy_z;
                status.control_code_known=object_sample.control_code_known;
                status.control_code_linked=object_sample.control_code_linked;
            }
            (void)read_native_garage_status(native_index,&status);
        }
        (void)read_runtime_model_status(door.model,&status);
        statuses.push_back(status);
    }
    return statuses;
}

SaGarageDoorResolve sa_garage_door_resolve(uintptr_t pool_root,uint16_t model,
        const std::array<float,3>& placement,uintptr_t& entity,SaGarageDoorEntityPool pool) {
    entity=0;
    if(!pool_root || !model || model>=20000)return SaGarageDoorResolve::unavailable;
    for(const float value:placement)if(!std::isfinite(value))return SaGarageDoorResolve::unavailable;
    uintptr_t found=0;
    const auto state=scan(pool_root,model,placement,pool,&found);
    if(state==SaGarageDoorResolve::ready)entity=found;
    return state;
}

std::optional<float> sa_garage_door_height(const std::array<float,8>& bounds) {
    auto* door=door_for(bounds);
    if(!door || !profile())return {};
    uintptr_t entity=0;
    if(sa_garage_door_resolve(pool_root(door->pool),door->model,door->at,entity,door->pool)!=SaGarageDoorResolve::ready)return {};
    float height=0;
    if(!read_height(door->model,&height) || !(height>0.0f) || height>max_door_height)return {};
    return height;
}

NativeResult sa_garage_door_move(const std::array<float,8>& bounds,float position) {
    NativeResult result;
    if(!std::isfinite(position) || position<0.0f || position>max_door_height) {
        result.diagnostic="Garage door position is outside the retail door travel";return result;
    }
    auto* door=door_for(bounds);
    if(!door || !profile()) {
        result.diagnostic="Garage door placement or native consumer profile unavailable";return result;
    }
    uintptr_t entity=0;
    const auto found=sa_garage_door_resolve(pool_root(door->pool),door->model,door->at,entity,door->pool);
    if(found!=SaGarageDoorResolve::ready) {
        result.diagnostic=found==SaGarageDoorResolve::streamed_out?"Garage door mesh is streamed out":"Garage door entity ownership mismatch";
        return result;
    }
    result.executed=move_entity(entity,door->at[0],door->at[1],door->at[2]+position,door->pool,&result.native_entered);
    door->touched=result.executed || result.native_entered;
    if(!result.executed)result.diagnostic="Garage door native transform failed; replay forbidden after entry";
    return result;
}

bool sa_garage_door_cleanup() {
    const bool has_touched=std::any_of(doors.begin(),doors.end(),[](const DoorPlacement& door){return door.touched;});
    if(!has_touched)return true;
    if(!profile())return false;
    bool restored=true;
    for(auto& door:doors) {
        if(!door.touched)continue;
        uintptr_t entity=0;
        const auto found=sa_garage_door_resolve(pool_root(door.pool),door.model,door.at,entity,door.pool);
        if(found==SaGarageDoorResolve::streamed_out){door.touched=false;continue;}
        if(found!=SaGarageDoorResolve::ready){restored=false;continue;}
        bool entered=false;
        if(!move_entity(entity,door.at[0],door.at[1],door.at[2],door.pool,&entered)){restored=false;continue;}
        door.touched=false;
    }
    return restored;
}

std::optional<uint32_t> sa_garage_collected_read(uint32_t index) {
    if(index>=collected_slots || !collected_profile_matches())return {};
    uint32_t value=0;
    if(!read_collected(index,&value))return {};
    return value;
}

bool sa_garage_collected_write(uint32_t index,uint32_t mask) {
    if(index>=collected_slots || !collected_profile_matches())return false;
    return write_collected(index,mask);
}
}
