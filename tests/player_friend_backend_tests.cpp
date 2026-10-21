#include "../src/sa_player_friend.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace lcs {
void sa_player_friend_test_inject_faults(bool partial_write, bool rollback_failure);
void sa_player_friend_test_inject_lifecycle_state(int state);
}

namespace {
using namespace lcs;

alignas(16) std::array<uint8_t, 0x7C4> ped_storage{};
ObjectiveActor current_actor{5, reinterpret_cast<uintptr_t>(ped_storage.data())};
bool profile_available = true;

uint32_t* masks() {
    return reinterpret_cast<uint32_t*>(ped_storage.data() + 0x4E0);
}

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void reset() {
    ped_storage.fill(0);
    current_actor = ObjectiveActor{5, reinterpret_cast<uintptr_t>(ped_storage.data())};
    profile_available = true;
    lcs::sa_player_friend_test_inject_lifecycle_state(-1);
    masks()[0] = 0x12;
    masks()[1] = 0x24;
    masks()[2] = 0x48;
    masks()[3] = 0x11;
    masks()[4] = 0x21;
}
}

namespace lcs {
bool sa_executor_profile_matches() { return profile_available; }
std::optional<ObjectiveActor> sa_live_objective_actor(int32_t handle) {
    if (!profile_available || handle != current_actor.handle) return {};
    return current_actor;
}
}

int main() {
    try {
        reset();
        const auto original = std::array<uint32_t, 5>{masks()[0], masks()[1], masks()[2], masks()[3], masks()[4]};
        auto result = lcs::sa_set_char_player_friend(current_actor, true);
        check(result.executed && result.native_entered, "Friend setter commits the native relationship transaction");
        check((masks()[0] & 1u) && (masks()[1] & 1u) && !(masks()[3] & 1u) && !(masks()[4] & 1u),
              "Player 1 becomes respected/liked and ceases to be disliked/hated");
        check(masks()[2] == original[2] && (masks()[0] & ~1u) == (original[0] & ~1u) &&
                  (masks()[1] & ~1u) == (original[1] & ~1u) &&
                  (masks()[3] & ~1u) == (original[3] & ~1u) &&
                  (masks()[4] & ~1u) == (original[4] & ~1u),
              "The ignore mask and every non-player relationship bit remain native");

        masks()[0] |= 0x80u; // Native change to another ped type while friend policy is active.
        result = lcs::sa_set_char_player_friend(current_actor, true);
        check(result.executed && (masks()[0] & 0x80u), "Repeated friend set preserves unrelated native relationship changes");
        result = lcs::sa_set_char_player_friend(current_actor, false);
        check(result.executed && !(masks()[0] & 1u) && !(masks()[1] & 1u) &&
                  (masks()[3] & 1u) && (masks()[4] & 1u),
              "Friend clear restores only the captured Player 1 bits");
        check((masks()[0] & 0x80u) && masks()[2] == original[2],
              "Clear preserves later unrelated relationship changes and ignore state");

        reset();
        profile_available = false;
        result = lcs::sa_set_char_player_friend(current_actor, true);
        check(!result.executed && !result.native_entered && (masks()[0] & 1u) == 0,
              "A profile refusal occurs before any relationship write");

        reset();
        const auto before_fault = std::array<uint32_t, 5>{masks()[0], masks()[1], masks()[2], masks()[3], masks()[4]};
        lcs::sa_player_friend_test_inject_faults(true, true);
        result = lcs::sa_set_char_player_friend(current_actor, true);
        check(!result.executed && result.native_entered,
              "A partial relationship transaction reports entered so the script cannot replay it");
        check(lcs::sa_release_player_friends(), "A retained partial-write receipt is restored on cleanup retry");
        check(masks()[0] == before_fault[0] && masks()[1] == before_fault[1] &&
                  masks()[2] == before_fault[2] && masks()[3] == before_fault[3] && masks()[4] == before_fault[4],
              "Cleanup retry restores the captured block after rollback failure");

        reset();
        result = lcs::sa_set_char_player_friend(current_actor, true);
        check(result.executed, "Lifecycle fixture enables the policy");
        ObjectiveActor new_generation{6, current_actor.native_ped};
        masks()[0] = 0x40;
        masks()[1] = 0x80;
        masks()[2] = 0x100;
        masks()[3] = 0x200;
        masks()[4] = 0x400;
        current_actor = new_generation;
        check(lcs::sa_erase_player_friend(new_generation), "Accepted actor reuse retires the stale policy");
        result = lcs::sa_set_char_player_friend(new_generation, false);
        check(result.executed && masks()[0] == 0x40 && masks()[1] == 0x80 && masks()[3] == 0x200 && masks()[4] == 0x400,
              "A recycled actor never receives the old actor's snapshot");

        reset();
        result = lcs::sa_set_char_player_friend(current_actor, true);
        check(result.executed, "Retirement fixture enables the policy");
        const auto active_masks = std::array<uint32_t, 5>{masks()[0], masks()[1], masks()[2], masks()[3], masks()[4]};
        current_actor = ObjectiveActor{6, current_actor.native_ped};
        lcs::sa_player_friend_test_inject_lifecycle_state(1); // A pool snapshot could not be proven.
        check(!lcs::sa_release_player_friends(), "Unverifiable actor-pool state retains the cleanup receipt");
        lcs::sa_player_friend_test_inject_lifecycle_state(0); // Stable pool proof: old slot was retired.
        check(lcs::sa_release_player_friends(), "A proven free or recycled actor retires its receipt");
        check(masks()[0] == active_masks[0] && masks()[1] == active_masks[1] && masks()[2] == active_masks[2] &&
                  masks()[3] == active_masks[3] && masks()[4] == active_masks[4],
              "Retirement never writes a captured snapshot into a reused actor slot");

        reset();
        const auto before_release = std::array<uint32_t, 5>{masks()[0], masks()[1], masks()[2], masks()[3], masks()[4]};
        result = lcs::sa_set_char_player_friend(current_actor, true);
        check(result.executed && lcs::sa_release_player_friends(), "Shutdown releases live friend policies");
        check(masks()[0] == before_release[0] && masks()[1] == before_release[1] &&
                  masks()[2] == before_release[2] && masks()[3] == before_release[3] && masks()[4] == before_release[4],
              "Shutdown restores the original native acquaintance block");

        std::cout << "player-friend backend fixtures passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
