#include "sa_ped_audio.hpp"

#include "lcs/continuous_sound.hpp"
#include "lcs/sa_executor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <utility>
#include <vector>

#if defined(_WIN32) && defined(_M_IX86)
#include <windows.h>
#endif

namespace lcs {
namespace {
constexpr uintptr_t add_say_event_call = 0x5F000B;
constexpr uintptr_t add_say_event_target = 0x4E6550;
constexpr uintptr_t speech_service_volume_load = 0x4E5D94;
constexpr uintptr_t line_of_sight_target = 0x56A490;
constexpr uintptr_t find_player_ped_target = 0x56E210;
constexpr uintptr_t find_player_wanted_target = 0x56E230;
constexpr uintptr_t wanted_context_profile = 0x562360;
constexpr uint32_t ped_audio_entity_offset = 0x294;
constexpr uint32_t speech_volume_offset = 0xAC;
constexpr uint32_t ped_model_offset = 0x22;
constexpr uint32_t ped_state_offset = 0x530;
constexpr uint32_t wanted_level_offset = 0x2C;
constexpr float source_ped_comment_range = 40.0f;
constexpr uint8_t source_ped_comment_volume = 127;
constexpr uint8_t source_ped_comment_volume_behind_wall = 31;

using PedAudioProvider = std::function<SaPedAudio*()>;
PedAudioProvider ped_audio_provider;
bool hook_installed = false;

struct NativeVector { float x, y, z; };

#if defined(_WIN32) && defined(_M_IX86)
using AddSayEvent = uint16_t(__thiscall*)(void*, uint16_t, uint16_t, uint32_t,
                                         float, bool, bool, bool);
using GetIsLineOfSightClear = bool(__cdecl*)(const NativeVector&, const NativeVector&,
                                              bool, bool, bool, bool, bool, bool, bool);
using FindPlayerPed = void*(__cdecl*)(int32_t);
using FindPlayerWanted = void*(__cdecl*)(int32_t);

bool call_targets(uintptr_t site, uintptr_t target) {
    __try {
        const auto* code = reinterpret_cast<const uint8_t*>(site);
        int32_t relative = 0;
        if (code[0] != 0xE8) return false;
        std::memcpy(&relative, code + 1, sizeof(relative));
        return uintptr_t(int64_t(site + 5) + relative) == target;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool player_context_profile_matches() {
    constexpr uint8_t find_ped_signature[]{
        0x8B,0x44,0x24,0x04,0x85,0xC0,0x7D,0x07,0x0F,0xB6,0x05,0x74,0xCD,0xB7,
        0x00,0x69,0xC0,0x90,0x01,0x00,0x00,0x8B,0x80,0x98,0xCD,0xB7,0x00,0xC3};
    constexpr uint8_t find_wanted_signature[]{
        0x8B,0x44,0x24,0x04,0x85,0xC0,0x7D,0x07,0x0F,0xB6,0x05,0x74,0xCD,0xB7,
        0x00,0x69,0xC0,0x90,0x01,0x00,0x00,0x8B,0x80,0x9C,0xCD,0xB7,0x00,0xC3};
    // UpdateEachFrame obtains FindPlayerWanted(-1), reads the wanted level at
    // +0x2C, and tests its valid range. This pins the layout before the adapter
    // uses the same native field for the LCS cop-bump predicate.
    constexpr uint8_t wanted_level_reader[]{
        0x6A,0xFF,0xE8,0xC9,0xBE,0x00,0x00,0x8A,0x48,0x1E,0x83,0xC4,0x04,
        0xF6,0xC1,0x07,0x75,0x0F,0x8B,0x40,0x2C,0x83,0xF8,0x03,0x74,0x0E};
    __try {
        return std::memcmp(reinterpret_cast<const void*>(find_player_ped_target),
                           find_ped_signature, sizeof(find_ped_signature)) == 0 &&
               std::memcmp(reinterpret_cast<const void*>(find_player_wanted_target),
                           find_wanted_signature, sizeof(find_wanted_signature)) == 0 &&
               std::memcmp(reinterpret_cast<const void*>(wanted_context_profile),
                           wanted_level_reader, sizeof(wanted_level_reader)) == 0 &&
               call_targets(wanted_context_profile + 2, find_player_wanted_target);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool read_player_ped_state(int32_t& state) {
    if (!player_context_profile_matches()) return false;
    __try {
        auto* player = reinterpret_cast<FindPlayerPed>(find_player_ped_target)(-1);
        if (!player) return false;
        state = *reinterpret_cast<const int32_t*>(
            reinterpret_cast<uintptr_t>(player) + ped_state_offset);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    // ePedState is a dense retail enum ending at DEPLOY_STINGER (64).
    return state >= 0 && state <= 64;
}

bool read_player_wanted_level(int32_t& level) {
    if (!player_context_profile_matches()) return false;
    __try {
        auto* wanted = reinterpret_cast<FindPlayerWanted>(find_player_wanted_target)(-1);
        if (!wanted) return false;
        level = *reinterpret_cast<const int32_t*>(
            reinterpret_cast<uintptr_t>(wanted) + wanted_level_offset);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return level >= 0 && level <= 6;
}

bool cop_context_source_predicate(SaPedSpeechContext context) {
    if (context == SaPedSpeechContext::chase_foot) {
        int32_t player_state = -1;
        if (!read_player_ped_state(player_state)) return false;
        // reLCS GetCopTalkSfx returns NO_SAMPLE when the player is arrested,
        // dead, or dying. Retail SA ePedState values are pinned by the enum.
        constexpr int32_t ped_die = 54;
        constexpr int32_t ped_dead = 55;
        constexpr int32_t ped_arrested = 63;
        return player_state != ped_arrested && player_state != ped_dead &&
               player_state != ped_die;
    }
    if (context == SaPedSpeechContext::bump) {
        int32_t wanted_level = -1;
        // reLCS emits a cop bump line only above wanted level zero. An
        // unavailable pointer or invalid level leaves the accepted SA line.
        return read_player_wanted_level(wanted_level) && wanted_level > 0;
    }
    return true;
}

bool valid_voice_role(SaPedVoiceRole role) {
    switch (role) {
    case SaPedVoiceRole::generic:
    case SaPedVoiceRole::cop:
    case SaPedVoiceRole::swat:
    case SaPedVoiceRole::fbi:
    case SaPedVoiceRole::medic:
    case SaPedVoiceRole::fireman:
    case SaPedVoiceRole::maria:
    case SaPedVoiceRole::donald_love:
    case SaPedVoiceRole::jd_otool: return true;
    }
    return false;
}

bool line_of_sight_profile_matches() {
    // Same executable profile used by the native route-effect LOS consumer.
    constexpr uint8_t signature[]{
        0x83,0xEC,0x5C,0x66,0x81,0x3D,0x78,0xCD,
        0xB7,0x00,0xFF,0xFF,0x73,0x09,0x66,0xFF};
    __try {
        return std::memcmp(reinterpret_cast<const void*>(line_of_sight_target),
                           signature, sizeof(signature)) == 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool speech_service_profile_matches() {
    // Service copies speech slot +0xAC to the native sound volume before
    // CAESound initialization. The AddSayEvent hook may safely mute that slot
    // after scheduling and before this later service pass.
    constexpr uint8_t volume_load[]{0x8B, 0x96, 0xAC, 0x00, 0x00, 0x00, 0x52};
    __try {
        return std::memcmp(reinterpret_cast<const void*>(speech_service_volume_load),
                           volume_load, sizeof(volume_load)) == 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool patch_call(uintptr_t site, uintptr_t expected, uintptr_t replacement) {
    if (!call_targets(site, expected)) return false;
    const int64_t relative64 = int64_t(replacement) - int64_t(site + 5);
    if (relative64 < INT32_MIN || relative64 > INT32_MAX) return false;
    auto* code = reinterpret_cast<uint8_t*>(site);
    DWORD old_protection = 0;
    if (!VirtualProtect(code, 5, PAGE_EXECUTE_READWRITE, &old_protection)) return false;
    const int32_t relative = static_cast<int32_t>(relative64);
    std::memcpy(code + 1, &relative, sizeof(relative));
    FlushInstructionCache(GetCurrentProcess(), code, 5);
    DWORD ignored = 0;
    if (VirtualProtect(code, 5, old_protection, &ignored)) return true;

    const int32_t original_relative =
        static_cast<int32_t>(int64_t(expected) - int64_t(site + 5));
    std::memcpy(code + 1, &original_relative, sizeof(original_relative));
    FlushInstructionCache(GetCurrentProcess(), code, 5);
    VirtualProtect(code, 5, old_protection, &ignored);
    return false;
}

bool read_speech_entity(void* speech_entity, SaPedVoiceProfile& profile,
                        std::array<float, 3>& position, float& native_volume) {
    if (!speech_entity) return false;
    const auto speech = reinterpret_cast<uintptr_t>(speech_entity);
    if (speech < ped_audio_entity_offset) return false;
    const auto ped = speech - ped_audio_entity_offset;
    uintptr_t matrix = 0;
    int32_t ped_type = -1;
    uintptr_t player_data = 0;
    int16_t native_model = -1;
    __try {
        ped_type = *reinterpret_cast<const int32_t*>(ped + 0x598);
        player_data = *reinterpret_cast<const uintptr_t*>(ped + 0x480);
        native_model = *reinterpret_cast<const int16_t*>(ped + ped_model_offset);
        matrix = *reinterpret_cast<const uintptr_t*>(ped + 0x14);
        const auto* world = reinterpret_cast<const float*>(matrix ? matrix + 0x30 : ped + 4);
        std::memcpy(position.data(), world, 3 * sizeof(float));
        native_volume = *reinterpret_cast<const float*>(speech + speech_volume_offset);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (ped_type < 0 || ped_type > std::numeric_limits<uint8_t>::max() ||
        !std::isfinite(native_volume)) return false;
    profile.ped_type = static_cast<uint8_t>(ped_type);
    profile.has_player_data = player_data != 0;
    profile.native_model = native_model;
    uint32_t seed = static_cast<uint32_t>(ped) ^
                    (static_cast<uint32_t>(static_cast<uint16_t>(native_model)) *
                     0x9E3779B9u);
    seed ^= seed >> 16;
    seed *= 0x7FEB352Du;
    seed ^= seed >> 15;
    seed *= 0x846CA68Bu;
    profile.actor_seed = seed ^ (seed >> 16);
    return std::all_of(position.begin(), position.end(),
                       [](float value) { return std::isfinite(value); });
}

std::optional<bool> source_line_of_sight(const std::array<float, 3>& from,
                                         const std::array<float, 3>& to) {
    if (!line_of_sight_profile_matches()) return std::nullopt;
    const NativeVector source{from[0], from[1], from[2]};
    const NativeVector target{to[0], to[1], to[2]};
    bool clear = false;
    __try {
        // reLCS SetupPedComments: buildings block comments; other entity classes
        // do not. SA exposes the extra see-through flag, which remains false.
        clear = reinterpret_cast<GetIsLineOfSightClear>(line_of_sight_target)(
            source, target, true, false, false, false, false, false, false);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return std::nullopt;
    }
    return clear;
}

bool set_speech_volume(void* speech_entity, float value) {
    if (!speech_entity) return false;
    __try {
        *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(speech_entity) +
                                  speech_volume_offset) = value;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint16_t __fastcall add_say_event_hook(void* self, void*, uint16_t audio_event,
                                       uint16_t context, uint32_t delay_ms,
                                       float probability, bool override_silence,
                                       bool force_audible, bool frontend) {
    const auto accepted = reinterpret_cast<AddSayEvent>(add_say_event_target)(
        self, audio_event, context, delay_ms, probability, override_silence,
        force_audible, frontend);
    if (accepted == 0xFFFF || !ped_audio_provider) return accepted;
    try {
        auto* audio = ped_audio_provider();
        if (audio)
            audio->replace_if_ready(self, audio_event, context, delay_ms,
                                    force_audible != 0, frontend != 0);
    } catch (...) {
        // A replacement failure leaves the already accepted native event intact.
    }
    return accepted;
}
#endif
} // namespace

SaPedAudio::SaPedAudio(const AudioArchive& archive, std::shared_ptr<SaAudioMixer> mixer,
                       ListenerProvider listener)
    : archive_(archive), mixer_(std::move(mixer)), listener_(std::move(listener)) {
    worker_ = std::make_unique<AudioWorker>([] {}, std::chrono::milliseconds(10));
    SaPedVoiceProfile male{4, false}, female{5, false}, female_alt{22, false}, toni{0, true};
    SaPedVoiceProfile cop{4, false}, swat{4, false}, fbi{4, false}, medic{4, false};
    SaPedVoiceProfile fireman{4, false}, maria{5, false}, donald{4, false}, jd{4, false};
    cop.role = SaPedVoiceRole::cop;
    swat.role = SaPedVoiceRole::swat;
    fbi.role = SaPedVoiceRole::fbi;
    medic.role = SaPedVoiceRole::medic;
    fireman.role = SaPedVoiceRole::fireman;
    maria.role = SaPedVoiceRole::maria;
    donald.role = SaPedVoiceRole::donald_love;
    jd.role = SaPedVoiceRole::jd_otool;
    const std::array profiles{male, female, female_alt, toni, cop, swat, fbi,
                              medic, fireman, maria, donald, jd};
    const std::array contexts{
        SaPedSpeechContext::arrest, SaPedSpeechContext::bump,
        SaPedSpeechContext::car_crash, SaPedSpeechContext::chase_foot,
        SaPedSpeechContext::chat, SaPedSpeechContext::dodge,
        SaPedSpeechContext::fight, SaPedSpeechContext::jacked_car,
        SaPedSpeechContext::rope, SaPedSpeechContext::shocked,
        SaPedSpeechContext::shoot, SaPedSpeechContext::shoot_generic,
        SaPedSpeechContext::van, SaPedSpeechContext::victim,
        SaPedSpeechContext::death_high, SaPedSpeechContext::pain_high,
        SaPedSpeechContext::pain_low, SaPedSpeechContext::panic,
        SaPedSpeechContext::burning};
    for (const auto profile : profiles) {
        for (const auto context : contexts) {
            if (const auto range = lcs_ped_voice_range(profile, context)) request_range(*range);
        }
    }
}

SaPedAudio::~SaPedAudio() { shutdown(); }

void SaPedAudio::set_model_resolver(ModelRoleResolver resolver) {
    model_role_resolver_ = std::move(resolver);
}

void SaPedAudio::request_range(LcsPedVoiceRange range) {
    if (!range.count || !worker_ || stopping_.load(std::memory_order_acquire)) return;
    std::vector<uint32_t> pending;
    const auto index_at = [&range](uint32_t bank, uint32_t sample) {
        return range.first_sample + bank * range.bank_stride + sample;
    };
    try {
        std::lock_guard<std::mutex> guard(sample_mutex_);
        for (uint32_t bank = 0; bank < range.bank_count; ++bank) {
            for (uint32_t n = 0; n < range.count; ++n) {
                const uint32_t index = index_at(bank, n);
                if (samples_.count(index) || failed_.count(index) ||
                    !loading_.insert(index).second)
                    continue;
                pending.push_back(index);
            }
        }
    } catch (...) {
        std::lock_guard<std::mutex> guard(sample_mutex_);
        for (uint32_t bank = 0; bank < range.bank_count; ++bank)
            for (uint32_t n = 0; n < range.count; ++n)
                loading_.erase(index_at(bank, n));
        return;
    }
    if (pending.empty()) return;
    try {
        worker_->post([this, indices = pending] {
            for (const uint32_t index : indices) {
                std::shared_ptr<const AudioSample> decoded;
                try {
                    auto sample = archive_.sample(index);
                    if ((sample.channels == 1 || sample.channels == 2) && sample.frequency &&
                        !sample.pcm.empty() &&
                        sample.pcm.size() % (size_t(sample.channels) * sizeof(int16_t)) == 0)
                        decoded = std::make_shared<AudioSample>(std::move(sample));
                } catch (...) {
                }
                std::lock_guard<std::mutex> guard(sample_mutex_);
                loading_.erase(index);
                if (decoded) samples_.emplace(index, std::move(decoded));
                else failed_.insert(index);
            }
        });
    } catch (...) {
        std::lock_guard<std::mutex> guard(sample_mutex_);
        for (const auto index : pending) loading_.erase(index);
    }
}

std::shared_ptr<const AudioSample> SaPedAudio::sample(uint32_t index) {
    std::lock_guard<std::mutex> guard(sample_mutex_);
    const auto found = samples_.find(index);
    return found == samples_.end() ? nullptr : found->second;
}

bool SaPedAudio::replace_accepted(void* speech_entity, uint16_t audio_event,
                                  uint16_t context, uint32_t delay_ms,
                                  bool force_audible, bool frontend) {
    if (!sa_ped_voice_takeover_eligible(audio_event, context, delay_ms, frontend,
                                        force_audible, true, true))
        return false;

    SaPedVoiceProfile profile;
    std::array<float, 3> position{};
    float old_volume = 0;
    if (!read_speech_entity(speech_entity, profile, position, old_volume) ||
        old_volume <= -999.0f) return false;
    if (model_role_resolver_ && profile.native_model >= 0) {
        const auto role = model_role_resolver_(profile.native_model);
        profile.role = valid_voice_role(role) ? role : SaPedVoiceRole::generic;
    }
    const auto voice_context = static_cast<SaPedSpeechContext>(context);
    if (lcs_ped_voice_requires_live_player_gate(profile, voice_context) &&
        !cop_context_source_predicate(voice_context))
        return false;

    // reLCS explicitly supplies NO_SAMPLE for player death comments. Keep the
    // already accepted SA slot and its scheduler, but silence its native dB
    // gain; this is different from a missing custom sample, which falls back.
    if (lcs_ped_voice_is_intentionally_silent(profile, voice_context))
        return set_speech_volume(speech_entity, -1000.0f);

    if (!mixer_ || !mixer_->available() || !listener_) return false;
    const auto range = lcs_ped_voice_range(profile, voice_context);
    if (!range) return false;

    random_.regenerate();
    const uint32_t variation = random_.table(context % 5) % range->count;
    const auto sample_index = lcs_ped_voice_sample(profile, voice_context, variation);
    if (!sample_index) return false;
    auto pcm = sample(*sample_index);
    if (!pcm) {
        request_range(*range);
        return false;
    }

    const auto listener = listener_();
    if (!listener) return false;
    float distance_squared = 0;
    float camera_x = 0;
    for (size_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(listener->position[axis]) || !std::isfinite(listener->right[axis]))
            return false;
        const float delta = position[axis] - listener->position[axis];
        distance_squared += delta * delta;
        camera_x += delta * listener->right[axis];
    }
    if (!std::isfinite(distance_squared) || !std::isfinite(camera_x)) return false;
    const float distance = std::sqrt(distance_squared);
    const auto clear = source_line_of_sight(listener->position, position);
    if (!clear) return false;
    const uint8_t emitting = *clear ? source_ped_comment_volume
                                    : source_ped_comment_volume_behind_wall;
    const auto volume = continuous_sound_volume(emitting, source_ped_comment_range, distance);
    const auto pan = continuous_sound_pan(distance, camera_x);
    if (!volume || !pan) return false;
    // LCS does not enqueue comments once ComputeVolume reaches zero at the
    // 40 m limit. Suppress SA's already accepted line in that source-silent
    // case; invalid listener/geometry remains a native fallback above.
    if (!*volume) return set_speech_volume(speech_entity, -1000.0f);

    // The original AddSayEvent already committed the event and its callback
    // slot. Temporarily mute that accepted slot; if the shared mixer refuses
    // the PCM, restore its exact native dB value so SA remains the fallback.
    if (!set_speech_volume(speech_entity, -1000.0f)) return false;
    bool queued = false;
    const uint32_t frequency = pcm->frequency;
    try {
        queued = mixer_->play_one_shot(std::move(pcm), frequency, *volume, *pan);
    } catch (...) {
    }
    if (queued) return true;
    set_speech_volume(speech_entity, old_volume);
    return false;
}

bool SaPedAudio::replace_if_ready(void* speech_entity, uint16_t audio_event,
                                  uint16_t context, uint32_t delay_ms,
                                  bool force_audible, bool frontend) {
    if (stopping_.load(std::memory_order_acquire)) return false;
    try {
        return replace_accepted(speech_entity, audio_event, context, delay_ms,
                                force_audible, frontend);
    } catch (...) {
        return false;
    }
}

void SaPedAudio::shutdown() {
    if (stopping_.exchange(true, std::memory_order_acq_rel)) return;
    if (worker_) worker_->stop();
}

bool sa_ped_audio_profile_matches() {
#if defined(_WIN32) && defined(_M_IX86)
    return sa_executor_profile_matches() &&
           call_targets(add_say_event_call, add_say_event_target) &&
           speech_service_profile_matches() &&
           line_of_sight_profile_matches() &&
           player_context_profile_matches();
#else
    return false;
#endif
}

bool sa_ped_audio_install(std::function<SaPedAudio*()> provider) {
#if defined(_WIN32) && defined(_M_IX86)
    if (!provider || hook_installed || !sa_ped_audio_profile_matches()) return false;
    try {
        ped_audio_provider = std::move(provider);
    } catch (...) {
        return false;
    }
    if (!patch_call(add_say_event_call, add_say_event_target,
                    reinterpret_cast<uintptr_t>(&add_say_event_hook))) {
        ped_audio_provider = {};
        return false;
    }
    hook_installed = true;
    return true;
#else
    (void)provider;
    return false;
#endif
}

} // namespace lcs
