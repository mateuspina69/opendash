// The parts of the script / stage block that town_room_cases.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/stage_state.hpp"

namespace od::port::town_room_cases_state {

// the stage block of ST0B: the time-up flash's ticks
struct LicenceOfficeBlock : StagePart<LicenceOfficeBlock> {
    uint32_t timeUpFlash = 0; // +0x0
};

} // namespace od::port::town_room_cases_state
