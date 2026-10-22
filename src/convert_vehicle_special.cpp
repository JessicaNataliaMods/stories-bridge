// Offline adapter for original LCS vehicle assets whose native SA class is
// not the class recorded in the source IDE row.
#include "convert_vehicle_special.hpp"
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

rw::Clump* read_vehicle_clump(const std::filesystem::path& path) {
    rw::StreamFile in;
    if(!in.open(path.string().c_str(), "rb") ||
       !rw::findChunk(&in, rw::ID_CLUMP, nullptr, nullptr))
        throw std::runtime_error("Cannot open special vehicle clump");
    auto* clump = rw::Clump::streamRead(&in);
    in.close();
    if(!clump)
        throw std::runtime_error("Cannot read special vehicle clump");
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
        throw std::runtime_error("Missing special vehicle clump root");
    std::vector<rw::Frame*> all;
    collect_frames(clump->getFrame(), all);
    rw::Frame* found = nullptr;
    for(auto* frame : all) {
        if(std::strcmp(gta::getNodeName(frame), name) != 0)
            continue;
        if(found)
            throw std::runtime_error(std::string("Duplicate special vehicle frame: ") + name);
        found = frame;
    }
    if(!found && required)
        throw std::runtime_error(std::string("Missing special vehicle frame: ") + name);
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
        throw std::runtime_error("Missing special vehicle clump root");
    FORLIST(link, clump->atomics) {
        auto* atomic = rw::Atomic::fromClump(link);
        auto* geometry = atomic->geometry;
        if(!atomic->getFrame() || !geometry || rw::Skin::get(geometry) ||
           geometry->numMorphTargets != 1 || !geometry->numVertices || !geometry->numTriangles)
            throw std::runtime_error("Special vehicle clump contains invalid, skinned, or empty source geometry");
    }
}

void require_atomic(rw::Clump* clump, const char* name) {
    auto* frame = unique_frame(clump, name);
    if(frame == clump->getFrame() || direct_atomics(clump, frame) != 1)
        throw std::runtime_error(std::string("Special vehicle component must own exactly one atomic: ") + name);
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

struct PlaneGeometrySample {
    std::vector<rw::V3d> points;
    rw::V3d minimum{};
    rw::V3d maximum{};
    rw::V3d centroid{};
};

rw::Frame* ambient_plane_model_root(rw::Clump* plane) {
    if(!plane || !plane->getFrame())
        throw std::runtime_error("Missing ambient plane clump root");
    auto* airtrain = unique_frame(plane, "airtrain", false);
    auto* deaddodo = unique_frame(plane, "deaddodo", false);
    if((airtrain == nullptr) == (deaddodo == nullptr))
        throw std::runtime_error("Ambient plane must be exactly the original Airtrain or Dead Dodo");
    auto* model = airtrain ? airtrain : deaddodo;
    if(model != plane->getFrame() && model->getParent() != plane->getFrame())
        throw std::runtime_error("Ambient plane model frame must be the clump root or its direct child");
    return model;
}

rw::Matrix matrix_to_ancestor(rw::Frame* frame, rw::Frame* ancestor) {
    if(!frame || !ancestor || !inside(frame, ancestor))
        throw std::runtime_error("Ambient plane geometry is outside its model frame");
    if(frame == ancestor) {
        rw::Matrix identity;
        identity.setIdentity();
        return identity;
    }
    rw::Matrix transform = frame->matrix;
    for(auto* parent = frame->getParent(); parent && parent != ancestor; parent = parent->getParent())
        rw::Matrix::mult(&transform, &transform, &parent->matrix);
    return transform;
}

rw::V3d frame_origin_in_model(rw::Frame* frame, rw::Frame* model) {
    const auto transform = matrix_to_ancestor(frame, model);
    const rw::V3d origin{0.0f, 0.0f, 0.0f};
    rw::V3d position;
    rw::V3d::transformPoints(&position, &origin, 1, &transform);
    return position;
}

PlaneGeometrySample sample_plane_atomic(rw::Clump* plane, rw::Frame* model,
                                        rw::Frame* expected_frame, const char* label) {
    rw::Atomic* found = nullptr;
    FORLIST(link, plane->atomics) {
        auto* atomic = rw::Atomic::fromClump(link);
        if(atomic->getFrame() != expected_frame)
            continue;
        if(found)
            throw std::runtime_error(std::string("Duplicate ambient plane wheel atomics: ") + label);
        found = atomic;
    }
    if(!found || !found->geometry || found->geometry->instData || rw::Skin::get(found->geometry) ||
       found->geometry->numMorphTargets != 1 || !found->geometry->numVertices ||
       !found->geometry->morphTargets[0].vertices)
        throw std::runtime_error(std::string("Ambient plane wheel must have one editable source geometry: ") + label);

    PlaneGeometrySample sample;
    const auto transform = matrix_to_ancestor(expected_frame, model);
    const auto* vertices = found->geometry->morphTargets[0].vertices;
    sample.points.reserve(static_cast<size_t>(found->geometry->numVertices));
    for(int i = 0; i < found->geometry->numVertices; ++i) {
        const auto& source = vertices[i];
        if(!std::isfinite(source.x) || !std::isfinite(source.y) || !std::isfinite(source.z))
            throw std::runtime_error(std::string("Ambient plane wheel has nonfinite source vertices: ") + label);
        rw::V3d point;
        rw::V3d::transformPoints(&point, &source, 1, &transform);
        if(!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            throw std::runtime_error(std::string("Ambient plane wheel has nonfinite transformed vertices: ") + label);
        if(sample.points.empty())
            sample.minimum = sample.maximum = point;
        else {
            sample.minimum.x = std::min(sample.minimum.x, point.x);
            sample.minimum.y = std::min(sample.minimum.y, point.y);
            sample.minimum.z = std::min(sample.minimum.z, point.z);
            sample.maximum.x = std::max(sample.maximum.x, point.x);
            sample.maximum.y = std::max(sample.maximum.y, point.y);
            sample.maximum.z = std::max(sample.maximum.z, point.z);
        }
        sample.centroid.x += point.x;
        sample.centroid.y += point.y;
        sample.centroid.z += point.z;
        sample.points.push_back(point);
    }
    const auto inverse_count = 1.0f / static_cast<float>(sample.points.size());
    sample.centroid.x *= inverse_count;
    sample.centroid.y *= inverse_count;
    sample.centroid.z *= inverse_count;
    return sample;
}

std::array<float, 2> derive_rear_wheel_centers(const PlaneGeometrySample& rear, float wheel_size) {
    std::vector<float> lateral_positions;
    lateral_positions.reserve(rear.points.size());
    for(const auto& point : rear.points)
        lateral_positions.push_back(point.x);
    std::sort(lateral_positions.begin(), lateral_positions.end());
    if(lateral_positions.size() < 16)
        throw std::runtime_error("Ambient plane rear wheel geometry is too small to identify both source wheels");

    const float span = lateral_positions.back() - lateral_positions.front();
    size_t split = 0;
    float largest_gap = 0.0f;
    for(size_t i = 1; i < lateral_positions.size(); ++i) {
        const float gap = lateral_positions[i] - lateral_positions[i - 1];
        if(gap > largest_gap) {
            largest_gap = gap;
            split = i;
        }
    }
    const auto minimum_cluster = lateral_positions.size() / 4;
    if(!span || split < minimum_cluster || lateral_positions.size() - split < minimum_cluster ||
       largest_gap <= std::max(span * 0.15f, wheel_size * 0.25f))
        throw std::runtime_error("Ambient plane rear wheel vertices do not form two distinct lateral source meshes");

    float left = 0.0f;
    for(size_t i = 0; i < split; ++i)
        left += lateral_positions[i];
    left /= static_cast<float>(split);
    float right = 0.0f;
    for(size_t i = split; i < lateral_positions.size(); ++i)
        right += lateral_positions[i];
    right /= static_cast<float>(lateral_positions.size() - split);
    if(!(left < right))
        throw std::runtime_error("Ambient plane rear wheel centers are not ordered left-to-right");
    return {left, right};
}

using AmbientPlaneAnchors = std::array<rw::V3d, 4>;

AmbientPlaneAnchors derive_ambient_plane_anchors(rw::Clump* plane, float wheel_size) {
    if(!std::isfinite(wheel_size) || std::fabs(wheel_size - 1.0f) > 0.001f)
        throw std::runtime_error("Original LCS plane conversion requires the source loader's fixed wheel size of 1.0");
    auto* model = ambient_plane_model_root(plane);
    auto* front_dummy = unique_frame(plane, "wheel_front_dummy");
    auto* rear_dummy = unique_frame(plane, "wheel_rear_dummy");
    auto* front_wheel = unique_frame(plane, "wheel_front");
    auto* rear_wheel = unique_frame(plane, "wheel_rear");
    if(!inside(front_dummy, model) || !inside(rear_dummy, model) ||
       front_wheel->getParent() != front_dummy || rear_wheel->getParent() != rear_dummy ||
       direct_atomics(plane, front_wheel) != 1 || direct_atomics(plane, rear_wheel) != 1 ||
       subtree_atomics(plane, front_dummy) != 1 || subtree_atomics(plane, rear_dummy) != 1)
        throw std::runtime_error("Original LCS plane wheel pivots must own one front and one rear source mesh");

    const auto front_pivot = frame_origin_in_model(front_dummy, model);
    const auto rear_pivot = frame_origin_in_model(rear_dummy, model);
    if(!(front_pivot.y > rear_pivot.y))
        throw std::runtime_error("Original LCS plane front and rear wheel pivots are not longitudinally ordered");

    const auto front_geometry = sample_plane_atomic(plane, model, front_wheel, "wheel_front");
    const auto rear_geometry = sample_plane_atomic(plane, model, rear_wheel, "wheel_rear");
    if(std::fabs(front_geometry.centroid.x - front_pivot.x) > wheel_size * 0.5f)
        throw std::runtime_error("Original LCS plane nose wheel geometry is not centered on its source pivot");
    const auto rear_centers = derive_rear_wheel_centers(rear_geometry, wheel_size);
    if(!(rear_centers[0] < rear_pivot.x && rear_centers[1] > rear_pivot.x))
        throw std::runtime_error("Original LCS plane rear wheel meshes do not straddle their source pivot");

    // The original plane has one nose wheel and two rear wheels. SA's CPlane
    // expects four suspension frames, so its front pair share the source nose
    // pivot while the rear pair use the centers measured from each original
    // rear tire mesh. No wheel geometry is cloned or synthesized.
    return {{
        {front_geometry.centroid.x, front_pivot.y, front_pivot.z}, // wheel_rf_dummy
        {front_geometry.centroid.x, front_pivot.y, front_pivot.z}, // wheel_lf_dummy
        {rear_centers[1], rear_pivot.y, rear_pivot.z},             // wheel_rb_dummy
        {rear_centers[0], rear_pivot.y, rear_pivot.z},             // wheel_lb_dummy
    }};
}

void require_ambient_plane_source(rw::Clump* plane, float wheel_size) {
    check_geometry(plane);
    auto* model = ambient_plane_model_root(plane);
    const bool dead_dodo = std::strcmp(gta::getNodeName(model), "deaddodo") == 0;
    if(unique_frame(plane, "chassis", false) ||
       unique_frame(plane, "wheel_rf_dummy", false) ||
       unique_frame(plane, "wheel_lf_dummy", false) ||
       unique_frame(plane, "wheel_rb_dummy", false) ||
       unique_frame(plane, "wheel_lb_dummy", false))
        throw std::runtime_error("Original ambient plane already contains SA CPlane frames");
    for(const char* name : {"chassis_hi", "chassis_lo", "chassis_vlo"})
        require_atomic(plane, name);
    unique_frame(plane, "chassis_dummy");
    if(dead_dodo)
        require_atomic(plane, "extra1");
    else if(unique_frame(plane, "extra1", false))
        throw std::runtime_error("Original Airtrain has an unexpected extra1 component");
    derive_ambient_plane_anchors(plane, wheel_size);
}

void add_empty_plane_wheel_dummy(rw::Frame* model, const char* name, const rw::V3d& position) {
    auto* frame = rw::Frame::create();
    if(!frame)
        throw std::runtime_error("Could not allocate ambient plane suspension dummy frame");
    frame->matrix.setIdentity();
    frame->matrix.pos = position;
    frame->matrix.update();
    std::strcpy(gta::getNodeName(frame), name);
    model->addChild(frame);
}

void check_sa_ambient_plane(rw::Clump* plane) {
    check_geometry(plane);
    auto* model = ambient_plane_model_root(plane);
    const bool dead_dodo = std::strcmp(gta::getNodeName(model), "deaddodo") == 0;
    if(unique_frame(plane, "chassis_hi", false))
        throw std::runtime_error("SA ambient plane retains the unadapted chassis_hi frame");
    require_atomic(plane, "chassis");
    for(const char* name : {"chassis_lo", "chassis_vlo"})
        require_atomic(plane, name);
    unique_frame(plane, "chassis_dummy");
    if(dead_dodo)
        require_atomic(plane, "extra1");
    else if(unique_frame(plane, "extra1", false))
        throw std::runtime_error("Adapted Airtrain has an unexpected extra1 component");

    auto* front_dummy = unique_frame(plane, "wheel_front_dummy");
    auto* rear_dummy = unique_frame(plane, "wheel_rear_dummy");
    auto* front_wheel = unique_frame(plane, "wheel_front");
    auto* rear_wheel = unique_frame(plane, "wheel_rear");
    if(!inside(front_dummy, model) || !inside(rear_dummy, model) ||
       front_wheel->getParent() != front_dummy || rear_wheel->getParent() != rear_dummy ||
       direct_atomics(plane, front_wheel) != 1 || direct_atomics(plane, rear_wheel) != 1 ||
       subtree_atomics(plane, front_dummy) != 1 || subtree_atomics(plane, rear_dummy) != 1)
        throw std::runtime_error("SA ambient plane must retain both original static wheel meshes");

    const auto anchors = derive_ambient_plane_anchors(plane, 1.0f);
    static constexpr const char* names[] = {
        "wheel_rf_dummy", "wheel_lf_dummy", "wheel_rb_dummy", "wheel_lb_dummy"
    };
    for(size_t i = 0; i < anchors.size(); ++i) {
        auto* frame = unique_frame(plane, names[i]);
        if(frame->getParent() != model || frame->child || direct_atomics(plane, frame) || subtree_atomics(plane, frame))
            throw std::runtime_error(std::string("SA ambient plane suspension frame must be empty under its model root: ") + names[i]);
        const auto& actual = frame->matrix.pos;
        if(!std::isfinite(actual.x) || !std::isfinite(actual.y) || !std::isfinite(actual.z) ||
           std::fabs(actual.x - anchors[i].x) > 0.001f ||
           std::fabs(actual.y - anchors[i].y) > 0.001f ||
           std::fabs(actual.z - anchors[i].z) > 0.001f)
            throw std::runtime_error(std::string("SA ambient plane suspension point does not match original wheel geometry: ") + names[i]);
    }
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

void adapt_sa_ambient_plane(rw::Clump* plane, const std::filesystem::path& collision,
                            float wheel_size) {
    if(collision.empty())
        throw std::runtime_error("Ambient plane conversion requires its original class collision input");
    require_ambient_plane_source(plane, wheel_size);
    auto* model = ambient_plane_model_root(plane);
    const auto anchors = derive_ambient_plane_anchors(plane, wheel_size);

    std::strcpy(gta::getNodeName(unique_frame(plane, "chassis_hi")), "chassis");
    static constexpr const char* names[] = {
        "wheel_rf_dummy", "wheel_lf_dummy", "wheel_rb_dummy", "wheel_lb_dummy"
    };
    for(size_t i = 0; i < anchors.size(); ++i)
        add_empty_plane_wheel_dummy(model, names[i], anchors[i]);

    // SA's CPlane/Cvehicle descriptor reads these four wheel points and owns
    // flight and suspension. The LCS front wheel is singular, the rear source
    // atomic contains its original left/right tires, so these dummies stay
    // empty and the two original wheel meshes remain visible with their source
    // transforms. This avoids SA cloning one source wheel over both axles.
    adapt_sa_vehicle_common(plane, collision);
    check_sa_ambient_plane(plane);
    std::cout << "sa_ambient_plane_class=true native_cplane_wheel_points=true "
                 "source_wheel_geometry_preserved_static=true front_nose_point_duplicated=true "
                 "rear_wheel_points_derived_from_source_vertices=true ";
}

void validate_sa_ambient_plane(const std::filesystem::path& path) {
    auto* plane = read_vehicle_clump(path);
    try {
        check_sa_ambient_plane(plane);
        validate_sa_vehicle_common(plane);
    } catch(...) {
        plane->destroy();
        throw;
    }
    const auto atomics = plane->countAtomics();
    plane->destroy();
    std::cout << "sa_ambient_plane_hierarchy_validated=true atomics=" << atomics
                 << " game_executed=false\n";
}
