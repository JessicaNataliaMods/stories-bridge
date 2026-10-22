// Offline adapter for original LCS helicopter hierarchies.
#include "convert_vehicle_aircraft.hpp"
#include "convert_vehicle.hpp"

#include <rw.h>
#include <rwgta.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

rw::Clump* read_heli_clump(const std::filesystem::path& path) {
    rw::StreamFile in;
    if(!in.open(path.string().c_str(), "rb") ||
       !rw::findChunk(&in, rw::ID_CLUMP, nullptr, nullptr))
        throw std::runtime_error("Cannot open helicopter clump");
    auto* clump = rw::Clump::streamRead(&in);
    in.close();
    if(!clump)
        throw std::runtime_error("Cannot read helicopter clump");
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
        throw std::runtime_error("Missing helicopter clump root");
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
            throw std::runtime_error(std::string("Duplicate helicopter frame: ") + name);
        found = frame;
    }
    if(!found && required)
        throw std::runtime_error(std::string("Missing helicopter frame: ") + name);
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

void check_heli_geometry(rw::Clump* clump) {
    all_frames(clump);
    FORLIST(link, clump->atomics) {
        auto* atomic = rw::Atomic::fromClump(link);
        auto* geometry = atomic->geometry;
        if(!atomic->getFrame() || !geometry || rw::Skin::get(geometry))
            throw std::runtime_error("Helicopter clump contains an invalid or skinned source atomic");
        if(geometry->numMorphTargets != 1 || !geometry->numVertices || !geometry->numTriangles)
            throw std::runtime_error("Helicopter atomic must retain one nonempty editable source geometry");
    }
}

void require_renderable_component(rw::Clump* clump, rw::Frame* frame, const char* name) {
    if(!frame || frame == clump->getFrame() || direct_atomics(clump, frame) == 0)
        throw std::runtime_error(std::string("Helicopter component must retain source geometry: ") + name);
}

void rename_component(rw::Clump* clump, const char* source, const char* target,
                      bool required, bool renderable) {
    auto* frame = unique_frame(clump, source, required);
    if(!frame)
        return;
    auto* target_frame = unique_frame(clump, target, false);
    if(target_frame && target_frame != frame)
        throw std::runtime_error(std::string("Helicopter source and SA frames conflict: ") + target);
    if(renderable)
        require_renderable_component(clump, frame, source);
    std::strcpy(gta::getNodeName(frame), target);
}

void require_name_available(rw::Clump* clump, const char* name) {
    if(unique_frame(clump, name, false))
        throw std::runtime_error(std::string("Helicopter already has target SA frame: ") + name);
}

bool frame_within(rw::Frame* frame, rw::Frame* ancestor) {
    for(auto* current = frame; current; current = current->getParent())
        if(current == ancestor)
            return true;
    return false;
}

rw::Matrix relative_to_root(rw::Frame* frame, rw::Frame* root) {
    if(!frame || !root || !frame_within(frame, root))
        throw std::runtime_error("Helicopter geometry is outside its clump root");
    if(frame == root) {
        rw::Matrix identity;
        identity.setIdentity();
        return identity;
    }
    rw::Matrix transform = frame->matrix;
    for(auto* parent = frame->getParent(); parent && parent != root; parent = parent->getParent())
        rw::Matrix::mult(&transform, &transform, &parent->matrix);
    return transform;
}

struct GeometryBounds {
    rw::V3d minimum{};
    rw::V3d maximum{};
    bool has_points = false;
    size_t point_count = 0;

    void add(const rw::V3d& point) {
        if(!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            throw std::runtime_error("Helicopter geometry has nonfinite root-space bounds");
        if(!has_points) {
            minimum = maximum = point;
            has_points = true;
            point_count = 1;
            return;
        }
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
        ++point_count;
    }
};

GeometryBounds component_bounds_in_root(rw::Clump* heli, rw::Frame* component, const char* name) {
    GeometryBounds bounds;
    auto* root = heli->getFrame();
    FORLIST(link, heli->atomics) {
        auto* atomic = rw::Atomic::fromClump(link);
        auto* frame = atomic->getFrame();
        if(!frame_within(frame, component))
            continue;
        auto* geometry = atomic->geometry;
        if(!geometry || geometry->instData || geometry->numMorphTargets != 1 ||
           !geometry->numVertices || !geometry->morphTargets[0].vertices)
            throw std::runtime_error(std::string("Helicopter envelope requires editable source vertices: ") + name);

        const auto transform = relative_to_root(frame, root);
        const auto* vertices = geometry->morphTargets[0].vertices;
        for(int i = 0; i < geometry->numVertices; ++i) {
            const auto& source = vertices[i];
            if(!std::isfinite(source.x) || !std::isfinite(source.y) || !std::isfinite(source.z))
                throw std::runtime_error(std::string("Helicopter envelope has nonfinite source vertices: ") + name);
            rw::V3d point;
            rw::V3d::transformPoints(&point, &source, 1, &transform);
            bounds.add(point);
        }
    }
    if(!bounds.has_points)
        throw std::runtime_error(std::string("Helicopter envelope has no source geometry: ") + name);
    return bounds;
}

using WheelAnchors = std::array<rw::V3d, 4>; // Construction order follows the names below.

struct VirtualSuspension {
    WheelAnchors anchors;
    float track;
    float wheelbase;
    float body_bottom;
    size_t source_vertices;
};

VirtualSuspension derive_body_envelope_wheel_anchors(rw::Clump* heli, float wheel_size) {
    if(!std::isfinite(wheel_size) || wheel_size <= 0.0f)
        throw std::runtime_error("Virtual helicopter suspension requires a positive IDE wheel size");
    // The LCS class collision is not a reliable suspension source: it may be
    // shared with an ambient vehicle or absent from the world collision list.
    // The original chassis_hi vertices provide one stable source-owned envelope.
    const auto body = component_bounds_in_root(heli, unique_frame(heli, "chassis_hi"), "chassis_hi");
    const float track = body.maximum.x - body.minimum.x;
    const float wheelbase = body.maximum.y - body.minimum.y;
    // CHeli inherits SA's four-corner suspension setup. Require enough span
    // for the converted wheel size and a clearly longitudinal aircraft body.
    if(!std::isfinite(track) || !std::isfinite(wheelbase) ||
       track <= wheel_size || wheelbase <= wheel_size || wheelbase <= track * 1.05f)
        throw std::runtime_error("Original helicopter chassis envelope does not define a stable four-point suspension footprint");

    const float half_wheel = wheel_size * 0.5f;
    const float anchor_z = body.minimum.z + half_wheel;
    WheelAnchors anchors{{
        {body.maximum.x, body.maximum.y, anchor_z}, // wheel_rf_dummy
        {body.minimum.x, body.maximum.y, anchor_z}, // wheel_lf_dummy
        {body.maximum.x, body.minimum.y, anchor_z}, // wheel_rb_dummy
        {body.minimum.x, body.minimum.y, anchor_z}, // wheel_lb_dummy
    }};
    for(const auto& anchor : anchors)
        if(!std::isfinite(anchor.x) || !std::isfinite(anchor.y) || !std::isfinite(anchor.z))
            throw std::runtime_error("Derived helicopter suspension anchor is nonfinite");
    return {anchors, track, wheelbase, body.minimum.z, body.point_count};
}

bool has_native_wheel_dummies(rw::Clump* heli) {
    unsigned found = 0;
    for(const char* name : {"wheel_rf_dummy", "wheel_lf_dummy", "wheel_rb_dummy", "wheel_lb_dummy"})
        found += unique_frame(heli, name, false) != nullptr;
    if(found != 0 && found != 4)
        throw std::runtime_error("Helicopter must have either all four suspension dummies or none");
    return found == 4;
}

void require_root_wheel_dummies(rw::Clump* heli) {
    auto* root = heli->getFrame();
    for(const char* name : {"wheel_rf_dummy", "wheel_lf_dummy", "wheel_rb_dummy", "wheel_lb_dummy"}) {
        auto* frame = unique_frame(heli, name);
        if(frame->getParent() != root)
            throw std::runtime_error(std::string("Helicopter suspension dummy must be a direct root child: ") + name);
        const auto& position = frame->matrix.pos;
        if(!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            throw std::runtime_error(std::string("Helicopter suspension dummy has a nonfinite position: ") + name);
    }
}

void add_empty_wheel_dummy(rw::Clump* heli, const char* name, const rw::V3d& position) {
    auto* root = heli->getFrame();
    auto* frame = rw::Frame::create();
    if(!frame)
        throw std::runtime_error("Could not allocate helicopter suspension dummy frame");
    // CHeli's inherited suspension setup only needs the named frame position.
    // Keeping it childless leaves SA's guarded mainWheelAtomic lookup empty,
    // so preprocessing cannot clone or substitute wheel geometry.
    frame->matrix.setIdentity();
    frame->matrix.pos = position;
    frame->matrix.update();
    std::strcpy(gta::getNodeName(frame), name);
    root->addChild(frame);
}

void add_virtual_wheel_dummies(rw::Clump* heli, const WheelAnchors& anchors) {
    static constexpr const char* names[] = {
        "wheel_rf_dummy", "wheel_lf_dummy", "wheel_rb_dummy", "wheel_lb_dummy"
    };
    for(size_t i = 0; i < anchors.size(); ++i)
        add_empty_wheel_dummy(heli, names[i], anchors[i]);
}

bool check_source_heli(rw::Clump* heli) {
    check_heli_geometry(heli);
    unique_frame(heli, "chassis_dummy");
    require_renderable_component(heli, unique_frame(heli, "chassis_hi"), "chassis_hi");
    require_renderable_component(heli, unique_frame(heli, "toprotor"), "toprotor");

    auto* back = unique_frame(heli, "backrotor", false);
    auto* rear = unique_frame(heli, "rearrotor", false);
    if(back && rear)
        throw std::runtime_error("Helicopter has both backrotor and rearrotor source frames");
    if(back)
        require_renderable_component(heli, back, "backrotor");
    if(rear)
        require_renderable_component(heli, rear, "rearrotor");

    const bool has_wheels = has_native_wheel_dummies(heli);
    if(!has_wheels) {
        for(const char* name : {"wheel_rf_dummy", "wheel_lf_dummy", "wheel_rb_dummy", "wheel_lb_dummy"})
            require_name_available(heli, name);
    } else
        require_root_wheel_dummies(heli);

    // Check all rename destinations before changing any frame name so an
    // invalid source cannot leave a partially adapted clump in memory.
    require_name_available(heli, "chassis");
    require_name_available(heli, "moving_rotor");
    if(back || rear)
        require_name_available(heli, "moving_rotor2");

    // These LCS-specific dummies stay in the source hierarchy. They do not
    // correspond to SA rotor slots, so no geometry is discarded or repurposed.
    for(const char* name : {"tail", "topknot", "skid_left", "skid_right"})
        unique_frame(heli, name, false);
    return has_wheels;
}

void check_sa_heli(rw::Clump* heli) {
    check_heli_geometry(heli);
    for(const char* name : {"chassis_hi", "toprotor", "backrotor", "rearrotor"})
        if(unique_frame(heli, name, false))
            throw std::runtime_error(std::string("Unadapted LCS helicopter frame remains: ") + name);
    unique_frame(heli, "chassis_dummy");
    require_renderable_component(heli, unique_frame(heli, "chassis"), "chassis");
    require_renderable_component(heli, unique_frame(heli, "moving_rotor"), "moving_rotor");
    auto* tail_rotor = unique_frame(heli, "moving_rotor2", false);
    if(tail_rotor)
        require_renderable_component(heli, tail_rotor, "moving_rotor2");
    require_root_wheel_dummies(heli);

    // SA's static rotor slots are optional and are not synthesized from the
    // source's single animated meshes. Existing source-named frames survive.
    for(const char* name : {"static_rotor", "static_rotor2", "chassis_vlo",
                            "tail", "topknot", "skid_left", "skid_right"})
        unique_frame(heli, name, false);
}

void require_collision(const std::filesystem::path& collision) {
    if(collision.empty())
        throw std::runtime_error("Helicopter conversion requires the original class collision input");
}

} // namespace

void adapt_sa_heli(rw::Clump* heli, const std::filesystem::path& collision, float wheel_size) {
    require_collision(collision);
    const bool has_wheels = check_source_heli(heli);
    VirtualSuspension virtual_suspension{};
    if(!has_wheels)
        virtual_suspension = derive_body_envelope_wheel_anchors(heli, wheel_size);
    else if(!std::isfinite(wheel_size) || wheel_size <= 0.0f)
        throw std::runtime_error("Helicopter adapter requires a positive converted IDE wheel size");

    // SA CHeli requires chassis at node 1. Its rotor descriptor is 12..15;
    // source LCS has one rotating mesh per rotor, so bind only the native
    // moving slots. Native code null-checks the unused static slots.
    rename_component(heli, "chassis_hi", "chassis", true, true);
    rename_component(heli, "toprotor", "moving_rotor", true, true);
    auto* back = unique_frame(heli, "backrotor", false);
    if(back)
        rename_component(heli, "backrotor", "moving_rotor2", false, true);
    else
        rename_component(heli, "rearrotor", "moving_rotor2", false, true);

    if(!has_wheels)
        add_virtual_wheel_dummies(heli, virtual_suspension.anchors);

    check_sa_heli(heli);
    adapt_sa_vehicle_common(heli, collision);
    std::cout << "sa_heli_class=true native_chassis_and_rotor_frames=true "
                 "source_geometry_and_transforms_preserved=true static_rotors_synthesized=false "
              << "suspension_dummies=" << (has_wheels ? "source" : "virtual")
              << " suspension_anchor_source=" << (has_wheels ? "native_frame_positions" : "chassis_hi_envelope");
    if(!has_wheels)
        std::cout << " suspension_source_vertices=" << virtual_suspension.source_vertices
                  << " suspension_track=" << virtual_suspension.track
                  << " suspension_wheelbase=" << virtual_suspension.wheelbase
                  << " suspension_body_bottom=" << virtual_suspension.body_bottom
                  << " suspension_wheel_size=" << wheel_size
                  << " suspension_x_min=" << virtual_suspension.anchors[1].x
                  << " suspension_x_max=" << virtual_suspension.anchors[0].x
                  << " suspension_y_min=" << virtual_suspension.anchors[2].y
                  << " suspension_y_max=" << virtual_suspension.anchors[0].y
                  << " suspension_anchor_z=" << virtual_suspension.anchors[0].z;
    std::cout << ' ';
}

void validate_sa_heli(const std::filesystem::path& path) {
    auto* heli = read_heli_clump(path);
    try {
        check_sa_heli(heli);
        validate_sa_vehicle_common(heli);
    } catch(...) {
        heli->destroy();
        throw;
    }
    const auto atomics = heli->countAtomics();
    heli->destroy();
    std::cout << "sa_heli_hierarchy_validated=true atomics=" << atomics << " game_executed=false\n";
}
