#pragma once
#include <cstdint>
#include <optional>
namespace lcs {
// Original LCS vehicle models are DEFAULT.IDE ids 130 (spider) .. 216 (vcnmav);
// cAudioManager::aVehicleSettings has one row per model in that order
// (AudioLogic.cpp: params.m_nIndex = modelIndex - MI_FIRST_VEHICLE).
inline constexpr int first_lcs_vehicle_model=130;
inline constexpr int lcs_vehicle_model_count=87;
enum class VehicleDoorType : uint8_t {old_door,new_door,truck_door,bus_door};
struct VehicleSoundRow {
    uint32_t accel_sample;       // aVehicleSettings.m_nAccelerationSampleIndex (eSfxSample)
    uint8_t bank;                // engine bank (SFX_BANK_*), index into the idle/rev table after the car offset
    uint32_t horn_sample;
    int32_t horn_frequency;
    uint32_t siren_sample;       // siren, or the alarm tone of vehicles without one
    int32_t siren_frequency;
    VehicleDoorType door;
};
struct VehicleEngineSamples {uint32_t idle,rev;};
const VehicleSoundRow* vehicle_sound_row(int lcs_model);
std::optional<VehicleEngineSamples> vehicle_engine_samples(uint8_t bank);
// SFX_OLD/NEW_CAR_DOOR_*, SFX_TRUCK_DOOR_*; buses use SFX_AIR_BRAKES for both.
uint32_t vehicle_door_sample(VehicleDoorType type,bool open);
uint32_t vehicle_engine_start_sample();   // SFX_CAR_STARTER
bool vehicle_engine_halves_frequency(uint32_t sample);   // truck idle/rev loops play at half rate
uint32_t vehicle_fast_siren_sample();     // SFX_SIREN_FAST (horn pressed while a siren runs)

// cAudioManager constants for vehicle sounds (AudioLogic.cpp).
inline constexpr float vehicle_horn_range=40.f,vehicle_siren_range=110.f,vehicle_engine_range=50.f,vehicle_door_range=50.f,vehicle_start_range=40.f;
inline constexpr uint8_t vehicle_horn_volume=80,vehicle_siren_volume=80,vehicle_engine_base_volume=75,vehicle_engine_full_volume=90,
    vehicle_player_engine_volume=120,vehicle_start_volume=60,vehicle_door_open_volume=117,vehicle_door_close_volume=122;

// Loop state of a vehicle engine (ProcessVehicleEngine, non-player branch).
struct VehicleEngineInputs {
    float speed;            // signed, SA units per frame
    float max_velocity;     // handling transmission
    float gear_change_down; // ChangeDownVelocity of the gear in use
    float first_gear_change_up;
    float gas_audio;        // smoothed pedal 0..1 (m_GasPedalAudioRevs)
    float gas_pedal;        // raw pedal
    bool is_bike;
    bool drive_wheels_on_ground;
    bool player;
};
struct VehicleEngineSound {bool idle; uint32_t frequency; uint8_t volume; float modificator;};
VehicleEngineSound vehicle_engine_sound(const VehicleEngineInputs&);
}
