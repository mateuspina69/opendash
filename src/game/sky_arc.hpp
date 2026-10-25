// The sky arc (pool 0xe0 controller, exe 0x4174f0; the stage class type 2 of ST00 and ST20): a fixed point of the sky
// projected through the camera with the class's own focal and drawn as a screen-space arc of 24 segments, a
// subtractive pass of 3 bands then an additive pass of 5 bands over the segments the first kept, coloured by segment
// (a rainbow-like arc; hypothesis). A class of two areas (the start's ST00, Calbania's ST20), registered by game/actors.
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x4174f0: the sky arc's update (its states: exe table 0x889f80)
void skyArcUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
