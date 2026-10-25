// The parts of the script / stage block that town_story.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/field_alias.hpp"
#include "game/script_state.hpp"

namespace od::port::town_story_state {

// the chapter scripts' own bytes (beside the head target, ScriptHeadTarget): a countdown that becomes the yaw the player
// turns to (+0x12), the door sounds' timer (+0x16)
struct ChapterScriptBytes : ScriptPart<ChapterScriptBytes> {
    uint16_t counter = 0; // +0x12
    FieldAlias<int16_t, uint16_t> turnYaw{counter};
    uint16_t soundTimer = 0; // +0x16
};

} // namespace od::port::town_story_state
