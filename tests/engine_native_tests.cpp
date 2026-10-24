#include "sa_vehicle_engine_native.hpp"
#include <windows.h>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
template<class T,size_t N> void put(std::array<unsigned char,N>& b,size_t p,T v){std::memcpy(b.data()+p,&v,sizeof(v));}
template<class T,size_t N> T get(const std::array<unsigned char,N>& b,size_t p){T v;std::memcpy(&v,b.data()+p,sizeof(v));return v;}
int main(int argc,char** argv){try{
 using namespace lcs;
 int calls=0,rpm=0,observed=0,fallback=0;
 for(int i=0;i<10;++i)dispatch_vehicle_engine([]{return true;},[&]{++calls;++rpm;},[&]{observed=rpm;return true;},[](bool own){return own;},[&]{++fallback;});
 check(native_vehicle_engine_slot_eligible(0,true),"Player wheelspin CAR_REV slot zero must not remain audible alongside LCS");
 check(calls==10 && observed==10 && fallback==0,"Native RPM must keep advancing before every LCS request after takeover");
 dispatch_vehicle_engine([]{return false;},[&]{++calls;},[]{throw std::runtime_error("Unsafe replacement must not run");return true;},[](bool own){return !own;},[&]{++fallback;});
 check(calls==11 && fallback==1,"Native processing and fallback survive unavailable mute ownership");
 check(argc==2,"Expected executable as profile data only");
 std::ifstream f(argv[1],std::ios::binary|std::ios::ate);check(bool(f),"Profile readable");std::vector<unsigned char> bytes(size_t(f.tellg()));f.seekg(0);f.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));check(bool(f),"Complete profile");
 auto u32=[&](size_t p){uint32_t v;check(p+4<=bytes.size(),"PE bounds");std::memcpy(&v,bytes.data()+p,4);return v;};auto u16=[&](size_t p){uint16_t v;check(p+2<=bytes.size(),"PE bounds");std::memcpy(&v,bytes.data()+p,2);return v;};const size_t pe=u32(60);
 auto offset=[&](uintptr_t va){const auto rva=va-u32(pe+52);for(size_t i=0;i<u16(pe+6);++i){const size_t p=pe+24+u16(pe+20)+40*i;if(rva>=u32(p+12) && rva-u32(p+12)<u32(p+16))return size_t(u32(p+20)+rva-u32(p+12));}throw std::runtime_error("Profile address missing");};
 check(u32(offset(0x4FBBA7)+2)==0x4B4,"Actual transmission gear offset agrees with native engine reader");
 check(u32(offset(0x4FBCBD)+2)==0x961 && u32(offset(0x4FBC91)+2)==0x805,"Driven-wheel contact offsets agree with native car/bike readers");
 // Read-only-in-purpose profile data in NON-EXECUTABLE authored test storage.
 auto* profile=VirtualAlloc(reinterpret_cast<void*>(0x4F0000),0x20000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);check(profile!=nullptr,"Private test profile range available");
 for(auto item:std::array<std::pair<uintptr_t,size_t>,4>{{{0x4FBB10,12},{0x501480,12},{0x4F55CD,7},{0x4F56EE,6}}})std::memcpy(reinterpret_cast<void*>(item.first),bytes.data()+offset(item.first),item.second);
 check(sa_vehicle_engine_native::profile_matches(),"Actual native layout signatures match without executing original bytes");
 alignas(4) std::array<unsigned char,0x200> entity{};alignas(4) std::array<unsigned char,0x40> vehicle{};
 alignas(4) std::array<std::array<unsigned char,0x74>,12> voices{};
 const auto self=reinterpret_cast<uintptr_t>(entity.data());put(entity,4,reinterpret_cast<uintptr_t>(vehicle.data()));put<uint16_t>(vehicle,0x22,400);
 for(uint16_t i=0;i<12;++i){put<uint16_t>(voices[i],0,7);put<uint16_t>(voices[i],2,200+i);put(voices[i],4,self);put<float>(voices[i],0x14,30.f+i);put<uint16_t>(voices[i],0x56,4);put<uint16_t>(voices[i],0x58,1);put(entity,0xE8+i*8,reinterpret_cast<uintptr_t>(voices[i].data()));}
 check(sa_vehicle_engine_native::before(entity.data(),true) && sa_vehicle_engine_native::after(entity.data(),true,true),"Player native engine gain ownership acquired");
 for(uint16_t i=0;i<12;++i)check(get<float>(voices[i],0x14)==(i<=6?-100.f:30.f+i),"Only owned engine slots may be muted; effects/horns stay intact");
 check(sa_vehicle_engine_native::before(entity.data(),true),"Previous native gains restored before SA processing");
 for(uint16_t i=0;i<12;++i)check(get<float>(voices[i],0x14)==30.f+i,"Original gain restored exactly");
 put<float>(voices[2],0x14,-12.f);check(sa_vehicle_engine_native::after(entity.data(),true,true),"Fresh native output can be muted again");put<float>(voices[2],0x14,-33.f);
 check(sa_vehicle_engine_native::before(entity.data(),true) && get<float>(voices[2],0x14)==-33.f,"A later native/mod volume writer is never overwritten by a stale saved gain");
 check(sa_vehicle_engine_native::after(entity.data(),false,true),"Dummy engine ownership acquired");check(get<float>(voices[0],0x14)==-100.f && get<float>(voices[1],0x14)==-100.f && get<float>(voices[3],0x14)==33.f,"Dummy ownership covers only slots zero and one");
 check(sa_vehicle_engine_native::before(entity.data(),false) && sa_vehicle_engine_native::after(entity.data(),false,false),"Fallback restores native gain");
 put<uintptr_t>(entity,0xE8+3*8,1);check(!sa_vehicle_engine_native::after(entity.data(),true,true),"Invalid native slot refuses takeover");check(get<float>(voices[1],0x14)==31.f && get<float>(voices[2],0x14)==-33.f,"Partial mute is rolled back on refusal");put(entity,0xE8+3*8,reinterpret_cast<uintptr_t>(voices[3].data()));
 check(sa_vehicle_engine_native::after(entity.data(),true,true),"Engine takeover recovers");put<uintptr_t>(voices[1],4,0x1234);check(sa_vehicle_engine_native::before(entity.data(),true) && get<float>(voices[1],0x14)==-100.f,"Recycled/foreign voice never receives old owner gain");
 sa_vehicle_engine_native::reset();VirtualFree(profile,0,MEM_RELEASE);
 std::cout<<"PASS: native RPM service, scoped gain restore/mute, dummy/player isolation, recycled voice and partial failure; game/audio never executed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
