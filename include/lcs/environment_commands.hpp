#pragma once
#include <cstdint>
namespace lcs {
// These operations access real native fields, but keep source snapshots owned
// by the LCS adapter. SA's weather opcodes are empty and its saved clock also
// includes the calendar, unlike LCS.
enum class EnvironmentCommand : uint8_t {
    read_weather,restore_weather_and_store_clock,read_clock,restore_clock
};
}
