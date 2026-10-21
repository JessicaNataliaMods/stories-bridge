#include "lcs/phone_native_frame.hpp"
#include <iostream>
#include <stdexcept>
namespace fixture {
inline lcs::NativeFrameClock clock;
inline lcs::NativePhoneBooth booth{1,10,20,30,10000,0,{0,0,0},1.f};
inline bool present=true,render_ok=true,slow=false;
inline size_t random_calls=0;inline std::string effects;
}
namespace lcs {
std::optional<NativeFrameClock> sa_frame_clock(){return fixture::clock;}
std::optional<std::array<float,3>> sa_camera_placeable_position(){return std::array<float,3>{0,0,0};}
std::optional<std::array<float,3>> sa_focused_player_position(){return std::array<float,3>{0,0,0};}
std::optional<SpatialListener> sa_phone_camera_listener(){return SpatialListener{};}
std::optional<bool> sa_phone_slow_motion(){return fixture::slow;}
bool sa_phone_random_ready(){return true;}
std::optional<NativePhoneBooth> sa_phone_booth_snapshot(int32_t model,const std::array<float,3>& position){
    if(!fixture::present)return NativePhoneBooth{};
    if(model!=10000 || position!=fixture::booth.position)return {};
    return fixture::booth;
}
std::optional<NativePhoneRenderSnapshot> sa_phone_render_snapshot(const NativePhoneBooth& before){
    if(!fixture::render_ok || before.entity!=fixture::booth.entity)return {};
    return NativePhoneRenderSnapshot{fixture::booth,40,50,60};
}
NativeResult sa_phone_random_sample(){++fixture::random_calls;fixture::effects+='R';NativeResult r;r.executed=r.native_entered=true;r.values[0]=1023;return r;}
NativeResult sa_phone_write_up_z(const NativePhoneRenderSnapshot& before,float up){
    NativeResult r;if(before.booth.up_z!=fixture::booth.up_z)return r;
    fixture::effects+='U';fixture::booth.up_z=up;r.executed=r.native_entered=true;return r;
}
NativeResult sa_phone_refresh_visual(const NativePhoneRenderSnapshot& before){
    NativeResult r;if(before.booth.up_z!=fixture::booth.up_z)return r;
    fixture::effects+='F';r.executed=r.native_entered=true;return r;
}
}
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try {
    lcs::PhoneFrameCoordinator state;lcs::AudioSample sample{22050,{0,0,1,0},1};size_t plays=0;
    auto play=[&](const lcs::AudioSample& prepared,lcs::AudioMix mix){
        ++plays;fixture::effects+='A';check(prepared.frequency==16538 && mix.volume==80 && mix.pan==63,"Static provider passes actual prepared frequency and spatial mix");
        return lcs::PhoneEffectReceipt{true,true};};
    check(lcs::sa_update_phone_frame(10000,{0,0,0},state,sample,play)==lcs::PhoneFrameResult::accepted && fixture::effects.empty(),"Native provider activation does not emit in same frame");
    fixture::clock={1880,1879,1};fixture::slow=true;
    check(lcs::sa_update_phone_frame(10000,{0,0,0},state,sample,play)==lcs::PhoneFrameResult::accepted && fixture::effects=="RUAF" && plays==1,"Production provider composes actual RNG/CPU/audio/RW services in original order");
    check(lcs::sa_update_phone_frame(10000,{0,0,0},state,sample,play)==lcs::PhoneFrameResult::accepted && plays==1,"Native provider cannot replay a completed frame");
    fixture::clock.frame=2;fixture::render_ok=false;const auto before=fixture::random_calls;
    check(lcs::sa_update_phone_frame(10000,{0,0,0},state,sample,play)==lcs::PhoneFrameResult::deferred && fixture::random_calls==before,"Missing render linkage refuses before RNG");
    fixture::render_ok=true;fixture::present=false;
    check(lcs::sa_update_phone_frame(10000,{0,0,0},state,sample,play)==lcs::PhoneFrameResult::deferred && fixture::random_calls==before,"Missing streamed booth performs no effects");
    fixture::present=true;sample.channels=0;
    check(lcs::sa_update_phone_frame(10000,{0,0,0},state,sample,play)==lcs::PhoneFrameResult::deferred && fixture::random_calls==before,"Malformed PCM refuses before original RNG or booth mutation");
    std::cout<<"PASS: production phone provider native observation composition, source order and preflight; mocked engine/output only\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
