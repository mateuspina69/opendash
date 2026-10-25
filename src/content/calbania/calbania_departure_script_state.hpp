// The parts of the script / stage block that calbania_departure_script.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::calbania_departure_script_state {

// the script's own byte: the music was faded by the steps (the wrap-up fades it otherwise)
struct DepartureBytes : ScriptPart<DepartureBytes> {
    uint8_t musicFaded = 0; // +0xc
};

} // namespace od::port::calbania_departure_script_state
