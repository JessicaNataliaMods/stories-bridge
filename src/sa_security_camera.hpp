#pragma once

#include "lcs/native.hpp"

namespace lcs {

// Enable/disable San Andreas' native CCTV post effect for the source
// SWITCH_SECURITY_CAMERA command. This changes presentation only; it never
// changes the active camera or its pose.
NativeResult sa_security_camera_set(bool enabled);

// Restore the effect only if it is still in the state this module applied.
// Returns false when the retail backend cannot be reached or verified.
bool sa_security_camera_cleanup();

#ifdef LCS_ISOLATED_SECURITY_CAMERA_BACKEND_TEST
struct SecurityCameraTestCallbacks {
    bool (*profile_matches)();
    bool (*read_cctv)(bool* enabled);
    bool (*switch_cctv)(bool enabled);
};
void sa_security_camera_test_bind(const SecurityCameraTestCallbacks& callbacks);
void sa_security_camera_test_reset();
#endif

}
