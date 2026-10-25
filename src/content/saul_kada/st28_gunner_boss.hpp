// The fire ruin's gunner boss (ST28 sub 0's boss room; pool 0x20 type 0x51 sub 0, exe 0x4daf40), see
// content/saul_kada/st28_gunner_boss.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"

namespace od::port {

// The boss's parameter its helpers watch: its beam caught the player
struct GunnerBossParams {
    explicit GunnerBossParams(const AnimatedActor& actor) : GunnerBossParams(const_cast<AnimatedActor&>(actor)) {}
    explicit GunnerBossParams(AnimatedActor& actor) : caught(actor.params[0]) {}
    uint8_t& caught;
};

// exe 0x4daf40: advances, retreats home, shot volleys, a spray, the beam, bombs; its two helpers; the boss gauge
void gunnerBossUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
