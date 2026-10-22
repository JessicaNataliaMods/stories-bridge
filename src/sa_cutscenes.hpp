#pragma once
#include "lcs/native.hpp"
#include "lcs/audio.hpp"
#include "sa_audio.hpp"
#include <atomic>
#include <thread>
#include <map>
namespace lcs {
// Owns mounted original resources and a separate stereo voice. All native
// game calls run on the game thread. The pause observer only reads the two
// verified timer flags and calls waveOut under MissionSpeech's mutex.
class NativeCutscenes {
    struct Scene {std::string alias,hash;};
    std::filesystem::path directory_,audio_directory_;
    std::map<std::string,Scene> scenes_;
    MissionSpeech voice_;
    AudioSample sample_;
    bool armed_=false,started_=false;
    std::atomic<bool> exiting_{false};
    std::thread pause_observer_;
public:
    // audio_directory: the original PS2 AUDIO/CUTSCENE folder. Each decoded
    // stream must hash to the canonical wave recorded in the scene index.
    NativeCutscenes(const std::filesystem::path& game,const std::filesystem::path& directory,const std::filesystem::path& audio_directory,NativeAdapter& adapter);
    ~NativeCutscenes();
    void service();
    void clear();
};
}
