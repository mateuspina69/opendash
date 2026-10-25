// End-game cutscene cast of ST52 (script 0x3f 0x52bc40, content/late_game/end_scripts) and ST5C (script 0x75,
// content/late_game/end_scripts), the ST52 / ST1F bobbing prop (exe 0x50b750) and their effects,
// content/late_game/end_cast.cpp. Shadows (exe 0x5f20f0 / 0x5f2220) are render output and left out. Names are
// hypotheses from the observed behaviour.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// ---- the pool-0x20 cast, its variant routine [table + variant * 4](actor, the script block), cued on the step tick ---
// ST52 (script 0x3f): exe 0x44b0b0 (type 2), 0x4787d0 (0x16), 0x47d8c0 (0x1a: a talking mouth), 0x49b4c0 (0x2f),
// 0x4d5230 (0x4c: variants 0 / 1 / 2, variant 2 sparkles (ending effect variant 3) and fades out), 0x4ef2a0 (0x68:
// variants 0 / 1, each with an ending-effect glow disc)
void endingTalkerAUpdate(const PlayerEnv& env, uint32_t actor);     // exe 0x44b0b0
void endingTalkerBUpdate(const PlayerEnv& env, uint32_t actor);     // exe 0x4787d0
void endingMouthTalkerUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x47d8c0
void endingWalkerUpdate(const PlayerEnv& env, uint32_t actor);      // exe 0x49b4c0
void endingGroupCastUpdate(const PlayerEnv& env, uint32_t actor);   // exe 0x4d5230
void endingGlowCastUpdate(const PlayerEnv& env, uint32_t actor);    // exe 0x4ef2a0
// ST5C (script 0x75): state routines [table + state * 4] that follow the script's scene (state k runs while the scene is
// k + 2): exe 0x49c270 (type 0x2f; its state 0 spawns a light pillar), 0x4f0050 (type 0x68)
void finaleCastAUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x49c270
void finaleCastBUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x4f0050

// exe 0x50b750 (pool 0x20 types 0x71 / 0x75, ST52 and the ST1F records of subs 0 / 2 / 4 / 6): by its kind: 0 bobbing
// forever, 1 bobbing until stopped, 2 still (yaw 0xc00, scale 0x80)
void bobbingPropUpdate(const PlayerEnv& env, uint32_t actor);

// exe 0x43d030 (pool 0xa0 type 0x21, variant [0x88d970 + variant * 4]): 0 0x43d050 the player's ring band, 1 0x43d970
// the glow disc + ring under an actor, 2 0x43e260 the swirl cylinder, 3 0x43e730 bone sparkles, 4 0x43ec20 the trail
// (spawns variant 5), 5 0x43ef80 a drifting puff, 6 0x43f260 a tumbling glow fan, 7 0x43f980 a pulsing glow
void endingEffectUpdate(const PlayerEnv& env, uint32_t actor);

// The stage dust effect (exe 0x43ff80, pool 0xa0 type 0x24, content/late_game/st5c) variants 0 / 1: exe 0x43ffa0 the
// light pillar (spawns variant 1), exe 0x440670 a rising streak
void lightPillarUpdate(const PlayerEnv& env, uint32_t actor);
void risingStreaksUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
