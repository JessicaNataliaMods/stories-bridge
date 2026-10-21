#pragma once

#include <cstdint>
#include <optional>

namespace lcs {

// Read the current event's living ped source for GET_CHAR_THREAT_CHAR.
// A present value of 0 is the retail null-event/source sentinel; -1 is the
// retail pool-helper result for a non-null source outside the ped pool. nullopt
// means the native read/profile was unavailable and output must remain intact.
std::optional<int32_t> sa_get_char_threat_char(int32_t char_handle);

}
