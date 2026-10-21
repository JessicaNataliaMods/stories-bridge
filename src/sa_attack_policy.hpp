#pragma once
#include "lcs/native.hpp"
#include <optional>

namespace lcs {
// Facts for the 1593 reaction (NativeAdapter::tick_attack_with_cops), read from SA on the game thread.
//
// released: SA's own creator tag of the ped is not PED_MISSION (2). The retail test is CharCreatedBy != MISSION_CHAR and the
// ported MARK_CHAR_AS_NO_LONGER_NEEDED moves a mission ped to SA's ordinary game ownership.
// cops_present: LCS tests the player's CurrentCops (cops spawned for the pursuit). The SA equivalent is a wanted player
// (IS_WANTED_LEVEL_GREATER 0) with at least one live, non-removed cop ped (SA ped type 6) within 120m of the player.
std::optional<NativeAdapter::AttackPolicyActor> sa_attack_policy_actor(const ObjectiveActor& actor);
std::optional<NativeAdapter::AttackPolicyWorld> sa_attack_policy_world();
}
