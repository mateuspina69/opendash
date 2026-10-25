// ST5C (the stage of scripts 0x75 / 0x6d, content/late_game/end_scripts): the pool-0x20 type 0x83 classes (sub 0 the
// boss exe 0x50b9c0 of the stage record 0x8cd8c0, sub 1 the intro cast exe 0x50d4a0 of script 0x75, sub 2 the boss's
// after-images exe 0x50d3d0, sub 3 the defeat cast exe 0x50dae0 of script 0x6d), the spark bursts (static type 0x64,
// exe 0x5bfcf0) and the dust effect (pool 0xa0 type 0x24, exe 0x43ff80; variant 2: script 0x6d's dust puffs). See
// content/late_game/st5c.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

// the stage's classes the end scripts spawn too: the spark bursts (static type 0x64) and the dust effect (pool 0xa0
// type 0x24)
constexpr uint8_t kSparkBurstType = 0x64, kStageDustEffectType = 0x24;
// The burst's spawn bytes: its ring phase (& 0x3f: the next spark's angle), its speed (s8), its mask, its life
struct SparkBurstParams {
    explicit SparkBurstParams(const StaticActor& actor) : SparkBurstParams(const_cast<StaticActor&>(actor)) {}
    explicit SparkBurstParams(StaticActor& actor) : phase(actor.params[0]), speed(actor.params[1]), mask(actor.params[2]), life(actor.params[3]) {}
    uint8_t& phase;
    FieldAlias<int8_t, uint8_t> speed;
    uint8_t& mask;
    uint8_t& life;
};

void st5cBossUpdate(const PlayerEnv& env, uint32_t actor);        // exe 0x50b9c0
void bossIntroCastUpdate(const PlayerEnv& env, uint32_t actor);   // exe 0x50d4a0
void bossAfterImageUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x50d3d0
void bossDefeatCastUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x50dae0
void sparkBurstUpdate(const PlayerEnv& env, uint32_t actor);      // exe 0x5bfcf0
void stageDustEffectUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x43ff80
void driftingPuffsUpdate(const PlayerEnv& env, uint32_t actor);   // exe 0x440940 (also exe 0x43d030 variant 12)

} // namespace od::port
