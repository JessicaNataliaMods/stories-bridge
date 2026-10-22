// Offline adapter for original LCS vehicle assets whose native SA class is
// not the class recorded in the source IDE row.
#include "convert_vehicle_special.hpp"
#include "convert_vehicle.hpp"

#include <rw.h>
#include <rwgta.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

rw::Clump* read_vehicle_clump(const std::filesystem::path& path) {
    rw::StreamFile in;
    if(!in.open(path.string().c_str(), "rb") ||
       !rw::findChunk(&in, rw::ID_CLUMP, nullptr, nullptr))
        throw std::runtime_error("Cannot open Dodo vehicle clump");
    auto* clump = rw::Clump::streamRead(&in);
    in.close();
    if(!clump)
        throw std::runtime_error("Cannot read Dodo vehicle clump");
    return clump;
}

void collect_frames(rw::Frame* frame, std::vector<rw::Frame*>& out) {
    if(!frame)
        return;
    out.push_back(frame);
    for(auto* child = frame->child; child; child = child->next)
        collect_frames(child, out);
}

rw::Frame* unique_frame(rw::Clump* clump, const char* name, bool required = true) {
    if(!clump || !clump->getFrame())
        throw std::runtime_error("Missing Dodo clump root");
    std::vector<rw::Frame*> all;
    collect_frames(clump->getFrame(), all);
    rw::Frame* found = nullptr;
    for(auto* frame : all) {
        if(std::strcmp(gta::getNodeName(frame), name) != 0)
            continue;
        if(found)
            throw std::runtime_error(std::string("Duplicate Dodo frame: ") + name);
        found = frame;
    }
    if(!found && required)
        throw std::runtime_error(std::string("Missing Dodo frame: ") + name);
    return found;
}

bool inside(rw::Frame* frame, rw::Frame* parent) {
    for(auto* current = frame; current; current = current->getParent())
        if(current == parent)
            return true;
    return false;
}

unsigned direct_atomics(rw::Clump* clump, rw::Frame* frame) {
    unsigned count = 0;
    FORLIST(link, clump->atomics) {
        auto* atomic = rw::Atomic::fromClump(link);
        if(atomic->getFrame() == frame)
            ++count;
    }
    return count;
}

unsigned subtree_atomics(rw::Clump* clump, rw::Frame* frame) {
    unsigned count = 0;
    FORLIST(link, clump->atomics) {
        auto* atomic = rw::Atomic::fromClump(link);
        if(atomic->getFrame() && inside(atomic->getFrame(), frame))
            ++count;
    }
    return count;
}

void check_geometry(rw::Clump* clump) {
    if(!clump || !clump->getFrame())
        throw std::runtime_error("Missing Dodo clump root");
    FORLIST(link, clump->atomics) {
        auto* atomic = rw::Atomic::fromClump(link);
        auto* geometry = atomic->geometry;
        if(!atomic->getFrame() || !geometry || rw::Skin::get(geometry) ||
           geometry->numMorphTargets != 1 || !geometry->numVertices || !geometry->numTriangles)
            throw std::runtime_error("Dodo clump contains invalid, skinned, or empty source geometry");
    }
}

void require_atomic(rw::Clump* clump, const char* name) {
    auto* frame = unique_frame(clump, name);
    if(frame == clump->getFrame() || direct_atomics(clump, frame) != 1)
        throw std::runtime_error(std::string("Dodo component must own exactly one atomic: ") + name);
}

void require_damage_pair(rw::Clump* clump, const char* dummy_name,
                         const char* ok_name, const char* damaged_name) {
    auto* dummy = unique_frame(clump, dummy_name);
    auto* ok = unique_frame(clump, ok_name);
    auto* damaged = unique_frame(clump, damaged_name);
    if(!inside(ok, dummy) || !inside(damaged, dummy))
        throw std::runtime_error(std::string("Dodo damage pair is outside its component: ") + dummy_name);
    require_atomic(clump, ok_name);
    require_atomic(clump, damaged_name);
}

void check_source_dodo(rw::Clump* dodo) {
    check_geometry(dodo);
    auto* root = dodo->getFrame();
    if(std::strcmp(gta::getNodeName(root), "dodo") != 0)
        throw std::runtime_error("Source clump is not the original LCS Dodo");
    if(unique_frame(dodo, "chassis", false) || unique_frame(dodo, "wheel", false))
        throw std::runtime_error("Dodo clump is already adapted or has ambiguous SA frames");
    require_atomic(dodo, "chassis_hi");
    require_atomic(dodo, "chassis_vlo");
    unique_frame(dodo, "chassis_dummy");

    for(const char* name : {"wheel_rf_dummy", "wheel_lf_dummy", "wheel_rb_dummy", "wheel_lb_dummy"}) {
        auto* frame = unique_frame(dodo, name);
        if(subtree_atomics(dodo, frame))
            throw std::runtime_error(std::string("Original Dodo wheel dummy unexpectedly contains geometry: ") + name);
    }
    if(direct_atomics(dodo, unique_frame(dodo, "chassis_dummy")))
        throw std::runtime_error("Original Dodo chassis dummy unexpectedly contains geometry");

    require_damage_pair(dodo, "door_lf_dummy", "door_lf_hi_ok", "door_lf_hi_dam");
    require_damage_pair(dodo, "door_rf_dummy", "door_rf_hi_ok", "door_rf_hi_dam");
    require_damage_pair(dodo, "wing_lr_dummy", "wing_lr_hi_ok", "wing_lr_hi_dam");
    require_damage_pair(dodo, "wing_rr_dummy", "wing_rr_hi_ok", "wing_rr_hi_dam");
    require_damage_pair(dodo, "windscreen_dummy", "windscreen_hi_ok", "windscreen_hi_dam");
    require_damage_pair(dodo, "boot_dummy", "boot_hi_ok", "boot_hi_dam");

    // Its four wheel dummies are empty because LCS supplies wheels from
    // DEFAULT.IDE; the adapter embeds one of those original wheel meshes.
}

void check_sa_dodo_plane(rw::Clump* dodo) {
    check_geometry(dodo);
    if(!dodo || !dodo->getFrame() || std::strcmp(gta::getNodeName(dodo->getFrame()), "dodo") != 0)
        throw std::runtime_error("SA Dodo clump root must remain dodo");
    if(unique_frame(dodo, "chassis_hi", false))
        throw std::runtime_error("SA Dodo retains the unadapted chassis_hi frame");
    require_atomic(dodo, "chassis");
    require_atomic(dodo, "chassis_vlo");
    unique_frame(dodo, "chassis_dummy");

    auto* main_wheel = unique_frame(dodo, "wheel_rf_dummy");
    auto* wheel = unique_frame(dodo, "wheel");
    if(wheel->getParent() != main_wheel || direct_atomics(dodo, wheel) != 1 ||
       subtree_atomics(dodo, main_wheel) != 1)
        throw std::runtime_error("SA Dodo main wheel must be one original wheel atomic under wheel_rf_dummy");
    for(const char* name : {"wheel_lf_dummy", "wheel_rb_dummy", "wheel_lb_dummy"}) {
        auto* frame = unique_frame(dodo, name);
        if(subtree_atomics(dodo, frame))
            throw std::runtime_error(std::string("SA Dodo secondary wheel geometry would be cloned twice: ") + name);
    }

    require_damage_pair(dodo, "door_lf_dummy", "door_lf_hi_ok", "door_lf_hi_dam");
    require_damage_pair(dodo, "door_rf_dummy", "door_rf_hi_ok", "door_rf_hi_dam");
    require_damage_pair(dodo, "wing_lr_dummy", "wing_lr_hi_ok", "wing_lr_hi_dam");
    require_damage_pair(dodo, "wing_rr_dummy", "wing_rr_hi_ok", "wing_rr_hi_dam");
    require_damage_pair(dodo, "windscreen_dummy", "windscreen_hi_ok", "windscreen_hi_dam");
    require_damage_pair(dodo, "boot_dummy", "boot_hi_ok", "boot_hi_dam");
}

} // namespace

void adapt_sa_dodo_plane(rw::Clump* dodo, const std::filesystem::path& wheel,
                         float wheel_scale, const std::filesystem::path& collision) {
    if(wheel.empty() || collision.empty())
        throw std::runtime_error("Dodo plane conversion requires its original wheel and collision inputs");
    check_source_dodo(dodo);

    // The source IDE calls Dodo a car; the caller selects SA's native plane
    // class using the validated source handling flags. CPlane inherits the
    // automobile wheel/chassis setup and consumes this four-wheel hierarchy.
    // adapt_sa_car supplies only those shared mesh adjustments and the common
    // source material/COL3 processing. CPlane's renderer hides the retained
    // `_dam` atomics; LCS-only wing/boot/windscreen damage groups stay intact
    // because SA's plane descriptor has no matching component slots.
    adapt_sa_car(dodo, wheel, wheel_scale, collision);
    check_sa_dodo_plane(dodo);
    std::cout << "sa_dodo_plane_class=true native_plane_wheel_frames=true "
                 "source_geometry_preserved=true lcs_only_damage_frames_retained=true ";
}

void validate_sa_dodo_plane(const std::filesystem::path& path) {
    auto* dodo = read_vehicle_clump(path);
    try {
        check_sa_dodo_plane(dodo);
        validate_sa_vehicle_common(dodo);
    } catch(...) {
        dodo->destroy();
        throw;
    }
    const auto atomics = dodo->countAtomics();
    dodo->destroy();
    std::cout << "sa_dodo_plane_hierarchy_validated=true atomics=" << atomics
                 << " game_executed=false\n";
}
