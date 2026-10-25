// The parts of the script / stage block that fire_ruin_visit_scripts.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::fire_ruin_visit_scripts_state {

// The scripts' own bytes: script 0x2e's talk ticks
struct VisitScriptFields : ScriptPart<VisitScriptFields> {
    int32_t talkTicks = 0; // +0xc
};

} // namespace od::port::fire_ruin_visit_scripts_state
