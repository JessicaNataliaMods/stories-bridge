#include "../src/sa_security_camera.hpp"

#include <iostream>
#include <stdexcept>

namespace {
bool available = true;
bool cctv = false;
bool fail_switch = false;
int switch_calls = 0;

bool profile_matches() { return available; }
bool read_cctv(bool* enabled) {
    if (!available) return false;
    *enabled = cctv;
    return true;
}
bool switch_cctv(bool enabled) {
    ++switch_calls;
    if (!available || fail_switch) return false;
    cctv = enabled;
    return true;
}
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void reset(bool initial = false) {
    lcs::sa_security_camera_test_reset();
    available = true;
    cctv = initial;
    fail_switch = false;
    switch_calls = 0;
    lcs::sa_security_camera_test_bind({profile_matches, read_cctv, switch_cctv});
}
}

int main() {
    try {
        reset();
        auto enabled = lcs::sa_security_camera_set(true);
        check(enabled.executed && enabled.native_entered && cctv && switch_calls == 1,
              "enable applies the native CCTV post effect");
        check(lcs::sa_security_camera_set(true).executed && switch_calls == 1,
              "repeated enable is idempotent and retains the first snapshot");
        check(lcs::sa_security_camera_cleanup() && !cctv && switch_calls == 2,
              "cleanup restores the state captured before ownership");
        check(lcs::sa_security_camera_cleanup() && switch_calls == 2,
              "cleanup is idempotent after ownership ends");

        reset();
        check(lcs::sa_security_camera_set(true).executed && cctv,
              "module acquires CCTV for cleanup ownership");
        cctv = false;
        check(lcs::sa_security_camera_cleanup() && !cctv && switch_calls == 1,
              "cleanup leaves a later native change untouched");

        reset(true);
        check(lcs::sa_security_camera_set(false).executed && !cctv && switch_calls == 1,
              "false command reaches the native switch even when another owner enabled CCTV");

        reset(true);
        check(lcs::sa_security_camera_set(true).executed && switch_calls == 0,
              "pre-existing CCTV remains owned by its existing native owner");
        cctv = false;
        check(lcs::sa_security_camera_cleanup() && !cctv && switch_calls == 0,
              "cleanup does not overwrite a later native change");

        reset();
        check(lcs::sa_security_camera_set(true).executed && cctv,
              "owned CCTV state is acquired for false-to-true transition");
        cctv = false;
        check(lcs::sa_security_camera_set(false).executed && !cctv && switch_calls == 1,
              "disable preserves a later external change instead of restoring over it");

        reset();
        fail_switch = true;
        const auto failed = lcs::sa_security_camera_set(true);
        check(!failed.executed && failed.native_entered && !cctv,
              "native failure is reported without claiming the effect succeeded");
        check(lcs::sa_security_camera_cleanup() && !cctv,
              "cleanup retires ownership when the failed native call changed no state");

        reset();
        available = false;
        const auto unavailable = lcs::sa_security_camera_set(true);
        check(!unavailable.executed && !unavailable.native_entered && switch_calls == 0,
              "unsupported profile is rejected before touching native effect state");

        std::cout << "PASS: native CCTV effect transition, ownership, compare-and-restore, cleanup and failure containment; no game executed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
