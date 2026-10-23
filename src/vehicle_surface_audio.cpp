#include "lcs/vehicle_surface_audio.hpp"
#include "lcs/continuous_sound.hpp"
#include "lcs/vehicle_audio.hpp"
#include "audio_sample_ids.inc"
#include <algorithm>
#include <cmath>

namespace lcs {
namespace {
constexpr float road_noise_range = 95.0f;
constexpr float wet_road_range = 30.0f;
constexpr float skid_range = 40.0f;
constexpr float reverse_range = 30.0f;

// reLCS car sound banks begin at 4; GolfCart is the twelfth car-bank value.
constexpr uint8_t SFX_BANK_GOLF_CART = 15;

bool finite_spatial(float distance, float camera_x) {
    return std::isfinite(distance) && std::isfinite(camera_x) && distance >= 0.0f;
}

std::optional<VehicleSurfaceLoop> attenuated_loop(
    VehicleSurfaceSample sample, uint32_t frequency, uint8_t emitting_volume,
    float range, float distance, float camera_x) {
    if (!frequency || frequency > 192000 || !finite_spatial(distance, camera_x) ||
        !(distance < range)) return {};
    const auto volume = continuous_sound_volume(emitting_volume, range, distance);
    const auto pan = continuous_sound_pan(distance, camera_x);
    if (!volume || !pan || !*volume) return {};
    return VehicleSurfaceLoop{sample, frequency, *volume, *pan};
}

float skid_turn_contribution(float turn_speed_z) {
    return std::min(1.0f, std::fabs(turn_speed_z) * 20.0f);
}

float drive_wheel_skid(VehicleWheelState state, float gas_audio,
                       float velocity, float max_velocity) {
    float value = 0.0f;
    switch (state) {
    case VehicleWheelState::spinning:
        if (gas_audio > 0.4f)
            value = (gas_audio - 0.4f) * (5.0f / 3.0f) * 0.75f;
        break;
    case VehicleWheelState::skidding:
        value = std::min(1.0f, std::fabs(velocity) / max_velocity);
        break;
    case VehicleWheelState::fixed: {
        float relative_gas = gas_audio;
        if (relative_gas > 0.4f)
            relative_gas = (gas_audio - 0.4f) * (5.0f / 3.0f);
        const float relative_velocity = std::fabs(velocity) > 0.04f
            ? std::min(1.0f, std::fabs(velocity) / max_velocity)
            : 0.0f;
        value = std::max(relative_gas, relative_velocity);
        break;
    }
    case VehicleWheelState::normal:
    default:
        break;
    }
    return value;
}

float non_drive_wheel_skid(VehicleWheelState state, float velocity,
                           float max_velocity) {
    return state == VehicleWheelState::skidding
        ? std::min(1.0f, std::fabs(velocity) / max_velocity)
        : 0.0f;
}
}

uint32_t vehicle_surface_sample(VehicleSurfaceSample sample) {
    switch (sample) {
    case VehicleSurfaceSample::road_noise: return SFX_ROAD_NOISE;
    case VehicleSurfaceSample::water_road_loop: return SFX_BOAT_WATER_LOOP;
    case VehicleSurfaceSample::skid_rain: return SFX_RAIN;
    case VehicleSurfaceSample::gravel_skid: return SFX_GRAVEL_SKID;
    case VehicleSurfaceSample::skid: return SFX_SKID;
    case VehicleSurfaceSample::reverse_gear: return SFX_REVERSE_GEAR;
    case VehicleSurfaceSample::reverse_gear_2: return SFX_REVERSE_GEAR_2;
    }
    return UINT32_MAX;
}

std::optional<VehicleRoadNoiseSounds> vehicle_road_noise_sounds(
    const VehicleRoadNoiseInputs& in) {
    if (!finite_spatial(in.distance, in.camera_x) ||
        !std::isfinite(in.velocity) || !std::isfinite(in.max_velocity) ||
        !std::isfinite(in.wet_roads) || in.max_velocity <= 0.0f ||
        in.wet_roads < 0.0f || in.wet_roads > 1.0f ||
        !in.road_noise_base_frequency || in.road_noise_base_frequency > 192000)
        return {};

    VehicleRoadNoiseSounds sounds;
    if (in.wheels_on_ground && std::fabs(in.velocity) > 0.0f &&
        in.distance < road_noise_range) {
        const auto emitting = uint8_t(30.0f * std::min(
            1.0f, std::fabs(in.velocity) / (0.5f * in.max_velocity)));
        uint32_t frequency = in.road_noise_base_frequency;
        auto sample = VehicleSurfaceSample::road_noise;
        if (in.surface == VehicleRoadSurface::water) {
            sample = VehicleSurfaceSample::water_road_loop;
            frequency = uint32_t(6050.0f * float(emitting) / 30.0f + 16000.0f);
        } else {
            const float distance_factor = (in.distance / road_noise_range) * 0.5f;
            frequency = uint32_t(float(frequency) * (0.75f + distance_factor));
        }
        sounds.rolling = attenuated_loop(sample, frequency, emitting,
                                         road_noise_range, in.distance, in.camera_x);
    }

    if (in.wet_roads > 0.0f && in.wheels_on_ground &&
        std::fabs(in.velocity) > 0.0f && in.distance < wet_road_range) {
        const float relative_velocity = std::min(
            1.0f, std::fabs(in.velocity) / (0.5f * in.max_velocity));
        const auto emitting = uint8_t(23.0f * relative_velocity * in.wet_roads);
        const float distance_factor = (in.distance / wet_road_range) * 0.5f;
        const auto frequency = uint32_t(float(in.road_noise_base_frequency) *
                                        (1.0f + distance_factor));
        sounds.wet = attenuated_loop(VehicleSurfaceSample::road_noise, frequency,
                                     emitting, wet_road_range, in.distance, in.camera_x);
    }
    if (!sounds.rolling && !sounds.wet) return {};
    return sounds;
}

std::optional<VehicleSurfaceLoop> vehicle_skid_sound(const VehicleSkidInputs& in) {
    if (!finite_spatial(in.distance, in.camera_x) ||
        !std::isfinite(in.velocity) || !std::isfinite(in.max_velocity) ||
        !std::isfinite(in.turn_speed_z) || !std::isfinite(in.gas_audio) ||
        in.max_velocity <= 0.0f || in.gas_audio < 0.0f || in.gas_audio > 1.0f ||
        in.wheel_count == 0 || in.wheel_count > in.wheels.size() ||
        !in.wheels_on_ground || !(in.distance < skid_range)) return {};

    float skid_value = 0.0f;
    const float turn_value = skid_turn_contribution(in.turn_speed_z);
    for (size_t i = 0; i < in.wheel_count; ++i) {
        const auto& wheel = in.wheels[i];
        if (!std::isfinite(wheel.contact_time) || wheel.contact_time <= 0.0f ||
            wheel.state == VehicleWheelState::normal) continue;
        const float wheel_value = wheel.drive_wheel
            ? drive_wheel_skid(wheel.state, in.gas_audio, in.velocity, in.max_velocity)
            : non_drive_wheel_skid(wheel.state, in.velocity, in.max_velocity);
        skid_value = std::max(skid_value, std::max(wheel_value, turn_value));
    }
    if (!(skid_value > 0.0f)) return {};

    const auto emitting = uint8_t(50.0f * skid_value);
    auto sample = VehicleSurfaceSample::skid;
    uint32_t frequency = uint32_t(5000.0f * skid_value + 11000.0f);
    bool quarter_volume = false;
    if (in.surface == VehicleRoadSurface::grass || in.surface == VehicleRoadSurface::hedge) {
        sample = VehicleSurfaceSample::skid_rain;
        frequency = uint32_t(13000.0f * skid_value + 35000.0f);
        quarter_volume = true;
    } else if (in.surface == VehicleRoadSurface::gravel ||
               in.surface == VehicleRoadSurface::sand ||
               in.surface == VehicleRoadSurface::water) {
        sample = VehicleSurfaceSample::gravel_skid;
        frequency = uint32_t(6000.0f * skid_value + 10000.0f);
    } else if (in.bike) {
        frequency += 2000;
    }

    auto sound = attenuated_loop(sample, frequency, emitting, skid_range,
                                 in.distance, in.camera_x);
    if (sound && quarter_volume) {
        sound->volume >>= 2;
        if (!sound->volume) return {};
    }
    return sound;
}

std::optional<VehicleSurfaceLoop> vehicle_reverse_sound(const VehicleReverseInputs& in) {
    if (!finite_spatial(in.distance, in.camera_x) ||
        !std::isfinite(in.velocity) || !std::isfinite(in.max_reverse_velocity) ||
        !std::isfinite(in.gas_audio) || in.max_reverse_velocity <= 0.0f ||
        in.gas_audio < 0.0f || in.gas_audio > 1.0f || !(in.distance < reverse_range) ||
        !in.engine_on || (!in.gas_negative && !in.gear_is_reverse)) return {};

    float modifier = in.drive_wheels_on_ground
        ? in.velocity / in.max_reverse_velocity
        : in.gas_audio;
    if (!in.drive_wheels_on_ground && in.drive_wheels_were_on_ground)
        modifier *= 0.4f;
    modifier = std::fabs(modifier);
    const float volume_value = 24.0f * modifier;
    if (!std::isfinite(volume_value) || volume_value > 127.0f) return {};
    const auto emitting = uint8_t(volume_value);
    const auto sample = in.gas_negative
        ? VehicleSurfaceSample::reverse_gear
        : VehicleSurfaceSample::reverse_gear_2;
    const auto frequency = uint32_t(6000.0f * modifier + 7000.0f);
    return attenuated_loop(sample, frequency, emitting, reverse_range,
                           in.distance, in.camera_x);
}

bool vehicle_reverse_eligible(int lcs_model) {
    const auto* row = vehicle_sound_row(lcs_model);
    return row && row->bank != SFX_BANK_GOLF_CART;
}

} // namespace lcs
