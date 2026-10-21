#include "../src/sa_crane.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace lcs;
static bool engine_ready=false,release_blocked=false;
static std::optional<RouteVehicleSnapshot> current_vehicle;
static uintptr_t* registered_cell=nullptr;
namespace lcs {
bool sa_lcs_bridge_profile_matches(){return engine_ready;}
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
template<class T>static T get(const unsigned char* at){T value;std::memcpy(&value,at,sizeof(value));return value;}

int main() {
    constexpr uint16_t model=11351;
    const std::array<float,3> at{1118.984f,52.417f,10.270f};
    std::array<unsigned char,0x38*3> entities{};
    std::array<unsigned char,3> flags{{1,1,0x80}};
    std::array<uintptr_t,3> pool{};
    pool[0]=reinterpret_cast<uintptr_t>(entities.data());pool[1]=reinterpret_cast<uintptr_t>(flags.data());pool[2]=3;
    auto pool_ptr=reinterpret_cast<uintptr_t>(pool.data());
    const auto root=reinterpret_cast<uintptr_t>(&pool_ptr);
    auto* crane=entities.data();
    put<uint16_t>(crane+0x22,model);crane[0x36]=1;std::memcpy(crane+4,at.data(),12);
    sa_crane_test_pool_root(root);

    check(!sa_crane_configure(0,at) && !sa_crane_configure(20000,at) && sa_crane_configure(model,at),"Crane model configuration");
    uintptr_t resolved=0;
    check(sa_crane_resolve(root,model,at,resolved)==SaCraneResolve::ready && resolved==reinterpret_cast<uintptr_t>(crane),"The exact mounted crane resolves");
    flags[0]=0x80;
    check(sa_crane_resolve(root,model,at,resolved)==SaCraneResolve::streamed_out,"A streamed-out crane is a benign pause");
    flags[0]=1;crane[0x36]=4;
    check(sa_crane_resolve(root,model,at,resolved)==SaCraneResolve::unavailable,"A foreign pool class using the model is refused");
    crane[0x36]=1;put<float>(crane+4,at[0]+5.0f);
    check(sa_crane_resolve(root,model,at,resolved)==SaCraneResolve::unavailable,"A foreign placement cannot claim the crane");
    std::memcpy(crane+4,at.data(),12);
    std::memcpy(crane+0x38,crane,0x38);flags[1]=1;
    check(sa_crane_resolve(root,model,at,resolved)==SaCraneResolve::unavailable,"Duplicate crane placements refuse ambiguity");
    flags[1]=0x80;

    check(!sa_crane_ready(0),"No verified engine profile, no crane");
    engine_ready=true;
    check(sa_crane_ready(0)==std::optional<bool>(true) && !sa_crane_ready(1),"Only crane zero is mounted");
    check(!sa_crane_rotate(1,1.0f).executed && !sa_crane_rotate(0,-1.0f).executed && !sa_crane_rotate(0,7.0f).executed,"Out-of-range crane addressing is refused before entry");
    flags[0]=0x80;
    const auto streamed=sa_crane_rotate(0,1.0f);
    check(!streamed.executed && !streamed.native_entered,"A streamed-out crane is refused before native entry");
    flags[0]=1;
    // Matrix-less building: the placement heading is the owned state.
    auto turned=sa_crane_rotate(0,1.25f);
    check(turned.executed && turned.native_entered && std::fabs(get<float>(crane+0x10)-1.25f)<0.0001f && get<float>(crane+4)==at[0],
        "The placement heading follows the hook angle without moving the crane");
    std::array<unsigned char,0x40> matrix{};std::memcpy(matrix.data()+0x30,at.data(),12);
    put<uintptr_t>(crane+0x14,reinterpret_cast<uintptr_t>(matrix.data()));
    turned=sa_crane_rotate(0,0.5f);
    check(turned.executed && std::fabs(get<float>(matrix.data())-std::cos(0.5f))<0.0001f && std::fabs(get<float>(matrix.data()+4)-std::sin(0.5f))<0.0001f &&
          std::fabs(get<float>(matrix.data()+0x10)+std::sin(0.5f))<0.0001f && get<float>(matrix.data()+0x30)==at[0],
        "An allocated matrix is rotated about Z only");
    check(!sa_crane_configure(model,{at[0]+1,at[1],at[2]}),"A turned crane cannot be rebound to another placement");
    check(sa_crane_cleanup(),"Cleanup returns the crane to the IPL pose");
    check(std::fabs(get<float>(matrix.data())-1.0f)<0.0001f,"Cleanup restores identity orientation");

    // Vehicle: model info -> collision model -> box max Z; forward from the matrix; held pose.
    alignas(4) std::array<unsigned char,0x40> info{};alignas(4) std::array<unsigned char,0x40> col{};
    put<uintptr_t>(info.data()+0x14,reinterpret_cast<uintptr_t>(col.data()));put<float>(col.data()+0x14,1.45f);
    sa_crane_test_model_info(reinterpret_cast<uintptr_t>(info.data()));
    alignas(4) std::array<unsigned char,0x100> vehicle{};
    alignas(4) std::array<unsigned char,0x40> vmatrix{};
    put<uintptr_t>(vehicle.data()+0x14,reinterpret_cast<uintptr_t>(vmatrix.data()));
    put<float>(vmatrix.data()+0x10,0.0f);put<float>(vmatrix.data()+0x14,1.0f);        // forward +Y: angle pi/2
    RouteVehicleSnapshot identity;identity.handle=0x101;identity.generation=1;identity.entity=reinterpret_cast<uintptr_t>(vehicle.data());
    check(!sa_crane_vehicle_top(identity) && !sa_crane_vehicle_forward_angle(identity),"Facts about an absent vehicle are unavailable");
    current_vehicle=identity;
    const auto top=sa_crane_vehicle_top(identity);
    check(top && std::fabs(*top-1.45f)<0.0001f,"The crane reads the collision box top of the exact vehicle");
    const auto forward=sa_crane_vehicle_forward_angle(identity);
    check(forward && std::fabs(*forward-1.57079633f)<0.0001f,"The forward angle comes from the matrix");
    vehicle[0x1C]=0xA5;vehicle[0x40]=0x5B;put<float>(vehicle.data()+0x44,3.0f);put<float>(vehicle.data()+0x50,2.0f);
    engine_ready=false;
    check(!sa_crane_hold(identity).executed && !registered_cell,"Unknown profile cannot hold a vehicle");
    engine_ready=true;
    const auto held=sa_crane_hold(identity);
    check(held.executed && held.native_entered && registered_cell && vehicle[0x1C]==0xA4 && vehicle[0x40]==0x59,"Hold suspends collision and gravity bits");
    check(get<float>(vehicle.data()+0x44)==0 && get<float>(vehicle.data()+0x50)==0,"Hold zeroes move and turn speed");
    auto foreign=identity;foreign.entity+=4;
    check(!sa_crane_hold(foreign).executed,"A replacement entity cannot inherit the hold");
    check(!sa_crane_carry(foreign,{1,2,3},0,{0,0,1}).executed,"Only the held vehicle can be carried");
    check(!sa_crane_carry(identity,{1,2,3},0,{0,0,0}).executed,"A degenerate orientation is refused");
    const auto carried=sa_crane_carry(identity,{1,2,3},0.0f,{0,0,1});
    check(carried.executed && get<float>(vmatrix.data()+0x30)==1 && get<float>(vmatrix.data()+0x34)==2 && get<float>(vmatrix.data()+0x38)==3,"Carry writes the hook position");
    check(std::fabs(get<float>(vmatrix.data())-0.0f)<0.0001f && std::fabs(get<float>(vmatrix.data()+4)+1.0f)<0.0001f &&
          std::fabs(get<float>(vmatrix.data()+0x10)-1.0f)<0.0001f && get<float>(vmatrix.data()+0x28)==1.0f,
        "Retail Right = Forward x Up and Forward = Up x Right for an upright car heading along +X");
    vehicle[0x1C]^=0x40;vehicle[0x40]^=0x80;
    release_blocked=true;
    check(!sa_crane_release() && registered_cell,"Uncertain reference cleanup retains its cell");
    check(vehicle[0x1C]==0xE1 && vehicle[0x40]==0xDB,
          "Drop restores owned collision/gravity, preserves unrelated flags, and wakes the vehicle through SetIsStatic(false)");
    release_blocked=false;
    check(sa_crane_release() && !registered_cell,"Release completes on a later retry");
    check(sa_crane_hold(identity).executed,"A released vehicle can be held again");
    *registered_cell=0;current_vehicle.reset();
    check(sa_crane_release() && !registered_cell,"Deleting the vehicle releases the cell without dereferencing it");
    std::cout<<"Crusher crane native pose and hold contracts passed\n";
}
