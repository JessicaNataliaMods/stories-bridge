#pragma once
#include "lcs/script_marker_pool.hpp"
#include <filesystem>
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
std::optional<float> sa_script_marker_ground(float x,float y);
bool sa_script_marker_submit(ScriptMarkerPool&,const ScriptMarkerCommand&,float time_step);
}
