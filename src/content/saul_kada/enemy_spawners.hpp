// Saul Kada's enemy spawners (pool 0xe0 type 0xa): ST26 exe 0x420ac0, ST27 exe 0x420e80, ST28 exe 0x421030, ST2B exe
// 0x421070, ST34 exe 0x421210, ST3B exe 0x421e40: the controllers that spawn the stage's enemies through the encounter
// slots (game/encounter) or directly, and the story flags they keep. See content/saul_kada/enemy_spawners.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x420ac0: ST26: encounter groups by variant (flags 0x3ed / 0x56), the giant at the window
void spawnerSt26Update(const PlayerEnv& env, uint32_t actor);
// exe 0x420e80: ST27: encounter groups by variant (flags 0x43..0x45 / 0x56)
void spawnerSt27Update(const PlayerEnv& env, uint32_t actor);
// exe 0x421030: ST28: encounter group 0
void spawnerSt28Update(const PlayerEnv& env, uint32_t actor);
// exe 0x421070: ST2B: the wall at grid cell (0x48, 0x40) broken -> flag 0x58a, three blasts, group 1
void spawnerSt2bUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x421210: ST34: encounter groups by variant (flags 0x45 / 0x56)
void spawnerSt34Update(const PlayerEnv& env, uint32_t actor);
// exe 0x421e40: ST3B: the flame walls' timer / a breakable floor block (grid segment 9)
void spawnerSt3bUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
