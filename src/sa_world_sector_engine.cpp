#include <windows.h>
#include "sa_world_sector_engine.hpp"
#include "sa_lcs_bridge.hpp"
#include "sa_world_preview.hpp"
#include "lcs/sa_executor.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace lcs { namespace {
// Reconstructing Campaign does not prove that SA retired the native world.
// Keep uncertain receipts quarantined for this process instead of invalidating
// them with an invented epoch at every factory call.
uint64_t world_epoch=1;
struct RawEntity {uintptr_t address;uint32_t slot;uint8_t generation;uint16_t model;uint32_t flags;float position[3],basis[9];};
struct ExpectedCollision {float bounds[10];bool volumes=false;};
std::map<uint16_t,ExpectedCollision> collision_contract(const std::filesystem::path& directory){
    std::ifstream index(directory/"model-bindings.tsv");
    std::map<std::string,uint16_t> aliases;int32_t source=0,native=0;std::string name,alias;
    while(index>>source>>native>>name>>alias)if(native>=10000 && native<20000)aliases.emplace(alias,uint16_t(native));
    if(!index.eof() || aliases.empty())throw std::runtime_error("World-sector model binding index unavailable");
    std::ifstream input(directory.parent_path().parent_path()/"models"/"lcs"/"world.col",std::ios::binary|std::ios::ate);
    if(!input || input.tellg()<104 || input.tellg()>128*1024*1024)throw std::runtime_error("World-sector collision contract unavailable");
    std::vector<uint8_t> data(static_cast<size_t>(input.tellg()));input.seekg(0);
    if(!input.read(reinterpret_cast<char*>(data.data()),data.size()))throw std::runtime_error("Truncated world-sector collision contract");
    std::map<uint16_t,ExpectedCollision> result;
    for(size_t offset=0;offset<data.size();){
        if(data.size()-offset<104 || std::memcmp(data.data()+offset,"COL2",4))throw std::runtime_error("Unsupported world collision record");
        uint32_t bytes=0;std::memcpy(&bytes,data.data()+offset+4,4);
        if(bytes<96 || size_t(bytes)>data.size()-offset-8)throw std::runtime_error("Invalid world collision record bounds");
        const char* begin=reinterpret_cast<const char*>(data.data()+offset+8);size_t length=0;while(length<22 && begin[length])++length;
        const auto found=aliases.find(std::string(begin,length));
        if(found!=aliases.end()){
            ExpectedCollision row;std::memcpy(row.bounds,data.data()+offset+32,40);
            for(float v:row.bounds)if(!std::isfinite(v))throw std::runtime_error("Invalid world collision bounds");
            uint16_t counts[3];std::memcpy(counts,data.data()+offset+72,6);row.volumes=counts[0] || counts[1] || counts[2];
            if(!result.emplace(found->second,row).second)throw std::runtime_error("Ambiguous world-sector collision alias");
        }
        offset+=size_t(bytes)+8;
    }
    return result;
}
int32_t bits(float v){int32_t r;std::memcpy(&r,&v,4);return r;}
bool pool_info(uintptr_t root,uintptr_t* entries,uintptr_t* flags,int32_t* count){
    __try{
        const auto pool=*reinterpret_cast<const uintptr_t*>(root);if(!pool)return false;
        *entries=*reinterpret_cast<const uintptr_t*>(pool);*flags=*reinterpret_cast<const uintptr_t*>(pool+4);*count=*reinterpret_cast<const int32_t*>(pool+8);
        return *entries && *flags && *count>=0 && *count<=200000;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool read_entity(uintptr_t root,uint32_t slot,uint32_t stride,uint8_t type,RawEntity* out){
    __try{
        uintptr_t entries=0,flags=0;int32_t count=0;
        if(!pool_info(root,&entries,&flags,&count) || slot>=uint32_t(count))return false;
        const auto generation=*reinterpret_cast<const uint8_t*>(flags+slot);if(generation&128)return false;
        const auto entity=entries+slot*stride;if((*reinterpret_cast<const uint8_t*>(entity+0x36)&7)!=type)return false;
        out->address=entity;out->slot=slot;out->generation=generation;out->model=*reinterpret_cast<const uint16_t*>(entity+0x22);out->flags=*reinterpret_cast<const uint32_t*>(entity+0x1C);
        const auto matrix=*reinterpret_cast<const uintptr_t*>(entity+0x14);
        const auto position=matrix?matrix+0x30:entity+4;
        for(int j=0;j<3;++j){out->position[j]=*reinterpret_cast<const float*>(position+4*j);if(!std::isfinite(out->position[j]))return false;}
        if(matrix){for(int c=0;c<3;++c)for(int j=0;j<3;++j){const auto v=*reinterpret_cast<const float*>(matrix+c*16+j*4);if(!std::isfinite(v))return false;out->basis[c*3+j]=v;}}
        else{
            const float a=*reinterpret_cast<const float*>(entity+0x10);if(!std::isfinite(a))return false;
            const float c=std::cos(a),s=std::sin(a);const float basis[9]={c,s,0,-s,c,0,0,0,1};std::memcpy(out->basis,basis,sizeof(basis));
        }
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void basis_of(const SaWorldSectorTransform& at,float* b){
    // Tables contain text-IPL quaternions; SA's IPL loader conjugates XYZ.
    const auto& q=at.quaternion;const float x=-q[0],y=-q[1],z=-q[2],w=q[3];
    const float m[9]={1-2*y*y-2*z*z,2*x*y+2*w*z,2*x*z-2*w*y,2*x*y-2*w*z,1-2*x*x-2*z*z,2*y*z+2*w*x,2*x*z+2*w*y,2*y*z-2*w*x,1-2*x*x-2*y*y};
    std::memcpy(b,m,sizeof(m));
}
bool matches(const RawEntity& raw,const SaWorldSectorTransform& at){
    float b[9];basis_of(at,b);
    for(int j=0;j<3;++j)if(!std::isfinite(at.position[j]) || std::fabs(raw.position[j]-at.position[j])>.03f)return false;
    for(int j=0;j<9;++j)if(!std::isfinite(b[j]) || std::fabs(raw.basis[j]-b[j])>.003f)return false;
    return true;
}
bool building_live(const SaWorldSectorBuildingRef& ref){
    RawEntity raw{};return ref.world_epoch==world_epoch && read_entity(0xB74498,ref.slot,0x38,1,&raw) &&
        raw.address==ref.address && raw.generation==ref.pool_generation && raw.model==ref.model && matches(raw,ref.transform);
}
bool object_identity(const SaWorldSectorObjectRef& ref,RawEntity* raw){
    return ref.world_epoch==world_epoch && ref.script_handle>=0 &&
        uint32_t(ref.script_handle)>>8==ref.slot && uint8_t(ref.script_handle)==ref.pool_generation &&
        read_entity(0xB7449C,ref.slot,0x19C,4,raw) && raw->address==ref.address && raw->generation==ref.pool_generation && raw->model==ref.model;
}
bool object_live(const SaWorldSectorObjectRef& ref){RawEntity raw{};return object_identity(ref,&raw) && matches(raw,ref.transform);}
bool read_hour(uint8_t* value){__try{*value=*reinterpret_cast<const uint8_t*>(0xB70153);return *value<24;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
// Loaded geometry alone is insufficient: require its texture dictionary and actual collision data.
bool model_dependencies(uint16_t model,const ExpectedCollision* expected,bool* loaded){
    __try{
        *loaded=false;if(model>=20000)return false;
        const auto info=*reinterpret_cast<const uintptr_t*>(0xA9B0C8+4*model);if(!info)return true;
        if(!*reinterpret_cast<const uintptr_t*>(info+0x1C))return true;
        const auto col=*reinterpret_cast<const uintptr_t*>(info+0x14);if(!col)return true;
        // A render-only alias has a deliberate bounds-only COL2, not missing collision.
        // Require the mounted bounds even for these aliases; physical layers also need volumes.
        for(int j=0;j<10;++j){const auto v=*reinterpret_cast<const float*>(col+4*j);if(!std::isfinite(v) || std::fabs(v-expected->bounds[j])>.05f)return true;}
        if(expected->volumes && !*reinterpret_cast<const uintptr_t*>(col+0x2C))return true;
        const auto txd=*reinterpret_cast<const int16_t*>(info+0xA);if(txd<0)return false;
        uintptr_t items=0,flags=0;int32_t count=0;if(!pool_info(0xC8800C,&items,&flags,&count) || txd>=count)return false;
        if((*reinterpret_cast<const uint8_t*>(flags+txd)&128) || !*reinterpret_cast<const uintptr_t*>(items+12*txd))return true;
        *loaded=true;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool toggle_building_native(const SaWorldSectorBuildingRef* ref,bool restore){
    __try{
        if(!building_live(*ref))return false;
        auto& flags=*reinterpret_cast<uint32_t*>(ref->address+0x1C);
        if(restore){flags=(flags&~129u)|(ref->flags&129u);reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x533020)(ref->address);}
        else{reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x534AE0)(ref->address);flags&=~129u;}
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool transform_object_native(const SaWorldSectorObjectRef* ref,const SaWorldSectorTransform* at){
    __try{
        RawEntity raw{};if(!object_identity(*ref,&raw))return false;
        const auto matrix=*reinterpret_cast<const uintptr_t*>(ref->address+0x14);if(!matrix)return false;
        float basis[9];basis_of(*at,basis);
        if(!sa_world_entity_remove(ref->address))return false;
        for(int c=0;c<3;++c)for(int j=0;j<3;++j)*reinterpret_cast<float*>(matrix+c*16+j*4)=basis[c*3+j];
        for(int j=0;j<3;++j)*reinterpret_cast<float*>(matrix+0x30+j*4)=at->position[j];
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x446F90)(ref->address);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x532B00)(ref->address);
        if(!sa_world_entity_add(ref->address))return false;
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
NativeResult refusal(const char* message){NativeResult r;r.diagnostic=message;return r;}
NativeResult change_building(const SaWorldSectorBuildingRef& ref,bool restore){
    if(!sa_lcs_bridge_profile_matches() || !building_live(ref))return refusal("World overlay building identity/native API changed");
    NativeResult r;r.native_entered=true;r.executed=toggle_building_native(&ref,restore);if(!r.executed)r.diagnostic="World overlay building mutation faulted";return r;
}
// Do not turn an executor exception into a retry: it may already have entered the engine.
NativeResult packet(const NativeCall& call){try{return execute_sa(call);}catch(const std::exception& e){NativeResult r;r.native_entered=true;r.diagnostic=e.what();return r;}}
}
namespace {
struct MembershipFinding {uint8_t problem,list;uint16_t index;uintptr_t entity;uint16_t model;uint32_t flags;int8_t pool;int32_t slot;};
struct MembershipAudit {MembershipFinding found[24];uint32_t count;uint32_t nodes;bool readable;};
// Pool tables: buildings 0xB74498, dummies 0xB744A0 (0x38), objects 0xB7449C (0x19C).
bool audit_slot(uintptr_t entity,int8_t* pool,int32_t* slot,bool* freed){
    static const uintptr_t roots[3]={0xB74498,0xB744A0,0xB7449C};static const uint32_t strides[3]={0x38,0x38,0x19C};
    for(int8_t p=0;p<3;++p){uintptr_t items=0,flags=0;int32_t count=0;if(!pool_info(roots[p],&items,&flags,&count))continue;
        if(entity<items || entity>=items+uintptr_t(count)*strides[p] || (entity-items)%strides[p])continue;
        *pool=p;*slot=int32_t((entity-items)/strides[p]);*freed=(*reinterpret_cast<const uint8_t*>(flags+*slot)&128)!=0;return true;}
    *pool=-1;*slot=-1;*freed=false;return false;
}
void audit_list(MembershipAudit* audit,uintptr_t head,uint8_t list,uint16_t index,uint32_t next_offset){
    uintptr_t seen[256];uint32_t seen_count=0;
    auto node=*reinterpret_cast<const uintptr_t*>(head);
    for(uint32_t guard=0;node && guard<100000;++guard,node=*reinterpret_cast<const uintptr_t*>(node+next_offset)){
        ++audit->nodes;const auto entity=*reinterpret_cast<const uintptr_t*>(node);
        uint8_t problem=0;int8_t pool=-1;int32_t slot=-1;bool freed=false;
        const bool pooled=entity && audit_slot(entity,&pool,&slot,&freed);
        const auto vtable=entity?*reinterpret_cast<const uintptr_t*>(entity):0;
        const auto flags=entity?*reinterpret_cast<const uint32_t*>(entity+0x1C):0;
        const bool big=(flags&(1u<<8))!=0;
        if(!entity || freed || vtable==0x863C40)problem=1;                    // destroyed member
        else {for(uint32_t i=0;i<seen_count;++i)if(seen[i]==entity){problem=2;break;}} // repeated node
        if(!problem && (list==0)!=big)problem=3;                              // list contradicts BIG flag
        if(!problem && !pooled)problem=4;                                     // not a native pool entity
        if(seen_count<256)seen[seen_count++]=entity;
        if(problem && audit->count<24)audit->found[audit->count++]={problem,list,index,entity,
            entity?*reinterpret_cast<const uint16_t*>(entity+0x22):uint16_t(0xFFFF),flags,pool,slot};
        else if(problem)++audit->count;
    }
}
bool run_membership_audit(MembershipAudit* audit){
    __try{
        // ms_aLodPtrLists 30x30 single-link {item,next}; ms_aSectors 120x120
        // {buildings,dummies} double-link {item,next,prev} (CWorld::ShutDown 0x564050).
        for(uint16_t i=0;i<900;++i)audit_list(audit,0xB99EB8+4u*i,0,i,4);
        for(uint16_t i=0;i<14400;++i){audit_list(audit,0xB7D0B8+8u*i,1,i,4);audit_list(audit,0xB7D0B8+8u*i+4,2,i,4);}
        audit->readable=true;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){audit->readable=false;return false;}
}
}
std::vector<std::string> sa_world_membership_audit(){
    MembershipAudit audit{};std::vector<std::string> lines;
    if(!run_membership_audit(&audit)){lines.push_back("World membership audit unreadable after "+std::to_string(audit.nodes)+" nodes");return lines;}
    static const char* problems[]={"","destroyed-member","repeated-node","list-contradicts-BIG","non-pool-entity"};
    static const char* lists[]={"lod","sector-buildings","sector-dummies"};
    lines.push_back("World membership audit: nodes="+std::to_string(audit.nodes)+" findings="+std::to_string(audit.count));
    for(uint32_t i=0;i<audit.count && i<24;++i){const auto& f=audit.found[i];char text[256];
        std::snprintf(text,sizeof(text),"World membership finding: %s list=%s index=%u entity=%08X model=%u flags=%08X pool=%d slot=%d",
            problems[f.problem],lists[f.list],unsigned(f.index),unsigned(f.entity),unsigned(f.model),unsigned(f.flags),int(f.pool),int(f.slot));
        lines.emplace_back(text);}
    return lines;
}
void sa_world_sector_world_retiring() noexcept {
    // Every receipt is tagged with the current world generation. Advance before
    // SA starts tearing down its sectors/pools so deferred cleanup will discard
    // those receipts instead of calling into entities whose lifetime is ending.
    ++world_epoch;
    if(world_epoch==0)world_epoch=1;
}
SaWorldSectorEngine make_sa_world_sector_engine(const std::filesystem::path& directory){
    const auto collisions=std::make_shared<const std::map<uint16_t,ExpectedCollision>>(collision_contract(directory));
    SaWorldSectorEngine engine;
    engine.epoch=[](){return world_epoch;};
    engine.hour=[]()->std::optional<uint8_t>{uint8_t h=0;if(!read_hour(&h))return {};return h;};
    engine.request_model=[](uint16_t model){if(model>=20000)return refusal("World overlay model outside native range");return packet({0x0247,{model}});};
    engine.model_ready=[collisions](uint16_t model)->std::optional<bool>{
        const auto found=collisions->find(model);if(found==collisions->end())return {};
        const auto result=packet({0x0248,{model},{},0,0,true});if(!result.executed)return {};
        if(!result.condition)return false;bool ready=false;if(!model_dependencies(model,&found->second,&ready))return {};return ready;
    };
    engine.find_buildings=[](uint16_t model,const SaWorldSectorTransform& at)->std::optional<std::vector<SaWorldSectorBuildingRef>>{
        if(!sa_lcs_bridge_profile_matches())return {};
        uintptr_t items=0,flags=0;int32_t count=0;if(!pool_info(0xB74498,&items,&flags,&count))return {};
        std::vector<SaWorldSectorBuildingRef> found;
        for(int32_t slot=0;slot<count;++slot){RawEntity raw{};if(!read_entity(0xB74498,uint32_t(slot),0x38,1,&raw))continue;
            if(raw.model!=model || !matches(raw,at))continue;
            SaWorldSectorBuildingRef ref;ref.address=raw.address;ref.slot=raw.slot;ref.pool_generation=raw.generation;ref.model=raw.model;ref.flags=raw.flags;ref.transform=at;ref.world_epoch=world_epoch;found.push_back(ref);
        }
        return found;
    };
    engine.building_live=building_live;
    engine.remove_building=[](const auto& ref){return change_building(ref,false);};
    engine.restore_building=[](const auto& ref){return change_building(ref,true);};
    engine.object_live=object_live;
    engine.prepare_object=[](uint16_t model,const SaWorldSectorTransform& at){
        SaWorldSectorObjectReceipt receipt;
        if(!sa_lcs_bridge_profile_matches()){receipt.result=refusal("World overlay native profile unavailable");return receipt;}
        receipt.result=packet({0x029B,{model,bits(at.position[0]),bits(at.position[1]),bits(at.position[2])},{},0,1});
        if(!receipt.result.executed)return receipt;
        const auto handle=receipt.result.values[0];RawEntity raw{};
        if(handle<0 || !read_entity(0xB7449C,uint32_t(handle)>>8,0x19C,4,&raw) || raw.generation!=uint8_t(handle) || raw.model!=model){receipt.result.executed=false;receipt.result.native_entered=true;receipt.result.diagnostic="Created world overlay object has no verified pool receipt";return receipt;}
        SaWorldSectorObjectRef ref;ref.script_handle=handle;ref.address=raw.address;ref.slot=raw.slot;ref.pool_generation=raw.generation;ref.model=raw.model;ref.transform=at;ref.world_epoch=world_epoch;receipt.object=ref;
        // The object receipt is retained before every later engine call, including faults.
        for(const auto& call:std::initializer_list<NativeCall>{{0x0750,{handle,0}},{0x0382,{handle,0}},{0x0550,{handle,1}},{0x0392,{handle,0}},
                {0x07C4,{handle,bits(at.quaternion[0]),bits(at.quaternion[1]),bits(at.quaternion[2]),bits(at.quaternion[3])}}}){
            const auto result=packet(call);if(!result.executed){receipt.result=result;receipt.result.native_entered=true;return receipt;}
        }
        receipt.result.native_entered=true;receipt.result.executed=transform_object_native(&ref,&at) && bind_sa_world_overlay_culling(ref.address);
        if(!receipt.result.executed)receipt.result.diagnostic="World overlay object transform failed";
        return receipt;
    };
    engine.set_object_active=[](const SaWorldSectorObjectRef& ref,bool active){
        if(!object_live(ref))return refusal("World overlay object identity changed before visibility/collision update");
        auto a=packet({0x0382,{ref.script_handle,active?1:0}});if(!a.executed)return a;
        auto b=packet({0x0750,{ref.script_handle,active?1:0}});b.native_entered=true;return b;
    };
    engine.destroy_object=[](const SaWorldSectorObjectRef& ref){
        RawEntity raw{};if(!object_identity(ref,&raw))return refusal("World overlay object identity changed before deletion");
        auto r=packet({0x0108,{ref.script_handle}});r.native_entered=true;
        if(r.executed && object_identity(ref,&raw)){r.executed=false;r.diagnostic="World overlay object deletion not confirmed";}return r;
    };
    return engine;
}
}
