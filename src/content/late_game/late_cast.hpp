// Late-game cutscene cast of ST4B (script 0x59) and ST54 (script 0x6f), content/late_game/late_cast.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// ---- ST4B (pool 0x20, stage table 0x90b0fc; records in script 0x59's camera sequence 0x8aa6e0) ------------------------
// exe 0x45c570 (type 9; records 0x8aa598 v0 / 0x8aa618 v1): cue-timed animations + face rows 0x4fb5d0
void script59CuedTalkerAUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4660d0 (type 0xb; record 0x8aa654): idle, animation stepping from tick 0x53b (run state = 0x4daf20)
void script59LateStepperUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x49ba80 (type 0x2f; record 0x8aa5d8): animations / face rows by the scene and the tick
void script59SceneTalkerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4bc280 (type 0x3f sub 0; records 0x8aa67c v0 / 0x8aa668 v1): flies in along a waypoint table 0x894910, lands
void script59FlyerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4dadc0 (type 0x50; records 0x8aa5ac v0 / 0x8aa62c v1): idle (v0) / animation stepping from tick 0x53b (v1)
void script59IdleOrStepperUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4efe50 (type 0x68; record 0x8aa5ec): animations by scene / tick (its sub-state)
void script59SceneWalkerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4f43f0 (type 0x72; records 0x8aa5c0 v0 / 0x8aa640 v1): cue-timed animations + face rows 0x4fb5d0
void script59CuedTalkerBUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4fb3e0 (type 0x75; record 0x8aa4f0 v0): animation stepping from tick 0x294 + face rows 0x4fb5d0
void script59LateTalkerUpdate(const PlayerEnv& env, uint32_t actor);

// ---- ST54 (pool 0x20, stage table 0x914e0c; records in script 0x6f's camera sequence 0x8ade10) -----------------------
// The script drives each through its sub-state (the action) / step (its phase); animations + texture rows by the
// cast timeline's tick.
void script6fActionTalkerUpdate(const PlayerEnv& env, uint32_t actor); // type 2, record 0x8addac (slot 1)
void script6fHoldingTalkerUpdate(const PlayerEnv& env, uint32_t actor); // type 9, record 0x8add98 (slot 0)
void script6fPacerUpdate(const PlayerEnv& env, uint32_t actor); // type 0xb, 0x8add70 / 0x8addd4
void script6fRowTalkerUpdate(const PlayerEnv& env, uint32_t actor); // type 0x16, record 0x8add5c (slot 3)
void script6fMouthTalkerUpdate(const PlayerEnv& env, uint32_t actor); // type 0x1a, record 0x8add48 (slot 2)
void script6fCirclerUpdate(const PlayerEnv& env, uint32_t actor); // type 0x63, record 0x8addc0 (slot 2)
void script6fSparkTalkerUpdate(const PlayerEnv& env, uint32_t actor); // type 0x72, record 0x8add34 (slot 1)

} // namespace od::port
