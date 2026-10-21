#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

namespace lcs {
// Explicit source operations, never an SA opcode with different units or
// coordinate space, an incompatible enum, or an ignored argument.
enum class EntityCommand : uint8_t {
    car_move_speed,car_turn_speed,object_turn_speed,
    get_car_orientation,set_car_orientation,get_object_orientation,set_object_orientation,
    car_alarm,object_render_damaged,get_out_of_jail_free,
    random_car_unchecked,random_car_no_save,
    player_fireproof,player_max_health,player_max_armour,
    radar_brightness,car_is_boat,car_is_bike,car_being_jacked,
    boat_in_water,heli_rotor_full_speed,get_pickup_value,set_pickup_value,
    char_drowning,first_person_weapon_camera,player_clip_ammo,remove_car_boot,
    photograph_taken,credits_finished,activate_save_menu,save_game_finished,enable_control_camera,char_locomotion_group,
    reset_player_havoc,get_player_havoc,
    attach_object_car,detach_object_car,heli_goto,car_ram_car,char_attach_object,
    car_collision,car_wreck,char_wander_path,char_leaving_vehicle_to_die,blow_up_rc_buggy,clear_area_objects,
    cancel_remote_mode,create_remote_car,override_remote_car,char_in_chars_group,restart_critical_mission
};
inline float entity_float(int32_t bits) {float v;std::memcpy(&v,&bits,4);return v;}
inline int32_t entity_bits(float v) {int32_t bits;std::memcpy(&bits,&v,4);return bits;}
// CLEAR_AREA_OF_OBJECTS uses the entity origin, inclusive XYZ bounds,
// independently normalized on all three axes (not bounding-box overlap).
inline std::optional<std::array<float,6>> source_clear_object_box(const int32_t* values) {
    std::array<float,6> result{};
    for(size_t i=0;i<6;++i){result[i]=entity_float(values[i]);if(!std::isfinite(result[i]))return {};}
    for(size_t i=0;i<3;++i)if(result[i]>result[i+3])std::swap(result[i],result[i+3]);
    return result;
}
inline bool source_clear_object_contains(const std::array<float,6>& box,const std::array<float,3>& point) {
    for(size_t i=0;i<3;++i)if(!std::isfinite(box[i]) || !std::isfinite(box[i+3]) || box[i]>box[i+3] || !std::isfinite(point[i]) || point[i]<box[i] || point[i]>box[i+3])return false;
    return true;
}
inline uint8_t source_player_maximum(uint8_t current,int32_t amount) {
    return uint8_t(uint32_t(current)+uint32_t(amount));
}
inline std::optional<std::array<float,3>> source_vector(const int32_t* values,float divisor=1.f) {
    std::array<float,3> result{};
    for(size_t i=0;i<3;++i){result[i]=entity_float(values[i])/divisor;if(!std::isfinite(result[i]))return {};}
    return result;
}
inline std::optional<int32_t> source_vehicle_status(int32_t status) {
    // LCS inserted two ferry states before HELI. SA has no ferry controller.
    if(status>=0 && status<=7)return status;
    if(status>=10 && status<=14)return status-2;
    return {};
}
inline std::optional<std::array<float,3>> source_orientation(const std::array<float,3>& right,
        const std::array<float,3>& forward,const std::array<float,3>& up) {
    for(const auto& v:{right,forward,up})for(auto x:v)if(!std::isfinite(x))return {};
    if(std::fabs(forward[2])>1.0001f)return {};
    constexpr float degrees=180.f/3.14159265358979323846f;
    std::array<float,3> a{{std::atan2(-right[2],up[2])*degrees,
        std::asin(std::fmax(-1.f,std::fmin(1.f,forward[2])))*degrees,
        std::atan2(-forward[0],forward[1])*degrees}};
    for(auto& value:a)if(value<0)value+=360.f;
    return a;
}
inline std::optional<float> source_angle_between_points(const std::array<float,4>& p) {
    for(auto value:p)if(!std::isfinite(value))return {};
    const auto x=p[2]-p[0];auto y=p[3]-p[1];
    if(!std::isfinite(x) || !std::isfinite(y))return {};
    if(y==0.f)y=.0001f;
    const auto a=std::atan2(x/y,1.f);constexpr float pi=3.14159265358979323846f;
    return ((x>0.f)?(y>0.f?pi-a:-a):(y>0.f?-(pi+a):-a))*(180.f/pi);
}
// Retail CPools::GetAt indexes the generation array without a bounds check.
inline bool valid_entity_slot(int32_t handle,int32_t size,const uint8_t* generations) {
    return handle>=0 && size>0 && generations && (uint32_t(handle)>>8)<uint32_t(size) &&
        !(generations[uint32_t(handle)>>8]&0x80) && generations[uint32_t(handle)>>8]==uint8_t(handle);
}
inline bool source_car_in_area(float x,float y,const std::array<float,4>& bounds) {
    for(auto v:bounds)if(!std::isfinite(v))return false;
    return std::isfinite(x) && std::isfinite(y) && x>=std::fmin(bounds[0],bounds[2]) && x<=std::fmax(bounds[0],bounds[2]) &&
        y>=std::fmin(bounds[1],bounds[3]) && y<=std::fmax(bounds[1],bounds[3]);
}
inline std::optional<size_t> source_radar_slot(int32_t handle,const uint8_t* traces,size_t bytes) {
    const auto bits=uint32_t(handle);const size_t slot=bits&0xFFFF;
    if(bits==0xFFFFFFFF || slot>=175 || !traces || bytes<(slot+1)*0x28)return {};
    const auto* trace=traces+slot*0x28;uint16_t generation=0;std::memcpy(&generation,trace+0x14,2);
    return (trace[0x25]&2) && generation==uint16_t(bits>>16)?std::optional<size_t>(slot):std::nullopt;
}
inline uint8_t source_radar_brightness(uint8_t flags,int32_t brightness) {
    // Both source m_bDim (despite its name) and native m_bBright feed the
    // renderer's bright-colour argument. The original command clears it at 1.
    return brightness==1?uint8_t(flags&~1u):uint8_t(flags|1u);
}
inline std::optional<size_t> source_pickup_slot(int32_t handle,const uint8_t* pickups,size_t bytes) {
    const auto bits=uint32_t(handle);const size_t slot=bits&0xFFFF;
    if(bits==0xFFFFFFFF || slot>=620 || !pickups || bytes<(slot+1)*0x20)return {};
    uint16_t generation=0;std::memcpy(&generation,pickups+slot*0x20+0x1A,2);
    return generation==uint16_t(bits>>16)?std::optional<size_t>(slot):std::nullopt;
}
inline std::optional<int32_t> source_pickup_integer(float revenue) {
    // Source GET stores float revenue through SET_INTEGER_PARAM, truncating
    // towards zero. Avoid undefined C++ conversion for corrupted revenue.
    if(!std::isfinite(revenue) || revenue<-2147483648.f || revenue>=2147483648.f)return {};
    return int32_t(revenue);
}
struct EntityFieldMutation {size_t offset=0,length=0;std::array<uint8_t,12> bytes{};};
inline std::optional<EntityFieldMutation> entity_field_mutation(EntityCommand action,const int32_t* inputs,
        size_t count,const uint8_t* entity,size_t size) {
    EntityFieldMutation mutation;
    switch(action) {
    case EntityCommand::car_move_speed:case EntityCommand::car_turn_speed:case EntityCommand::object_turn_speed: {
        if(count!=4)return {};const auto vector=source_vector(inputs+1,50.f);if(!vector)return {};
        mutation.offset=action==EntityCommand::car_move_speed?0x44:0x50;mutation.length=12;
        std::memcpy(mutation.bytes.data(),vector->data(),12);break;
    }
    case EntityCommand::car_alarm:
        if(count!=1)return {};mutation.offset=0x45C;mutation.length=2;mutation.bytes[0]=mutation.bytes[1]=255;break;
    case EntityCommand::object_render_damaged:
        if(count!=1 || !entity || size<=0x1D)return {};
        mutation.offset=0x1D;mutation.length=1;mutation.bytes[0]=entity[0x1D]|2;break;
    case EntityCommand::get_out_of_jail_free:
        if(count!=2)return {};mutation.offset=0x151;mutation.length=1;mutation.bytes[0]=inputs[1]!=0;break;
    case EntityCommand::player_fireproof:
        if(count!=2)return {};mutation.offset=0x14E;mutation.length=1;mutation.bytes[0]=inputs[1]!=0;break;
    case EntityCommand::player_max_health:case EntityCommand::player_max_armour:
        mutation.offset=action==EntityCommand::player_max_health?0x14F:0x150;
        if(count!=2 || !entity || size<=mutation.offset)return {};
        mutation.length=1;mutation.bytes[0]=source_player_maximum(entity[mutation.offset],inputs[1]);break;
    default:return {};
    }
    if(!entity || mutation.offset+mutation.length>size)return {};
    return mutation;
}
inline bool apply_entity_field_mutation(uint8_t* entity,size_t size,const EntityFieldMutation& mutation) {
    if(!entity || !mutation.length || mutation.length>mutation.bytes.size() || mutation.offset>size ||
       mutation.length>size-mutation.offset)return false;
    std::memcpy(entity+mutation.offset,mutation.bytes.data(),mutation.length);return true;
}
}
