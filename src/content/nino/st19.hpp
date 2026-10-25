// ST19 actor classes, content/nino/st19.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4e7d20 (pool 0x20: ST19): the scripted machinery (drops / slides / rises on script cues, opens with bursts)
void st19MachineUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
