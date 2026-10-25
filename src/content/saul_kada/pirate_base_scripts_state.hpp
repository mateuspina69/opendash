// The parts of the script / stage block that pirate_base_scripts.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::pirate_base_scripts_state {

// The scripts' own byte: the opening's end (+0xd; the victory's keeps it in the cast's start, ScriptCastStart +0xc)
struct BaseScriptFields : ScriptPart<BaseScriptFields> {
    uint8_t opened = 0; // +0xd
};

} // namespace od::port::pirate_base_scripts_state
