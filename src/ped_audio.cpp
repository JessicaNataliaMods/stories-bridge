#include "lcs/ped_audio.hpp"

#include "audio_sample_ids.inc"

namespace lcs {
namespace {
constexpr uint8_t generic_female_type = 5;
constexpr uint8_t generic_female_type_alt = 22;

std::optional<LcsPedVoiceKind> voice_kind(SaPedSpeechContext context) {
    switch (context) {
    case SaPedSpeechContext::drowning_death:
    case SaPedSpeechContext::death_high:
    case SaPedSpeechContext::death_low: return LcsPedVoiceKind::death;
    case SaPedSpeechContext::pain_high: return LcsPedVoiceKind::pain_high;
    case SaPedSpeechContext::pain_low: return LcsPedVoiceKind::pain_low;
    case SaPedSpeechContext::burning: return LcsPedVoiceKind::fire;
    case SaPedSpeechContext::panic: return LcsPedVoiceKind::panic;
    default: return std::nullopt;
    }
}

LcsPedVoice voice_for(SaPedVoiceProfile ped) {
    if (ped.has_player_data) return LcsPedVoice::toni;
    if (ped.role == SaPedVoiceRole::maria) return LcsPedVoice::female;
    if (ped.role != SaPedVoiceRole::generic) return LcsPedVoice::male;
    if (ped.ped_type == generic_female_type || ped.ped_type == generic_female_type_alt)
        return LcsPedVoice::female;
    return LcsPedVoice::male;
}

LcsPedVoiceRange range_for(LcsPedVoice voice, LcsPedVoiceKind kind) {
    switch (voice) {
    case LcsPedVoice::toni:
        switch (kind) {
        case LcsPedVoiceKind::pain_high: return LcsPedVoiceRange{SFX_TONI_GENERAL_IMP_HI_01, 16};
        case LcsPedVoiceKind::pain_low: return LcsPedVoiceRange{SFX_TONI_GENERAL_IMP_LOW_01, 15};
        case LcsPedVoiceKind::fire: return LcsPedVoiceRange{SFX_TONI_GENERAL_ON_FIRE_01, 7};
        case LcsPedVoiceKind::death: return {};
        case LcsPedVoiceKind::panic: return LcsPedVoiceRange{SFX_MALE_PANIC_01, 23};
        }
        break;
    case LcsPedVoice::female:
        switch (kind) {
        case LcsPedVoiceKind::death: return LcsPedVoiceRange{SFX_FEMALE_DEATH_01, 16};
        case LcsPedVoiceKind::pain_high:
        case LcsPedVoiceKind::pain_low: return LcsPedVoiceRange{SFX_FEMALE_PAIN_01, 34};
        case LcsPedVoiceKind::fire: return LcsPedVoiceRange{SFX_FEMALE_PAIN_ON_FIRE_01, 11};
        case LcsPedVoiceKind::panic: return LcsPedVoiceRange{SFX_FEMALE_PANIC_01, 13};
        }
        break;
    case LcsPedVoice::male:
        switch (kind) {
        case LcsPedVoiceKind::death: return LcsPedVoiceRange{SFX_MALE_DEATH_01, 28};
        case LcsPedVoiceKind::pain_high:
        case LcsPedVoiceKind::pain_low: return LcsPedVoiceRange{SFX_MALE_PAIN_01, 51};
        case LcsPedVoiceKind::fire: return LcsPedVoiceRange{SFX_MALE_PAIN_ON_FIRE_01, 21};
        case LcsPedVoiceKind::panic: return LcsPedVoiceRange{SFX_MALE_PANIC_01, 23};
        }
        break;
    }
    return {};
}

LcsPedVoiceRange banked_range(uint32_t first, uint8_t count,
                              uint32_t second, uint8_t banks) {
    return {first, count, second - first, banks};
}

std::optional<LcsPedVoiceRange> role_range(SaPedVoiceRole role,
                                           SaPedSpeechContext context) {
    // Source equivalents are GetCopTalkSfx, GetSwatTalkSfx, GetFBITalkSfx,
    // GetMedicTalkSfx and the named story-character talk functions. Global SA
    // contexts are adapted by event meaning; no role is inferred from pedType.
    switch (role) {
    case SaPedVoiceRole::cop:
        switch (context) {
        case SaPedSpeechContext::arrest:
            return banked_range(SFX_COP1_ARREST_1, 2, SFX_COP2_ARREST_1, 6);
        case SaPedSpeechContext::chase_foot:
            return banked_range(SFX_COP1_CHASE_FOOT_1, 6, SFX_COP2_CHASE_FOOT_1, 6);
        case SaPedSpeechContext::fight:
            return banked_range(SFX_COP1_FIGHT_1, 3, SFX_COP2_FIGHT_1, 6);
        case SaPedSpeechContext::dodge:
            return banked_range(SFX_COP1_DODGE_1, 2, SFX_COP2_DODGE_1, 6);
        case SaPedSpeechContext::bump:
            return banked_range(SFX_COP1_BUMP_1, 5, SFX_COP2_BUMP_1, 6);
        case SaPedSpeechContext::shoot:
        case SaPedSpeechContext::shoot_generic:
            return banked_range(SFX_COP1_SHOOT_1, 6, SFX_COP2_SHOOT_1, 6);
        default: return std::nullopt;
        }
    case SaPedVoiceRole::swat:
        switch (context) {
        case SaPedSpeechContext::rope:
            return banked_range(SFX_SWAT_VOICE_1_ROPE_1, 2,
                                SFX_SWAT_VOICE_2_ROPE_1, 2);
        case SaPedSpeechContext::shoot:
        case SaPedSpeechContext::shoot_generic:
            return banked_range(SFX_SWAT_VOICE_1_SHOOT_1, 5,
                                SFX_SWAT_VOICE_2_SHOOT_1, 2);
        default: return std::nullopt;
        }
    case SaPedVoiceRole::fbi:
        switch (context) {
        case SaPedSpeechContext::shoot:
        case SaPedSpeechContext::shoot_generic:
            return banked_range(SFX_FBI_VOICE_1_SHOOT_1, 5,
                                SFX_FBI_VOICE_2_SHOOT_1, 2);
        default: return std::nullopt;
        }
    case SaPedVoiceRole::medic:
        switch (context) {
        case SaPedSpeechContext::victim:
            return banked_range(SFX_AMBULAN_VOICE_1_VICTIM_1, 17,
                                SFX_AMBULAN_VOICE_2_VICTIM_1, 2);
        case SaPedSpeechContext::van:
            return banked_range(SFX_AMBULAN_VOICE_1_VAN_1, 10,
                                SFX_AMBULAN_VOICE_2_VAN_1, 2);
        default: return std::nullopt;
        }
    case SaPedVoiceRole::maria:
        switch (context) {
        // CTX_GLOBAL_CAR_CRASH, JACKED_CAR and SHOCKED map to the source's
        // CRASH_VEHICLE, CAR_JACKED and CHAT_EVENT lines respectively.
        case SaPedSpeechContext::car_crash: return LcsPedVoiceRange{SFX_MARIA_CRASH_CAR_1, 4};
        case SaPedSpeechContext::jacked_car: return LcsPedVoiceRange{SFX_MARIA_JACKED_CAR_1, 3};
        case SaPedSpeechContext::shocked: return LcsPedVoiceRange{SFX_MARIA_SHOCKED_1, 2};
        default: return std::nullopt;
        }
    case SaPedVoiceRole::donald_love:
        switch (context) {
        case SaPedSpeechContext::car_crash: return LcsPedVoiceRange{SFX_DONALD_CRASH_CAR_1, 3};
        case SaPedSpeechContext::jacked_car: return LcsPedVoiceRange{SFX_DONALD_JACKED_CAR_1, 3};
        case SaPedSpeechContext::shocked: return LcsPedVoiceRange{SFX_DONALD_SHOCKED_1, 2};
        default: return std::nullopt;
        }
    case SaPedVoiceRole::jd_otool:
        switch (context) {
        case SaPedSpeechContext::car_crash: return LcsPedVoiceRange{SFX_JD_CRASH_CAR_1, 3};
        case SaPedSpeechContext::jacked_car: return LcsPedVoiceRange{SFX_JD_JACKED_CAR_1, 3};
        case SaPedSpeechContext::shocked: return LcsPedVoiceRange{SFX_JD_SHOCKED_1, 2};
        default: return std::nullopt;
        }
    case SaPedVoiceRole::generic:
    case SaPedVoiceRole::fireman:
        return std::nullopt;
    }
    return std::nullopt;
}
} // namespace

std::optional<LcsPedVoiceRange> lcs_ped_voice_range(SaPedVoiceProfile ped,
                                                    SaPedSpeechContext context) {
    if (const auto specialized = role_range(ped.role, context)) {
        if (specialized->count) return specialized;
        return std::nullopt;
    }
    const auto kind = voice_kind(context);
    if (!kind) return std::nullopt;
    const auto range = range_for(voice_for(ped), *kind);
    if (!range.count) return std::nullopt;
    return range;
}

std::optional<uint32_t> lcs_ped_voice_sample(SaPedVoiceProfile ped,
                                             SaPedSpeechContext context,
                                             uint32_t variation) {
    const auto range = lcs_ped_voice_range(ped, context);
    if (!range || !range->count || !range->bank_count) return std::nullopt;
    const auto bank = ped.actor_seed % range->bank_count;
    return range->first_sample + bank * range->bank_stride + variation % range->count;
}

bool lcs_ped_voice_is_intentionally_silent(SaPedVoiceProfile ped,
                                           SaPedSpeechContext context) {
    if (!ped.has_player_data) return false;
    switch (context) {
    case SaPedSpeechContext::drowning_death:
    case SaPedSpeechContext::death_high:
    case SaPedSpeechContext::death_low: return true;
    default: return false;
    }
}

bool lcs_ped_voice_requires_live_player_gate(SaPedVoiceProfile ped,
                                             SaPedSpeechContext context) {
    return ped.role == SaPedVoiceRole::cop &&
           (context == SaPedSpeechContext::chase_foot ||
            context == SaPedSpeechContext::bump);
}

bool sa_ped_voice_takeover_eligible(uint16_t audio_event, uint16_t context,
                                    uint32_t delay_ms, bool frontend,
                                    bool force_audible, bool native_accepted,
                                    bool sample_ready) {
    if (audio_event != 52 || delay_ms != 0 || frontend || force_audible ||
        !native_accepted || !sample_ready)
        return false;
    switch (static_cast<SaPedSpeechContext>(context)) {
    case SaPedSpeechContext::arrest:
    case SaPedSpeechContext::bump:
    case SaPedSpeechContext::car_crash:
    case SaPedSpeechContext::chase_foot:
    case SaPedSpeechContext::dodge:
    case SaPedSpeechContext::fight:
    case SaPedSpeechContext::jacked_car:
    case SaPedSpeechContext::rope:
    case SaPedSpeechContext::shocked:
    case SaPedSpeechContext::shoot:
    case SaPedSpeechContext::shoot_generic:
    case SaPedSpeechContext::van:
    case SaPedSpeechContext::victim:
    case SaPedSpeechContext::drowning_death:
    case SaPedSpeechContext::death_high:
    case SaPedSpeechContext::death_low:
    case SaPedSpeechContext::pain_high:
    case SaPedSpeechContext::pain_low:
    case SaPedSpeechContext::burning:
    case SaPedSpeechContext::panic: return true;
    case SaPedSpeechContext::arrest_criminal:
    case SaPedSpeechContext::chat:
    case SaPedSpeechContext::sprayed: return false;
    }
    return false;
}

} // namespace lcs
