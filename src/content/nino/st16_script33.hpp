// ST16's script 0x33 (the player caught by the box carrier), content/nino/st16_script33.cpp.
#pragma once

#include <cstddef>
#include <functional>
#include "game/actor_state.hpp"
#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// Script 0x33, dispatched by the handler [0x89b6d0 + id * 4]
bool st16Script33Ported(uint8_t id);
bool st16Script33Tick(const PlayerEnv& env, uint8_t id);

// A session (ADR: a hold is a move, not a scene): the world finds who the carrier holds and tells their machine, where script 0x33
// runs for them alone. The hook: the player, the carrier's slot, its grip pose, whether it holds them
void st16SetHoldHook(std::function<void(size_t, uint8_t, uint8_t, bool)> hook);
void st16WorldCatch();
void st16WorldCatchTick();
// ... on their machine: the carrier as the world said, the script on its first word
void st16ClientCatch(ActorHeader& carrier, uint8_t gripPose, bool holds);

} // namespace od::port
