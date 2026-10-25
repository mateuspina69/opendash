// The static classes of Nino island's fields (ST15 / ST17 / ST19), content/nino/st15_props.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x58ef40 (ST15 static): a still model (variant 0)
void st15StillModelUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5a2620 (ST15 static): the patrol drone circling the point 0x8cebc0 (crashes once flag 0x690 is set)
void patrolDroneUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5a2060 (ST19 static): the drone of the ST19 cutscenes (variant 0 climbs, 1 follows the script's steps)
void st19CutsceneDroneUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5a3910 (ST17 static): the hovering machine with 3 hit capsules: burns, rocks on strong hits, explodes
void hoverMachineUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5af870 (ST19 static): a solid model (state 0) / a hittable target that jolts back (states 2 / 3)
void st19TargetUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
