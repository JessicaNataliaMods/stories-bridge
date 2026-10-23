#pragma once


#include <array>
#include <cstdint>
#include <optional>

namespace lcs {
enum class VehicleRoadSurface : uint8_t {concrete,grass,gravel,metal,sand,water,wood,hedge};

// Semantic LCS sample identities. The PS2 sample numbers stay in the private
// audio index generated for the build and are resolved by vehicle_surface_sample.
enum class VehicleSurfaceSample : uint8_t {
    road_noise,
    water_road_loop,
    skid_rain,
    gravel_skid,
    skid,
    reverse_gear,
    reverse_gear_2,
};

uint32_t vehicle_surface_sample(VehicleSurfaceSample sample);

struct VehicleSurfaceLoop {
    VehicleSurfaceSample sample = VehicleSurfaceSample::road_noise;
    uint32_t frequency = 0;
    uint8_t volume = 0;
    uint8_t pan = 63;
};

struct VehicleRoadNoiseInputs {
    float distance = 0.0f;
    float camera_x = 0.0f;
    float velocity = 0.0f;
    float max_velocity = 0.0f;
    float wet_roads = 0.0f;
    uint32_t road_noise_base_frequency = 0;
    uint8_t wheels_on_ground = 0;
    VehicleRoadSurface surface = VehicleRoadSurface::concrete;
};

struct VehicleRoadNoiseSounds {
    std::optional<VehicleSurfaceLoop> rolling;
    std::optional<VehicleSurfaceLoop> wet;
};

// Mirrors the LCS road and wet-road loops. `velocity` is the vehicle's
// forward speed supplied by the SA vehicle-audio params, and wetness is the
// live native CWeather::WetRoads scalar.
std::optional<VehicleRoadNoiseSounds> vehicle_road_noise_sounds(
    const VehicleRoadNoiseInputs& inputs);

enum class VehicleWheelState : uint8_t { normal, spinning, skidding, fixed };

struct VehicleSkidWheel {
    VehicleWheelState state = VehicleWheelState::normal;
    float contact_time = 0.0f;
    bool drive_wheel = false;
};

struct VehicleSkidInputs {
    float distance = 0.0f;
    float camera_x = 0.0f;
    float velocity = 0.0f;
    float max_velocity = 0.0f;
    float turn_speed_z = 0.0f;
    float gas_audio = 0.0f;
    uint8_t wheels_on_ground = 0;
    VehicleRoadSurface surface = VehicleRoadSurface::concrete;
    std::array<VehicleSkidWheel, 4> wheels{};
    uint8_t wheel_count = 0;
    bool bike = false;
};

std::optional<VehicleSurfaceLoop> vehicle_skid_sound(const VehicleSkidInputs& inputs);

struct VehicleReverseInputs {
    float distance = 0.0f;
    float camera_x = 0.0f;
    float velocity = 0.0f;
    float max_reverse_velocity = 0.0f;
    float gas_audio = 0.0f;
    bool engine_on = false;
    bool gas_negative = false;
    bool gear_is_reverse = false;
    bool drive_wheels_on_ground = false;
    bool drive_wheels_were_on_ground = false;
};

std::optional<VehicleSurfaceLoop> vehicle_reverse_sound(const VehicleReverseInputs& inputs);

// Golf-cart exclusion is keyed by the mapped LCS audio bank, not by an
// inferred model name or a hard-coded model ID.
bool vehicle_reverse_eligible(int lcs_model);

} // namespace lcs
