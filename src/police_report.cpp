#include "lcs/police_report.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace lcs {
namespace {
constexpr uint32_t kNoise = 248;
constexpr uint32_t kSuspect = 250;
constexpr uint32_t kLastSeen = 246;
constexpr uint32_t kIn = 204;
constexpr uint32_t kNorth = 229;
constexpr uint32_t kSouth = 283;
constexpr uint32_t kEast = 163;
constexpr uint32_t kWest = 310;
constexpr uint32_t kCentral = 96;

struct ZoneSample {
    const char* name;
    uint32_t sample;
};

// Names and sample numbers were recovered from the retail SLUS_214.23
// initializer at 0x22C798 and its strings at 0x3D59D8..0x3D5A48.
constexpr ZoneSample kRetailZoneSamples[] = {
    {"VICE_C", 3955}, {"IND_ZON", 3956}, {"COM_ZON", 3957},
    {"BEACH1", 3931}, {"BEACH2", 3959}, {"BEACH3", 3958},
    {"GOLFC", 3916}, {"STARI", 3915}, {"DOCKS", 3954},
    {"HAVANA", 3925}, {"HAITI", 3945}, {"PORNI", 3927},
    {"DTOWN", 3937}, {"A_PORT", 3926},
};

bool valid_zone_name(const std::string& name) {
    return !name.empty() && name.size() <= 8 &&
        std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') || c == '_';
        });
}

std::string source_zone_name(std::string name) {
    for (char& c : name)
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    // CTheZones::CreateZone copies at most seven bytes into its 8-byte label.
    if (name.size() > 7) name.resize(7);
    return name;
}

} // namespace

std::optional<uint32_t> PoliceReportConsumer::zone_sample(const std::string& name) const {
    for (const auto& entry : kRetailZoneSamples)
        if (name == entry.name) return entry.sample;
    return std::nullopt;
}

bool PoliceReportConsumer::build_zone_order(const std::vector<PoliceReportZone>& input) {
    zones_.clear();
    audio_zone_order_.clear();
    if (input.empty() || input.size() > 70 || !input.front().root || input.front().type != 0) {
        fail("Police report requires the verified default root followed by source navigation zones", true);
        return false;
    }

    zones_.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        const auto& source = input[i];
        if (source.root != (i == 0) || (source.type != 0 && source.type != 1) ||
            !valid_zone_name(source.name)) {
            fail("Invalid police report navigation-zone identity or ordering", true);
            return false;
        }

        ZoneNode node;
        node.zone = source;
        node.zone.name = source_zone_name(node.zone.name);
        for (size_t axis = 0; axis < 3; ++axis) {
            auto& minimum = node.zone.bounds[axis];
            auto& maximum = node.zone.bounds[axis + 3];
            if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
                fail("Nonfinite police report zone bounds", true);
                return false;
            }
            if (minimum > maximum) std::swap(minimum, maximum);
        }
        zones_.push_back(std::move(node));
    }

    // Match CTheZones::PostZoneCreation: insert every loaded navigation zone
    // into index zero in source file order, then walk children before parents.
    for (size_t i = 1; i < zones_.size(); ++i)
        insert_zone(i, 0);

    const auto visit = [&](const auto& self, size_t index) -> void {
        for (const auto child : zones_[index].children) self(self, child);
        if (zones_[index].zone.type == 0) audio_zone_order_.push_back(index);
    };
    visit(visit, 0);
    if (audio_zone_order_.empty() || audio_zone_order_.size() > 36) {
        fail("Original audio-zone traversal is empty or exceeds the retail 36-entry table", true);
        return false;
    }
    return true;
}

bool PoliceReportConsumer::insert_zone(size_t inner, size_t outer) {
    const auto& in = zones_[inner].zone.bounds;
    const auto& out = zones_[outer].zone.bounds;
    for (size_t axis = 0; axis < 3; ++axis)
        if (in[axis] < out[axis] || in[axis + 3] > out[axis + 3]) return false;

    // Existing children get the first chance to own the new zone. The source
    // hierarchy prepends newly inserted siblings and moved children.
    const auto existing_children = zones_[outer].children;
    for (const auto child : existing_children)
        if (insert_zone(inner, child)) return true;

    auto& parent_children = zones_[outer].children;
    parent_children.clear();
    parent_children.push_back(inner);

    for (const auto child : existing_children) {
        const auto& child_bounds = zones_[child].zone.bounds;
        bool contained = true;
        for (size_t axis = 0; axis < 3; ++axis)
            if (child_bounds[axis] < in[axis] || child_bounds[axis + 3] > in[axis + 3]) {
                contained = false;
                break;
            }
        if (contained) {
            zones_[inner].children.insert(zones_[inner].children.begin(), child);
        } else {
            parent_children.push_back(child);
        }
    }
    return true;
}

bool PoliceReportConsumer::prepare(const AudioArchive& archive,
                                  const std::vector<PoliceReportZone>& zones,
                                  PoliceReportCallbacks callbacks) {
    if (prepared_ || shutdown_) {
        fail("Police report consumer cannot be prepared twice or after shutdown", true);
        return false;
    }
    fatal_ = false;
    diagnostic_.clear();
    zones_.clear();
    audio_zone_order_.clear();
    samples_.clear();
    if (!callbacks.play || !callbacks.finished || !callbacks.stop) {
        fail("Police report requires play, finished, and checked-stop callbacks", true);
        return false;
    }
    if (!build_zone_order(zones)) return false;

    const uint32_t base_samples[] = {
        kNoise, kSuspect, kLastSeen, kIn, kNorth, kSouth, kEast, kWest, kCentral,
    };
    std::vector<uint32_t> required_samples(std::begin(base_samples), std::end(base_samples));
    for (const auto zone_index : audio_zone_order_) {
        const auto sample = zone_sample(zones_[zone_index].zone.name);
        if (sample && std::find(required_samples.begin(), required_samples.end(), *sample) == required_samples.end())
            required_samples.push_back(*sample);
    }
    if (required_samples.size() == std::size(base_samples)) {
        fail("No loaded source audio zone has a retail police-report sample mapping", true);
        return false;
    }

    std::unordered_map<uint32_t, AudioSample> loaded_samples;
    try {
        for (const auto sample_index : required_samples) {
            if (sample_index >= archive.sample_count())
                throw std::runtime_error("Retail police-report sample is outside the loaded audio archive");
            auto sample = archive.sample(sample_index);
            if (!sample.frequency || sample.pcm.empty() || sample.pcm.size() % 2)
                throw std::runtime_error("Retail police-report PCM is invalid");
            loaded_samples.emplace(sample_index, std::move(sample));
        }
    } catch (const std::exception& error) {
        fail(std::string("Cannot prepare retail police-report PCM: ") + error.what(), true);
        return false;
    }

    callbacks_ = std::move(callbacks);
    samples_ = std::move(loaded_samples);
    prepared_ = true;
    diagnostic_.clear();
    return true;
}

bool PoliceReportConsumer::zone_contains(const PoliceReportZone& zone,
                                         const std::array<float, 3>& position) const {
    for (size_t axis = 0; axis < 3; ++axis)
        if (position[axis] < zone.bounds[axis] || position[axis] > zone.bounds[axis + 3]) return false;
    return true;
}

const PoliceReportZone* PoliceReportConsumer::find_audio_zone(
    const std::array<float, 3>& position) const {
    for (const auto index : audio_zone_order_)
        if (zone_contains(zones_[index].zone, position)) return &zones_[index].zone;
    return nullptr;
}

PoliceReportRequestResult PoliceReportConsumer::request(const std::array<float, 3>& position,
                                                        bool cutscene) {
    if (!prepared_ || shutdown_ || fatal_ || !diagnostic_.empty() ||
        !callbacks_.play || !callbacks_.finished || !callbacks_.stop)
        return PoliceReportRequestResult::unavailable;
    if (!std::all_of(position.begin(), position.end(), [](float value) { return std::isfinite(value); }))
        return PoliceReportRequestResult::unavailable;

    // Retail checks the cutscene and its 60-entry queue before zone lookup.
    if (cutscene || queued_entries_ > source_queue_capacity ||
        source_queue_capacity - queued_entries_ <= 9)
        return PoliceReportRequestResult::source_drop;

    const auto* zone = find_audio_zone(position);
    if (!zone) return PoliceReportRequestResult::source_drop;
    const auto zone_index = zone_sample(zone->name);
    if (!zone_index) return PoliceReportRequestResult::source_drop;

    std::vector<uint32_t> sequence;
    sequence.reserve(9);
    sequence.push_back(kNoise);
    sequence.push_back(kSuspect);
    sequence.push_back(kLastSeen);
    sequence.push_back(kIn);

    const auto& bounds = zone->bounds;
    const float range_x = bounds[3] - bounds[0];
    const float range_y = bounds[4] - bounds[1];
    const float half_x = 0.5f * range_x + bounds[0];
    const float half_y = 0.5f * range_y + bounds[1];
    const float quarter_x = 0.25f * range_x;
    const float quarter_y = 0.25f * range_y;
    const float north_threshold = half_y + quarter_y;
    const float south_threshold = half_y - quarter_y;
    const float east_threshold = half_x + quarter_x;
    const float west_threshold = half_x - quarter_x;
    if (!std::isfinite(range_x) || !std::isfinite(range_y) ||
        !std::isfinite(half_x) || !std::isfinite(half_y) ||
        !std::isfinite(quarter_x) || !std::isfinite(quarter_y) ||
        !std::isfinite(north_threshold) || !std::isfinite(south_threshold) ||
        !std::isfinite(east_threshold) || !std::isfinite(west_threshold))
        return PoliceReportRequestResult::unavailable;

    bool processed_vertical = false;
    if (position[1] > north_threshold) {
        sequence.push_back(kNorth);
        processed_vertical = true;
    } else if (position[1] < south_threshold) {
        sequence.push_back(kSouth);
        processed_vertical = true;
    }
    if (position[0] > east_threshold) {
        sequence.push_back(kEast);
    } else if (position[0] < west_threshold) {
        sequence.push_back(kWest);
    } else if (!processed_vertical) {
        sequence.push_back(kCentral);
    }
    sequence.push_back(*zone_index);
    sequence.push_back(kNoise);
    sequence.push_back(source_report_end);

    if (sequence.size() > source_queue_capacity - queued_entries_)
        return PoliceReportRequestResult::source_drop;
    for (const auto sample_index : sequence)
        if (sample_index != source_report_end && samples_.find(sample_index) == samples_.end())
            return PoliceReportRequestResult::unavailable;

    try {
        reports_.push_back(QueuedReport{std::move(sequence), 0});
        queued_entries_ += reports_.back().samples.size();
    } catch (const std::exception& error) {
        fail(std::string("Cannot commit complete police-report queue entry: ") + error.what(), false);
        return PoliceReportRequestResult::unavailable;
    }
    return PoliceReportRequestResult::queued;
}

void PoliceReportConsumer::tick() {
    if (!prepared_ || shutdown_ || fatal_) return;

    if (active_voice_) {
        std::optional<bool> is_finished;
        try {
            is_finished = callbacks_.finished(*active_voice_);
        } catch (const std::exception& error) {
            fail(std::string("Cannot observe owned police-report voice: ") + error.what(), true);
            return;
        } catch (...) {
            fail("Cannot observe owned police-report voice", true);
            return;
        }
        if (!is_finished) {
            fail("Owned police-report voice receipt is no longer observable", true);
            return;
        }
        if (!*is_finished) return;

        bool released = false;
        try {
            released = callbacks_.stop(*active_voice_);
        } catch (const std::exception& error) {
            fail(std::string("Cannot release completed police-report voice: ") + error.what(), false);
            return;
        } catch (...) {
            fail("Cannot release completed police-report voice", false);
            return;
        }
        if (!released) {
            fail("Checked stop refused the completed police-report voice", false);
            return;
        }
        active_voice_.reset();
        clear_recoverable_error();
    }

    if (reports_.empty()) return;
    auto& report = reports_.front();
    if (report.next >= report.samples.size()) {
        reports_.pop_front();
        return;
    }
    const auto sample_index = report.samples[report.next];
    if (sample_index == source_report_end) {
        ++report.next;
        --queued_entries_;
        if (report.next == report.samples.size()) reports_.pop_front();
        clear_recoverable_error();
        return;
    }
    const auto sample = samples_.find(sample_index);
    if (sample == samples_.end()) {
        fail("Queued police-report sample was not prepared", true);
        return;
    }

    PoliceReportPlaybackReceipt receipt;
    try {
        receipt = callbacks_.play(sample_index, sample->second);
    } catch (const std::exception& error) {
        fail(std::string("Police-report playback callback threw: ") + error.what(), true);
        return;
    } catch (...) {
        fail("Police-report playback callback threw", true);
        return;
    }

    if (receipt.entered && receipt.accepted && receipt.voice) {
        active_voice_ = receipt.voice;
        ++report.next;
        --queued_entries_;
        clear_recoverable_error();
        return;
    }

    if (!receipt.entered && !receipt.accepted && !receipt.voice) {
        fail("MissionSpeech refused before device entry; queued report remains for retry", false);
        return;
    }

    if (receipt.voice) active_voice_ = receipt.voice;
    fail("MissionSpeech entered without a complete owned playback receipt; report is held", true);
}

bool PoliceReportConsumer::shutdown() {
    shutdown_ = true;
    reports_.clear();
    queued_entries_ = 0;
    if (!active_voice_) return true;

    bool released = false;
    try {
        released = callbacks_.stop && callbacks_.stop(*active_voice_);
    } catch (const std::exception& error) {
        fail(std::string("Cannot release active police-report voice during shutdown: ") + error.what(), false);
        return false;
    } catch (...) {
        fail("Cannot release active police-report voice during shutdown", false);
        return false;
    }
    if (!released) {
        fail("Checked stop refused the active police-report voice during shutdown", false);
        return false;
    }
    active_voice_.reset();
    clear_recoverable_error();
    return true;
}

void PoliceReportConsumer::fail(std::string message, bool fatal) {
    if (diagnostic_.empty()) diagnostic_ = std::move(message);
    fatal_ = fatal_ || fatal;
}

void PoliceReportConsumer::clear_recoverable_error() {
    if (!fatal_) diagnostic_.clear();
}

} // namespace lcs
