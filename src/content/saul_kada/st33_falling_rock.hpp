// The falling rocks of the ST33 guardian boss (static ST33 type 0x3b sub 1, exe 0x5afde0), see
// content/saul_kada/st33_falling_rock.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5afde0: a falling rock of the guardian boss's stomp rain (kind 1) or quake (kind 0), or one of the 4 fragments
// (kind 2) a quake rock shatters into on landing / when hit (sound 0x14a)
void fallingRockUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
