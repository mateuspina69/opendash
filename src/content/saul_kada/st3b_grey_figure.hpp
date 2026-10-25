// The ST3B grey figure (pool 0x20 type 0xd sub 1, exe 0x46b990), content/saul_kada/st3b_grey_figure.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x46b990 (ST3B type 0xd sub 1; the records 0x8ab648.. of content/saul_kada/pirate_base_scripts): a still model with
// its bones scaled 1.0 (exe 0x5e3220) and one part hidden, hit points 1
void greyFigureUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
