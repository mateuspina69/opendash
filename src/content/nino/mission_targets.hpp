// The things the missions defend (ST15 / ST16 static type 0x2d, exe 0x5ab8b0), content/nino/mission_targets.cpp. Its
// smoke states also run the lava plume 0x5abc30 (declared with content/saul_kada/lava_vents.hpp).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5ab8b0: a mission target (the bombs' / the tanks' targets): hit by the kinds its row lists, it loses hit points
// (a quarter left: its damaged model, its flag and message; none left: its destroyed flag and 0x6f4)
void missionTargetUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
