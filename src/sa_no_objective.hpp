#pragma once

#include <cstdint>
#include <optional>

namespace lcs {

// COMMAND_IS_CHAR_OBJ_NO_OBJ adaptation: true means no current SA script
// event/task is associated with this live ped. It does not report completion.
// An absent/unstable native snapshot remains unavailable.
std::optional<bool> sa_char_has_no_objective(int32_t ped);

}
