// The licence exam's spawner (ST55, pool 0xe0 type 0xa, exe 0x422d60) and its clock's picture: state 0 initialises,
// state 1 runs while the exam's clock is drawn.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor_movement.hpp"
#include "game/hud.hpp"

namespace od::port {

// exe 0x422d60: ST55 e0 type 0xa: the licence exam timer (picture od::hud::examTimerDraw)
void spawnerSt55Update(const PlayerEnv& env, uint32_t actor);
// exe 0x422d90 (the ST55 spawner's state 1, unless the frame draws nothing): the licence exam's clock
// 0xa6d298 -- 3 plates (0x88bd00; left out on the ticks with (t + 1) % 15 == 0 outside room 0: the blink),
// then minutes / seconds / hundredths, 2 digits each, at the x of 0x88bd30
std::vector<hud::Prim> examTimerDraw();

} // namespace od::port
