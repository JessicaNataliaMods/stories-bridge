#include "sa_hand_aim.hpp"
#include "lcs/native_memory.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// GTA SA 1.0 US only. Offsets were checked against the executable:
//  - ped+0x598 is m_nPedType (CPed::IsPlayer, 0x5DF8F0, tests it against 0 and 1),
//  - ped+0x47C is m_pIntelligence (CPedIntelligence::SetTaskSecondary callers pass it +4 to CTaskManager),
//  - CTaskManager keeps six secondary tasks at +0x14 (slot 0 = attack) and m_pPed at +0x2C (0x681A30).
namespace lcs {
namespace {
using u8=uint8_t;using u32=uint32_t;
struct Vec3 {float x,y,z;};
struct RwMatrixLite {Vec3 right;u32 flags;Vec3 up;u32 pad1;Vec3 at;u32 pad2;Vec3 pos;u32 pad3;};
struct RpHAnimHierarchyLite {int flags;int numNodes;RwMatrixLite* pMatrixArray;};

constexpr u32 addr_get_skin_hierarchy=0x00734A40u,addr_hanim_index=0x007C51A0u,addr_weapon_skill=0x005E6580u,
              addr_weapon_info=0x00743C60u,addr_is_player=0x005DF8F0u;
constexpr u32 ped_rwobject=0x18u,ped_weapons=0x5A0u,ped_intelligence=0x47Cu,ped_type=0x598u,ped_slot=0x718u;
constexpr u32 intel_taskmgr=0x04u,taskmgr_secondary=0x14u,taskmgr_ped=0x2Cu;
constexpr u32 winfo_flags=0x18u,winfo_group=0x1Cu,weapon_flag_twin=0x800u;
constexpr u32 vtables[]={0x0086C358u,0x0086D168u,0x0086C0A8u,0x0086C120u,0x0086C200u};   // every CPed subclass
constexpr int vtable_count=int(sizeof(vtables)/sizeof(vtables[0]));
constexpr u32 vtable_player=0x0086D168u,slot_prerender=0x44u,slot_render=0x48u;
constexpr int bone_r_fore=23,bone_r_hand=24,bone_r_finger=25,bone_r_finger01=26;
constexpr int bone_l_fore=33,bone_l_hand=34,bone_l_finger=35,bone_l_finger01=36;
enum PedType {pt_player1=0,pt_player2=1,pt_civmale=4,pt_civfemale=5,pt_cop=6,pt_ballas=7,pt_grove=8,pt_vagos=9,
              pt_sfrifa=10,pt_danang=11,pt_mafia=12,pt_triad=13,pt_aztecas=14};
constexpr int max_weapon_types=64;

typedef void(__thiscall* PedVirtualFn)(void* ped);
typedef RpHAnimHierarchyLite*(__cdecl* GetSkinHierarchyFn)(void* clump);
typedef int(__cdecl* HAnimIndexFn)(RpHAnimHierarchyLite* hierarchy,int id);
typedef int(__thiscall* WeaponSkillFn)(void* ped);
typedef u8*(__cdecl* WeaponInfoFn)(int type,int skill);

HandAimConfig cfg;
bool weapon_listed[max_weapon_types];
PedVirtualFn prev_prerender[vtable_count],prev_render[vtable_count];
int exceptions_reported=0;
u32 corrections=0;
bool logged_group[32];

Vec3 V(float x,float y,float z){return Vec3{x,y,z};}
Vec3 Sub(const Vec3& a,const Vec3& b){return V(a.x-b.x,a.y-b.y,a.z-b.z);}
Vec3 Add(const Vec3& a,const Vec3& b){return V(a.x+b.x,a.y+b.y,a.z+b.z);}
Vec3 Mul(const Vec3& a,float s){return V(a.x*s,a.y*s,a.z*s);}
float Dot(const Vec3& a,const Vec3& b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 Cross(const Vec3& a,const Vec3& b){return V(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
bool Finite3(const Vec3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
Vec3 NormalizeOr(const Vec3& v,const Vec3& fallback){const float l2=Dot(v,v);return l2>1e-8f?Mul(v,1.0f/std::sqrt(l2)):fallback;}
Vec3 RotateVec(const Vec3& v,const Vec3& axis,float s,float c){
    return Add(Add(Mul(v,c),Mul(Cross(axis,v),s)),Mul(axis,Dot(axis,v)*(1.0f-c)));
}
Vec3 ProjectPerpendicular(const Vec3& v,const Vec3& n){return Sub(v,Mul(n,Dot(v,n)));}
float Clamp1(float d){return d>1.0f?1.0f:(d<-1.0f?-1.0f:d);}
Vec3 Slerp(const Vec3& a,const Vec3& b,float t){
    const float theta=std::acos(Clamp1(Dot(a,b)));
    if(theta<1e-4f)return a;
    const float s=std::sin(theta);
    if(s<1e-4f)return a;
    return NormalizeOr(Add(Mul(a,std::sin((1.0f-t)*theta)/s),Mul(b,std::sin(t*theta)/s)),a);
}
float AngleBetweenDeg(const Vec3& a,const Vec3& b){return std::acos(Clamp1(Dot(a,b)))*57.29577951308232f;}
Vec3 BasisDelta(const Vec3& v,const Vec3& oR,const Vec3& oU,const Vec3& oA,const Vec3& nR,const Vec3& nU,const Vec3& nA){
    return Add(Add(Mul(nR,Dot(v,oR)),Mul(nU,Dot(v,oU))),Mul(nA,Dot(v,oA)));
}
void ApplyDeltaToChild(RwMatrixLite* child,const Vec3& pivot,const Vec3& oR,const Vec3& oU,const Vec3& oA,
                       const Vec3& nR,const Vec3& nU,const Vec3& nA){
    const Vec3 off=Sub(child->pos,pivot);
    child->right=BasisDelta(child->right,oR,oU,oA,nR,nU,nA);
    child->up=BasisDelta(child->up,oR,oU,oA,nR,nU,nA);
    child->at=BasisDelta(child->at,oR,oU,oA,nR,nU,nA);
    child->pos=Add(pivot,BasisDelta(off,oR,oU,oA,nR,nU,nA));
}

// Per-ped state. The saved pose is only put back into the skeleton it was taken from.
struct PedState {
    void* ped;u32 lastUse;int lockedSign[2];
    RwMatrixLite* matrixArray;int savedCount;int savedIndex[6];RwMatrixLite saved[6];
    int appliedThisFrame;float weight;u32 lastTick;int lastDual;
};
constexpr int max_states=48;
PedState states[max_states];u32 use_counter=0;

PedState* GetState(void* ped,bool create){
    PedState* oldest=&states[0];
    for(auto& s:states){
        if(s.ped==ped){s.lastUse=++use_counter;return &s;}
        if(s.lastUse<oldest->lastUse)oldest=&s;
    }
    if(!create)return nullptr;
    std::memset(oldest,0,sizeof(*oldest));
    oldest->ped=ped;oldest->lastUse=++use_counter;
    return oldest;
}
RpHAnimHierarchyLite* GetHierarchy(void* ped){
    void* clump=*reinterpret_cast<void**>(reinterpret_cast<u8*>(ped)+ped_rwobject);
    if(!clump)return nullptr;
    auto* h=reinterpret_cast<GetSkinHierarchyFn>(native_entity_address(addr_get_skin_hierarchy))(clump);
    if(!h || !h->pMatrixArray || h->numNodes<=0 || h->numNodes>256)return nullptr;
    return h;
}
void RestorePed(void* ped){
    PedState* st=GetState(ped,false);
    if(!st || st->savedCount<=0)return;
    RpHAnimHierarchyLite* h=GetHierarchy(ped);
    if(h && h->pMatrixArray==st->matrixArray)
        for(int i=0;i<st->savedCount;++i)
            if(st->savedIndex[i]>=0 && st->savedIndex[i]<h->numNodes)h->pMatrixArray[st->savedIndex[i]]=st->saved[i];
    st->savedCount=0;st->appliedThisFrame=0;
}

bool GroupEnabled(void* ped){
    const u32 vtbl=*reinterpret_cast<u32*>(ped);
    const u32 type=*reinterpret_cast<u32*>(reinterpret_cast<u8*>(ped)+ped_type);
    if(vtbl==vtable_player || type==pt_player1 || type==pt_player2)return cfg.cj;
    if(!cfg.npcs)return false;
    switch(type){
    case pt_grove:return cfg.grove;case pt_ballas:return cfg.ballas;case pt_vagos:return cfg.vagos;
    case pt_sfrifa:return cfg.sfrifa;case pt_danang:return cfg.danang;case pt_mafia:return cfg.mafia;
    case pt_triad:return cfg.triads;case pt_aztecas:return cfg.aztecas;case pt_cop:return cfg.cops;
    case pt_civmale:case pt_civfemale:return cfg.civilians;
    default:return cfg.otherPeds;
    }
}
bool PlausibleInfo(const u8* info){return info && uintptr_t(info)>0x10000u && uintptr_t(info)<0x7FFF0000u;}
u8* WeaponInfo(void* ped,int type){
    int skill=1;
    __try{skill=int(int8_t(reinterpret_cast<WeaponSkillFn>(native_entity_address(addr_weapon_skill))(ped)&0xFF));}__except(EXCEPTION_EXECUTE_HANDLER){}
    u8* info=reinterpret_cast<WeaponInfoFn>(native_entity_address(addr_weapon_info))(type,skill);
    return PlausibleInfo(info)?info:nullptr;
}
bool group_set[256];bool group_set_ready=false;
void BuildGroupSet(){
    std::memset(group_set,0,sizeof(group_set));
    for(int t=0;t<max_weapon_types;++t){
        if(!weapon_listed[t])continue;
        for(int skill=0;skill<3;++skill){
            u8* info=reinterpret_cast<WeaponInfoFn>(native_entity_address(addr_weapon_info))(t,skill);
            if(!PlausibleInfo(info))continue;
            const u32 group=*reinterpret_cast<u32*>(info+winfo_group);
            if(group<256)group_set[group]=true;
        }
    }
    group_set_ready=true;
}
struct WeaponDecision {bool apply;bool dual;int type;};
WeaponDecision DecideWeapon(void* ped){
    WeaponDecision d{false,false,-1};
    const u8 slot=*reinterpret_cast<volatile u8*>(reinterpret_cast<u8*>(ped)+ped_slot);
    if(slot<2 || slot>7)return d;                       // firearms only
    const int type=*reinterpret_cast<int*>(reinterpret_cast<u8*>(ped)+ped_weapons+slot*0x1C);
    if(type<=0 || type>=max_weapon_types)return d;
    d.type=type;
    u8* info=WeaponInfo(ped,type);
    if(!info)return d;
    bool match=weapon_listed[type];
    if(!match && cfg.sameAnimationGroup){
        if(!group_set_ready)BuildGroupSet();
        const u32 group=*reinterpret_cast<u32*>(info+winfo_group);
        match=group<256 && group_set[group];
    }
    if(!match)return d;
    d.apply=true;
    d.dual=(*reinterpret_cast<u32*>(info+winfo_flags)&weapon_flag_twin)!=0;
    return d;
}
// 1 aiming/firing (secondary attack task present), 0 not, -1 unknown layout (never corrected).
int IsAiming(void* ped){
    const u32 intel=*reinterpret_cast<u32*>(reinterpret_cast<u8*>(ped)+ped_intelligence);
    if(intel<0x10000u)return -1;
    const u8* mgr=reinterpret_cast<const u8*>(intel)+intel_taskmgr;
    if(*reinterpret_cast<const u32*>(mgr+taskmgr_ped)!=u32(uintptr_t(ped)))return -1;
    return *reinterpret_cast<const u32*>(mgr+taskmgr_secondary)!=0?1:0;
}

void (*log_callback)(const char*)=nullptr;
void Log(const char* fmt,...){
    if(!log_callback)return;
    char buffer[384];va_list ap;va_start(ap,fmt);std::vsnprintf(buffer,sizeof(buffer),fmt,ap);va_end(ap);
    log_callback(buffer);
}

bool CorrectHand(PedState* st,RpHAnimHierarchyLite* h,int hand01,int boneFore,int boneHand,int boneFinger,int boneFinger01,
                 float weight){
    auto getIndex=reinterpret_cast<HAnimIndexFn>(native_entity_address(addr_hanim_index));
    const int n=h->numNodes;
    const int iFore=getIndex(h,boneFore),iHand=getIndex(h,boneHand),iFinger=getIndex(h,boneFinger),iFinger01=getIndex(h,boneFinger01);
    if(iHand<0 || iHand>=n || iFore<0 || iFore>=n || st->savedCount+3>6)return false;
    RwMatrixLite* hand=&h->pMatrixArray[iHand];
    RwMatrixLite* fore=&h->pMatrixArray[iFore];
    RwMatrixLite* finger=(iFinger>=0 && iFinger<n)?&h->pMatrixArray[iFinger]:nullptr;
    RwMatrixLite* finger01=(iFinger01>=0 && iFinger01<n)?&h->pMatrixArray[iFinger01]:nullptr;

    const Vec3 oldR=NormalizeOr(hand->right,V(1,0,0)),oldU=NormalizeOr(hand->up,V(0,1,0)),oldA=NormalizeOr(hand->at,V(0,0,1));
    Vec3 armDir=Sub(hand->pos,fore->pos);
    if(!Finite3(armDir) || Dot(armDir,armDir)<1e-8f || !Finite3(oldR) || !Finite3(oldU) || !Finite3(oldA))return false;
    armDir=NormalizeOr(armDir,oldR);
    // Mirrored bones may have a left-handed basis and a +X pointing away from the child: keep both as they are.
    const float handed=Dot(Cross(oldR,oldU),oldA)<0.0f?-1.0f:1.0f;
    const Vec3 limbR=Dot(oldR,armDir)<0.0f?Mul(oldR,-1.0f):oldR;

    // Forward axis: the forearm line (steady while the aim settles), the aim IK's hand axis, or a blend; the
    // result never leaves the game's own axis by more than maxDeviationDegrees.
    Vec3 fwd;
    switch(cfg.aimAxis){
    case 1:fwd=limbR;break;
    case 2:fwd=Slerp(limbR,armDir,float(cfg.blendPercent)/100.0f);break;
    default:fwd=armDir;break;
    }
    const float deviation=AngleBetweenDeg(fwd,limbR);
    if(cfg.maxDeviationDegrees>0 && deviation>float(cfg.maxDeviationDegrees))fwd=Slerp(limbR,fwd,float(cfg.maxDeviationDegrees)/deviation);
    const Vec3 newR=Dot(oldR,fwd)<0.0f?Mul(fwd,-1.0f):fwd;

    // Upright axis (hand.at): world up projected perpendicular to the forward axis, side kept per ped and hand.
    Vec3 vertical=ProjectPerpendicular(V(0,0,1),newR);
    if(Dot(vertical,vertical)<1e-6f)return false;
    vertical=NormalizeOr(vertical,V(0,0,1));
    if(st->lockedSign[hand01]==0)st->lockedSign[hand01]=Dot(oldA,vertical)<0.0f?-1:1;
    Vec3 newA=Mul(vertical,float(st->lockedSign[hand01]));
    Vec3 newU=NormalizeOr(Mul(Cross(newA,newR),handed),oldU);
    newA=NormalizeOr(Mul(Cross(newR,newU),handed),vertical);
    if(!Finite3(newR) || !Finite3(newU) || !Finite3(newA))return false;

    // Strength: the entering/leaving ramp times a fade while the arm hangs nearly vertical (rest pose, walking
    // with the gun), where "upright" is ill-conditioned. The whole basis is blended toward the target.
    float w=weight;
    {
        const float e=std::fabs(newR.z);
        float t=(e-0.55f)/(0.92f-0.55f);
        t=t<0.0f?0.0f:(t>1.0f?1.0f:t);
        w*=1.0f-t*t*(3.0f-2.0f*t);
    }
    if(w<0.002f)return false;
    Vec3 outR=newR,outU=newU,outA=newA;
    if(w<0.999f){
        outR=NormalizeOr(Add(Mul(oldR,1.0f-w),Mul(newR,w)),newR);
        Vec3 a=NormalizeOr(Add(Mul(oldA,1.0f-w),Mul(newA,w)),newA);
        a=NormalizeOr(Sub(a,Mul(outR,Dot(a,outR))),newA);
        outU=NormalizeOr(Mul(Cross(a,outR),handed),newU);
        outA=NormalizeOr(Mul(Cross(outR,outU),handed),a);
    }
    if((Dot(Cross(outR,outU),outA)<0.0f?-1.0f:1.0f)!=handed)return false;   // would turn the mesh inside out

    st->matrixArray=h->pMatrixArray;
    st->savedIndex[st->savedCount]=iHand;st->saved[st->savedCount++]=*hand;
    if(finger){st->savedIndex[st->savedCount]=iFinger;st->saved[st->savedCount++]=*finger;}
    if(finger01){st->savedIndex[st->savedCount]=iFinger01;st->saved[st->savedCount++]=*finger01;}
    const Vec3 pivot=hand->pos;
    if(finger)ApplyDeltaToChild(finger,pivot,oldR,oldU,oldA,outR,outU,outA);
    if(finger01)ApplyDeltaToChild(finger01,pivot,oldR,oldU,oldA,outR,outU,outA);
    hand->right=outR;hand->up=outU;hand->at=outA;
    return true;
}

void ApplyHandCorrection(void* ped){
    if(!GroupEnabled(ped))return;
    const int aiming=IsAiming(ped);
    const bool aimOk=aiming==1;
    WeaponDecision weapon{false,false,-1};
    if(aimOk)weapon=DecideWeapon(ped);
    const bool want=aimOk && weapon.apply;
    PedState* st=GetState(ped,want);   // only a ped that is (or was) corrected needs state
    if(!st)return;

    const u32 now=GetTickCount();
    float dt=st->lastTick?float(now-st->lastTick):0.0f;
    st->lastTick=now;
    if(dt>100.0f)dt=100.0f;
    if(want){
        st->weight=cfg.blendInMs>0?st->weight+dt/float(cfg.blendInMs):1.0f;
        st->lastDual=weapon.dual;
    }else st->weight=cfg.blendOutMs>0?st->weight-dt/float(cfg.blendOutMs):0.0f;
    if(st->weight>1.0f)st->weight=1.0f;
    if(st->weight<=0.0f){st->weight=0.0f;return;}
    if(st->appliedThisFrame)return;   // Render may run more than once per frame

    RpHAnimHierarchyLite* h=GetHierarchy(ped);
    if(!h)return;
    if(!want)weapon.dual=st->lastDual!=0;
    st->savedCount=0;
    const bool right=CorrectHand(st,h,0,bone_r_fore,bone_r_hand,bone_r_finger,bone_r_finger01,st->weight);
    bool left=false;
    if(weapon.dual && cfg.leftHand)left=CorrectHand(st,h,1,bone_l_fore,bone_l_hand,bone_l_finger,bone_l_finger01,st->weight);
    if(!right && !left)return;
    st->appliedThisFrame=1;
    ++corrections;
    const u32 type=*reinterpret_cast<u32*>(reinterpret_cast<u8*>(ped)+ped_type);
    if(type<32 && !logged_group[type]){
        logged_group[type]=true;
        Log("Hand aim: first correction pedType=%u weaponType=%d dual=%d right=%d left=%d",type,weapon.type,int(weapon.dual),int(right),int(left));
    }
}

int VtableIndex(void* ped){
    const u32 v=*reinterpret_cast<u32*>(ped);
    for(int i=0;i<vtable_count;++i)if(v==vtables[i])return i;
    return -1;
}
void __fastcall HookPreRender(void* ped,void*){
    RestorePed(ped);   // undo last frame's correction before the game rebuilds this frame's skeleton
    const int i=VtableIndex(ped);
    PedVirtualFn prev=i>=0?prev_prerender[i]:prev_prerender[0];
    if(prev)prev(ped);
}
u32 last_exception_code=0,last_exception_address=0;
int ExceptionFilter(EXCEPTION_POINTERS* info){
    last_exception_code=info->ExceptionRecord->ExceptionCode;
    last_exception_address=u32(uintptr_t(info->ExceptionRecord->ExceptionAddress));
    return EXCEPTION_EXECUTE_HANDLER;
}
void ReportException(void* ped){
    if(exceptions_reported<5){
        ++exceptions_reported;
        Log("Hand aim: exception 0x%08X at 0x%08X on ped 0x%08X; correction skipped",last_exception_code,last_exception_address,u32(uintptr_t(ped)));
    }
}
void __fastcall HookRender(void* ped,void*){
    __try{ApplyHandCorrection(ped);}__except(ExceptionFilter(GetExceptionInformation())){ReportException(ped);}
    const int i=VtableIndex(ped);
    PedVirtualFn prev=i>=0?prev_render[i]:prev_render[0];
    if(prev)prev(ped);
}

// CPed::IsPlayer must still read m_nPedType (+0x598).
bool ExecutableMatchesProfile(){
    static const u8 prologue[]={0x8B,0x81,0x98,0x05,0x00,0x00,0x85,0xC0};
    __try{return std::memcmp(reinterpret_cast<const void*>(native_entity_address(addr_is_player)),prologue,sizeof(prologue))==0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ExecutableMatches(std::string& diagnostic){
    if(ExecutableMatchesProfile())return true;
    diagnostic="not a GTA SA 1.0 US executable (CPed::IsPlayer differs); nothing hooked";
    return false;
}
bool IsExecutableCode(u32 address){
    MEMORY_BASIC_INFORMATION mbi;
    if(address<0x10000u || !VirtualQuery(reinterpret_cast<LPCVOID>(uintptr_t(address)),&mbi,sizeof(mbi)))return false;
    return mbi.State==MEM_COMMIT && (mbi.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)) && !(mbi.Protect&PAGE_GUARD);
}
// Chains to whatever the slot held (the game's function or another mod's hook).
bool PatchSlot(u32 address,void* hook,PedVirtualFn* previous){
    void** slot=reinterpret_cast<void**>(uintptr_t(address));
    void* old=*slot;
    if(!old || old==hook)return false;
    DWORD protect=0;
    if(!VirtualProtect(slot,4,PAGE_EXECUTE_READWRITE,&protect))return false;
    *previous=reinterpret_cast<PedVirtualFn>(old);
    *slot=hook;
    DWORD ignored=0;VirtualProtect(slot,4,protect,&ignored);
    return true;
}
void ParseWeaponList(const std::string& text){
    const char* p=text.c_str();
    while(*p){
        while(*p && (*p<'0' || *p>'9'))++p;
        if(!*p)break;
        const int v=std::atoi(p);
        if(v>=0 && v<max_weapon_types)weapon_listed[v]=true;
        while(*p>='0' && *p<='9')++p;
    }
}
}   // namespace

bool sa_hand_aim_install(const HandAimConfig& config,std::string& diagnostic){
    static bool installed=false;
    if(installed){diagnostic="already installed";return true;}
    cfg=config;
    // The offsets above only exist in the 1.0 US executable.
    if(!ExecutableMatches(diagnostic))return false;

    std::memset(weapon_listed,0,sizeof(weapon_listed));
    for(int t:{22,23,24,26,28,32})weapon_listed[t]=true;   // pistol, silenced, deagle, sawn-off, micro uzi, tec9
    ParseWeaponList(cfg.extraWeaponTypes);
    cfg.aimAxis=cfg.aimAxis<0?0:(cfg.aimAxis>2?2:cfg.aimAxis);
    cfg.blendPercent=cfg.blendPercent<0?0:(cfg.blendPercent>100?100:cfg.blendPercent);
    cfg.maxDeviationDegrees=cfg.maxDeviationDegrees<0?0:(cfg.maxDeviationDegrees>90?90:cfg.maxDeviationDegrees);
    cfg.blendInMs=cfg.blendInMs<0?0:(cfg.blendInMs>2000?2000:cfg.blendInMs);
    cfg.blendOutMs=cfg.blendOutMs<0?0:(cfg.blendOutMs>2000?2000:cfg.blendOutMs);

    int hooked=0,chained=0;
    for(int i=0;i<vtable_count;++i){
        const u32 pre=*reinterpret_cast<u32*>(native_entity_address(vtables[i]+slot_prerender));
        const u32 ren=*reinterpret_cast<u32*>(native_entity_address(vtables[i]+slot_render));
        if(!IsExecutableCode(pre) || !IsExecutableCode(ren))continue;
        if(pre!=0x5E8A20u || ren!=0x5E7680u)++chained;   // another mod hooked first: we chain to it
        if(PatchSlot(native_entity_address(vtables[i]+slot_prerender),reinterpret_cast<void*>(&HookPreRender),&prev_prerender[i]) &&
           PatchSlot(native_entity_address(vtables[i]+slot_render),reinterpret_cast<void*>(&HookRender),&prev_render[i]))++hooked;
    }
    if(!hooked){diagnostic="no ped vtable could be hooked";return false;}
    installed=true;
    diagnostic="hooked "+std::to_string(hooked)+" of "+std::to_string(vtable_count)+" ped vtables"+
               (chained?" ("+std::to_string(chained)+" chained to another mod's hook)":"");
    return true;
}
void sa_hand_aim_set_logger(void(*callback)(const char*)){log_callback=callback;}
}
