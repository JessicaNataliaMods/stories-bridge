#include "lcs/weapon_loop_audio.hpp"
#include "lcs/audio.hpp"
#include "lcs/continuous_sound.hpp"
#include "audio_sample_ids.inc"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int peak(const lcs::AudioSample& sample) {
    int result = 0;
    for (size_t at = 0; at + 1 < sample.pcm.size(); at += 2) {
        int16_t value = 0;
        std::memcpy(&value, sample.pcm.data() + at, sizeof(value));
        result = std::max(result, std::abs(int(value)));
    }
    return result;
}
}

// argv[1] is the user's original PS2 AUDIO directory. The test only decodes
// PCM and reads SDT-derived loop metadata; it never opens an audio device.
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected original PS2 AUDIO folder");
        using namespace lcs;

        check(sa_weapon_loop_cue(38, 145) == WeaponLoopCue::minigun_ammo,
              "Vehicle/standard minigun fire must request the firing loop");
        check(sa_weapon_loop_cue(38, 150) == WeaponLoopCue::minigun_ammo,
              "Minigun-ammo event must request the firing loop");
        check(sa_weapon_loop_cue(38, 151) == WeaponLoopCue::minigun_no_ammo,
              "Minigun-no-ammo event must preserve the SA fallback path");
        check(sa_weapon_loop_cue(37, 145) == WeaponLoopCue::flamethrower_fire,
              "Flamethrower fire must request its continuous sample");
        check(!sa_weapon_loop_cue(37, 158),
              "Flamethrower idle is outside the hooked weapon-fire event range");
        check(!sa_weapon_loop_cue(38, 157), "Event 157 is not sent through the hooked WeaponFire path");
        check(!sa_weapon_loop_cue(34, 145) && !sa_weapon_loop_cue(37, 150),
              "Unrelated weapon/event pairs must stay with SA");

        const auto attack = weapon_loop_sample_spec(WeaponLoopCue::minigun_ammo);
        const auto no_ammo = weapon_loop_sample_spec(WeaponLoopCue::minigun_no_ammo);
        const auto stop = weapon_loop_sample_spec(WeaponLoopCue::minigun_stop);
        const auto flame = weapon_loop_sample_spec(WeaponLoopCue::flamethrower_fire);
        check(attack && attack->sample == SFX_MINIGUN_FIRE_LEFT && attack->looping &&
                  attack->volume == 127 && attack->max_distance == 150.0f,
              "Minigun attack source constants changed");
        check(!no_ammo, "No-ammo stays on the native SA sound path");
        check(stop && stop->sample == SFX_MINIGUN_STOP && !stop->looping,
              "Minigun rundown source constant changed");
        check(flame && flame->sample == SFX_FLAMETHROWER_LEFT && flame->looping &&
                  flame->volume == 90 && flame->max_distance == 60.0f &&
                  flame->start_sample == SFX_FLAMETHROWER_START_LEFT && flame->start_volume == 70 &&
                  flame->paired_sample == SFX_FLAMETHROWER_RIGHT && flame->paired_stereo_distance == 0.2f,
              "Flamethrower source constants changed");
        check(weapon_loop_request_ttl_ms(WeaponLoopCue::minigun_ammo) == 150,
              "Minigun request lifetime must match 3 source frames plus scheduler tolerance");
        check(weapon_loop_request_ttl_ms(WeaponLoopCue::flamethrower_fire) == 250,
              "Flamethrower request lifetime must match 6 source frames plus scheduler tolerance");
        check(weapon_loop_request_expired(1149, 1000, 150) == false &&
                  weapon_loop_request_expired(1150, 1000, 150),
              "Minigun request must expire exactly at its TTL boundary");
        check(!weapon_loop_request_expired(99, UINT32_MAX - 99, 200) &&
                  weapon_loop_request_expired(100, UINT32_MAX - 99, 200),
              "Request expiry must handle GetTickCount wraparound");
        check(weapon_loop_request_ttl_ms(WeaponLoopCue::minigun_no_ammo) == 0,
              "Native no-ammo path must not create an LCS request lifetime");

        const AudioArchive archive(AudioArchive::Ps2Audio{}, argv[1]);
        // Diagnose all source records before the assertions, so a first silent
        // source cannot hide the metadata of the remaining records.
        for(const uint32_t id:{SFX_MINIGUN_FIRE_LEFT,SFX_FLAMETHROWER_LEFT,SFX_FLAMETHROWER_RIGHT,SFX_MINIGUN_STOP,SFX_FLAMETHROWER_START_LEFT}) {
            const auto pcm=archive.sample(id);
            std::cout<<"sample="<<id<<" bytes="<<pcm.pcm.size()<<" peak="<<peak(pcm)<<" loop="<<archive.record(id).loop_end<<'\n';
        }
        for (const uint32_t sample_id : {SFX_MINIGUN_FIRE_LEFT, SFX_FLAMETHROWER_LEFT, SFX_FLAMETHROWER_RIGHT}) {
            const auto pcm = archive.sample(sample_id);
            const auto& record = archive.record(sample_id);
            const auto span = continuous_pcm_loop_span(record.size, record.loop_start, record.loop_end);
            check(pcm.channels == 1 && pcm.frequency > 0 && pcm.pcm.size() == record.size,
                  "Continuous weapon sample did not decode as indexed mono PCM");
            check(span && span->begin < span->end && span->end <= pcm.pcm.size(),
                  "Continuous weapon sample lacks valid decoder-derived PS2 loop metadata");
            if(record.loop_end==-1)check(span->begin==0 && span->end==pcm.pcm.size(),
                  "Source infinite-loop requests with sentinel -1 must repeat the whole sample");
            if(sample_id==SFX_FLAMETHROWER_RIGHT) {
                // Retail PS2 keeps a single silent ADPCM frame in this named
                // right-channel slot. Do not invent audible content for it.
                check(pcm.pcm.size()==56 && peak(pcm)==0 && record.loop_start==0 && record.loop_end==-1,
                      "PS2 flame right-channel placeholder changed");
            } else {
                check(peak(pcm) > 1500, ("Continuous weapon sample is silent: index="+std::to_string(sample_id)+", peak="+std::to_string(peak(pcm))).c_str());
            }
        }
        for (const uint32_t sample_id : {SFX_MINIGUN_STOP, SFX_FLAMETHROWER_START_LEFT}) {
            const auto pcm = archive.sample(sample_id);
            check(pcm.channels == 1 && pcm.frequency > 0 && pcm.pcm.size() > 100 && peak(pcm) > 1500,
                  "Weapon transition one-shot did not decode as audible mono PCM");
        }
        std::cout << "PASS: LCS weapon-loop event ownership and PS2 loop metadata verified; gameExecuted=false\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
