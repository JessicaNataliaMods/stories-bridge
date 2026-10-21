#define LCS_ATTACK_TIMER_BACKEND_TESTS
#include "../src/sa_attack_timer.cpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lcs;
constexpr uintptr_t gun_vtable = 0x00A1B2C0;
constexpr uintptr_t native_process_ped = 0x00625270;
constexpr uint32_t sentinel = UINT32_MAX;
constexpr int8_t fireburst = 3;
constexpr int8_t aim = 1;

struct alignas(128) TaskMemory { uint8_t bytes[128]{}; };

uint32_t ped_memory = 0;
TaskMemory gun_task;
TaskMemory use_gun_task;
const uintptr_t ped_address = reinterpret_cast<uintptr_t>(&ped_memory);
const uintptr_t gun_task_address = reinterpret_cast<uintptr_t>(&gun_task);
const uintptr_t use_gun_task_address = reinterpret_cast<uintptr_t>(&use_gun_task);
ObjectiveActor actor{27, ped_address};
bool actor_live = true;
uint32_t now_ms = 0;
uint32_t native_period_ms = 800;
uint32_t native_calls = 0;
uint32_t bursts = 0;
uint32_t leisure_ends = 0;
uint32_t last_native_process_ms = 0;
std::array<uintptr_t, 8> vtable{};

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void set_now(uint32_t value) {
    now_ms = value;
    attack_timer_test::set_now(value);
}

template<class T>
void write(uintptr_t address, uint32_t offset, T value) {
    std::memcpy(reinterpret_cast<void*>(address + offset), &value, sizeof(value));
}

template<class T>
T read(uintptr_t address, uint32_t offset) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address + offset), sizeof(value));
    return value;
}

bool native_process(uintptr_t task, uintptr_t) {
    ++native_calls;
    const uint32_t elapsed = now_ms - last_native_process_ms;
    last_native_process_ms = now_ms;
    const int32_t leisure_duration = read<int32_t>(task, 0x28);
    if (leisure_duration > 0) {
        if (elapsed >= static_cast<uint32_t>(leisure_duration)) {
            write(task, 0x28, int32_t(0));
            write(task, 0x2E, int8_t(6));
            write(use_gun_task_address, 0x0F, int8_t(6));
            ++leisure_ends;
            return true;
        }
        write(task, 0x28, leisure_duration - static_cast<int32_t>(elapsed));
    }
    const uint32_t deadline = read<uint32_t>(task, 0x34);
    if (deadline == sentinel) {
        if (read<int8_t>(use_gun_task_address, 0x10) != fireburst) {
            write(task, 0x34, uint32_t(0));
            write(task, 0x34, uint32_t(now_ms + native_period_ms));
            const int8_t next = read<int8_t>(use_gun_task_address, 0x0F);
            if (next < aim) write(use_gun_task_address, 0x0F, aim);
        }
        return false;
    }
    if (now_ms < deadline) return false; // PE 0x62555F uses unsigned JB.
    write(task, 0x34, sentinel);          // PE 0x625576, before ControlGun(FIREBURST).
    write(use_gun_task_address, 0x0F, fireburst);
        write(use_gun_task_address, 0x10, fireburst);
    ++bursts;
    return false;
}

void reset(uint32_t now = 100, uint32_t native_period = 800,
           int32_t native_leisure_duration = -1) {
    actor_live = true;
    now_ms = now;
    native_period_ms = native_period;
    native_calls = 0;
    bursts = 0;
    leisure_ends = 0;
    last_native_process_ms = now;
    gun_task = {};
    use_gun_task = {};
    vtable = {};
    write(gun_task_address, 0, gun_vtable);
    write(gun_task_address, 0x28, native_leisure_duration);
    write(gun_task_address, 0x2E, int8_t(2));
    write(gun_task_address, 0x34, uint32_t(0));
    write(use_gun_task_address, 0x0F, aim);
    write(use_gun_task_address, 0x10, aim);
    vtable[7] = native_process_ped;
    attack_timer_test::bind(vtable.data(), gun_vtable, native_process_ped, native_process, now);
    attack_timer_test::set_use_gun(ped_address, use_gun_task_address);
}

uint32_t start_native_burst_then_arm(uint32_t interval) {
    check(sa_set_char_attack_timer(actor, interval), "Nonzero policy registers for a live actor");
    check(!attack_timer_test::process(gun_task_address, ped_address), "Native first burst returns normally");
    check(bursts == 1 && read<uint32_t>(gun_task_address, 0x34) == sentinel,
          "The first burst remains native and enters the native wait sentinel");
    write(use_gun_task_address, 0x0F, fireburst);
    write(use_gun_task_address, 0x10, fireburst);
    attack_timer_test::process(gun_task_address, ped_address);
    write(use_gun_task_address, 0x0F, aim);
    write(use_gun_task_address, 0x10, aim);
    attack_timer_test::process(gun_task_address, ped_address);
    const uint32_t expected = now_ms + interval;
    const uint32_t reserved_safe = expected == 0 ? 1 : expected == sentinel ? sentinel - 1 : expected;
    check(read<uint32_t>(gun_task_address, 0x34) == reserved_safe,
          "Only the completed native burst arms the raw uint32 interval");
    return reserved_safe;
}

void test_first_burst_and_native_cadence() {
    reset();
    const uint32_t custom = start_native_burst_then_arm(3000);
    check(custom == 3100 && bursts == 1, "Override begins after the first native burst");
    set_now(custom - 1);
    attack_timer_test::process(gun_task_address, ped_address);
    check(bursts == 1 && read<uint32_t>(gun_task_address, 0x34) == custom,
          "Native ProcessPed preserves the requested threshold before it expires");
    set_now(custom);
    attack_timer_test::process(gun_task_address, ped_address);
    check(bursts == 2 && read<uint32_t>(gun_task_address, 0x34) == sentinel &&
          read<int8_t>(use_gun_task_address, 0x0F) == fireburst,
          "SA issues the next burst through its own sentinel and UseGun command path");
}

void test_zero_change_and_release_restore() {
    reset();
    start_native_burst_then_arm(3000);
    check(sa_set_char_attack_timer(actor, 0), "Zero removes the override");
    check(read<uint32_t>(gun_task_address, 0x34) == now_ms + native_period_ms,
          "Zero restores the last SA-scheduled deadline");

    reset();
    start_native_burst_then_arm(3000);
    const uint32_t native_deadline = now_ms + native_period_ms;
    check(sa_set_char_attack_timer(actor, 5000), "Changing interval replaces the policy");
    check(read<uint32_t>(gun_task_address, 0x34) == native_deadline,
          "Changing interval restores the old native deadline before replacing policy");

    reset();
    start_native_burst_then_arm(3000);
    attack_timer_test::fail_next_deadline_write();
    check(!sa_set_char_attack_timer(actor, 0), "Zero reports a failed checked restoration");
    check(read<uint32_t>(gun_task_address, 0x34) != now_ms + native_period_ms,
          "Failed zero retains the owned deadline receipt for retry");
    check(sa_set_char_attack_timer(actor, 0), "Retrying zero completes checked restoration");
    check(read<uint32_t>(gun_task_address, 0x34) == now_ms + native_period_ms,
          "Successful retry restores the native deadline");

    reset();
    start_native_burst_then_arm(3000);
    attack_timer_test::fail_next_deadline_write();
    check(!sa_release_char_attack_timer(), "Release reports a failed checked restoration");
    check(vtable[7] == attack_timer_test::process_hook_address(),
          "Release keeps the forwarding hook while a deadline receipt needs retry");
    check(sa_release_char_attack_timer(), "Release retry restores and removes the hook");
    check(vtable[7] == native_process_ped &&
          read<uint32_t>(gun_task_address, 0x34) == now_ms + native_period_ms,
          "Successful release restores the task and original vtable entry");
}

void test_identity_and_foreign_deadline_guards() {
    reset();
    const uint32_t custom = start_native_burst_then_arm(3000);
    attack_timer_test::set_task_generation(gun_task_address, 1);
    sa_clear_char_attack_timer(actor);
    check(read<uint32_t>(gun_task_address, 0x34) == custom,
          "A recycled CTask generation never receives the old deadline restoration");
    check(sa_release_char_attack_timer(), "Recycled task state can be safely released");

    reset();
    start_native_burst_then_arm(3000);
    write(gun_task_address, 0x34, uint32_t(4567));
    check(sa_set_char_attack_timer(actor, 0), "Zero clears without overwriting a foreign deadline");
    check(read<uint32_t>(gun_task_address, 0x34) == 4567,
          "A deadline changed by SA or another owner is preserved");
    check(sa_release_char_attack_timer(), "Foreign deadline does not block hook cleanup");
}

void test_unsigned_boundaries() {
    reset(0xFFFFFFFDu);
    check(start_native_burst_then_arm(2) == 0xFFFFFFFEu,
          "Interval reaching the UINT32_MAX wait sentinel moves one millisecond aside");
    check(sa_set_char_attack_timer(actor, 0), "Boundary deadline clears normally");
    check(read<uint32_t>(gun_task_address, 0x34) == now_ms + native_period_ms,
          "Boundary cleanup restores modulo-32-bit native time");

    reset(0xFFFFFFFEu);
    check(start_native_burst_then_arm(2) == 1,
          "A modulo-32-bit zero deadline is reserved as immediate and moved by one tick");
    check(sa_set_char_attack_timer(actor, 0), "Wrapped-zero deadline clears normally");

    reset(10);
    check(start_native_burst_then_arm(UINT32_MAX) == 9,
          "Full-width uint32 intervals retain raw modulo-32-bit addition");
    check(sa_set_char_attack_timer(actor, 0), "Full-width deadline clears normally");
}

void test_shoot_timer_native_restore_and_end_leisure() {
    reset(100, 800, 9000);
    const uint32_t ped_before = ped_memory;
    check(sa_set_char_attack_timer(actor, 3000), "1586 interval remains independently active");
    check(sa_set_char_shoot_timer(actor, 250), "1585 duration policy registers on the same hook");
    check(!attack_timer_test::process(gun_task_address, ped_address),
          "SA processes the original first UseGun burst");
    check(bursts == 1 && read<int32_t>(gun_task_address, 0x28) == 250 &&
          read<int8_t>(use_gun_task_address, 0x0F) == fireburst,
          "1585 starts only after native UseGun issues FIREBURST");
    check(read<uint32_t>(gun_task_address, 0x34) == sentinel,
          "Shoot duration does not replace the independent 1586 attack deadline");

    set_now(200);
    attack_timer_test::process(gun_task_address, ped_address);
    check(read<int32_t>(gun_task_address, 0x28) == 150,
          "Native GunControl decrements the SA leisure countdown");
    check(sa_set_char_shoot_timer(actor, 0), "Zero restores the remaining SA duration");
    check(read<int32_t>(gun_task_address, 0x28) == 8900,
          "Clearing 1585 restores the original duration with native elapsed time applied");

    reset(100, 800, 9000);
    check(sa_set_char_shoot_timer(actor, 250), "Duration can be installed independently");
    attack_timer_test::process(gun_task_address, ped_address);
    attack_timer_test::fail_next_duration_write();
    check(!sa_set_char_shoot_timer(actor, 0),
          "A failed restore reports failure and retains its ownership receipt");
    check(read<int32_t>(gun_task_address, 0x28) == 250,
          "A failed restore never drops or changes the timer receipt");
    attack_timer_test::process(gun_task_address, ped_address);
    check(read<int32_t>(gun_task_address, 0x28) == 9000,
          "The installed forwarding hook retries the retained native duration restore");
    check(sa_release_char_attack_timer(), "Restored shoot duration releases the shared hook");

    reset(100, 800, 9000);
    check(sa_set_char_shoot_timer(actor, 100), "Short native duration policy is accepted");
    attack_timer_test::process(gun_task_address, ped_address);
    set_now(201);
    check(attack_timer_test::process(gun_task_address, ped_address),
          "Native GunControl completes after its leisure duration expires");
    check(leisure_ends == 1 && bursts == 1 && read<int8_t>(gun_task_address, 0x2E) == 6 &&
          read<int8_t>(use_gun_task_address, 0x0F) == 6,
          "Expiry follows SA's END_LEISURE / UseGun abort path without extra bursts");
    check(ped_memory == ped_before,
          "GunControl completion does not remove the outer actor objective or kill the ped");
    check(sa_set_char_shoot_timer(actor, 0), "Expired duration policy clears cleanly");
    check(sa_release_char_attack_timer(), "Expired native task releases the shared hook");
}

void test_shoot_timer_full_uint32_segmentation() {
    reset();
    check(sa_set_char_shoot_timer(actor, UINT32_MAX),
          "1585 preserves the full raw uint32 duration");
    attack_timer_test::process(gun_task_address, ped_address);
    check(read<int32_t>(gun_task_address, 0x28) == INT32_MAX && bursts == 1,
          "The first signed SA segment is positive and never aliases the -1 sentinel");

    const uint32_t first_slice = static_cast<uint32_t>(INT32_MAX / 2 + 1);
    set_now(now_ms + first_slice);
    check(!attack_timer_test::process(gun_task_address, ped_address) &&
          read<int32_t>(gun_task_address, 0x28) == INT32_MAX / 2,
          "Native countdown reaches the renewal point without ending UseGun");
    check(!attack_timer_test::process(gun_task_address, ped_address) &&
          read<int32_t>(gun_task_address, 0x28) == INT32_MAX,
          "The remaining raw uint32 tail is added before SA can consume the final timestep");

    set_now(now_ms + first_slice);
    check(!attack_timer_test::process(gun_task_address, ped_address) &&
          read<int32_t>(gun_task_address, 0x28) == INT32_MAX / 2,
          "The second segment renews while the native countdown remains positive");
    check(!attack_timer_test::process(gun_task_address, ped_address) &&
          read<int32_t>(gun_task_address, 0x28) == INT32_MAX,
          "All remaining full-width duration is represented by a positive native chunk");

    set_now(now_ms + static_cast<uint32_t>(INT32_MAX));
    check(attack_timer_test::process(gun_task_address, ped_address) && leisure_ends == 1 &&
          bursts == 1 && read<int32_t>(gun_task_address, 0x28) == 0,
          "UINT32_MAX expires through native END_LEISURE after its final SA chunk");
    check(sa_set_char_shoot_timer(actor, 0), "Segmented duration clears after native expiry");
    check(sa_release_char_attack_timer(), "Segmented duration releases the single shared hook");
}

void test_foreign_hook_is_preserved() {
    reset();
    constexpr uintptr_t foreign_hook=0x12345678;
    vtable[7]=foreign_hook;
    check(!sa_set_char_attack_timer(actor,100) && vtable[7]==foreign_hook,
          "A pre-existing foreign ProcessPed hook refuses installation without mutation");
    vtable[7]=native_process_ped;
    check(sa_set_char_attack_timer(actor,100),"An untouched native hook can be owned");
    vtable[7]=foreign_hook;
    check(!sa_release_char_attack_timer() && vtable[7]==foreign_hook,
          "Cleanup preserves a later plugin hook and retains forwarding state");
    vtable[7]=attack_timer_test::process_hook_address();
    check(sa_release_char_attack_timer() && vtable[7]==native_process_ped,
          "Cleanup retries only after our owned slot is visible again");
}

void test_unregistered_actor_remains_native() {
    reset();
    check(sa_set_char_attack_timer(actor, 100), "Test installs the forwarding hook through the public API");
    check(sa_set_char_attack_timer(actor, 0), "Test removes policy while keeping the hook installed");
    check(!attack_timer_test::process(gun_task_address, ped_address),
          "An actor without an explicit policy still runs ProcessPed");
    check(native_calls == 1 && bursts == 1 && read<uint32_t>(gun_task_address, 0x34) == sentinel,
          "The backend does not alter an unregistered actor's native burst");
    check(sa_release_char_attack_timer(), "Empty backend releases cleanly");
}
}

namespace lcs {
bool sa_executor_profile_matches() { return true; }
std::optional<ObjectiveActor> sa_live_objective_actor(int32_t handle) {
    return actor_live && handle == actor.handle ? std::optional<ObjectiveActor>(actor) : std::nullopt;
}
}

int main() {
    try {
        std::cerr << "case first-burst\n" << std::flush;
        test_first_burst_and_native_cadence();
        std::cerr << "case restore\n" << std::flush;
        test_zero_change_and_release_restore();
        std::cerr << "case identity\n" << std::flush;
        test_identity_and_foreign_deadline_guards();
        std::cerr << "case uint32\n" << std::flush;
        test_unsigned_boundaries();
        std::cerr << "case shoot-duration\n" << std::flush;
        test_shoot_timer_native_restore_and_end_leisure();
        std::cerr << "case shoot-uint32\n" << std::flush;
        test_shoot_timer_full_uint32_segmentation();
        std::cerr << "case unregistered\n" << std::flush;
        test_unregistered_actor_remains_native();
        test_foreign_hook_is_preserved();
        std::cout << "attack timer backend tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
