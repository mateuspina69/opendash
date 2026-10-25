// The Birdbots of the scripted scenes in Glyde's base: class exe 0x497590 (pool 0x20 type 0x2d sub 2 of ST30) and class
// exe 0x4971c0 (the same type of ST31: scripts 0x23 / 0x24 in the cell of Shu's brother),
// content/calbania/glyde_base_scene_birdbots.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x497590: a scene Birdbot of ST30 -- by variant, cued animations with blinking parts on the script's clock, a
// cued walk-in, or (while the script runs) one shaken and sinking with smoke
void glydeBaseSceneBirdbotUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4971c0: a scene Birdbot of ST31 (scripts 0x23 / 0x24) -- by variant, the script's cued animations
void cellSceneBirdbotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
