#include "sa_player_friend.hpp"

#include "lcs/sa_executor.hpp"
#include "lcs/native_memory.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#if !defined(LCS_PLAYER_FRIEND_BACKEND_TEST)
#include <windows.h>
#endif

namespace lcs {
namespace {

constexpr uintptr_t acquaintance_offset = 0x4E0;
constexpr uint32_t player1_ped_flag = 1u << 0;
constexpr size_t acquaintance_word_count = 5;
constexpr size_t respect_word = 0;
constexpr size_t like_word = 1;
constexpr size_t dislike_word = 3;
constexpr size_t hate_word = 4;
constexpr std::array<size_t, 4> relationship_words{{respect_word, like_word, dislike_word, hate_word}};
constexpr size_t ped_stride = 0x7C4;

enum class ActorPoolState : uint8_t { live, retired, unavailable };

struct PedPoolSnapshot {
    uintptr_t pool;
    uintptr_t objects;
    uintptr_t generations;
    int32_t count;
};

struct FriendPolicy {
    ObjectiveActor actor;
    // One bit per relationship_words entry, recording the pre-command Player 1 bit.
    uint8_t original_player_bits = 0;
    // Set before entering a block write, so partial faults remain recoverable.
    uint8_t owned_relationships = 0;
    bool restoring = false;
};

std::vector<FriendPolicy> friend_policies;

#if defined(LCS_PLAYER_FRIEND_BACKEND_TEST)
bool test_fail_next_write_after_entry = false;
bool test_fail_next_rollback = false;
int test_lifecycle_override = -1;
#endif

template<size_t N> bool ped_pool_profile_matches(uintptr_t base, const uint8_t (&bytes)[N]) {
    return std::memcmp(reinterpret_cast<const void*>(base), bytes, N) == 0;
}

bool read_ped_pool_snapshot(PedPoolSnapshot* out) {
    if (!out) return false;
#if defined(LCS_PLAYER_FRIEND_BACKEND_TEST)
    return false;
#else
    const uint8_t pool_helper[] = {
        0x8B,0x4C,0x24,0x04,0x8B,0x15,0x90,0x44,0xB7,0x00,0x53,0x56,0x8B,0x72,0x04,0x8B,0xC1,
        0xC1,0xF8,0x08,0x8A,0x1C,0x30,0x5E,0x3A,0xD9,0x5B,0x75,0x0B,0x8B,0x0A,0x69,0xC0,0xC4,0x07,0x00,0x00
    };
    __try {
        if (!ped_pool_profile_matches(native_entity_address(0x54FF90), pool_helper)) return false;
        const auto root = native_entity_address(0xB74490);
        const auto pool = *reinterpret_cast<const uintptr_t*>(root);
        if (!pool || pool + 12 < pool) return false;
        PedPoolSnapshot snapshot;
        snapshot.pool = pool;
        snapshot.objects = *reinterpret_cast<const uintptr_t*>(pool);
        snapshot.generations = *reinterpret_cast<const uintptr_t*>(pool + 4);
        snapshot.count = *reinterpret_cast<const int32_t*>(pool + 8);
        if (!snapshot.objects || !snapshot.generations || snapshot.count <= 0 || snapshot.count > 0x7FFFFF)
            return false;
        *out = snapshot;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#endif
}

bool same_pool_snapshot(const PedPoolSnapshot& a, const PedPoolSnapshot& b) {
    return a.pool == b.pool && a.objects == b.objects && a.generations == b.generations && a.count == b.count;
}

ActorPoolState inspect_actor_pool_state(const ObjectiveActor& actor) {
    if (!sa_executor_profile_matches()) return ActorPoolState::unavailable;
    const auto live = sa_live_objective_actor(actor.handle);
    if (live) return *live == actor ? ActorPoolState::live : ActorPoolState::retired;
#if defined(LCS_PLAYER_FRIEND_BACKEND_TEST)
    if (test_lifecycle_override == 0) return ActorPoolState::retired;
    return ActorPoolState::unavailable;
#else
    PedPoolSnapshot before, after;
    if (!read_ped_pool_snapshot(&before) || !read_ped_pool_snapshot(&after) || !same_pool_snapshot(before, after))
        return ActorPoolState::unavailable;
    const auto slot = uint32_t(actor.handle) >> 8;
    if (slot >= uint32_t(after.count)) return ActorPoolState::retired;
    __try {
        if (after.generations + uintptr_t(slot) < after.generations) return ActorPoolState::unavailable;
        const auto generation = *reinterpret_cast<const uint8_t*>(after.generations + slot);
        const auto offset = uintptr_t(slot) * ped_stride;
        if (offset / ped_stride != slot || after.objects + offset < after.objects) return ActorPoolState::unavailable;
        const auto ped = after.objects + offset;
        if (ped + 0x37 < ped) return ActorPoolState::unavailable;
        const auto type = *reinterpret_cast<const uint8_t*>(ped + 0x36) & 7;
        PedPoolSnapshot verify;
        if (!read_ped_pool_snapshot(&verify) || !same_pool_snapshot(after, verify)) return ActorPoolState::unavailable;
        const auto generation_after = *reinterpret_cast<const uint8_t*>(verify.generations + slot);
        const auto type_after = *reinterpret_cast<const uint8_t*>(ped + 0x36) & 7;
        if (generation_after != generation || type_after != type) return ActorPoolState::unavailable;
        if ((generation & 0x80) || generation != uint8_t(actor.handle) || type != 3 || ped != actor.native_ped)
            return ActorPoolState::retired;
        return ActorPoolState::live;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return ActorPoolState::unavailable;
    }
#endif
}

bool writable_acquaintance_block(uintptr_t ped) {
#if defined(LCS_PLAYER_FRIEND_BACKEND_TEST)
    return ped != 0 && ped + acquaintance_offset >= ped;
#else
    if (!ped || ped + acquaintance_offset < ped) return false;
    const auto address = ped + acquaintance_offset;
    if (address + sizeof(uint32_t) * acquaintance_word_count < address) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto region_begin = reinterpret_cast<uintptr_t>(info.BaseAddress);
    const auto region_end = region_begin + info.RegionSize;
    const auto access = info.Protect & 0xFF;
    const bool writable = access == PAGE_READWRITE || access == PAGE_WRITECOPY ||
        access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY;
    return writable && region_end >= region_begin && address >= region_begin &&
        address + sizeof(uint32_t) * acquaintance_word_count <= region_end;
#endif
}

bool read_acquaintance(uintptr_t ped, std::array<uint32_t, acquaintance_word_count>& masks) {
#if defined(LCS_PLAYER_FRIEND_BACKEND_TEST)
    if (!ped || ped + acquaintance_offset < ped) return false;
    std::memcpy(masks.data(), reinterpret_cast<const void*>(ped + acquaintance_offset), sizeof(masks));
    return true;
#else
    __try {
        std::memcpy(masks.data(), reinterpret_cast<const void*>(ped + acquaintance_offset), sizeof(masks));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#endif
}

// Compare the complete native block before one bounded copy. SA script and AI
// processing run on the game thread; no scanner observes an intermediate mask.
bool write_acquaintance(uintptr_t ped,
                        const std::array<uint32_t, acquaintance_word_count>& expected,
                        const std::array<uint32_t, acquaintance_word_count>& desired,
                        bool& entered) {
#if defined(LCS_PLAYER_FRIEND_BACKEND_TEST)
    if (!ped || ped + acquaintance_offset < ped) return false;
    auto* const block = reinterpret_cast<uint32_t*>(ped + acquaintance_offset);
    if (std::memcmp(block, expected.data(), sizeof(expected)) != 0) return false;
    entered = true;
    if (test_fail_next_write_after_entry) {
        test_fail_next_write_after_entry = false;
        block[respect_word] = desired[respect_word];
        return false;
    }
    std::memcpy(block, desired.data(), sizeof(desired));
    return true;
#else
    __try {
        auto* const block = reinterpret_cast<uint32_t*>(ped + acquaintance_offset);
        if (std::memcmp(block, expected.data(), sizeof(expected)) != 0) return false;
        entered = true;
        std::memcpy(block, desired.data(), sizeof(desired));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#endif
}

bool rollback_acquaintance(uintptr_t ped,
                           const std::array<uint32_t, acquaintance_word_count>& previous) {
#if defined(LCS_PLAYER_FRIEND_BACKEND_TEST)
    if (!ped || ped + acquaintance_offset < ped) return false;
    if (test_fail_next_rollback) {
        test_fail_next_rollback = false;
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(ped + acquaintance_offset), previous.data(), sizeof(previous));
    return true;
#else
    __try {
        std::memcpy(reinterpret_cast<void*>(ped + acquaintance_offset), previous.data(), sizeof(previous));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#endif
}

bool actor_is_live(const ObjectiveActor& actor) {
    if (actor.handle < 0 || !actor.native_ped || !sa_executor_profile_matches()) return false;
    const auto live = sa_live_objective_actor(actor.handle);
    return live && *live == actor;
}

uint8_t capture_player_bits(const std::array<uint32_t, acquaintance_word_count>& masks) {
    uint8_t saved = 0;
    for (size_t slot = 0; slot < relationship_words.size(); ++slot)
        if (masks[relationship_words[slot]] & player1_ped_flag) saved |= uint8_t(1u << slot);
    return saved;
}

uint8_t owned_relationship_bits(uint8_t original_player_bits) {
    uint8_t owned = 0;
    for (size_t slot = 0; slot < relationship_words.size(); ++slot) {
        const bool was_set = (original_player_bits & uint8_t(1u << slot)) != 0;
        const bool friend_value = slot < 2;
        if (was_set != friend_value) owned |= uint8_t(1u << slot);
    }
    return owned;
}

std::array<uint32_t, acquaintance_word_count> friend_masks(
    const std::array<uint32_t, acquaintance_word_count>& current) {
    auto desired = current;
    desired[respect_word] |= player1_ped_flag;
    desired[like_word] |= player1_ped_flag;
    desired[dislike_word] &= ~player1_ped_flag;
    desired[hate_word] &= ~player1_ped_flag;
    return desired;
}

std::array<uint32_t, acquaintance_word_count> restored_masks(
    const std::array<uint32_t, acquaintance_word_count>& current, const FriendPolicy& policy) {
    auto desired = current;
    for (size_t slot = 0; slot < relationship_words.size(); ++slot) {
        if (!(policy.owned_relationships & uint8_t(1u << slot))) continue;
        const auto word = relationship_words[slot];
        const bool was_set = (policy.original_player_bits & uint8_t(1u << slot)) != 0;
        const bool friend_value = slot < 2;
        const bool current_set = (current[word] & player1_ped_flag) != 0;
        // If native SA has already returned the bit to its captured value,
        // leave it there; otherwise restore only the Player 1 bit.
        if (current_set == friend_value) {
            if (was_set) desired[word] |= player1_ped_flag;
            else desired[word] &= ~player1_ped_flag;
        }
    }
    return desired;
}

void erase_policy(size_t index) {
    friend_policies.erase(friend_policies.begin() + static_cast<std::ptrdiff_t>(index));
}

NativeResult refused(const char* diagnostic, bool entered = false) {
    NativeResult result;
    result.native_entered = entered;
    result.diagnostic = diagnostic;
    return result;
}

NativeResult accepted() {
    NativeResult result;
    result.executed = true;
    return result;
}

// Commits one relationship-block update. On a fault after entry, it attempts
// to restore the exact pre-write bytes while the original pool identity remains
// live. The caller keeps its receipt if either the write or rollback fails.
bool commit_masks(const ObjectiveActor& actor,
                  const std::array<uint32_t, acquaintance_word_count>& before,
                  const std::array<uint32_t, acquaintance_word_count>& after,
                  bool& entered, bool& rolled_back) {
    entered = false;
    rolled_back = false;
    if (before == after) return true;
    if (!writable_acquaintance_block(actor.native_ped) || !actor_is_live(actor)) return false;
    if (write_acquaintance(actor.native_ped, before, after, entered)) {
        if (actor_is_live(actor)) return true;
        return false;
    }
    if (!entered) return false;
    if (actor_is_live(actor) && writable_acquaintance_block(actor.native_ped))
        rolled_back = rollback_acquaintance(actor.native_ped, before);
    return false;
}

}

#if defined(LCS_PLAYER_FRIEND_BACKEND_TEST)
void sa_player_friend_test_inject_lifecycle_state(int state);

void sa_player_friend_test_inject_faults(bool partial_write, bool rollback_failure) {
    test_fail_next_write_after_entry = partial_write;
    test_fail_next_rollback = rollback_failure;
}

void sa_player_friend_test_inject_lifecycle_state(int state) {
    test_lifecycle_override = state;
}
#endif

NativeResult sa_set_char_player_friend(const ObjectiveActor& actor, bool enabled) {
    if (!sa_executor_profile_matches()) return refused("Native SA profile is unavailable");
    const auto live = sa_live_objective_actor(actor.handle);
    if (!actor.native_ped || !live || !(*live == actor))
        return refused("Player-friend actor identity is not live");
    if (!writable_acquaintance_block(actor.native_ped))
        return refused("Native acquaintance block is not writable");

    auto found = std::find_if(friend_policies.begin(), friend_policies.end(), [&](const FriendPolicy& policy) {
        return policy.actor.handle == actor.handle;
    });
    if (found != friend_policies.end() && !(found->actor == actor)) {
        // The exact handle now identifies another native ped. Do not replay the
        // old snapshot into its reused slot.
        erase_policy(static_cast<size_t>(found - friend_policies.begin()));
        found = friend_policies.end();
    }

    if (found == friend_policies.end() && !enabled) return accepted();

    std::array<uint32_t, acquaintance_word_count> before{};
    if (!read_acquaintance(actor.native_ped, before))
        return refused("Native acquaintance masks could not be read");

    bool inserted = false;
    if (found == friend_policies.end()) {
        FriendPolicy policy;
        policy.actor = actor;
        policy.original_player_bits = capture_player_bits(before);
        policy.owned_relationships = owned_relationship_bits(policy.original_player_bits);
        try {
            friend_policies.push_back(policy);
        } catch (...) {
            return refused("Player-friend ownership receipt could not be allocated");
        }
        found = friend_policies.end() - 1;
        inserted = true;
    }

    const size_t policy_index = static_cast<size_t>(found - friend_policies.begin());
    const auto previous_restoring = friend_policies[policy_index].restoring;
    const auto after = enabled ? friend_masks(before) : restored_masks(before, friend_policies[policy_index]);
    if (!actor_is_live(actor)) {
        if (inserted) erase_policy(policy_index);
        return refused("Player-friend actor changed before relationship commit");
    }
    if (before == after) {
        if (!enabled) erase_policy(policy_index);
        else friend_policies[policy_index].restoring = false;
        return accepted();
    }

    if (!enabled) friend_policies[policy_index].restoring = true;

    bool entered = false, rolled_back = false;
    const bool committed = commit_masks(actor, before, after, entered, rolled_back);
    if (!committed) {
        auto& policy = friend_policies[policy_index];
        if (rolled_back) {
            if (inserted) erase_policy(policy_index);
            else policy.restoring = previous_restoring;
        } else if (entered) {
            // Keep original Player 1 bits and phase for shutdown retry. If the
            // identity became stale, only the lifecycle callback may retire it.
            policy.owned_relationships = owned_relationship_bits(policy.original_player_bits);
        } else if (inserted) {
            erase_policy(policy_index);
        } else {
            policy.restoring = previous_restoring;
        }
        return refused("Native acquaintance transaction failed; receipt retained for cleanup", entered);
    }

    if (!enabled) erase_policy(policy_index);
    else friend_policies[policy_index].restoring = false;
    auto result = accepted();
    result.native_entered = entered;
    return result;
}

bool sa_erase_player_friend(const ObjectiveActor& actor) {
    if (actor.handle < 0 || !actor.native_ped) return false;
    const auto live = sa_executor_profile_matches() ? sa_live_objective_actor(actor.handle) : std::nullopt;
    const bool actor_is_current = live && *live == actor;
    const auto actor_slot = uint32_t(actor.handle) >> 8;
    for (size_t index = 0; index < friend_policies.size();) {
        const auto& policy = friend_policies[index];
        if (policy.actor == actor ||
            (actor_is_current && (policy.actor.native_ped == actor.native_ped ||
                (uint32_t(policy.actor.handle) >> 8) == actor_slot))) {
            // The callback receipt identifies this deleted actor, or a live
            // new generation proves that the saved pointer belongs to its old
            // occupant. Neither case permits snapshot writeback.
            erase_policy(index);
            continue;
        }
        ++index;
    }
    return true;
}

bool sa_release_player_friends() {
    if (!friend_policies.empty() && !sa_executor_profile_matches()) return false;
    bool complete = true;
    for (size_t index = 0; index < friend_policies.size();) {
        auto& policy = friend_policies[index];
        const auto state = inspect_actor_pool_state(policy.actor);
        if (state == ActorPoolState::unavailable) {
            // Unreadable, unstable, or unprofiled pool state is not proof that
            // the actor died; retain ownership for a later cleanup retry.
            complete = false;
            ++index;
            continue;
        }
        if (state == ActorPoolState::retired) {
            // A stable pool snapshot proves the identity is gone. Never write
            // a saved block into a free or recycled slot.
            erase_policy(index);
            continue;
        }

        std::array<uint32_t, acquaintance_word_count> before{};
        if (!writable_acquaintance_block(policy.actor.native_ped) ||
            !read_acquaintance(policy.actor.native_ped, before)) {
            complete = false;
            ++index;
            continue;
        }
        const auto after = restored_masks(before, policy);
        if (before == after) {
            if (inspect_actor_pool_state(policy.actor) == ActorPoolState::unavailable) {
                complete = false;
                ++index;
                continue;
            }
            erase_policy(index);
            continue;
        }
        bool entered = false, rolled_back = false;
        if (!commit_masks(policy.actor, before, after, entered, rolled_back)) {
            policy.restoring = true;
            complete = false;
            ++index;
            continue;
        }
        erase_policy(index);
    }
    return complete;
}

}
