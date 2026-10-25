// ST4C (Elysium, the boss room of exe 0x507280; room handler sub 4 0x58b480 in content/late_game/st4b_st4c_rooms): the
// boss intro / outro scripts 0x6b (0x53a4a0) and 0x6c (0x53aa60), their cutscene casts (pool 0x20 type 0x7d subs 1 /
// 2) and the ST4C classes those casts spawn (statics 0x5f / 0x63 / 0x65, effects 0x22 / 0x23). Names are hypotheses
// from the observed behaviour.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// Scripts 0x6b (0x53a4a0) / 0x6c (0x53aa60), dispatched by [0x89b6d0 + id * 4]
bool st4cScriptPorted(uint8_t id);
bool st4cScriptTick(const PlayerEnv& env, uint8_t id);

// exe 0x5086c0: pool 0x20 type 0x7d sub 1 (record 0x8ad0b0, camera op 0x40 of script 0x6b): the intro cast, variant
// 0x508800 (anims 0x20..0x25 / texture tracks on the script's steps 1..4; a teleport ring 0x43adb0 v2 at spawn)
void elysiumIntroCastUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x508cd0: pool 0x20 type 0x7d sub 2 (record 0x8ad360, step 0 of script 0x6c): the defeated boss of the outro,
// variant 0x508dd0 (sparks 0x22, trails 0x5f, bursts 0x63, motes 0x65, lightning bolts e0 0xf, a blast, the bones'
// fade-out)
void elysiumDefeatCastUpdate(const PlayerEnv& env, uint32_t actor);
// ST4C statics (pool 0x60 stage table): exe 0x5bda80 (type 0x5f: a 16-point trail behind a bone of a pool-0x20 actor;
// its ribbon 0x5bdd00), 0x5bf8a0 (type 0x63: a burst of 0x20 LINE_G2 rays, dc::otLineG2), 0x5c0d20 (type 0x65:
// swirling motes, draw commands)
void boneTrailUpdate(const PlayerEnv& env, uint32_t actor);
void rayBurstUpdate(const PlayerEnv& env, uint32_t actor);
void swirlingMotesUpdate(const PlayerEnv& env, uint32_t actor);
// ST4C effects (pool 0xa0 stage table): exe 0x43fc70 (type 0x22: an animated sprite following its pool-0x20 parent)
// and 0x43fe30 (type 0x23: the same, still), billboard draw commands
void animatedSpriteEffectUpdate(const PlayerEnv& env, uint32_t actor);
void stillSpriteEffectUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x43fc90 / 0x43fe50 themselves (also variants 8 / 9 of the ending effects 0x43d030, table 0x88d970)
void animatedFollowerSpriteUpdate(const PlayerEnv& env, uint32_t actor);
void stillFollowerSpriteUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
