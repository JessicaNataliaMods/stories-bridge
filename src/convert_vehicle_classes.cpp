// Offline class adapters for original LCS vehicle geometry.
// Component names are checked against reLCS/SA VehicleModelInfo descriptors;
// source meshes, frames, dummies, local transforms and optional components stay intact.
#include "convert_vehicle_classes.hpp"
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
        throw std::runtime_error("Cannot open vehicle clump");
    auto* clump = rw::Clump::streamRead(&in);
    in.close();
    if(!clump)
        throw std::runtime_error("Cannot read vehicle clump");
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
        throw std::runtime_error("Missing vehicle clump root");
    std::vector<rw::Frame*> all;
    collect_frames(clump->getFrame(), all);
    rw::Frame* found = nullptr;
    for(auto* frame : all) {
        if(std::strcmp(gta::getNodeName(frame), name) != 0)
            continue;
        if(found)
            throw std::runtime_error(std::string("Duplicate vehicle frame: ") + name);
        found = frame;
    }
    if(!found && required)
        throw std::runtime_error(std::string("Missing vehicle frame: ") + name);
    return found;
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

void check_vehicle_geometry(rw::Clump* clump) {
    if(!clump || !clump->getFrame())
        throw std::runtime_error("Missing vehicle clump root");
    FORLIST(link, clump->atomics) {
        auto* atomic = rw::Atomic::fromClump(link);
        auto* geometry = atomic->geometry;
        if(!atomic->getFrame() || !geometry || rw::Skin::get(geometry))
            throw std::runtime_error("Vehicle class clump contains an invalid or skinned source atomic");
        if(geometry->numMorphTargets != 1 || !geometry->numVertices || !geometry->numTriangles)
            throw std::runtime_error("Vehicle class atomic must retain one nonempty editable source geometry");
    }
}

void require_component(rw::Clump* clump, const char* name, bool geometry_required) {
    auto* frame = unique_frame(clump, name);
    if(frame == clump->getFrame())
        throw std::runtime_error(std::string("SA vehicle component cannot be the clump root: ") + name);
    if(geometry_required && direct_atomics(clump, frame) != 1)
        throw std::runtime_error(std::string("SA vehicle component must retain exactly one source atomic: ") + name);
}

void check_bike(rw::Clump* bike) {
    check_vehicle_geometry(bike);

    // reLCS and SA BIKE descriptors agree on these moving components. Their
    // source atomic must remain attached to the named frame for native pivoting.
    require_component(bike, "chassis_dummy", false);
    for(const char* name : {"forks_front", "forks_rear", "wheel_front", "wheel_rear", "handlebars"})
        require_component(bike, name, true);

    // Both source LOD bodies are present in the LCS bike set. Keep them as
    // authored; SA's vehicle renderer selects the *_vlo callback by frame name.
    require_component(bike, "chassis_hi", true);
    require_component(bike, "chassis_vlo", true);

    // Position/extra frames are consumed when available, and may be absent in
    // other valid SA bikes. Reject duplicate names without synthesizing dummies.
    for(const char* name : {"mudguard", "ped_frontseat", "ped_backseat", "headlights",
                            "taillights", "exhaust", "extra1", "extra2", "extra3",
                            "extra4", "extra5", "extra6"})
        unique_frame(bike, name, false);
}

void check_generic_boat(rw::Clump* boat) {
    check_vehicle_geometry(boat);

    // CBoat and the SA boat renderer use these exact names. Propeller/rudder/
    // flap frames are optional per model; retain them when present.
    require_component(boat, "boat_hi", true);
    require_component(boat, "boat_vlo", true);
    require_component(boat, "ped_frontseat", false);
    for(const char* name : {"boat_moving_hi", "boat_rudder_hi", "boat_flap_left",
                            "boat_flap_right", "boat_rearflap_left", "boat_rearflap_right",
                            "windscreen"}) {
        auto* frame = unique_frame(boat, name, false);
        if(frame && direct_atomics(boat, frame) > 1)
            throw std::runtime_error(std::string("SA boat moving component has ambiguous atomics: ") + name);
    }
}

void require_collision(const std::filesystem::path& collision) {
    if(collision.empty())
        throw std::runtime_error("Vehicle class conversion requires the original class collision input");
}

} // namespace

void adapt_sa_bike(rw::Clump* bike, const std::filesystem::path& collision) {
    require_collision(collision);
    check_bike(bike);
    adapt_sa_vehicle_common(bike, collision);
    std::cout << "sa_bike_class=true native_component_frames_preserved=true source_geometry_preserved=true ";
}

void validate_sa_bike(const std::filesystem::path& path) {
    auto* bike = read_vehicle_clump(path);
    try {
        check_bike(bike);
        validate_sa_vehicle_common(bike);
    } catch(...) {
        bike->destroy();
        throw;
    }
    const auto atomics = bike->countAtomics();
    bike->destroy();
    std::cout << "sa_bike_hierarchy_validated=true atomics=" << atomics << " game_executed=false\n";
}

void adapt_sa_generic_boat(rw::Clump* boat, const std::filesystem::path& collision) {
    require_collision(collision);
    check_generic_boat(boat);
    adapt_sa_vehicle_common(boat, collision);
    std::cout << "sa_generic_boat_class=true native_component_frames_preserved=true source_geometry_preserved=true ";
}

void validate_sa_generic_boat(const std::filesystem::path& path) {
    auto* boat = read_vehicle_clump(path);
    try {
        check_generic_boat(boat);
        validate_sa_vehicle_common(boat);
    } catch(...) {
        boat->destroy();
        throw;
    }
    const auto atomics = boat->countAtomics();
    boat->destroy();
    std::cout << "sa_generic_boat_hierarchy_validated=true atomics=" << atomics << " game_executed=false\n";
}
