#include "sa_signal_debris.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <windows.h>

#include "lcs/native.hpp"
#include "lcs/sa_executor.hpp"

namespace lcs {
namespace {

constexpr uintptr_t object_pool_global = 0xB7449C;
constexpr size_t object_stride = 0x19C;

// Offsets of each LCS piece from the assembled traffic light's origin, in the
// light's own axes (right, forward, up). Order matches `piece_names`.
struct PieceOffset { float x, y, z; };
constexpr PieceOffset piece_offsets[4] = {
    {2.96f, 0.15f, 0.34f},   // post
    {-3.70f, -0.25f, 2.85f}, // signal head
    {-3.70f, 0.60f, 2.85f},  // signal head
    {2.86f, 0.98f, -0.36f},  // pedestrian box
};
const char* const piece_names[4] = {"tlight_post", "tlight_box1", "tlight_box2", "tlight_walk"};

int32_t bits(float value) {
    int32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

uintptr_t object_from_handle(int32_t handle, uint16_t model) noexcept {
    __try {
        const auto pool = *reinterpret_cast<const uintptr_t*>(object_pool_global);
        if (!pool) return 0;
        const auto data = *reinterpret_cast<const uintptr_t*>(pool);
        const auto flags = *reinterpret_cast<const uint8_t* const*>(pool + 4);
        const auto count = *reinterpret_cast<const int32_t*>(pool + 8);
        const auto index = uint32_t(handle) >> 8;
        if (!data || !flags || count <= 0 || count > 0x7FFFFF || index >= uint32_t(count)) return 0;
        if ((flags[index] & 0x80) || (flags[index] & 0x7F) != (handle & 0x7F)) return 0;
        const auto entity = data + uintptr_t(index) * object_stride;
        return *reinterpret_cast<const uint16_t*>(entity + 0x22) == model ? entity : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Same sequence SA uses to relocate a world entity: leave the sectors, change
// the transform, refresh RenderWare and re-enter the sectors.
bool place(uintptr_t entity, const std::array<float, 9>& axes, float x, float y, float z) noexcept {
    __try {
        const auto matrix = *reinterpret_cast<const uintptr_t*>(entity + 0x14);
        if (!matrix) return false;
        auto* m = reinterpret_cast<float*>(matrix);
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned column = 0; column < 3; ++column)
                m[row * 4 + column] = axes[row * 3 + column];
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x534AE0)(entity);
        reinterpret_cast<void(__thiscall*)(uintptr_t, float, float, float)>(0x420B80)(entity, x, y, z);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x446F90)(entity);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x532B00)(entity);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x533020)(entity);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

bool SaSignalDebris::configure(const std::filesystem::path& model_bindings) {
    reset();
    std::ifstream input(model_bindings);
    if (!input) return false;
    uint16_t signal = 0;
    std::array<uint16_t, 4> pieces{};
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        int source_id = -1, runtime_id = -1;
        std::string source_name, runtime_name;
        if (!(row >> source_id >> runtime_id >> source_name >> runtime_name)) continue;
        if (runtime_id < 10000 || runtime_id >= 20000) continue;
        if (source_name == "trafficlight1") signal = uint16_t(runtime_id);
        for (unsigned i = 0; i < 4; ++i)
            if (source_name == piece_names[i]) pieces[i] = uint16_t(runtime_id);
    }
    for (const auto piece : pieces)
        if (!piece) return false;
    if (!signal) return false;
    signal_model_ = signal;
    piece_models_ = pieces;
    return true;
}

void SaSignalDebris::reset() {
    piece_count_ = 0;
    signal_model_ = 0;
    piece_models_ = {};
    queue_size_ = 0;
    captured_ = spawned_ = 0;
}

bool SaSignalDebris::handles_entity(uintptr_t entity) const noexcept {
    if (!entity || !signal_model_) return false;
    __try {
        return *reinterpret_cast<const uint16_t*>(entity + 0x22) == signal_model_;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SaSignalDebris::capture(uintptr_t entity) noexcept {
    if (!handles_entity(entity)) return false;
    if (queue_size_ >= queue_capacity) return true; // still never shatter per triangle
    Event event;
    __try {
        const auto matrix = *reinterpret_cast<const uintptr_t*>(entity + 0x14);
        if (!matrix) return false;
        const auto* m = reinterpret_cast<const float*>(matrix);
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned column = 0; column < 3; ++column)
                event.matrix[row * 3 + column] = m[row * 4 + column];
        for (unsigned i = 0; i < 3; ++i) event.matrix[9 + i] = m[12 + i];
        const auto* velocity = reinterpret_cast<const float*>(entity + 0x38);
        for (unsigned i = 0; i < 3; ++i) event.velocity[i] = velocity[i];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    queue_[queue_size_++] = event;
    ++captured_;
    return true;
}

// Script-created objects are not cleaned up by SA until deleted; pieces live
// for a while so they can be seen, then are deleted if still in the pool.
void SaSignalDebris::expire(bool all) {
    const auto now = GetTickCount64();
    unsigned kept = 0;
    for (unsigned i = 0; i < piece_count_; ++i) {
        const Piece piece = pieces_[i];
        if (!all && now < piece.expires) {
            pieces_[kept++] = piece;
            continue;
        }
        if (object_from_handle(piece.handle, piece.model)) execute_sa({0x0108, {piece.handle}});
    }
    piece_count_ = kept;
}

void SaSignalDebris::remember(int32_t handle, uint16_t model) {
    if (piece_count_ == piece_capacity) {
        const Piece oldest = pieces_[0];
        if (object_from_handle(oldest.handle, oldest.model)) execute_sa({0x0108, {oldest.handle}});
        for (unsigned i = 1; i < piece_count_; ++i) pieces_[i - 1] = pieces_[i];
        --piece_count_;
    }
    pieces_[piece_count_++] = Piece{handle, model, GetTickCount64() + 90000};
}

void SaSignalDebris::service() {
    if (piece_count_) expire(false);
    if (!queue_size_ || !signal_model_) return;
    // Pieces are never placed by the map, so their models are requested on use.
    for (const auto model : piece_models_) {
        if (!execute_sa({0x0247, {model}}).executed) { queue_size_ = 0; return; }
    }
    if (!execute_sa({0x038B, {}}).executed) { queue_size_ = 0; return; }

    const unsigned count = queue_size_;
    queue_size_ = 0;
    for (unsigned n = 0; n < count; ++n) {
        const Event& event = queue_[n];
        std::array<float, 9> axes;
        std::memcpy(axes.data(), event.matrix.data(), sizeof(axes));
        for (unsigned piece = 0; piece < 4; ++piece) {
            const auto& offset = piece_offsets[piece];
            const float x = event.matrix[9] + offset.x * axes[0] + offset.y * axes[3] + offset.z * axes[6];
            const float y = event.matrix[10] + offset.x * axes[1] + offset.y * axes[4] + offset.z * axes[7];
            const float z = event.matrix[11] + offset.x * axes[2] + offset.y * axes[5] + offset.z * axes[8];
            const auto created = execute_sa({0x029B, {piece_models_[piece], bits(x), bits(y), bits(z)}, {}, 0, 1});
            if (!created.executed) continue;
            const auto handle = created.values[0];
            const auto entity = object_from_handle(handle, piece_models_[piece]);
            if (entity) place(entity, axes, x, y, z);
            // Script-created objects stay static until made dynamic; the
            // pieces then fall from where they were mounted. No launch speed
            // is applied: the unit of the script speed command is unverified.
            execute_sa({0x0392, {handle, 1}});
            ++spawned_;
            remember(handle, piece_models_[piece]);
        }
    }
}

} // namespace lcs
