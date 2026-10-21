#pragma once
#include <cstdint>

namespace lcs {
// SET_CHAR_IGNORE_THREATS_BEHIND_OBJECTS (1383) over SA's own threat perception.
//
// The source flag CPed::bIgnoreThreatsBehindObjects is read only as the "objects block the line of sight" argument of the
// ped's threat scan. SA's scan (CEventScanner -> CPedGeometryAnalyser::CanPedTargetPed -> CanPedTargetPoint) already asks
// CWorld::GetIsLineOfSightClear with objects=1 for every ped, which is exactly TRUE. FALSE (objects do not hide threats)
// is served per actor: the single GetIsLineOfSightClear call inside CanPedTargetPoint is redirected to a pass-through
// wrapper that clears the `objects` argument when the scanning ped (ESI of that function) is one of the flagged actors
// and leaves every other ped untouched. The redirection exists only while at least one actor is flagged.
//
// objects_block = true  : SA native behaviour for this actor (a previous FALSE is withdrawn).
// objects_block = false : this actor's threat scan sees through objects until it dies, the flag is set again, or release.
// false is returned (command unsupported) when the actor is not live, the image does not match the pinned chain, or the
// redirection cannot be installed/removed with proof.
bool sa_set_char_threats_behind_objects(int32_t ped_handle,bool objects_block);
// Withdraws every flag and restores the original call instruction; false = could not prove restoration (retry later).
bool sa_release_threat_occlusion();
bool sa_threat_occlusion_installed();
}
