// The guardian of the Manda ruin (ST14 sub 4, story 4) and its cast. See content/manda/manda_boss.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x48fb90 (pool 0x20, the ST14 stage table type 0x2b, spawned by the fight's controller): hops between four
// perches toward the player's quadrant; debris / falling shots / ground shock / beam attacks; its death sets flag
// 0x5b5 (-> script 0xe)
void st14GuardianUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4b83e0 (pool 0x20, ST14 type 0x3e, spawned by the controller): the floating target of the beam
void st14FloaterUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4886d0 (pool 0x20, ST14 type 0x24 sub 0): script 0x1a's rising guardian puppet
void st14RisingPuppetUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x492050 (pool 0x20, ST14 type 0x2b sub 1): script 0x1a's guardian that drops in
void st14DroppingPuppetUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x423170 (pool 0xe0, ST14 type 0xc): the fight's controller (spawns the guardian, the floater, the rollers)
void st14ControllerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5aad40 (static, ST14 type 0x2c): the guardian's beam (the head variant 0, 16 segments)
void st14BeamUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5c7020 (pool 0x40, ST14 type 0x12): the guardian's homing debris
void st14DebrisUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
