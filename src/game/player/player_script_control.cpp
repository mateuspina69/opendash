// The player driven by the scripts (see player_script_control.hpp).
#include "game/player/player_script_control.hpp"

#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/player/player_animation.hpp"
#include "game/player/player_effects.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"

namespace od::port {

void playerToScripted() {
    Player& p = player();
    p.enterState(PlayerState::Scripted);
    p.channelMix = 0;
    p.runFlags |= uint8_t(kActorRun | kActorShown);
    p.lean = 0;
    p.leanTarget = 0;
    clearWeaponState(p);
    enableFiring(p);
    cameraSetFollow(CameraFollow::Rigid);
}

int playerToScriptIfFree() {
    if (player().playerState() == PlayerState::Playing) {
        playerToScripted();
        return 0;
    }
    return -1;
}

int playerToScriptWhenStanding() {
    Player& p = player();
    if (p.playerState() == PlayerState::Playing && p.sub() == PlayerSub::Idle && p.step != 0) {
        playerToScripted();
        return 0;
    }
    if (p.airborne == kAirborneHanging) p.airborne = kAirborneFalling;
    return -1;
}

void playerBackToPlay() { player().enterState(PlayerState::Playing); }

void playerScriptAnim(PlayerAnim anim, uint8_t step, bool force) { playerScriptAnimId(uint8_t(anim), step, force); }

void playerScriptAnimId(uint8_t anim, uint8_t step, bool force) {
    Player& p = player();
    p.scriptAnim = int8_t(anim);
    p.scriptAnimStep = int8_t(step);
    if (force) p.body.prevAnim = uint8_t(PlayerAnim::None);
}

void playerStageAnim(uint8_t n) { playerScriptAnimId(uint8_t(kStageAnims + n), 0, true); }

void scriptedUpdate(Player& p) {
    const bool forced = int8_t(p.body.prevAnim) == -1; // the queued animation forced (its previous one cleared)
    p.savePreviousPose();
    p.body.prevAnim = p.body.anim;
    p.upper.prevAnim = p.upper.anim;
    const int8_t queued = p.scriptAnim;
    if (queued != -1) {
        if (int8_t(p.body.anim) != queued) {
            requestAnimId(p, queued, p.scriptAnimStep, true);
        } else if (forced) { // the same animation forced again: from the queued step
            requestAnimId(p, queued, p.scriptAnimStep, true);
        }
        p.scriptAnim = -1;
    }
    // the key's events: the cue's sound; the steps (0x57b78e..0x57b80c: each step splashes once in water, before its
    // sound)
    const uint8_t events = p.body.key.events;
    if (events & kAnimEventCue) soundPlayAtPlayer(uint16_t(PlayerSound::ScriptCue));
    if (events & kAnimEventStep) {
        if (p.inWater != 0) splash(p); // exe 0x57b990
        soundPlayAtPlayer(uint16_t(PlayerSound::Step));
    }
    if (events & kAnimEventFootfall) {
        if (p.inWater != 0) splash(p); // exe 0x57b990
        soundPlayAtPlayer(uint16_t(PlayerSound::Footstep));
    }
}

} // namespace od::port
