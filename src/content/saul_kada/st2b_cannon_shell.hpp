// The cannon shell of the mine boss (ST2B pool 0x40 type 8, exe 0x5c4d60), see content/saul_kada/st2b_cannon_shell.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

constexpr uint8_t kCannonShellType = 8;
// its variants (its gravity by variant): an aimed lob (warns once near its target), a lob, a flat shot, a high lob
// (sparks while falling, no trail)
enum class CannonShellVariant : uint8_t { Warning = 0, Aimed = 1, Flat = 2, High = 3 };

// exe 0x5c4d60: flies under its gravity (a trail behind it), bursts on a wall (a blast and two shockwaves)
void cannonShellUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
