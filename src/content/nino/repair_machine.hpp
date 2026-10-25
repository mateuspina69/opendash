// ST18 (Nino island) at stories 9 / 0xa, Glyde's last attack: the machine Roll repairs, Roll at it, the machine's
// parts and the props riding them, content/nino/repair_machine.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4e41c0 (pool 0x20 type 0x5e sub 0): the machine Roll repairs -- a hit capsule and two hit entries, the HUD
// gauge, smoke columns by its damage; destroyed -> flag 0x2f. Spawns Roll and the repair gauge
void repairMachineUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4e67c0 (pool 0x20 type 0x60 sub 0): Roll at the machine -- her work animations by the machine's hit state,
// startled when it is hit, thrown up when it is destroyed
void repairRollUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4e4930 (pool 0x20 type 0x5e sub 1, ST18 sub 2): the machine's other parts (variant 1 sinks with a static type
// 0x3d riding it); each spawns a prop riding it
void machinePartUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x44a700 (pool 0x20 type 2 sub 0, from 0x4e4930): the prop riding a machine part -- script-timed animations and
// texture cells
void machinePartPropUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
