#include "../src/sa_crusher.hpp"
#include <array>
#include <cstring>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace lcs;
static bool engine_ready=false,release_blocked=false;
static std::optional<RouteVehicleSnapshot> current_vehicle;
static uintptr_t* registered_cell=nullptr;
namespace lcs {
bool sa_lcs_bridge_profile_matches(){return engine_ready;}
std::optional<uintptr_t> sa_garage_ped_identity(int32_t){return {};}
RouteVehicleLookup sa_route_vehicle_lookup(int32_t handle){
    RouteVehicleLookup result;
    if(current_vehicle && current_vehicle->handle==handle){result.status=VehicleLookupStatus::present;result.snapshot=current_vehicle;}
    return result;
}
bool sa_register_vehicle_reference(int32_t handle,uintptr_t* cell){
    if(!current_vehicle || current_vehicle->handle!=handle || *cell)return false;
    *cell=current_vehicle->entity;registered_cell=cell;return true;
}
bool sa_vehicle_reference_live(const RouteVehicleSnapshot& expected,const uintptr_t* cell){
    return current_vehicle && current_vehicle->handle==expected.handle && current_vehicle->entity==expected.entity &&
        current_vehicle->generation==expected.generation && cell==registered_cell && *cell==expected.entity;
}
bool sa_release_vehicle_reference(const RouteVehicleSnapshot&,uintptr_t* cell){
    if(release_blocked)return false;
    *cell=0;registered_cell=nullptr;return true;
}
}
static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class T>static void put(unsigned char* at,T value){std::memcpy(at,&value,sizeof(value));}
int main() {
    std::array<unsigned char,0x38*3> entities{};
    std::array<unsigned char,3> flags{{1,1,0x80}};
    std::array<uintptr_t,3> pool{};
    pool[0]=reinterpret_cast<uintptr_t>(entities.data());pool[1]=reinterpret_cast<uintptr_t>(flags.data());pool[2]=3;
    auto pool_ptr=reinterpret_cast<uintptr_t>(pool.data());
    const auto root=reinterpret_cast<uintptr_t>(&pool_ptr);
    auto* body=entities.data();auto* lid=body+0x38;
    put<uint16_t>(body+0x22,10654);put<uint16_t>(lid+0x22,11326);
    body[0x36]=lid[0x36]=1;
    const float body_at[]={1139.728759765625f,60.38515090942383f,1.098055362701416f};
    const float lid_at[]={1139.548583984375f,65.05116271972656f,4.590259552001953f};
    std::memcpy(body+4,body_at,12);std::memcpy(lid+4,lid_at,12);
    SaCrusherEntities resolved;
    check(sa_crusher_resolve(root,10654,11326,resolved)==SaCrusherResolve::ready &&
        resolved.body==reinterpret_cast<uintptr_t>(body) && resolved.lid==reinterpret_cast<uintptr_t>(lid),
        "Physical crusher resolves both exact mounted IPL models");
    flags[1]=0x80;
    check(sa_crusher_resolve(root,10654,11326,resolved)==SaCrusherResolve::streamed_out,"A streamed-out lid is not permission to animate the body or another entity");
    flags[1]=1;lid[0x36]=4;
    check(sa_crusher_resolve(root,10654,11326,resolved)==SaCrusherResolve::unavailable,"A foreign object using the same model is refused");
    lid[0x36]=1;put<float>(lid+4,lid_at[0]+1.0f);
    check(sa_crusher_resolve(root,10654,11326,resolved)==SaCrusherResolve::unavailable,"Model ID alone cannot claim a foreign placement");
    std::memcpy(lid+4,lid_at,12);
    std::memcpy(lid+0x38,lid,0x38);flags[2]=1;
    check(sa_crusher_resolve(root,10654,11326,resolved)==SaCrusherResolve::unavailable,"Duplicate owned lid placements refuse ambiguous mutation");
    flags[2]=0x80;
    std::array<unsigned char,0x40> matrix{};std::memcpy(matrix.data()+0x30,lid_at,12);
    put<uintptr_t>(lid+0x14,reinterpret_cast<uintptr_t>(matrix.data()));
    check(sa_crusher_resolve(root,10654,11326,resolved)==SaCrusherResolve::ready,"Native allocated matrix position is observed instead of stale simple coordinates");
    auto bad=std::array<float,8>{};
    check(!sa_crusher_door(3,bad,0).executed,"No physical producer without configured ownership/profile");
    check(!sa_crusher_configure(11326,11326) && sa_crusher_configure(10654,11326),"Model configuration rejects aliases and accepts the approved pair");
    alignas(4) std::array<unsigned char,0x990> vehicle{};
    RouteVehicleSnapshot identity;identity.handle=0x101;identity.generation=1;identity.entity=reinterpret_cast<uintptr_t>(vehicle.data());
    identity.alive=true;identity.health=1000.0f;identity.native_class=0;
    current_vehicle=identity;vehicle[0x1C]=0xA5;vehicle[0x40]=0x5B;
    put<float>(vehicle.data()+0x44,1.0f);put<float>(vehicle.data()+0x48,2.0f);put<float>(vehicle.data()+0x4C,3.0f);
    check(!sa_crusher_hold(identity).executed && !registered_cell,"Unknown profile cannot mutate vehicle flags");
    engine_ready=true;
    check(sa_crusher_collector_vehicle_deliverable(identity)==std::optional<bool>(true),
        "A healthy live automobile with no wreck, engine damage, or fire is deliverable");
    vehicle[0x981]=1;
    check(sa_crusher_collector_vehicle_deliverable(identity)==std::optional<bool>(false),
        "SA automobile fire-particle state blocks collector delivery");
    vehicle[0x981]=0;vehicle[0x5A4]=225;
    check(sa_crusher_collector_vehicle_deliverable(identity)==std::optional<bool>(false),
        "SA engine damage status 225 blocks collector delivery");
    vehicle[0x5A4]=0;vehicle[0x36]=uint8_t(5<<3);
    check(sa_crusher_collector_vehicle_deliverable(identity)==std::optional<bool>(false),
        "Wrecked native status blocks collector delivery");
    vehicle[0x36]=0;
    identity.health=249.0f;
    check(sa_crusher_collector_vehicle_deliverable(identity)==std::optional<bool>(false),
        "Retail 250-health delivery threshold blocks damaged vehicles");
    identity.health=1000.0f;
    check(sa_crusher_hold(identity,2.0f,false).executed && vehicle[0x1C]==0xA5 && vehicle[0x40]==0x5B,
        "Early closing dampens native motion without disabling physics prematurely");
    float slowed[3];std::memcpy(slowed,vehicle.data()+0x44,12);
    check(std::fabs(slowed[0]-0.64f)<0.00001f && std::fabs(slowed[2]-1.92f)<0.00001f,
        "Source0.8^timeStep braking scales actual native velocity");
    const auto held=sa_crusher_hold(identity);
    check(held.executed && held.native_entered && registered_cell && vehicle[0x1C]==0xA4 && vehicle[0x40]==0x59,
        "Crusher holds exact vehicle collision and gravity bits through its stable native weak reference");
    float velocity[3];std::memcpy(velocity,vehicle.data()+0x44,12);
    check(velocity[0]==0 && velocity[1]==0 && velocity[2]==0,"Crusher zeroes all three speed components");
    vehicle[0x1C]^=0x40;vehicle[0x40]^=0x80;
    auto foreign=identity;foreign.entity+=4;
    check(!sa_crusher_hold(foreign).executed,"A replacement entity cannot inherit physical ownership");
    release_blocked=true;
    check(!sa_crusher_release_vehicle() && registered_cell,"Uncertain reference cleanup retains its stable cell");
    check(vehicle[0x1C]==0xE5 && vehicle[0x40]==0xDB,"Cleanup restores only owned bits and preserves unrelated mod flags");
    release_blocked=false;
    check(sa_crusher_release_vehicle() && !registered_cell,"Reference cleanup can complete on a later retry");
    check(sa_crusher_hold(identity).executed,"The released target can be held again");
    *registered_cell=0;current_vehicle.reset();
    check(sa_crusher_release_vehicle() && !registered_cell,"Native vehicle deletion releases the cell without dereferencing a retired entity");
    std::cout<<"Physical crusher pool ownership contracts passed\n";
}
