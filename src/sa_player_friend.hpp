#pragma once

#include "lcs/objective_receipts.hpp"
#include "lcs/native.hpp"

#include <cstdint>

namespace lcs {

// Adapts SET_CHAR_IS_PLAYER_FRIEND to SA's own per-ped acquaintance masks.
// NativeResult.native_entered marks the first relationship-block write so a
// caller will never replay a setter whose transaction may have entered.
NativeResult sa_set_char_player_friend(const ObjectiveActor& actor, bool enabled);

// Retire the exact policy after an accepted actor creation/deletion callback.
// This never writes relationship data into a recycled pool slot.
bool sa_erase_player_friend(const ObjectiveActor& actor);

// Restore owned Player 1 bits during shutdown. Failed identity/profile/write
// checks retain the receipt for a later cleanup retry.
bool sa_release_player_friends();

}
