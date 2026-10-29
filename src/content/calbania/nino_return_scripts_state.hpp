// The scripts' own bytes of nino_return_scripts.cpp (game/script_state ScriptPart); the flying points it spawns keep
// content/nino/fly_point's FlyPointFields.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::nino_return_scripts_state {

// the script's own bytes: the ripples' interval, the countdown to the next and the next one's size
struct AttackEndBytes : ScriptPart<AttackEndBytes> {
    int16_t rippleInterval = 0; // +0xc
    uint16_t rippleCountdown = 0; // +0x10
    int8_t rippleSize = 0; // +0x12
};

// the script's own byte: the cast started (the voice plays)
struct RollToMachineBytes : ScriptPart<RollToMachineBytes> {
    uint8_t castStarted = 0; // +0xc
};

} // namespace od::port::nino_return_scripts_state
