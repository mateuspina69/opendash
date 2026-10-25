// The fire ruin's lava boss (ST28 sub 0's boss room; pool 0x20 type 0x48 sub 0, exe 0x4cd440), see
// content/saul_kada/st28_lava_boss.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"

namespace od::port {

constexpr uint8_t kLavaBossType = 0x48;
// the boss's words others read / write: the bomb it has out (its bombs clear it when they die: it may throw again)
// and whether it stands in the arena's box (its room handler reads it)
struct LavaBossBombOut {
    explicit LavaBossBombOut(const AnimatedActor& actor) : LavaBossBombOut(const_cast<AnimatedActor&>(actor)) {}
    explicit LavaBossBombOut(AnimatedActor& actor) : bombOut(actor.rotation.spare), inBox(actor.velocity.spare) {}
    int16_t& bombOut; // the rotation's spare lane
    int16_t& inBox; // the velocity's spare lane
};

// exe 0x4cd440: walks / charges and stomps / throws fire bombs / lobs shells; liftable and throwable (lava cells sink
// it), the boss gauge; beaten -> story flags 0x55 / 0x53
void lavaBossUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
