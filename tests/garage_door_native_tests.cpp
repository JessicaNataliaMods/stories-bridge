#include "../src/sa_garage_door.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace lcs;
static bool engine_ready=false;
namespace lcs {
bool sa_lcs_bridge_profile_matches(){return engine_ready;}
}
static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class T>static void put(unsigned char* at,T value){std::memcpy(at,&value,sizeof(value));}
template<class T>static T get(const unsigned char* at){T value;std::memcpy(&value,at,sizeof(value));return value;}

int main() {try {
    constexpr uint16_t model=2091;
    const std::array<float,3> at{73.61499786376953f,-1507.8719482421875f,25.645153045654297f};
    // Retail garage geometry of the single type 8 declaration, in SET_GARAGE operand order.
    const std::array<float,8> garage{73.63f,-1514.44f,21.0f,73.63f,-1501.3f,86.75f,-1514.44f,30.0f};
    std::array<unsigned char,0x38*3> entities{};
    std::array<unsigned char,3> flags{{1,1,0x80}};
    std::array<uintptr_t,3> pool{};
    pool[0]=reinterpret_cast<uintptr_t>(entities.data());pool[1]=reinterpret_cast<uintptr_t>(flags.data());pool[2]=3;
    auto pool_ptr=reinterpret_cast<uintptr_t>(pool.data());
    const auto root=reinterpret_cast<uintptr_t>(&pool_ptr);
    auto* door=entities.data();
    put<uint16_t>(door+0x22,model);door[0x36]=1;std::memcpy(door+4,at.data(),12);
    sa_garage_door_test_pool_root(root);

    uintptr_t resolved=0;
    check(!sa_garage_door_configure(0,at) && !sa_garage_door_configure(20000,at),"Door configuration rejects invalid model ids");
    check(sa_garage_door_configure(model,at),"Door configuration accepts the exact converted placement");
    const auto status_without_object=sa_garage_door_runtime_statuses();
    check(status_without_object.size()==1 && !status_without_object[0].object_pool_available &&
        !status_without_object[0].object_sample_available && !status_without_object[0].has_dummy_known &&
        !status_without_object[0].native_garage_state_available,
        "Runtime object and garage fields stay unknown without test fixtures");

    check(sa_garage_door_resolve(root,model,at,resolved)==SaGarageDoorResolve::ready &&
        resolved==reinterpret_cast<uintptr_t>(door),"The exact mounted door building resolves");
    auto* other_door=entities.data()+0x38;
    put<uint16_t>(other_door+0x22,model);other_door[0x36]=1;
    const std::array<float,3> other_at{500.0f,500.0f,21.0f};
    std::memcpy(other_door+4,other_at.data(),12);flags[1]=1;
    check(sa_garage_door_resolve(root,model,at,resolved)==SaGarageDoorResolve::ready &&
        resolved==reinterpret_cast<uintptr_t>(door),"Another placement of the same model does not shadow this exact door");
    flags[1]=0x80;
    flags[0]=0x80;
    check(sa_garage_door_resolve(root,model,at,resolved)==SaGarageDoorResolve::streamed_out && !resolved,
        "A streamed-out door is a pause, not permission to move another entity");
    flags[0]=1;door[0x36]=4;
    check(sa_garage_door_resolve(root,model,at,resolved)==SaGarageDoorResolve::unavailable,"A foreign pool class using the model id is refused");
    door[0x36]=1;put<float>(door+4,at[0]+1.0f);
    check(sa_garage_door_resolve(root,model,at,resolved)==SaGarageDoorResolve::streamed_out,"Model id alone cannot claim another placement");
    std::memcpy(door+4,at.data(),12);
    put<float>(door+12,at[2]+30.0f);
    check(sa_garage_door_resolve(root,model,at,resolved)==SaGarageDoorResolve::streamed_out,"A door far above the retail travel is not owned");
    std::memcpy(door+4,at.data(),12);
    std::memcpy(other_door,door,0x38);flags[1]=1;
    check(sa_garage_door_resolve(root,model,at,resolved)==SaGarageDoorResolve::unavailable,"Duplicate placements refuse ambiguous mutation");
    flags[1]=0x80;
    std::array<unsigned char,0x40> matrix{};
    const float lifted[]={at[0],at[1],at[2]+2.5f};
    std::memcpy(matrix.data()+0x30,lifted,12);
    put<uintptr_t>(door+0x14,reinterpret_cast<uintptr_t>(matrix.data()));
    check(sa_garage_door_resolve(root,model,at,resolved)==SaGarageDoorResolve::ready,
        "The native allocated matrix position is observed, so a half-open door is still the owned door");
    put<uintptr_t>(door+0x14,0);

    // FindDoorHeightForMI over the live collision model.
    check(!sa_garage_door_height(garage),"No profile, no height");
    engine_ready=true;
    check(!sa_garage_door_height(garage),"Unavailable model info is a pause, not a guessed 4.0");
    alignas(4) std::array<unsigned char,0x40> info{};
    alignas(4) std::array<unsigned char,0x40> col{};
    put<uintptr_t>(info.data()+0x14,reinterpret_cast<uintptr_t>(col.data()));
    put<float>(col.data()+8,-0.2f);put<float>(col.data()+0x14,4.0f);
    sa_garage_door_test_model_info(reinterpret_cast<uintptr_t>(info.data()));
    const auto height=sa_garage_door_height(garage);
    check(height && std::fabs(*height-4.1f)<0.0001f,"Door height is the collision box height minus 0.1");
    const std::array<float,8> far_garage{500,500,21,500,513,86,513,500};
    check(!sa_garage_door_height(far_garage),"A garage outside the retail 20 unit door window cannot use the door");
    put<float>(col.data()+0x14,-1.0f);
    check(!sa_garage_door_height(garage),"An inverted collision box is refused");
    put<float>(col.data()+0x14,40.0f);
    check(!sa_garage_door_height(garage),"An implausible door height is refused");
    put<float>(col.data()+0x14,4.0f);

    // UpdateDoorsHeight: placement Z plus the door position.
    engine_ready=false;
    check(!sa_garage_door_move(garage,1.0f).executed && get<float>(door+12)==at[2],"Unknown native profile cannot mutate the door");
    engine_ready=true;
    check(!sa_garage_door_move(garage,-0.5f).executed && !sa_garage_door_move(garage,13.0f).executed &&
        !sa_garage_door_move(far_garage,1.0f).executed && get<float>(door+12)==at[2],
        "Positions outside the retail travel and garages that do not own the door are refused before entry");
    flags[0]=0x80;
    const auto streamed=sa_garage_door_move(garage,1.0f);
    check(!streamed.executed && !streamed.native_entered,"A streamed-out door is refused before native entry");
    flags[0]=1;
    const auto moved=sa_garage_door_move(garage,3.5f);
    check(moved.executed && moved.native_entered && std::fabs(get<float>(door+12)-(at[2]+3.5f))<0.0001f &&
        get<float>(door+4)==at[0] && get<float>(door+8)==at[1],"The door translates only along Z from the IPL placement");
    check(sa_garage_door_move(garage,0.0f).executed && std::fabs(get<float>(door+12)-at[2])<0.0001f,"A closing door returns to the IPL height");
    check(sa_garage_door_move(garage,2.0f).executed,"Reopened door");
    check(sa_garage_door_configure(model,other_at) && sa_garage_door_configure(model,other_at),
        "A second exact map placement can be added idempotently without rebinding the moved door");
    check(sa_garage_door_cleanup() && std::fabs(get<float>(door+12)-at[2])<0.0001f,"Cleanup restores the approved IPL pose");
    check(sa_garage_door_cleanup(),"Cleanup is idempotent");
    check(sa_garage_door_move(garage,1.0f).executed,"Door can be moved again after cleanup");
    flags[0]=0x80;
    check(sa_garage_door_cleanup(),"A streamed-out door needs no cleanup");
    flags[0]=1;

    // A promoted IPL instance is a CObject in a different pool with a different stride.
    const std::array<float,3> promoted_at{900.0f,900.0f,21.0f};
    std::array<unsigned char,0x19C> object{};
    std::array<unsigned char,1> object_flags{{1}};
    std::array<uintptr_t,3> object_pool{};
    object_pool[0]=reinterpret_cast<uintptr_t>(object.data());
    object_pool[1]=reinterpret_cast<uintptr_t>(object_flags.data());object_pool[2]=1;
    auto object_pool_ptr=reinterpret_cast<uintptr_t>(object_pool.data());
    const auto object_root=reinterpret_cast<uintptr_t>(&object_pool_ptr);
    put<uint16_t>(object.data()+0x22,model);object[0x36]=4;
    std::memcpy(object.data()+4,promoted_at.data(),12);
    std::array<unsigned char,0x38> dummy{};
    const float dummy_position[]={promoted_at[0],promoted_at[1],promoted_at[2]+0.5f};
    std::memcpy(dummy.data()+4,dummy_position,sizeof(dummy_position));
    put<uintptr_t>(object.data()+0x138,uintptr_t(0x1234));
    put<int8_t>(object.data()+0x147,int8_t(1));
    put<uintptr_t>(object.data()+0x170,reinterpret_cast<uintptr_t>(dummy.data()));
    put<float>(object.data()+12,promoted_at[2]+2.25f);
    sa_garage_door_test_object_pool_root(object_root);
    std::array<uintptr_t,3> dummy_pool{};
    dummy_pool[0]=reinterpret_cast<uintptr_t>(dummy.data());
    std::array<unsigned char,1> dummy_flags{{1}};
    dummy_pool[1]=reinterpret_cast<uintptr_t>(dummy_flags.data());dummy_pool[2]=1;
    auto dummy_pool_ptr=reinterpret_cast<uintptr_t>(dummy_pool.data());
    sa_garage_door_test_dummy_pool_root(reinterpret_cast<uintptr_t>(&dummy_pool_ptr));
    check(sa_garage_door_resolve(object_root,model,promoted_at,resolved,SaGarageDoorEntityPool::object)==SaGarageDoorResolve::ready &&
        resolved==reinterpret_cast<uintptr_t>(object.data()),"A promoted CObject resolves through its own pool and stride");
    check(sa_garage_door_configure(model,promoted_at,SaGarageDoorEntityPool::object),"A promoted placement is explicitly configured as an object");
    auto promoted_statuses=sa_garage_door_runtime_statuses();
    bool found_unknown_garage=false;
    for(const auto& status:promoted_statuses)if(status.placement==promoted_at) {
        found_unknown_garage=status.object_pool_available && status.objects==1 &&
            status.object_sample_available && status.object_z_available &&
            std::fabs(status.object_z-(promoted_at[2]+2.25f))<0.0001f &&
            status.has_dummy_known && status.has_dummy && status.dummy_z_available &&
            std::fabs(status.dummy_z-(promoted_at[2]+0.5f))<0.0001f &&
            status.control_code_known && status.control_code_linked &&
            status.native_garage_index==1 && !status.native_garage_state_available;
    }
    check(found_unknown_garage,"A unique object snapshot is pointer-free and native garage state is unknown without a fixture");
    alignas(4) std::array<unsigned char,0xD8*50> garages{};
    auto* pns=garages.data()+0xD8;
    put<float>(pns+0x38,2.25f);pns[0x4D]=3;pns[0x4E]=0x08;
    sa_garage_door_test_native_garages(reinterpret_cast<uintptr_t>(garages.data()));
    promoted_statuses=sa_garage_door_runtime_statuses();
    bool found_native_garage=false;
    for(const auto& status:promoted_statuses)if(status.placement==promoted_at) {
        found_native_garage=status.native_garage_state_available && status.native_garage_state==3 &&
            std::fabs(status.native_garage_progress-2.25f)<0.0001f && status.native_garage_flags==0x08;
    }
    check(found_native_garage,"A bounded garage index reads only POD state, progress, and flags from the fixture");
    put<float>(object.data()+12,promoted_at[2]);
    const std::array<float,8> promoted_garage{895,895,20,895,905,905,895,30};
    check(sa_garage_door_height(promoted_garage).has_value(),"The promoted door uses the same live collision height");
    const auto moved_object=sa_garage_door_move(promoted_garage,1.0f);
    check(moved_object.executed && moved_object.native_entered && get<float>(object.data()+12)==promoted_at[2]+1.0f &&
        sa_garage_door_cleanup() && get<float>(object.data()+12)==promoted_at[2],
        "A promoted CObject moves and restores through its explicit pool binding");

    // CGarages::CarTypesCollected: SA's own saved array.
    check(!sa_garage_collected_read(0) && !sa_garage_collected_write(0,1),"No verified SA array, no import list");
    alignas(4) std::array<uint32_t,5> list{{0,0,0,0,0xCAFEF00D}};
    sa_garage_door_test_collected(reinterpret_cast<uintptr_t>(list.data()));
    check(sa_garage_collected_read(0)==std::optional<uint32_t>(0),"The saved list starts empty");
    check(sa_garage_collected_write(0,0x8001) && list[0]==0x8001 && sa_garage_collected_read(0)==std::optional<uint32_t>(0x8001),
        "A delivery bit is written to and confirmed from the native array");
    check(!sa_garage_collected_read(4) && !sa_garage_collected_write(4,1) && list[4]==0xCAFEF00D,
        "Only the four SA slots can be addressed");
    list[0]=0;
    check(sa_garage_collected_read(0)==std::optional<uint32_t>(0),"A loaded save is visible on the next read");
    std::cout<<"Import garage door and saved-list contracts passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
