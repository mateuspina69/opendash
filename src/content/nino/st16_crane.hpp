// The ST16 mission's crane (pool 0x20 class 0x4c7400, type 0x44): parked out of sight until its controller (pool 0xe0,
// slot in its parameters) calls it, it flies in carrying a new box (stage static type 0x25) hung from its bone 3, lets
// it go at x 0x610, registers the box in the controller's list, turns round and flies back.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port {

struct PlayerEnv;

constexpr uint8_t kCraneType = 0x44; // its animated type in ST16's table
// The crane's parameters: its controller's call (bring a box) and the controller's slot (pool 0xe0: the ST16 box
// spawner, content/nino/field_spawners)
struct CraneParams {
    explicit CraneParams(const AnimatedActor& actor) : CraneParams(const_cast<AnimatedActor&>(actor)) {}
    explicit CraneParams(AnimatedActor& actor) : called(actor.params[0]), controller(actor.params[1]) {}
    uint8_t& called;
    uint8_t& controller;
};

// exe 0x4c7400: the crane's update (ActorUpdateFn)
void st16CraneUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
