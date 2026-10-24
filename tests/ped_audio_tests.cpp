#include "lcs/audio.hpp"
#include "lcs/ped_audio.hpp"
#include "audio_sample_ids.inc"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int peak(const lcs::AudioSample& sample) {
    int value = 0;
    for (size_t n = 0; n + sizeof(int16_t) <= sample.pcm.size(); n += sizeof(int16_t)) {
        int16_t frame = 0;
        std::memcpy(&frame, sample.pcm.data() + n, sizeof(frame));
        value = std::max(value, std::abs(int(frame)));
    }
    return value;
}

void decode_source_voice_ranges(const char* audio_directory) {
    const lcs::AudioArchive archive(lcs::AudioArchive::Ps2Audio{}, audio_directory);
    const lcs::SaPedSpeechContext contexts[]{lcs::SaPedSpeechContext::arrest,
                                             lcs::SaPedSpeechContext::bump,
                                             lcs::SaPedSpeechContext::car_crash,
                                             lcs::SaPedSpeechContext::chase_foot,
                                             lcs::SaPedSpeechContext::dodge,
                                             lcs::SaPedSpeechContext::fight,
                                             lcs::SaPedSpeechContext::jacked_car,
                                             lcs::SaPedSpeechContext::rope,
                                             lcs::SaPedSpeechContext::shocked,
                                             lcs::SaPedSpeechContext::shoot,
                                             lcs::SaPedSpeechContext::shoot_generic,
                                             lcs::SaPedSpeechContext::van,
                                             lcs::SaPedSpeechContext::victim,
                                             lcs::SaPedSpeechContext::drowning_death,
                                             lcs::SaPedSpeechContext::death_high,
                                             lcs::SaPedSpeechContext::death_low,
                                             lcs::SaPedSpeechContext::pain_high,
                                             lcs::SaPedSpeechContext::pain_low,
                                             lcs::SaPedSpeechContext::burning,
                                             lcs::SaPedSpeechContext::panic};
    lcs::SaPedVoiceProfile profiles[]{{4, false}, {5, false}, {22, false}, {0, true},
                                      {4, false}, {4, false}, {4, false}, {4, false},
                                      {4, false}, {5, false}, {4, false}, {4, false}};
    profiles[4].role = lcs::SaPedVoiceRole::cop;
    profiles[5].role = lcs::SaPedVoiceRole::swat;
    profiles[6].role = lcs::SaPedVoiceRole::fbi;
    profiles[7].role = lcs::SaPedVoiceRole::medic;
    profiles[8].role = lcs::SaPedVoiceRole::fireman;
    profiles[9].role = lcs::SaPedVoiceRole::maria;
    profiles[10].role = lcs::SaPedVoiceRole::donald_love;
    profiles[11].role = lcs::SaPedVoiceRole::jd_otool;
    std::set<uint32_t> samples;
    for (const auto profile : profiles) {
        for (const auto context : contexts) {
            const auto range = lcs::lcs_ped_voice_range(profile, context);
            if (!range) continue; // Toni death intentionally has NO_SAMPLE.
            for (uint32_t bank = 0; bank < range->bank_count; ++bank)
                for (uint32_t n = 0; n < range->count; ++n)
                    samples.insert(range->first_sample + bank * range->bank_stride + n);
        }
    }
    check(!samples.empty(), "source voice sample ranges are populated");
    for (const uint32_t index : samples) {
        const auto decoded = archive.sample(index);
        check(decoded.channels == 1 && decoded.frequency > 0 && !decoded.pcm.empty() && peak(decoded) > 0,
              "every linked pedestrian voice sample decodes to non-silent mono PCM");
    }
    std::cout << "PASS: decoded " << samples.size()
              << " source-linked pedestrian voice samples without opening an audio device\n";
}
}

int main(int argc, char** argv) {
    try {
        const lcs::SaPedVoiceProfile player{0,true},npc{4,false};
        check(lcs::mute_native_player_speech(player,true,false),"Unmapped native CJ gameplay voice is silent");
        check(!lcs::mute_native_player_speech(player,true,true),"Accepted Toni replacement remains audible");
        check(!lcs::mute_native_player_speech(npc,true,false),"NPC native fallback remains audible");
        check(!lcs::mute_native_player_speech(player,false,false),"Explicit mute option off restores native policy");
        check(argc <= 2, "Usage: ped_audio_tests [original PS2 AUDIO directory]");
        using lcs::SaPedSpeechContext;
        using lcs::SaPedVoiceProfile;
        const SaPedVoiceProfile male{4, false};
        const SaPedVoiceProfile female{5, false};
        const SaPedVoiceProfile female_alt{22, false};
        const SaPedVoiceProfile cop{6, false};
        const SaPedVoiceProfile toni{0, true};
        SaPedVoiceProfile cop_voice{4, false};
        SaPedVoiceProfile swat_voice{4, false};
        SaPedVoiceProfile fbi_voice{4, false};
        SaPedVoiceProfile medic_voice{4, false};
        SaPedVoiceProfile maria{5, false};
        SaPedVoiceProfile donald{4, false};
        SaPedVoiceProfile jd{4, false};
        cop_voice.role = lcs::SaPedVoiceRole::cop;
        swat_voice.role = lcs::SaPedVoiceRole::swat;
        fbi_voice.role = lcs::SaPedVoiceRole::fbi;
        medic_voice.role = lcs::SaPedVoiceRole::medic;
        maria.role = lcs::SaPedVoiceRole::maria;
        donald.role = lcs::SaPedVoiceRole::donald_love;
        jd.role = lcs::SaPedVoiceRole::jd_otool;

        const auto male_death = lcs::lcs_ped_voice_range(male, SaPedSpeechContext::death_high);
        const auto female_death = lcs::lcs_ped_voice_range(female, SaPedSpeechContext::drowning_death);
        check(male_death && male_death->first_sample == SFX_MALE_DEATH_01 &&
                  male_death->count == 28,
              "generic male death uses the source death phrase range");
        check(female_death && female_death->first_sample == SFX_FEMALE_DEATH_01 &&
                  female_death->count == 16,
              "generic female drowning death uses the source female death range");
        const auto female_alt_death =
            lcs::lcs_ped_voice_range(female_alt, SaPedSpeechContext::death_low);
        check(female_alt_death && female_alt_death->first_sample == SFX_FEMALE_DEATH_01,
              "the second female ped type selects generic female comments");
        const auto cop_death = lcs::lcs_ped_voice_range(cop, SaPedSpeechContext::death_low);
        check(cop_death && cop_death->first_sample == SFX_MALE_DEATH_01,
              "cop death falls through to the generic male source voice");

        const auto cop_arrest = lcs::lcs_ped_voice_range(cop_voice, SaPedSpeechContext::arrest);
        const auto cop_fight = lcs::lcs_ped_voice_range(cop_voice, SaPedSpeechContext::fight);
        const auto cop_dodge = lcs::lcs_ped_voice_range(cop_voice, SaPedSpeechContext::dodge);
        const auto cop_shoot = lcs::lcs_ped_voice_range(cop_voice, SaPedSpeechContext::shoot_generic);
        const auto cop_bump = lcs::lcs_ped_voice_range(cop_voice, SaPedSpeechContext::bump);
        const auto cop_chase = lcs::lcs_ped_voice_range(cop_voice, SaPedSpeechContext::chase_foot);
        check(cop_arrest && cop_arrest->first_sample == SFX_COP1_ARREST_1 &&
                  cop_arrest->count == 2 && cop_arrest->bank_count == 6 &&
                  cop_fight && cop_fight->first_sample == SFX_COP1_FIGHT_1 &&
                  cop_fight->count == 3 && cop_dodge &&
                  cop_dodge->first_sample == SFX_COP1_DODGE_1 && cop_dodge->count == 2 &&
                  cop_shoot && cop_shoot->first_sample == SFX_COP1_SHOOT_1 &&
                  cop_shoot->count == 6,
              "COP arrest, fight, dodge and targeting use the source phrases and six banks");
        check(cop_bump && cop_bump->first_sample == SFX_COP1_BUMP_1 &&
                  cop_chase && cop_chase->first_sample == SFX_COP1_CHASE_FOOT_1 &&
                  lcs::lcs_ped_voice_requires_live_player_gate(cop_voice,
                                                               SaPedSpeechContext::bump) &&
                  lcs::lcs_ped_voice_requires_live_player_gate(cop_voice,
                                                               SaPedSpeechContext::chase_foot),
              "COP collision/chase phrases require the source's wanted/state predicates");
        check(!lcs::lcs_ped_voice_range(cop, SaPedSpeechContext::shoot) &&
                  !lcs::lcs_ped_voice_range(cop_voice, SaPedSpeechContext::arrest_criminal),
              "pedType alone and the criminal-arrest context do not select COP phrases");

        const auto swat_rope = lcs::lcs_ped_voice_range(swat_voice, SaPedSpeechContext::rope);
        const auto swat_shoot = lcs::lcs_ped_voice_range(swat_voice, SaPedSpeechContext::shoot);
        const auto fbi_shoot = lcs::lcs_ped_voice_range(fbi_voice, SaPedSpeechContext::shoot_generic);
        check(swat_rope && swat_rope->first_sample == SFX_SWAT_VOICE_1_ROPE_1 &&
                  swat_rope->count == 2 && swat_rope->bank_count == 2 &&
                  swat_shoot && swat_shoot->first_sample == SFX_SWAT_VOICE_1_SHOOT_1 &&
                  swat_shoot->count == 5 && fbi_shoot &&
                  fbi_shoot->first_sample == SFX_FBI_VOICE_1_SHOOT_1 &&
                  fbi_shoot->count == 5 && fbi_shoot->bank_count == 2,
              "SWAT rope/targeting and FBI targeting use their source voice banks");
        check(!lcs::lcs_ped_voice_range(swat_voice, SaPedSpeechContext::dodge) &&
                  !lcs::lcs_ped_voice_range(fbi_voice, SaPedSpeechContext::car_crash),
              "source NO_SAMPLE SWAT dodge and FBI crash retain the native fallback");

        const auto medic_victim =
            lcs::lcs_ped_voice_range(medic_voice, SaPedSpeechContext::victim);
        const auto medic_van = lcs::lcs_ped_voice_range(medic_voice, SaPedSpeechContext::van);
        check(medic_victim && medic_victim->first_sample == SFX_AMBULAN_VOICE_1_VICTIM_1 &&
                  medic_victim->count == 17 && medic_victim->bank_count == 2 &&
                  medic_van && medic_van->first_sample == SFX_AMBULAN_VOICE_1_VAN_1 &&
                  medic_van->count == 10 && medic_van->bank_count == 2,
              "medic victim and van contexts use the two source ambulance voice banks");

        const auto maria_crash = lcs::lcs_ped_voice_range(maria, SaPedSpeechContext::car_crash);
        const auto donald_chat = lcs::lcs_ped_voice_range(donald, SaPedSpeechContext::shocked);
        const auto jd_jacked = lcs::lcs_ped_voice_range(jd, SaPedSpeechContext::jacked_car);
        check(maria_crash && maria_crash->first_sample == SFX_MARIA_CRASH_CAR_1 &&
                  maria_crash->count == 4 && donald_chat &&
                  donald_chat->first_sample == SFX_DONALD_SHOCKED_1 &&
                  jd_jacked && jd_jacked->first_sample == SFX_JD_JACKED_CAR_1 &&
                  jd_jacked->count == 3,
              "exact story roles select their source crash, shocked and jacked-car phrases");
        check(!lcs::lcs_ped_voice_range(maria, SaPedSpeechContext::bump) &&
                  !lcs::lcs_ped_voice_range(maria, SaPedSpeechContext::chat),
              "story roles do not invent source bump or generic-chat recordings");
        const auto maria_death =
            lcs::lcs_ped_voice_range(maria, SaPedSpeechContext::death_high);
        const auto donald_death =
            lcs::lcs_ped_voice_range(donald, SaPedSpeechContext::death_high);
        check(maria_death && maria_death->first_sample == SFX_FEMALE_DEATH_01 &&
                  donald_death && donald_death->first_sample == SFX_MALE_DEATH_01,
              "story character fallback keeps the source gender-specific generic voice");

        cop_voice.actor_seed = 0;
        const auto cop_bank_one =
            lcs::lcs_ped_voice_sample(cop_voice, SaPedSpeechContext::arrest, 0);
        cop_voice.actor_seed = 1;
        const auto cop_bank_two =
            lcs::lcs_ped_voice_sample(cop_voice, SaPedSpeechContext::arrest, 0);
        check(cop_bank_one && *cop_bank_one == SFX_COP1_ARREST_1 && cop_bank_two &&
                  *cop_bank_two == SFX_COP2_ARREST_1,
              "stable native actor seed selects across the six LCS COP banks");
        swat_voice.actor_seed = 1;
        fbi_voice.actor_seed = 1;
        medic_voice.actor_seed = 1;
        check(lcs::lcs_ped_voice_sample(swat_voice, SaPedSpeechContext::rope, 0) ==
                  SFX_SWAT_VOICE_2_ROPE_1 &&
                  lcs::lcs_ped_voice_sample(fbi_voice, SaPedSpeechContext::shoot, 0) ==
                  SFX_FBI_VOICE_2_SHOOT_1 &&
                  lcs::lcs_ped_voice_sample(medic_voice, SaPedSpeechContext::victim, 0) ==
                  SFX_AMBULAN_VOICE_2_VICTIM_1,
              "two-bank SWAT/FBI/medic variation uses each source bank's own stride");

        const auto male_pain = lcs::lcs_ped_voice_range(male, SaPedSpeechContext::pain_high);
        const auto female_pain = lcs::lcs_ped_voice_range(female, SaPedSpeechContext::pain_low);
        const auto male_fire = lcs::lcs_ped_voice_range(male, SaPedSpeechContext::burning);
        const auto female_fire = lcs::lcs_ped_voice_range(female, SaPedSpeechContext::burning);
        check(male_pain && male_pain->first_sample == SFX_MALE_PAIN_01 && male_pain->count == 51,
              "generic male high and low pain use the source pain range");
        check(female_pain && female_pain->first_sample == SFX_FEMALE_PAIN_01 &&
                  female_pain->count == 34,
              "generic female high and low pain use the source pain range");
        check(male_fire && male_fire->first_sample == SFX_MALE_PAIN_ON_FIRE_01 &&
                  male_fire->count == 21,
              "generic male fire uses the source fire range");
        check(female_fire && female_fire->first_sample == SFX_FEMALE_PAIN_ON_FIRE_01 &&
                  female_fire->count == 11,
              "generic female fire uses the source fire range");

        const auto toni_high = lcs::lcs_ped_voice_range(toni, SaPedSpeechContext::pain_high);
        const auto toni_low = lcs::lcs_ped_voice_range(toni, SaPedSpeechContext::pain_low);
        const auto toni_fire = lcs::lcs_ped_voice_range(toni, SaPedSpeechContext::burning);
        check(toni_high && toni_high->first_sample == SFX_TONI_GENERAL_IMP_HI_01 &&
                  toni_high->count == 16,
              "player high pain uses Toni's source impact range");
        check(toni_low && toni_low->first_sample == SFX_TONI_GENERAL_IMP_LOW_01 &&
                  toni_low->count == 15,
              "player low pain uses Toni's source impact range");
        check(toni_fire && toni_fire->first_sample == SFX_TONI_GENERAL_ON_FIRE_01 &&
                  toni_fire->count == 7,
              "player fire uses Toni's source fire range");
        check(!lcs::lcs_ped_voice_range(toni, SaPedSpeechContext::death_high),
              "the source has no player death sample");
        check(lcs::lcs_ped_voice_is_intentionally_silent(toni,
                                                          SaPedSpeechContext::drowning_death) &&
                  lcs::lcs_ped_voice_is_intentionally_silent(toni,
                                                            SaPedSpeechContext::death_high) &&
                  lcs::lcs_ped_voice_is_intentionally_silent(toni,
                                                            SaPedSpeechContext::death_low),
              "Toni death contexts are explicit source silence, not missing custom clips");
        check(!lcs::lcs_ped_voice_is_intentionally_silent(male,
                                                           SaPedSpeechContext::death_high) &&
                  !lcs::lcs_ped_voice_is_intentionally_silent(toni,
                                                              SaPedSpeechContext::pain_high) &&
                  !lcs::lcs_ped_voice_is_intentionally_silent(toni,
                                                              SaPedSpeechContext::sprayed),
              "only Toni death is an intentional silent source mapping");

        const auto last = lcs::lcs_ped_voice_sample(male, SaPedSpeechContext::death_high, 28);
        check(last && *last == SFX_MALE_DEATH_01,
              "phrase variation wraps inside the selected source range");
        check(!lcs::lcs_ped_voice_range(male, SaPedSpeechContext::sprayed),
              "sprayed context 348 does not become a panic line");
        check(lcs::sa_ped_voice_takeover_eligible(52, 13, 0, false, false, true, true) &&
                  lcs::sa_ped_voice_takeover_eligible(52, 28, 0, false, false, true, true) &&
                  lcs::sa_ped_voice_takeover_eligible(52, 177, 0, false, false, true, true) &&
                  lcs::sa_ped_voice_takeover_eligible(52, 181, 0, false, false, true, true) &&
                  !lcs::sa_ped_voice_takeover_eligible(52, 14, 0, false, false, true, true) &&
                  !lcs::sa_ped_voice_takeover_eligible(52, 45, 0, false, false, true, true),
              "only classified native police, medic, crash and story contexts enter mapping");
        // Independent numeric identities from the native enum, rather than
        // merely exercising the same named constants as the implementation.
        check(lcs::lcs_ped_voice_range(toni,static_cast<SaPedSpeechContext>(344))->first_sample==SFX_TONI_GENERAL_IMP_HI_01 &&
              lcs::lcs_ped_voice_range(toni,static_cast<SaPedSpeechContext>(345))->first_sample==SFX_TONI_GENERAL_IMP_LOW_01 &&
              lcs::lcs_ped_voice_range(toni,static_cast<SaPedSpeechContext>(346))->first_sample==SFX_TONI_GENERAL_ON_FIRE_01 &&
              lcs::lcs_ped_voice_range(male,static_cast<SaPedSpeechContext>(347))->first_sample==SFX_MALE_PANIC_01 &&
              lcs::lcs_ped_voice_range(female,static_cast<SaPedSpeechContext>(347))->first_sample==SFX_FEMALE_PANIC_01,
              "native pain, fire and panic context numbers must select the correct source family");

        check(lcs::sa_ped_voice_takeover_eligible(52, 342, 0, false, false, true, true),
              "accepted immediate ped Say event is eligible");
        check(!lcs::sa_ped_voice_takeover_eligible(51, 342, 0, false, false, true, true),
              "other audio events are not intercepted");
        check(!lcs::sa_ped_voice_takeover_eligible(52, 348, 0, false, false, true, true),
              "sprayed context is preserved");
        check(!lcs::sa_ped_voice_takeover_eligible(52, 342, 1, false, false, true, true),
              "delayed native speech is preserved");
        check(!lcs::sa_ped_voice_takeover_eligible(52, 342, 0, true, false, true, true),
              "frontend speech is preserved");
        check(!lcs::sa_ped_voice_takeover_eligible(52, 342, 0, false, true, true, true),
              "forced-audible speech is preserved");
        check(!lcs::sa_ped_voice_takeover_eligible(52, 342, 0, false, false, false, true),
              "a refused native event is never replaced");
        check(!lcs::sa_ped_voice_takeover_eligible(52, 342, 0, false, false, true, false),
              "a missing custom sample retains SA playback");

        std::cout << "PASS: ped voice source ranges, player/cops fallback and takeover gates\n";
        if (argc == 2) decode_source_voice_ranges(argv[1]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
