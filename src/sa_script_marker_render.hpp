#pragma once
#include "lcs/script_marker_pool.hpp"
#include <filesystem>
#include <string>
namespace lcs {
// Requires actual render and before-shutdown bindings; merely compiling this
// module never admits a source opcode or allocates native graphics resources.
void sa_script_marker_render_bound(uintptr_t frame_callback,uintptr_t shutdown_callback);
bool sa_script_marker_render_profile();
bool sa_script_marker_phase_profile();
bool sa_script_marker_resources_load(const std::filesystem::path&);
bool sa_script_marker_frame(ScriptMarkerPool&);
bool sa_script_marker_resources_release();
bool sa_script_marker_render_ready();
bool sa_script_marker_health();
// Reason of the latest renderer refusal, and a reset for the cylinder replacement, which falls back to SA's marker.
const char* sa_script_marker_last_failure();
void sa_script_marker_set_logger(void (*)(const std::string&));
void sa_script_marker_forgive();
std::optional<float> sa_script_marker_ground(float x,float y);
bool sa_script_marker_submit(ScriptMarkerPool&,const ScriptMarkerCommand&,float time_step);
// Retail C3dMarkers::PlaceMarkerSet for the yellow mission cylinder (mission
// spheres, contact-point blips): three nested type-5 shells with the original
// colour. Size/pulse come from the native SA caller, which shares the retail
// values. False means nothing was accepted and the SA marker should draw.
bool sa_script_marker_cylinder_set(ScriptMarkerPool&,uintptr_t identity,const std::array<float,3>& position,
                                   float size,float pulse_fraction,uint16_t pulse_period,float time_step);
}
