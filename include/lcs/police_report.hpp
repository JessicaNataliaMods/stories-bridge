#pragma once

#include "lcs/audio.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lcs {

// Zone records for this consumer only. Supply NavigationZoneArray entries in
// source insertion order, including its verified index-zero root. Types match
// CTheZones: 0 is audio-eligible default, 1 participates in hierarchy only.
struct PoliceReportZone {
    std::string name;
    int32_t type = 0;
    std::array<float, 6> bounds{}; // min x/y/z, max x/y/z
    bool root = false; // true only for verified NavigationZoneArray[0]
};

struct PoliceReportVoice {
    size_t slot = 0;
    uint64_t generation = 0;
};

struct PoliceReportPlaybackReceipt {
    bool entered = false;
    bool accepted = false;
    std::optional<PoliceReportVoice> voice;
};

struct PoliceReportCallbacks {
    std::function<PoliceReportPlaybackReceipt(uint32_t, const AudioSample&)> play;
    // nullopt means the receipt is stale or can no longer be observed.
    std::function<std::optional<bool>(const PoliceReportVoice&)> finished;
    // True only after the owned voice was actually released.
    std::function<bool(const PoliceReportVoice&)> stop;
};

enum class PoliceReportRequestResult {
    queued,
    source_drop,
    unavailable,
};

// Consumer for the retail 943 last-seen composition. request() only stages a
// complete source report; tick() advances owned PCM receipts on the game frame.
class PoliceReportConsumer {
public:
    static constexpr size_t source_queue_capacity = 60;
    static constexpr uint32_t source_report_end = 0x161E;

    bool prepare(const AudioArchive&, const std::vector<PoliceReportZone>&,
                 PoliceReportCallbacks);
    PoliceReportRequestResult request(const std::array<float, 3>& position,
                                      bool cutscene);
    void tick();
    bool shutdown();

    bool prepared() const { return prepared_; }
    bool playing() const { return active_voice_.has_value(); }
    bool healthy() const { return prepared_ && !shutdown_ && !fatal_ && diagnostic_.empty(); }
    size_t queued_entries() const { return queued_entries_; }
    const std::string& diagnostic() const { return diagnostic_; }

private:
    struct ZoneNode {
        PoliceReportZone zone;
        std::vector<size_t> children;
    };
    struct QueuedReport {
        std::vector<uint32_t> samples; // includes retail NO_SAMPLE delimiter
        size_t next = 0;
    };

    bool prepared_ = false;
    bool shutdown_ = false;
    bool fatal_ = false;
    std::string diagnostic_;
    PoliceReportCallbacks callbacks_;
    std::vector<ZoneNode> zones_;
    std::vector<size_t> audio_zone_order_;
    std::unordered_map<uint32_t, AudioSample> samples_;
    std::deque<QueuedReport> reports_;
    size_t queued_entries_ = 0;
    std::optional<PoliceReportVoice> active_voice_;

    bool build_zone_order(const std::vector<PoliceReportZone>&);
    bool insert_zone(size_t inner, size_t outer);
    bool zone_contains(const PoliceReportZone&, const std::array<float, 3>&) const;
    const PoliceReportZone* find_audio_zone(const std::array<float, 3>&) const;
    std::optional<uint32_t> zone_sample(const std::string&) const;
    void fail(std::string message, bool fatal);
    void clear_recoverable_error();
};

} // namespace lcs
