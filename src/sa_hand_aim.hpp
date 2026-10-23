#pragma once
#include <string>
namespace lcs {
// Straight hand aim (the former CJHandStraight mod). SA's aim animations leave the hand of one-handed
// guns rolled sideways; for CJ and the NPCs selected below the right hand (and the left one when two
// guns are held) is rebuilt right before CPed::Render so the weapon stands upright while aiming. The game's
// aim does not change: only the roll and, optionally, the forward axis (clamped) are touched.
//
// The CPed::PreRender/Render vtable slots are chained to whatever was installed before (other mods keep working).
// Weapons are matched by type and by animation group, so weapons added by other mods that reuse a listed
// weapon's animation group are covered without configuration.
struct HandAimConfig {
    bool cj=true,npcs=true;
    bool grove=true,ballas=true,vagos=true,sfrifa=true,danang=true,mafia=true,triads=true,aztecas=true;
    bool cops=true,civilians=true,otherPeds=true;
    bool leftHand=true;                 // second hand of dual weapons (TWIN_PISTOL flag)
    bool sameAnimationGroup=true;       // weapons sharing an animation group with a listed weapon
    std::string extraWeaponTypes;       // "40,41": weapon types to correct besides the one-handed guns
    int aimAxis=0;                      // 0 forearm line, 1 hand axis from the aim IK, 2 blend
    int blendPercent=50,maxDeviationDegrees=20;
    int blendInMs=150,blendOutMs=350;
};
void sa_hand_aim_set_logger(void(*callback)(const char*));   // optional diagnostics
// False when the executable is not SA 1.0 US or no ped vtable could be hooked; nothing is changed then.
bool sa_hand_aim_install(const HandAimConfig& config,std::string& diagnostic);
}
