#pragma once
#include <cstdint>
namespace lcs {
// Reviewed original garage services. Numeric LCS garage handles are translated
// to registered native names; no empty legacy SA opcode is dispatched.
enum class GarageCommand : uint8_t {
    car_delivered,respray_happened,contains_car,free_bombs,
    car_bomb_type,arm_car_bomb,disarm_car_bomb,free_resprays
};
}
