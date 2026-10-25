// ST59 / ST5A / ST5B: the time-trial course minigame (three courses, index = stage - 0x59 in the race block),
// content/late_game/st59.cpp. The room-script stage case 0x58ca30 (the room handler's step byte) and the pool-e0 type
// 0x29 class 0x427b90 that runs it (kinds: 0 the race controller, 1 the speed gauge, 2 the "3, 2, 1, GO" countdown, 3
// the floor-cell watcher).
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor_movement.hpp"
#include "game/hud.hpp"

namespace od::port {

// the inline body of the room scripts' ST59 / 5A / 5B stage case 0x58ca30 (called every tick)
void st59StageCase();
// exe 0x427b90 (pool e0 type 0x29): the race's actors
void raceActorUpdate(const PlayerEnv& env, uint32_t actor);
// the HUD packets of the class (exe 0x4288b0 the speed gauge, exe 0x428c70 the countdown digit: SPRTs of tpage 0x36,
// CLUTs 0x7dc8 / 0x7d48 / the digit's 0x7dc0 / 0x7d80) in drawing order, from the state the last tick left
std::vector<hud::Prim> st59HudDraw();

} // namespace od::port
