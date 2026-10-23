#pragma once

#include <array>
#include <cstdint>
#include <filesystem>

namespace lcs {

// Original LCS breaks a traffic light into its separate post, signal-head and
// pedestrian-box models. SA's generic breakable effect shatters the mesh per
// triangle instead, so the call that hands the object to SA's break manager is
// answered here: the object is queued, and the next game-thread service call
// creates the LCS pieces as ordinary physical objects at its transform.
class SaSignalDebris final {
public:
    // Resolves the traffic-light model and its four piece models from the
    // generated model table. Returns false (and stays inactive) if any is absent.
    bool configure(const std::filesystem::path& model_bindings);
    void reset();

    bool active() const noexcept { return signal_model_ != 0; }
    bool handles_entity(uintptr_t entity) const noexcept;
    // Queues a break of a converted traffic light. False leaves SA's own path.
    bool capture(uintptr_t entity) noexcept;
    // Game thread: creates the pieces of every queued break.
    void service();

    uint64_t captured() const noexcept { return captured_; }
    uint64_t spawned() const noexcept { return spawned_; }

private:
    struct Event {
        std::array<float, 12> matrix{};
        std::array<float, 3> velocity{};
    };
    static constexpr unsigned queue_capacity = 8;

    struct Piece {
        int32_t handle = 0;
        uint16_t model = 0;
        uint64_t expires = 0;
    };
    static constexpr unsigned piece_capacity = 32;

    void expire(bool all);
    void remember(int32_t handle, uint16_t model);

    std::array<Piece, piece_capacity> pieces_{};
    unsigned piece_count_ = 0;
    uint16_t signal_model_ = 0;
    std::array<uint16_t, 4> piece_models_{};
    std::array<Event, queue_capacity> queue_{};
    unsigned queue_size_ = 0;
    uint64_t captured_ = 0, spawned_ = 0;
    uint32_t random_ = 0x9E3779B9u;
};

} // namespace lcs
