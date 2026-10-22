// Offline adapter for the original LCS ambient train hierarchy.
#include "convert_vehicle_ambient.hpp"
#include "convert_vehicle.hpp"

#include <rw.h>
#include <rwgta.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

rw::Clump* read_train_clump(const std::filesystem::path& path) {
    rw::StreamFile in;
    if(!in.open(path.string().c_str(), "rb") ||
       !rw::findChunk(&in, rw::ID_CLUMP, nullptr, nullptr))
        throw std::runtime_error("Cannot open LCS train clump");
    auto* clump = rw::Clump::streamRead(&in);
    in.close();
    if(!clump)
        throw std::runtime_error("Cannot read LCS train clump");
    return clump;
}

void collect_frames(rw::Frame* frame, std::vector<rw::Frame*>& out) {
    if(!frame)
        return;
    out.push_back(frame);
    for(auto* child = frame->child; child; child = child->next)
        collect_frames(child, out);
}

std::vector<rw::Frame*> all_frames(rw::Clump* clump) {
    if(!clump || !clump->getFrame())
        throw std::runtime_error("Missing train clump root");
    std::vector<rw::Frame*> frames;
    collect_frames(clump->getFrame(), frames);
    return frames;
}

rw::Frame* unique_frame(rw::Clump* clump, const char* name, bool required = true) {
    rw::Frame* found = nullptr;
    for(auto* frame : all_frames(clump)) {
        if(std::strcmp(gta::getNodeName(frame), name) != 0)
            continue;
        if(found)
            throw std::runtime_error(std::string("Duplicate train frame: ") + name);
        found = frame;
    }
    if(!found && required)
        throw std::runtime_error(std::string("Missing train frame: ") + name);
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

void check_geometry(rw::Clump* clump, const char* model) {
    all_frames(clump);
    size_t atomics = 0;
    FORLIST(link, clump->atomics) {
        auto* atomic = rw::Atomic::fromClump(link);
        auto* geometry = atomic->geometry;
        if(!atomic->getFrame() || !geometry || rw::Skin::get(geometry) ||
           geometry->numMorphTargets != 1 || !geometry->numVertices || !geometry->numTriangles)
            throw std::runtime_error(std::string(model) + " contains invalid, skinned, or empty source geometry");
        ++atomics;
    }
    if(!atomics)
        throw std::runtime_error(std::string(model) + " contains no renderable source atomics");
}

void require_atomic(rw::Clump* clump, const char* name) {
    auto* frame = unique_frame(clump, name);
    if(frame == clump->getFrame() || direct_atomics(clump, frame) != 1)
        throw std::runtime_error(std::string("Train component must own exactly one atomic: ") + name);
}

void require_empty(rw::Clump* clump, const char* name) {
    if(direct_atomics(clump, unique_frame(clump, name)))
        throw std::runtime_error(std::string("Train dummy unexpectedly contains geometry: ") + name);
}

void require_name_available(rw::Clump* clump, const char* name) {
    if(unique_frame(clump, name, false))
        throw std::runtime_error(std::string("Train already has target SA frame: ") + name);
}

void rename_frame(rw::Clump* clump, const char* source, const char* target) {
    auto* frame = unique_frame(clump, source);
    require_name_available(clump, target);
    std::strcpy(gta::getNodeName(frame), target);
}

void require_collision(const std::filesystem::path& collision) {
    if(collision.empty())
        throw std::runtime_error("LCS train conversion requires its original collision input");
}

void validate_train_source(rw::Clump* train) {
    check_geometry(train, "LCS train");
    if(std::strcmp(gta::getNodeName(train->getFrame()), "train") != 0)
        throw std::runtime_error("Source clump is not the original LCS train");
    for(const char* name : {"chassis_hi", "chassis_vlo", "Object02",
                            "door_lhs_hi", "door_lhs_lo", "door_rhs_hi", "door_rhs_lo"})
        require_atomic(train, name);
    for(const char* name : {"door_lhs_dummy", "door_rhs_dummy"})
        require_empty(train, name);
    for(const char* child : {"door_lhs_hi", "door_lhs_lo"})
        if(!inside(unique_frame(train, child), unique_frame(train, "door_lhs_dummy")))
            throw std::runtime_error(std::string("LCS train left door geometry is outside its pivot: ") + child);
    for(const char* child : {"door_rhs_hi", "door_rhs_lo"})
        if(!inside(unique_frame(train, child), unique_frame(train, "door_rhs_dummy")))
            throw std::runtime_error(std::string("LCS train right door geometry is outside its pivot: ") + child);
    if(unique_frame(train, "door_lf_dummy", false) || unique_frame(train, "door_rf_dummy", false) ||
       unique_frame(train, "door_lhs_vlo", false) || unique_frame(train, "door_rhs_vlo", false))
        throw std::runtime_error("Train hierarchy is already adapted or has conflicting SA frame names");
}

void validate_sa_train_clump(rw::Clump* train) {
    check_geometry(train, "SA train");
    if(std::strcmp(gta::getNodeName(train->getFrame()), "train") != 0)
        throw std::runtime_error("SA train root must remain train");
    for(const char* name : {"chassis_hi", "chassis_vlo", "Object02",
                            "door_lhs_hi", "door_rhs_hi", "door_lhs_vlo", "door_rhs_vlo"})
        require_atomic(train, name);
    for(const char* name : {"door_lf_dummy", "door_rf_dummy"})
        require_empty(train, name);
    for(const char* name : {"door_lhs_dummy", "door_rhs_dummy", "door_lhs_lo", "door_rhs_lo"})
        if(unique_frame(train, name, false))
            throw std::runtime_error(std::string("Unadapted LCS train frame remains: ") + name);
    for(const char* child : {"door_lhs_hi", "door_lhs_vlo"})
        if(!inside(unique_frame(train, child), unique_frame(train, "door_lf_dummy")))
            throw std::runtime_error(std::string("SA train left door geometry lost its pivot: ") + child);
    for(const char* child : {"door_rhs_hi", "door_rhs_vlo"})
        if(!inside(unique_frame(train, child), unique_frame(train, "door_rf_dummy")))
            throw std::runtime_error(std::string("SA train right door geometry lost its pivot: ") + child);
}

} // namespace

void adapt_sa_train(rw::Clump* train, const std::filesystem::path& collision) {
    require_collision(collision);
    validate_train_source(train);
    rename_frame(train, "door_lhs_dummy", "door_lf_dummy");
    rename_frame(train, "door_rhs_dummy", "door_rf_dummy");

    // SA has hi/vlo callbacks for trains. Map each LCS mid-detail door atomic
    // to SA's distant tier, retaining the source hi and vlo body atomics.
    rename_frame(train, "door_lhs_lo", "door_lhs_vlo");
    rename_frame(train, "door_rhs_lo", "door_rhs_vlo");
    adapt_sa_vehicle_common(train, collision);
    validate_sa_train_clump(train);
    std::cout << "sa_train_native_class=true source_door_pivots_bound=true "
                 "source_hi_vlo_body_preserved=true source_door_lo_mapped_to_vlo=true "
                 "unseparated_static_wheel_geometry_preserved=true ";
}

void validate_sa_train(const std::filesystem::path& path) {
    auto* train = read_train_clump(path);
    try {
        validate_sa_train_clump(train);
        validate_sa_vehicle_common(train);
    } catch(...) {
        train->destroy();
        throw;
    }
    const auto atomics = train->countAtomics();
    train->destroy();
    std::cout << "sa_train_hierarchy_validated=true atomics=" << atomics << " game_executed=false\n";
}
