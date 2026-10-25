// OpenDash (modern gameplay, not the exe): moving never cancels the buster's shot.
//
// The exe cancels the arm whenever Megaman starts to move, turns his run or stops (exe 0x5721e0: the arm flags cleared,
// incl. the "may fire" bits, and the upper body snapped to the body's animation; exe 0x57a150, the back run, clears it
// too) -- the animation cancel players use to fire faster by tapping a direction. "May fire" only comes back (exe
// 0x572230) once the run's start / stop animation ends, so a held fire button stopped shooting on every direction
// change. BUSTER ONLY (the Fire button, the buster's arm kArmBuster): with it held or its arm up none of those cancels
// runs, and every tick in a movement sub-state the buster is raised again while held. The special weapons (the Special
// button, kArmSpecial: charge, fire on release, stop to use ...) keep the exe's behaviour untouched.
#pragma once

#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"

namespace od::port {

// the buster is in use: its arm up, or its button held with no special weapon in use (its arm up, its button held, a
// queued special shot / refire, a carry). Not the kick: the town's buster (weapon mode town / special, its kick
// animation) cancels the walk as in the exe.
inline bool busterFiring(const Player& p) {
    if (p.activeBuster == kTownBuster) return false;
    const uint16_t f = p.armFlags;
    if (f & kArmBuster) return true;
    if ((f & (kArmSpecialRefire | kArmSpecialQueued | kArmSpecial)) || p.holds(PlayerButton::Special) ||
        p.carryFlags != 0)
        return false;
    return p.holds(PlayerButton::Fire);
}
// the buster's button held with the arm down
inline bool fireHeld(const Player& p) { return !(p.armFlags & kArmUp) && busterFiring(p); }

// exe 0x5721e0 on a run start / turn / stop: skipped while the buster fires
inline void cancelArmUnlessFiring(Player& p) {
    if (busterFiring(p)) return;
    putArmDown(p);
}

// exe 0x57a150 (the back run's start) puts the arm off: skipped while the buster fires
inline void clearArmForBackRun(Player& p) {
    if (busterFiring(p)) return;
    p.armFlags &= kArmOffMask;
}

// exe 0x5794c0 skips its input during the run's stop animation and the back run re-arms only on a fresh press: the
// buster raised again there while held
inline void rearmWhileFiring(Player& p) {
    if (fireHeld(p)) fireButtons(p, FireContext::Moving);
}

// After the sub-state handlers, every tick: on the ground in a movement sub-state (idle, walk, run, quick turn, side
// step) with the buster held and the arm down, firing enabled again (exe 0x572230) and the arm raised (exe 0x57ac60,
// which also takes the queued refire)
inline void keepFiringWhileMoving(Player& p) {
    const PlayerSub sub = p.sub();
    if (p.playerState() != PlayerState::Playing ||
        !(sub == PlayerSub::Idle || sub == PlayerSub::Walk || sub == PlayerSub::Run || sub == PlayerSub::QuickTurn ||
          sub == PlayerSub::SideStep))
        return;
    if (p.airborne != 0 || p.charge != 0 || !fireHeld(p)) return;
    enableFiring(p);
    fireButtons(p, sub == PlayerSub::Run ? FireContext::Moving : FireContext::Standing);
}

} // namespace od::port
