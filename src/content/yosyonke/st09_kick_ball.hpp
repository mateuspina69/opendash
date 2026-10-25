// ST09's kick ball, static class 0x5b4a50 (content/yosyonke/st09_kick_ball.cpp).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5b4a50 (ST09 static): the kick ball of the goal game (kicks cost zenny, a goal pays them back + a message)
void kickBallUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
