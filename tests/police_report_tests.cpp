#include "lcs/police_report.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct FakePlayback {
    std::vector<uint32_t> played;
    uint64_t generation = 0;
    bool active = false;
    bool done = false;
    bool allow_stop = true;
    size_t stops = 0;

    lcs::PoliceReportCallbacks callbacks() {
        lcs::PoliceReportCallbacks result;
        result.play = [this](uint32_t sample_index, const lcs::AudioSample& sample) {
            check(!sample.pcm.empty() && sample.pcm.size() % 2 == 0,
                  "Only prepared owned PCM reaches the consumer");
            played.push_back(sample_index);
            active = true;
            done = false;
            return lcs::PoliceReportPlaybackReceipt{
                true, true, lcs::PoliceReportVoice{3, ++generation}};
        };
        result.finished = [this](const lcs::PoliceReportVoice& voice) -> std::optional<bool> {
            if (!active || voice.slot != 3 || voice.generation != generation) return std::nullopt;
            return done;
        };
        result.stop = [this](const lcs::PoliceReportVoice& voice) {
            if (!active || voice.slot != 3 || voice.generation != generation || !allow_stop)
                return false;
            active = false;
            done = false;
            ++stops;
            return true;
        };
        return result;
    }
};

std::vector<lcs::PoliceReportZone> zone_fixture() {
    using Zone = lcs::PoliceReportZone;
    return {
        Zone{"ROOTTST", 0, {-1000.f, -1000.f, -500.f, 1000.f, 1000.f, 500.f}, true},
        // The source inserts this small zone first; later, larger parents move
        // it under themselves. It has no retail radio mapping and must win.
        Zone{"PORT_W", 0, {-100.f, -100.f, -100.f, 100.f, 100.f, 100.f}, false},
        Zone{"NAV_TEST", 1, {-300.f, -300.f, -200.f, 300.f, 300.f, 200.f}, false},
        Zone{"IND_ZON", 0, {-200.f, -200.f, -100.f, 200.f, 200.f, 100.f}, false},
    };
}

void test_zone_choice_and_owned_queue(const lcs::AudioArchive& archive) {
    FakePlayback backend;
    lcs::PoliceReportConsumer report;
    check(report.prepare(archive, zone_fixture(), backend.callbacks()),
          "Retail radio assets and the complete navigation hierarchy prepare");

    check(report.request({0.f, 0.f, 0.f}, false) == lcs::PoliceReportRequestResult::source_drop &&
          report.queued_entries() == 0 && backend.played.empty(),
          "An unmapped child zone wins over its mapped IND_ZON parent");
    check(report.request({150.f, 150.f, 0.f}, true) == lcs::PoliceReportRequestResult::source_drop &&
          report.queued_entries() == 0,
          "Cutscene requests are source drops before queueing");
    check(report.request({150.f, 150.f, 0.f}, false) == lcs::PoliceReportRequestResult::queued &&
          report.queued_entries() == 9 && backend.played.empty(),
          "A complete north/east report is atomically queued without opening playback");

    report.tick();
    check(backend.played == std::vector<uint32_t>{248} && report.playing() &&
          report.queued_entries() == 8,
          "Frame tick starts the first owned noise and dequeues only that sample");
    backend.done = true;
    backend.allow_stop = false;
    report.tick();
    check(report.playing() && report.queued_entries() == 8 && !report.diagnostic().empty() &&
          report.request({150.f, 150.f, 0.f}, false) == lcs::PoliceReportRequestResult::unavailable,
          "Refused checked cleanup retains ownership and blocks more requests");

    backend.allow_stop = true;
    for (int frame = 0; frame < 20 && (report.playing() || report.queued_entries()); ++frame) {
        backend.done = true;
        report.tick();
    }
    check(!report.playing() && report.queued_entries() == 0 && report.healthy() &&
          backend.played == std::vector<uint32_t>{248, 250, 246, 204, 229, 163, 3956, 248},
          "Owned receipts serialize the retail noise/suspect/last-seen/IN/north/east/zone/noise sequence");
    check(backend.stops == backend.played.size(),
          "Each submitted PCM sample has a matching accepted owned cleanup");

    check(report.shutdown() && report.request({150.f, 150.f, 0.f}, false) ==
          lcs::PoliceReportRequestResult::unavailable,
          "Shutdown prevents new requests after cleanup");
}

void test_source_queue_gate_and_shutdown(const lcs::AudioArchive& archive) {
    FakePlayback backend;
    lcs::PoliceReportConsumer report;
    check(report.prepare(archive, zone_fixture(), backend.callbacks()), "Queue-gate fixture prepares");
    const std::array<float, 3> position{{150.f, 150.f, 0.f}};
    for (int i = 0; i < 6; ++i)
        check(report.request(position, false) == lcs::PoliceReportRequestResult::queued,
              "Source queue accepts reports while more than nine entries remain free");
    check(report.queued_entries() == 54 &&
          report.request(position, false) == lcs::PoliceReportRequestResult::source_drop &&
          report.queued_entries() == 54,
          "Retail 60-entry queue drops when at most nine entries are free");
    check(report.request(position, false) == lcs::PoliceReportRequestResult::unavailable &&
          report.shutdown() && report.queued_entries() == 0 && backend.played.empty(),
          "Shutdown clears staged reports without ever opening playback");

    FakePlayback active_backend;
    lcs::PoliceReportConsumer active_report;
    check(active_report.prepare(archive, zone_fixture(), active_backend.callbacks()),
          "Active cleanup fixture prepares");
    check(active_report.request(position, false) == lcs::PoliceReportRequestResult::queued,
          "Active cleanup report is staged");
    active_report.tick();
    check(active_report.playing() && active_report.shutdown() && !active_report.playing() &&
          !active_backend.active,
          "Shutdown releases an active generation-bearing PCM receipt");
}

void test_prepare_rejects_unproven_root(const lcs::AudioArchive& archive) {
    FakePlayback backend;
    lcs::PoliceReportConsumer report;
    auto zones = zone_fixture();
    zones.front().root = false;
    check(!report.prepare(archive, zones, backend.callbacks()) && !report.prepared() &&
          !report.diagnostic().empty() &&
          report.request({0.f, 0.f, 0.f}, false) == lcs::PoliceReportRequestResult::unavailable,
          "Missing verified root is an observable prepare failure, not a guessed report zone");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Expected the converted LCS audio archive directory");
        const lcs::AudioArchive archive(argv[1]);
        test_zone_choice_and_owned_queue(archive);
        test_source_queue_gate_and_shutdown(archive);
        test_prepare_rejects_unproven_root(archive);
        std::cout << "Police report PCM consumer contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
