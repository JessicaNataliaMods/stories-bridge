#include "lcs/vehicle_audio.hpp"
#include "audio_sample_ids.inc"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace lcs {
namespace {
// reLCS sampman.h bank numbering (non-PS2 layout): the car banks start at 4.
enum {
    SFX_BANK_0=0,CAR_SFX_BANKS_OFFSET=4,
    SFX_BANK_PONTIAC=4,SFX_BANK_PORSCHE,SFX_BANK_SPIDER,SFX_BANK_MERC,SFX_BANK_TRUCK,SFX_BANK_HOTROD,SFX_BANK_COBRA,
    SFX_BANK_PONTIAC_SLOW,SFX_BANK_CADILLAC,SFX_BANK_PATHFINDER,SFX_BANK_PACARD,SFX_BANK_GOLF_CART,SFX_BANK_CAR_CHAINSAW,
    SFX_BANK_RC,SFX_BANK_RC_HELI,SFX_BANK_CAR_UNUSED_4,
    SFX_BANK_VTWIN,SFX_BANK_MOPED,SFX_BANK_HONDA250,SFX_BANK_SPORTS_BIKE,
    SFX_BANK_BIKE_UNUSED_1,SFX_BANK_BIKE_UNUSED_2,SFX_BANK_BIKE_UNUSED_3,SFX_BANK_BIKE_UNUSED_4,SFX_BANK_BIKE_UNUSED_5,SFX_BANK_BIKE_UNUSED_6,
    SFX_BANK_HELI_APACHE,
};
static_assert(SFX_BANK_VTWIN-CAR_SFX_BANKS_OFFSET==16 && SFX_BANK_SPORTS_BIKE-CAR_SFX_BANKS_OFFSET==19,"Bike banks follow the 16 car banks");
constexpr VehicleDoorType OLD_DOOR=VehicleDoorType::old_door,NEW_DOOR=VehicleDoorType::new_door,
    TRUCK_DOOR=VehicleDoorType::truck_door,BUS_DOOR=VehicleDoorType::bus_door;
// Rows copied verbatim by CMake from reLCS AudioLogic.cpp (aVehicleSettings / aEngineSounds).
constexpr VehicleSoundRow rows[]={
#include "vehicle_audio_rows.inc"
};
constexpr VehicleEngineSamples engines[]={
#include "vehicle_engine_rows.inc"
};
static_assert(std::size(rows)==lcs_vehicle_model_count,"One audio row per original vehicle model");
static_assert(std::size(engines)==24,"Original engine sample table size");
}
const VehicleSoundRow* vehicle_sound_row(int lcs_model) {
    const int index=lcs_model-first_lcs_vehicle_model;
    return index>=0 && index<lcs_vehicle_model_count?&rows[index]:nullptr;
}
std::optional<VehicleEngineSamples> vehicle_engine_samples(uint8_t bank) {
    if(bank<CAR_SFX_BANKS_OFFSET)return std::nullopt;
    const size_t index=bank-CAR_SFX_BANKS_OFFSET;
    if(index>=std::size(engines))return std::nullopt;
    return engines[index];
}
uint32_t vehicle_door_sample(VehicleDoorType type,bool open) {
    switch(type) {
    case VehicleDoorType::old_door:return open?SFX_OLD_CAR_DOOR_OPEN:SFX_OLD_CAR_DOOR_CLOSE;
    case VehicleDoorType::truck_door:return open?SFX_TRUCK_DOOR_OPEN:SFX_TRUCK_DOOR_CLOSE;
    case VehicleDoorType::bus_door:return SFX_AIR_BRAKES;
    default:return open?SFX_NEW_CAR_DOOR_OPEN:SFX_NEW_CAR_DOOR_CLOSE;
    }
}
uint32_t vehicle_engine_start_sample() {return SFX_CAR_STARTER;}
bool vehicle_engine_halves_frequency(uint32_t sample) {return sample==SFX_CAR_IDLE_TRUCK || sample==SFX_CAR_REV_TRUCK;}
uint32_t vehicle_fast_siren_sample() {return SFX_SIREN_FAST;}
// ProcessVehicleEngine: modificator blends the pedal with the position inside the
// current gear; idle sample below pedal 0.02, otherwise the model's acceleration
// loop. Frequencies are absolute playback rates.
VehicleEngineSound vehicle_engine_sound(const VehicleEngineInputs& in) {
    const float max=in.max_velocity>0.f?in.max_velocity:0.f;
    const float speed=std::fabs(in.speed);
    float traction=0.f;
    if(max>0.f && speed<in.first_gear_change_up)traction=0.7f;
    float modificator=0.f;
    if(max>0.f) {
        const float relative=std::min(1.f,(speed-in.gear_change_down)/max*2.5f);
        modificator=traction*in.gas_audio*0.95f+(1.f-traction)*std::max(0.f,relative);
    }
    modificator=std::clamp(modificator,0.f,1.f);
    VehicleEngineSound result{};
    result.modificator=modificator;
    result.idle=in.gas_pedal<0.02f;
    if(result.idle)result.frequency=uint32_t(10000.f*modificator+22050.f);
    else result.frequency=in.is_bike?22050u:uint32_t(13000.f*modificator+14000.f);
    if(modificator<0.75f)
        result.volume=uint8_t(modificator/0.75f*float(vehicle_engine_full_volume-vehicle_engine_base_volume)+float(vehicle_engine_base_volume));
    else result.volume=vehicle_engine_full_volume;
    if(in.player)result.volume=std::max(result.volume,vehicle_player_engine_volume);
    return result;
}
}
