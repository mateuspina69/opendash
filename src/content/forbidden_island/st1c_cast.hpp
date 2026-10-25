// The cast of script 0x13 (ST1C, content/forbidden_island/st1c_script13): the pool-0x20 classes of the ST1C type list
// the script spawns (its records 0x8a07a4.. and those of its cast swaps) and the stage's three statics. Each runs, per
// tick, the routine of its variant for the script's current step sub (its cue table, exe data) -- the animation it asks
// at given step ticks, the keys of its texture tracks at their exact ticks, its moves -- then its animation and (most)
// its texture tracks. The classes' model-bank animation indices are keyed by the script's timeline.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// pool 0x20 (the ST1C type list 0x8e56e8..)
void st1cFaceFigureUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x478120 type 0x16: parts toggled, a walk
void st1cMeshFlipFigureUpdate(const PlayerEnv& env, uint32_t actor);   // exe 0x47d160 type 0x1a: a track flips meshes
void st1cCuedFigureUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x4802e0 type 0x1d: animation cues only
void st1cWalkingFigureUpdate(const PlayerEnv& env, uint32_t actor);    // exe 0x480c10 type 0x1e: parts toggled, a walk
void st1cPosingFigureUpdate(const PlayerEnv& env, uint32_t actor);     // exe 0x482880 type 0x21: start keys, held poses
void st1cRisingPropUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x48a910 type 0x27: rises, sparks, bobs
void st1cFidgetingFigureUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x48ba20 type 0x28: a random idle
void st1cHoppingFlyerUpdate(const PlayerEnv& env, uint32_t actor);     // exe 0x49af90 type 0x2f: a hop, a flight
void st1cVanishingWalkerUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x4b5f00 type 0x3c: hides, a walk
void st1cFlyerUpdate(const PlayerEnv& env, uint32_t actor);            // exe 0x4bc020 type 0x3f: the flight
void st1cGroundedHopperUpdate(const PlayerEnv& env, uint32_t actor);   // exe 0x4e2230 type 0x59: on the ground, a hop
void st1cFallingFigureUpdate(const PlayerEnv& env, uint32_t actor);    // exe 0x4e25f0 type 0x5a: a fall to the ground
void st1cGlowingFigureUpdate(const PlayerEnv& env, uint32_t actor);    // exe 0x4e3a80 type 0x5d: a glow at its bone 10
void st1cHopperUpdate(const PlayerEnv& env, uint32_t actor);           // exe 0x4eef30 type 0x68: a hop
void st1cIdleFigureUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x4f1550 type 0x6b: animation 0, track 0
// statics: the stage's ship (exe 0x591270: it bobs, turns, smokes and lights a flare by the script's step) and two
// still models (exe 0x5b25d0 / 0x5b5d70); each publishes its class block's address (exe globals 0xa5a900 / 0xa5a920 /
// 0xa5a924)
void st1cShipUpdate(const PlayerEnv& env, uint32_t actor);
void st1cStillModelUpdate(const PlayerEnv& env, uint32_t actor);
void st1cSecondStillModelUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
