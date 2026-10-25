// The lava boss's lobbed shell (ST28 pool 0x40 type 0x1c, exe 0x5ca770), see content/saul_kada/st28_boss_shell.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"

namespace od::port {

// exe 0x5ca900(boss, near): a shell lobbed from the boss at the player (near: the lower, slower lob)
void lobBossShell(const AnimatedActor& boss, bool nearShot);
// exe 0x5ca770: ballistic, its pitch along its path; its first contact -> a blast
void bossShellUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
