#pragma once
#include <cstdint>
#include <optional>
namespace lcs {
// SA frontend events (eAudioEvents AE_FRONTEND_*, CAudioEngine::ReportFrontendAudioEvent) mapped to the
// sound cAudioManager::ProcessFrontEnd plays for the equivalent LCS event.
namespace sa_frontend {
enum : int {
    start=0,select=1,back=2,highlight=3,error=4,noise_test=5,pickup_weapon=6,pickup_money=7,pickup_health=8,pickup_adrenaline=9,
    pickup_body_armour=10,pickup_info=11,pickup_drugs=12,pickup_collectable=13,car_no_cash=14,car_is_hot=15,car_respray=16,
    car_already_rigged=17,fit_bomb_timed=18,fit_bomb_booby_trapped=19,fit_bomb_remote=20,bullet_pass_left_rear=27,
    bullet_pass_left_front=28,bullet_pass_right_rear=29,bullet_pass_right_front=30,display_info=32,timer_count=33,
    radio_click_on=37,radio_click_off=38,purchase_weapon=40,fire_fail_sniper=41,fire_fail_rocket=42,part_mission_complete=43,
    race_321=44,race_go=45,count=52
};
}
enum class InterfaceClip : uint8_t {
    mono,          // one sample, centred
    pair,          // LEFT at pan 0 plus RIGHT at pan 127
    frontend_mix,  // AudioArchive::frontend_effect(a): LEFT/RIGHT/CENTRE mix already used by the script frontend sounds
    random_mono    // sample a + random % b
};
enum class InterfaceLimit : uint8_t {none,pickup,mission};   // ProcessFrontEnd: 5 audio frames between pickups / mission jingles
struct InterfaceSound {
    const char* name;
    InterfaceClip clip;
    uint32_t a,b;
    int32_t frequency;   // 0: the sample's own rate
    InterfaceLimit limit;
    uint8_t pan;         // single-sample clips only; 63 is centred
};
std::optional<InterfaceSound> interface_sound_for_event(int sa_event);
inline constexpr uint8_t interface_volume=127;   // FRONTEND_VOLUME
}
