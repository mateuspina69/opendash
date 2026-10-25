// The parts of the script / stage block that pokte_rooms.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/stage_state.hpp"

namespace od::port::pokte_rooms_state {

// The village's state in the stage's block: the cost of the repairs left
struct PokteVillageState : StagePart<PokteVillageState> {
    int32_t repairCost = 0; // +0x20
};

} // namespace od::port::pokte_rooms_state
