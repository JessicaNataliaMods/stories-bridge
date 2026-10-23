#pragma once
#include "sa_audio_mixer.hpp"
#include "sa_audio_jobs.hpp"
#include "lcs/vehicle_surface_audio.hpp"
#include <functional>
#include <unordered_map>
#include <mutex>
namespace lcs {
class SaVehicleSurfaceAudio {
public:
    using ModelResolver=std::function<std::optional<int>(int)>;
private:
    struct Asset {std::shared_ptr<const AudioSample> pcm;uint32_t begin=0,end=0;};
    const AudioArchive& archive_;
    std::shared_ptr<SaAudioMixer> mixer_;
    ModelResolver resolver_;
    std::mutex mutex_;
    std::array<Asset,7> assets_{};
    std::unordered_map<uint64_t,uint32_t> touched_;
    std::unique_ptr<AudioWorker> worker_;
    bool publish(uint64_t,const VehicleSurfaceLoop&);
    void release(uint64_t);
public:
    SaVehicleSurfaceAudio(const AudioArchive&,std::shared_ptr<SaAudioMixer>,ModelResolver);
    ~SaVehicleSurfaceAudio();
    // Called inside the native processor boundary; false preserves SA gain.
    bool request(unsigned family,void* params);
    void service(uint32_t now,bool paused);
    void shutdown();
};
bool sa_vehicle_surface_audio_install(std::function<SaVehicleSurfaceAudio*()> provider);
}
