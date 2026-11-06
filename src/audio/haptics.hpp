// OpenDash's DualSense haptics: which of the game's sounds also play on the controller's two actuators (its USB
// audio device's channels 3 / 4), and how strongly. Only the common bank's ids (the player, the weapons, the doors,
// the blasts and hits): they name the same sound in every stage, a stage bank's ids do not.
#pragma once

#include <cstdint>

namespace od::audio {

struct HapticFeel {
    float weight = 0.0f; // on the actuators (0: not felt)
    bool heavy = false;  // a thump (the low carrier) rather than a tap (the high one)
};
HapticFeel hapticFeel(uint16_t id);

} // namespace od::audio
