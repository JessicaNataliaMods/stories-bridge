#pragma once

#include "lcs/fx_particles.hpp"

namespace lcs {

// San Andreas FxManager/FxSystem_c backend for FxParticles (retail 1.0 US). Systems are
// created by name through FxManager_c::CreateFxSystem (0x4A9BE0), started with Play
// (0x4AA2F0), fed with FxSystem_c::AddParticle (0x4AA440) and released with Kill (0x4AA3F0).
// A system pointer is trusted only while it is still in FxManager's list with the blueprint
// recorded at creation and without SA's kill flag.
FxStatus sa_fx_emit(const FxParticleRequest& request);
// Strength follows SA's own script-fire rule (CFire::CreateFxSysForStrength): <=1 "fire",
// <=2 "fire_med", otherwise "fire_large". Returns a generation-bearing token or 0.
uint32_t sa_fx_fire_start(const FxVec3& position,float strength,FxStatus& status);
// True when the system was killed or is already gone/replaced; false means retry later.
bool sa_fx_fire_stop(uint32_t token);
// Kills every system this backend created. A refused Kill keeps the reference (and, for fires, the
// slot and generation); false means some stop is still unconfirmed and sa_fx_service_pending() must
// keep running. Systems not created by this backend are never touched.
bool sa_fx_release_all();
// Retries every unconfirmed stop. Driven by the game frame hooks even after the campaign halted.
void sa_fx_service_pending();
size_t sa_fx_pending();
// Whether the profiles of every native entry point match retail.
bool sa_fx_profiles_match();

}
