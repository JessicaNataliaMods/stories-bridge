#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../src/sa_lcs_bridge.cpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace lcs {
bool sa_executor_profile_matches() { return true; }
}

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

uint32_t address32(const void* pointer) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
}

struct NativePoolLayout {
    uint32_t entries;
    uint32_t flags;
    int32_t size;
};

struct alignas(4) NativeMatrixLayout {
    uint8_t prefix[0x30]{};
    float position[3]{};
};

struct alignas(4) NativeBuildingSlot {
    uint8_t bytes[0x38]{};
};

static_assert(sizeof(uintptr_t) == 4, "The retail SA CPool layout is 32-bit");
static_assert(offsetof(NativePoolLayout, flags) == 4 && offsetof(NativePoolLayout, size) == 8,
              "The fixture must use the production CPool header layout");
static_assert(offsetof(NativeMatrixLayout, position) == 0x30, "CMatrix position offset");
static_assert(sizeof(NativeBuildingSlot) == 0x38, "CBuilding pool stride");

struct BuildingPoolFixture {
    std::array<NativeBuildingSlot, 8> slots{};
    std::array<NativeMatrixLayout, 8> matrices{};
    std::array<uint8_t, 8> slot_flags{};
    NativePoolLayout pool{};
    uintptr_t pool_pointer = 0;
    int32_t count = 0;

    BuildingPoolFixture() {
        pool.entries = address32(slots.data());
        pool.flags = address32(slot_flags.data());
        pool.size = static_cast<int32_t>(slots.size());
        slot_flags.fill(0x80);
        pool_pointer = reinterpret_cast<uintptr_t>(&pool);
    }

    void add(uint16_t model, uint8_t entity_type, float x, float y, float z) {
        const int32_t index = count++;
        auto* entity = slots[static_cast<size_t>(index)].bytes;
        const uint32_t matrix_address = address32(&matrices[static_cast<size_t>(index)]);
        std::memcpy(entity + 0x14, &matrix_address, sizeof(matrix_address));
        std::memcpy(entity + 0x22, &model, sizeof(model));
        entity[0x36] = entity_type & 7;
        slot_flags[static_cast<size_t>(index)] = 0;
        matrices[static_cast<size_t>(index)].position[0] = x;
        matrices[static_cast<size_t>(index)].position[1] = y;
        matrices[static_cast<size_t>(index)].position[2] = z;
    }

    uintptr_t root() const { return reinterpret_cast<uintptr_t>(&pool_pointer); }

    void add_expected_three() {
        add(11348, 1, -283.1590271f, -629.8465576f, 74.7115631f);
        add(11349, 1, -283.6372070f, -630.5426636f, 128.0713501f);
        add(11350, 1, -279.4381714f, -631.5642090f, 43.0088234f);
    }

    uintptr_t entity_address(size_t slot) const {
        return reinterpret_cast<uintptr_t>(slots[slot].bytes);
    }
};

constexpr lcs::SaLcsBridgeModelIds bridge_ids{11348, 11349, 11350};

void resolver_contract() {
    BuildingPoolFixture correct;
    correct.add_expected_three();
    lcs::SaLcsBridgeOwnedEntities owned{};
    check(lcs::sa_lcs_bridge_resolve_owned_pool_status(correct.root(), bridge_ids, owned) ==
              lcs::SaLcsBridgeResolveStatus::ready,
          "All three uniquely placed type-1 buildings resolve from the actual CPool layout");
    check(lcs::sa_lcs_bridge_resolve_owned_pool(correct.root(), bridge_ids, owned),
          "Boolean resolver reports a complete owned bridge");
    check(owned.lift.address == correct.entity_address(0) && owned.lift.pool_slot == 0 &&
          owned.weight.address == correct.entity_address(1) && owned.weight.pool_slot == 1 &&
          owned.road.address == correct.entity_address(2) && owned.road.pool_slot == 2,
          "Each active v63 IPL placement binds to its own SA building slot");

    BuildingPoolFixture wrong_class;
    wrong_class.add(11348, 4, -283.1590271f, -629.8465576f, 74.7115631f);
    wrong_class.add(11349, 1, -283.6372070f, -630.5426636f, 128.0713501f);
    wrong_class.add(11350, 1, -279.4381714f, -631.5642090f, 43.0088234f);
    owned.lift.address = 0x12345678;
    check(lcs::sa_lcs_bridge_resolve_owned_pool_status(wrong_class.root(), bridge_ids, owned) ==
              lcs::SaLcsBridgeResolveStatus::unexpected_mismatch && owned.lift.address == 0,
          "A bridge model in an object (type 4) slot is rejected and clears stale output");

    BuildingPoolFixture streamed_out;
    streamed_out.add_expected_three();
    streamed_out.slot_flags[1] = 0x80;
    check(lcs::sa_lcs_bridge_resolve_owned_pool_status(streamed_out.root(), bridge_ids, owned) ==
              lcs::SaLcsBridgeResolveStatus::retry_streaming &&
          !lcs::sa_lcs_bridge_resolve_owned_pool(streamed_out.root(), bridge_ids, owned),
          "A missing lift/counterweight is reported as a source-compatible stream retry");

    BuildingPoolFixture duplicate;
    duplicate.add_expected_three();
    duplicate.add(11348, 1, -283.1590271f, -629.8465576f, 74.7115631f);
    check(lcs::sa_lcs_bridge_resolve_owned_pool_status(duplicate.root(), bridge_ids, owned) ==
              lcs::SaLcsBridgeResolveStatus::unexpected_mismatch,
          "A duplicate owned placement is rejected rather than choosing an arbitrary pool entry");

    BuildingPoolFixture collision;
    collision.add(11348, 1, -283.1590271f, -629.8465576f, 74.7115631f);
    collision.add(11349, 1, -283.6372070f, -630.5426636f, 128.0713501f);
    collision.add(11350, 1, -283.1590271f, -629.8465576f, 43.0088234f);
    check(lcs::sa_lcs_bridge_resolve_owned_pool_status(collision.root(), bridge_ids, owned) ==
              lcs::SaLcsBridgeResolveStatus::unexpected_mismatch,
          "A bridge model occupying another placement's coordinates is rejected");

    BuildingPoolFixture unrelated_collision;
    unrelated_collision.add_expected_three();
    unrelated_collision.add(12000, 1, -283.1590271f, -629.8465576f, 74.7115631f);
    check(lcs::sa_lcs_bridge_resolve_owned_pool_status(unrelated_collision.root(), bridge_ids, owned) ==
              lcs::SaLcsBridgeResolveStatus::ready &&
          owned.lift.address == unrelated_collision.entity_address(0),
          "An unrelated building at a bridge coordinate is never captured as bridge ownership");

    BuildingPoolFixture optional_road;
    optional_road.add_expected_three();
    optional_road.slot_flags[2] = 0x80;
    check(lcs::sa_lcs_bridge_resolve_owned_pool_status(optional_road.root(), bridge_ids, owned) ==
              lcs::SaLcsBridgeResolveStatus::ready && owned.road.address == 0,
          "The source treats the road segment as optional while the lift and weight are required");
}

void state_contract() {
    const auto locked = lcs::sa_lcs_bridge_state_request(6, 0);
    const auto unlocked = lcs::sa_lcs_bridge_state_request(0, 1);
    const auto operational_from_down = lcs::sa_lcs_bridge_state_request(3, 2);
    const auto operational_from_unlocked = lcs::sa_lcs_bridge_state_request(6, 2);
    const auto operational_from_up = lcs::sa_lcs_bridge_state_request(1, 2);
    check(locked.valid && locked.next_state == 0 && locked.block_path_links,
          "Mode 0 locks the bridge and blocks native path links");
    check(unlocked.valid && unlocked.next_state == 6 && !unlocked.block_path_links,
          "Mode 1 sets the retail always-unlocked state and opens path links");
    check(operational_from_down.valid && operational_from_down.next_state == 4 &&
          operational_from_unlocked.next_state == 4 && operational_from_up.next_state == 2,
          "Mode 2 follows Script9's exact down/unlocked switch");
    check(!lcs::sa_lcs_bridge_state_request(0, 3).valid,
          "Unsupported bridge state requests fail closed");

    const auto locked_tick = lcs::sa_lcs_bridge_cycle_sample(0, 55, 1000);
    const auto unlocked_tick = lcs::sa_lcs_bridge_cycle_sample(6, 0, 1000);
    check(locked_tick.state == 0 && locked_tick.operational_since == 0 &&
          locked_tick.lift_height == 25.0f,
          "Locked cycle clears the operational timer and keeps the lift up");
    check(unlocked_tick.state == 6 && unlocked_tick.operational_since == 1000u - 20001u &&
          unlocked_tick.lift_height == 0.0f,
          "Always-unlocked cycle preserves the source timer offset and lowered lift");

    const uint32_t start = 1000;
    check(lcs::sa_lcs_bridge_cycle_sample(2, start, start + 20000u).state == 3 &&
          lcs::sa_lcs_bridge_cycle_sample(2, start, start + 80000u).state == 4 &&
          lcs::sa_lcs_bridge_cycle_sample(2, start, start + 90000u).state == 5,
          "Cycle boundaries select down, about-to-move-up, and moving-up states");
    const auto tail = lcs::sa_lcs_bridge_cycle_sample(2, start, start + 110000u);
    check(tail.state == 1 && tail.lift_height == 25.0f,
          "Cycle tail explicitly assigns STATE_LIFT_PART_IS_UP (1)");

    const uint32_t wrapped_start = 0xFFFFFF00u;
    const auto wrapped = lcs::sa_lcs_bridge_cycle_sample(2, wrapped_start, 0x00020400u);
    check(wrapped.state == 2 && std::fabs(wrapped.lift_height - 23.4f) < 0.01f,
          "Unsigned timer subtraction and cycle modulo remain correct across uint32 wrap");
}

struct MembershipFixture {uintptr_t table;int additions=0,removals=0,wrong_slot=0;};
void __fastcall fixture_add(uintptr_t entity,uintptr_t){++reinterpret_cast<MembershipFixture*>(entity)->additions;}
void __fastcall fixture_remove(uintptr_t entity,uintptr_t){++reinterpret_cast<MembershipFixture*>(entity)->removals;}
void __fastcall fixture_wrong(uintptr_t entity,uintptr_t){++reinterpret_cast<MembershipFixture*>(entity)->wrong_slot;}
void membership_contract() {
    std::array<uintptr_t,4> table{0,reinterpret_cast<uintptr_t>(&fixture_wrong),
        reinterpret_cast<uintptr_t>(&fixture_add),reinterpret_cast<uintptr_t>(&fixture_remove)};
    MembershipFixture entity{reinterpret_cast<uintptr_t>(table.data())};
    check(lcs::sa_world_entity_remove(reinterpret_cast<uintptr_t>(&entity)) &&
          lcs::sa_world_entity_add(reinterpret_cast<uintptr_t>(&entity)),"Native membership virtual dispatch succeeds");
    check(entity.additions==1 && entity.removals==1 && entity.wrong_slot==0,
          "Physical Add()/Remove overrides are preserved; Add(CRect&) is never called");
    table[3]=0x42480000;
    check(!lcs::sa_world_entity_remove(reinterpret_cast<uintptr_t>(&entity)) && entity.removals==1,
          "Destroyed/base-placeable data slot is refused before indirect call");
}
} // namespace

int main() {
    try {
        resolver_contract();
        state_contract();
        membership_contract();
        std::cout << "PASS: production bridge pool ownership, streaming/mismatch status, Script9 state request and cycle timing; no game/device executed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
