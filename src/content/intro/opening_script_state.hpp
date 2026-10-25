// The parts of the script / stage block that opening_script.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::opening_script_state {

// The script's own bytes: the choice taken, the engine's hum playing
struct OpeningScriptBytes : ScriptPart<OpeningScriptBytes> {
    uint8_t choice = 0; // +0xc
    uint8_t engineHum = 0; // +0xd
};

} // namespace od::port::opening_script_state
