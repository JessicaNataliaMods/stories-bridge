#pragma once
#include <filesystem>
namespace rw { struct Clump; }
// SA's automobile loader takes one wheel from wheel_rf_dummy and clones it.
// LCS instead attaches its DEFAULT.IDE wheel model to every wheel dummy.
// Explicit source ENVMAP materials keep their MatFX data and enable the
// per-atomic MatFX pipeline flag consumed by SA's vehicle renderer. The exact
// LCS env texture xv_vehicleenv128 is remapped to SA's native xvehicleenv128;
// its source pixels are absent from LCS PS2 TXDs, while SA resolves this name
// from vehicle.txd. Every explicit car ENVMAP also receives an SA EnvMat with
// unit scales and shininess derived from its MatFX coefficient; no SpecMat is
// inferred where the source has no specular signal.
void adapt_sa_car(rw::Clump* car, const std::filesystem::path& wheel, float wheel_scale,
                  const std::filesystem::path& collision = {});
void validate_sa_car(const std::filesystem::path& car);
// Adapt the original LCS ferry's two atomics to the generic SA CBoat slot
// selected by the package (TROPIC). Meshes and mission dummies stay intact;
// SA's seat marker is added as an alias of the retained Ped_point dummy.
void adapt_sa_boat(rw::Clump* boat, const std::filesystem::path& collision);
void validate_sa_boat(const std::filesystem::path& boat);
void merge_vehicle_textures(const std::filesystem::path& own,
                            const std::filesystem::path& shared,
                            const std::filesystem::path& output);
