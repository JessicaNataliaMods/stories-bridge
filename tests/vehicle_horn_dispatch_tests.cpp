#include "sa_vehicle_horn_dispatch.hpp"
#include <iostream>
#include <stdexcept>
struct NativeVoices {
    bool horn=false,siren=false;int calls=0,last_fast=0;
    void update(int h,int s,int f){++calls;horn=h!=0;siren=s!=0;last_fast=f;}
};
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    NativeVoices state;
    const auto native=[&](int h,int s,int f){state.update(h,s,f);};
    lcs::dispatch_vehicle_horn_siren(1,1,1,false,false,native);
    check(state.horn && state.siren,"Unready LCS assets allow native horn and siren to start");
    lcs::dispatch_vehicle_horn_siren(1,1,1,true,true,native);
    check(!state.horn && !state.siren && state.calls==2,"LCS takeover must service and stop previously started native horn/siren");
    lcs::dispatch_vehicle_horn_siren(0,0,0,true,true,native);
    check(!state.horn && !state.siren && state.calls==3,"Released inputs still reach native stop handling when both channels are owned");
    lcs::dispatch_vehicle_horn_siren(1,1,1,true,false,native);
    check(!state.horn && state.siren && state.last_fast==1,"Partial takeover preserves native siren and its fast flag");
    lcs::dispatch_vehicle_horn_siren(1,1,1,false,true,native);
    check(state.horn && !state.siren && state.last_fast==0,"Partial takeover preserves native horn and stops native siren");
    lcs::dispatch_vehicle_horn_siren(0x100,0x100,0x100,false,false,native);
    check(!state.horn && !state.siren && state.last_fast==0,"Only authored low-byte flags reach native ABI");
    std::cout<<"PASS: native horn/siren fallback takeover, release and independent ownership; no game or audio device\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
