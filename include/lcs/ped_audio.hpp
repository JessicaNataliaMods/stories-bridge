#pragma once

#include <cstdint>
#include <optional>

namespace lcs {

// Contexts observed at SA's CPed::Say -> CAEPedSpeechAudioEntity::AddSayEvent
// boundary. Numeric identities are verified against gta-reversed's
// Audio/Enums/PedSpeechContexts.h as well as the native pain-context range.
enum class SaPedSpeechContext : uint16_t {
    arrest = 13,
    arrest_criminal = 14,
    bump = 28,
    car_crash = 29,
    chase_foot = 43,
    chat = 45,
    dodge = 74,
    fight = 89,
    jacked_car = 118,
    rope = 177,
    shocked = 181,
    shoot = 182,
    shoot_generic = 184,
    van = 231,
    victim = 232,
    drowning_death = 341,
    death_high = 342,
    death_low = 343,
    pain_high = 344,
    pain_low = 345,
    burning = 346,
    panic = 347,
    sprayed = 348,
};

enum class LcsPedVoice : uint8_t { male, female, toni };
enum class LcsPedVoiceKind : uint8_t { death, pain_high, pain_low, fire, panic };
// Assigned only by the caller's exact native-model-name resolver. Ped type
// alone cannot distinguish COP/SWAT/FBI or story character voices.
enum class SaPedVoiceRole : uint8_t {
    generic,
    cop,
    swat,
    fbi,
    medic,
    fireman,
    maria,
    donald_love,
    jd_otool,
};

struct SaPedVoiceProfile {
    uint8_t ped_type = 0;
    bool has_player_data = false;
    int16_t native_model = -1;
    SaPedVoiceRole role = SaPedVoiceRole::generic;
    // Stable per-native-actor seed used to choose among LCS voice banks.
    // This deliberately adapts LCS's audio-entity slot index to the SA ped.
    uint32_t actor_seed = 0;
};

struct LcsPedVoiceRange {
    uint32_t first_sample = 0;
    uint8_t count = 0;
    uint32_t bank_stride = 0;
    uint8_t bank_count = 1;
};

// Maps only source-proven speech contexts. A missing range means that the
// context is unsupported or that the source intentionally has no player line.
std::optional<LcsPedVoiceRange> lcs_ped_voice_range(SaPedVoiceProfile ped,
                                                    SaPedSpeechContext context);
std::optional<uint32_t> lcs_ped_voice_sample(SaPedVoiceProfile ped,
                                             SaPedSpeechContext context,
                                             uint32_t variation);
// The source explicitly returns NO_SAMPLE for Toni's death contexts. This is
// distinct from an unsupported context or an unavailable custom sample.
bool lcs_ped_voice_is_intentionally_silent(SaPedVoiceProfile ped,
                                           SaPedSpeechContext context);
// COP chase-foot and bump lines depend on live player state/wanted level in
// the LCS source. The SA adapter must prove those predicates before takeover.
bool lcs_ped_voice_requires_live_player_gate(SaPedVoiceProfile ped,
                                             SaPedSpeechContext context);

// A successful original AddSayEvent call is required before a voice can be
// taken over. Delayed, frontend and forced-audible calls retain SA playback.
bool sa_ped_voice_takeover_eligible(uint16_t audio_event, uint16_t context,
                                    uint32_t delay_ms, bool frontend,
                                    bool force_audible, bool native_accepted,
                                    bool sample_ready);

} // namespace lcs
