// The ST27 lava valve (static type 0x3b, exe 0x5af970), content/saul_kada/st27_lava_valve.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"

namespace od::port {

// The valve's script cues (its own byte +0xca, the scripts set them): kValveShake shakes it once
struct LavaValveCues {
    explicit LavaValveCues(const StaticActor& actor) : LavaValveCues(const_cast<StaticActor&>(actor)) {}
    explicit LavaValveCues(StaticActor& actor) : cues(actor.gunshipCooldown()) {}
    uint8_t& cues;
};
constexpr uint8_t kValveShake = 0x01;

// exe 0x5af970 (static ST27 type 0x3b): the lava valve of scripts 0x40 / 0x41 (regenerating hit points, shakes, sinks)
void valveUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
