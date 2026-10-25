// The scripts of chapter 3 (content/calbania/ch3_story dispatches them by their handlers): one entry per script, each
// in the file of its subject, and the player's set-up steps they share. Internal to content/calbania.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/address_forms.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"

namespace od::port {

// ---- the scripts (their handlers [0x89b6d0 + id * 4]) ----------------------------------------------------------------
void calbaniaArrivalScript();      // 0x57 (exe 0x533520): content/calbania/calbania_arrival_script
void townFirstVisitScript();       // 0x10 (exe 0x51b090): content/calbania/calbania_town_scripts
void townReturnScript();           // 0x11 (exe 0x51b390)
void squareOpeningScript();        // 0x1d (exe 0x520c70): content/calbania/square_defence_scripts
void squareSecondPhaseScript();    // 0x1e (exe 0x5212a0)
void squareThirdPhaseScript();     // 0x1f (exe 0x521760)
void squareWallLiftScript();       // 0x20 (exe 0x521c60)
void baseEntranceScript();         // 0x21 (exe 0x522100): content/calbania/glyde_base_scripts
void baseCameraScript();           // 0x22 (exe 0x522450)
void brotherHeldScript();          // 0x23 (exe 0x522c70)
void brotherFreedScript();         // 0x24 (exe 0x522f30)
void rescueScript();               // 0x25 (exe 0x5235f0)
void baseExplosionScript();        // 0x26 (exe 0x523e90): content/calbania/base_explosion_script
void calbaniaDepartureScript();    // 0x69 (exe 0x539a60): content/calbania/calbania_departure_script
void ninoReturnScript();           // 0x39 (exe 0x529ac0): content/calbania/nino_return_scripts
void rollToMachineScript();        // 0x3a (exe 0x52a290)
void ninoAttackEndScript();        // 0x3b (exe 0x52ab20)
void islandBlownScript();          // 0x3c (exe 0x52b3b0): content/calbania/island_blown_script

// ---- the player's set-up steps of the scripts -----------------------------------------------------------------------
// the player's environment of the script that runs (bound by ch3ScriptTick)
inline const PlayerEnv& scriptEnv() { return *address_forms::E; }

// exe 0x5723d0(anim, step, 1): the player's scripted animation
inline void playerAnim(uint8_t anim, uint8_t step = 0) { playerScriptAnimId(anim, step, 1); }
// the player's move directions and their angles cleared (a set-up's byte and dword stores)
inline void clearPlayerDirections() {
    Player& p = player();
    p.setPrevMoveDir(MoveDir::Forward);
    p.setMoveDir(MoveDir::Forward);
    p.clearDirectionOffsetWords();
}
// the move directions' angles and the idle faces cleared (a set-up's dword and byte stores)
inline void clearPlayerAnglesAndFaces() {
    Player& p = player();
    p.clearDirectionOffsetWords();
    p.idleFaces[0] = 0;
    p.idleFaces[1] = 0;
}
// the player at a 16.16 point facing yaw (the dword stores of its position)
inline void placePlayer(uint32_t x, uint32_t y, uint32_t z, int16_t yaw) {
    Player& p = player();
    p.pos.x = int32_t(x);
    p.pos.y = int32_t(y);
    p.pos.z = int32_t(z);
    p.rotation.y = yaw;
}
// ... at an integer point (the word stores of its position's integer parts)
inline void placePlayerWhole(int16_t x, int16_t y, int16_t z, int16_t yaw) {
    Player& p = player();
    p.pos.x.whole = x;
    p.pos.y.whole = y;
    p.pos.z.whole = z;
    p.rotation.y = yaw;
}
// the script's face tracks on their tables, both on sequence 0 (exe 0x5e33a0 / 0x5e33d0 / 0x5e33f0)
inline void startFaceTracks(uint32_t eyes, uint32_t mouth) {
    KeyframeTracks& t = scriptState().faceTracks;
    t.init(eyes, mouth);
    t.first.select(0, 0);
    t.second.select(0, 0);
}
// the eyes' / the mouth's sequence of the player's face (exe 0x5e33d0 / 0x5e33f0)
inline void eyesKey(uint8_t seq) { scriptState().faceTracks.first.select(seq, 0); }
inline void mouthKey(uint8_t seq) { scriptState().faceTracks.second.select(seq, 0); }
inline void faceKeys(uint8_t seq) {
    eyesKey(seq);
    mouthKey(seq);
}
// a step routine's move to the next step with its sub-state cleared too (the steps that keep their phase there)
inline void nextStepFromSub() {
    scriptState().subState = 0;
    scriptNextStep();
}

} // namespace od::port
