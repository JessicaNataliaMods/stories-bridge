#pragma once
#include <cstdint>
namespace lcs {
// Original traffic/control services with reviewed native state and semantics.
enum class TrafficCommand : uint8_t {
    emergency_vehicles,cars_around_camera,release_vehicle_cleanup,
    pop_boot_physics,tank_detonates_cars,drunk_input_delay
};
}
