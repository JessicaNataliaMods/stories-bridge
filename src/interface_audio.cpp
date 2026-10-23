#include "lcs/interface_audio.hpp"
#include "audio_sample_ids.inc"

namespace lcs {
// Source: reLCS AudioLogic.cpp, cAudioManager::ProcessFrontEnd. Frontend_mix ids are those of
// AudioArchive::frontend_effect (13 weapon, 14 weapon at the fixed 5382 Hz, 1 part mission complete,
// 10 go, 16 arm bomb, 70 menu select). Stereo pairs are named LEFT/RIGHT samples, never "+1".
namespace {
static_assert(SFX_MONEY_RIGHT==SFX_MONEY_LEFT+1 && SFX_FE_INFO_RIGHT==SFX_FE_INFO_LEFT+1 &&
              SFX_FE_ERROR_RIGHT==SFX_FE_ERROR_LEFT+1 && SFX_BULLET_PASS_2==SFX_BULLET_PASS_1+1 &&
              SFX_FE_NOISE_BURST_3==SFX_FE_NOISE_BURST_1+2,"Adjacent interface samples");
}
std::optional<InterfaceSound> interface_sound_for_event(int event) {
    using L=InterfaceLimit;using C=InterfaceClip;
    switch(event) {
    case sa_frontend::start:return InterfaceSound{"menu starting",C::pair,SFX_FE_INFO_LEFT,SFX_FE_INFO_RIGHT,0,L::none,63};
    case sa_frontend::select:return InterfaceSound{"enter/adjust",C::mono,SFX_FE_SELECT,0,0,L::none,63};
    case sa_frontend::back:return InterfaceSound{"back",C::mono,SFX_FE_BACK,0,0,L::none,63};
    case sa_frontend::highlight:return InterfaceSound{"highlight",C::mono,SFX_FE_HIGHLIGHT,0,0,L::none,63};
    case sa_frontend::error:return InterfaceSound{"fail",C::pair,SFX_FE_ERROR_LEFT,SFX_FE_ERROR_RIGHT,0,L::none,63};
    case sa_frontend::noise_test:return InterfaceSound{"audio test",C::random_mono,SFX_FE_NOISE_BURST_1,3,0,L::none,63};
    case sa_frontend::pickup_weapon:case sa_frontend::purchase_weapon:
    case sa_frontend::fit_bomb_timed:case sa_frontend::fit_bomb_booby_trapped:case sa_frontend::fit_bomb_remote:
        return InterfaceSound{"weapon pickup / bomb set",C::frontend_mix,13,0,0,L::pickup,63};
    case sa_frontend::pickup_money:case sa_frontend::pickup_health:case sa_frontend::pickup_adrenaline:
    case sa_frontend::pickup_body_armour:case sa_frontend::car_respray:
        return InterfaceSound{"money / health / armour pickup",C::pair,SFX_MONEY_LEFT,SFX_MONEY_RIGHT,0,L::pickup,63};
    case sa_frontend::pickup_info:case sa_frontend::display_info:
        return InterfaceSound{"info",C::pair,SFX_FE_INFO_LEFT,SFX_FE_INFO_RIGHT,0,L::none,63};
    case sa_frontend::pickup_drugs:case sa_frontend::pickup_collectable:case sa_frontend::part_mission_complete:
    case sa_frontend::race_321:
        return InterfaceSound{"part mission complete / collectable",C::frontend_mix,1,0,0,L::mission,63};
    case sa_frontend::race_go:return InterfaceSound{"race go",C::frontend_mix,10,0,0,L::none,63};
    case sa_frontend::car_no_cash:case sa_frontend::car_is_hot:case sa_frontend::car_already_rigged:
        return InterfaceSound{"garage refusal",C::frontend_mix,14,0,0,L::none,63};
    case sa_frontend::timer_count:return InterfaceSound{"clock tick",C::mono,SFX_TIMER,0,0,L::none,63};
    case sa_frontend::radio_click_on:return InterfaceSound{"radio on",C::mono,SFX_RADIO_CLICK,0,32000,L::none,63};
    case sa_frontend::radio_click_off:return InterfaceSound{"radio off",C::mono,SFX_RADIO_CLICK,0,28509,L::none,63};
    case sa_frontend::fire_fail_sniper:return InterfaceSound{"sniper cannot fire",C::mono,SFX_ERROR_FIRE_RIFLE,0,0,L::none,63};
    case sa_frontend::fire_fail_rocket:return InterfaceSound{"rocket cannot fire",C::mono,SFX_ERROR_FIRE_ROCKET_LAUNCHER,0,0,L::none,63};
    case sa_frontend::bullet_pass_left_rear:case sa_frontend::bullet_pass_left_front:
        return InterfaceSound{"bullet pass left",C::random_mono,SFX_BULLET_PASS_1,2,0,L::none,20};
    case sa_frontend::bullet_pass_right_rear:case sa_frontend::bullet_pass_right_front:
        return InterfaceSound{"bullet pass right",C::random_mono,SFX_BULLET_PASS_1,2,0,L::none,107};
    default:return std::nullopt;
    }
}
}
