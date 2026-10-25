// The end of the game: the room-script tick's inline stage case ST5C (0x58cb10) and the cutscene scripts 0x75
// (0x53e880), 0x6d (0x53aec0), 0x3f (0x52bc40, ST52), 0x6f (0x53c3f0, ST54) and 0x6e (0x53bb60, ST57: the staff roll),
// content/late_game/end_scripts.cpp.
#pragma once

#include <cstdint>
#include <functional>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// the ST5C case body (0x58cb10, no handler table; state byte 0xa714e0), called each tick
void st5cStageCase();
// the scripts (block 0xa725a0): handler = [0x89b6d0 + id * 4]
bool endScriptPorted(RoutineId handler);
bool endScriptTick(const PlayerEnv& env, RoutineId handler);

// Host services of the staff roll (script 0x6e): dat\staff.dat read whole into a heap buffer (0x40b410 / 0x40b540 /
// 0x611465 / 0x40b4e0 / 0x40b580; the exe keeps the pointer in 0xa59fa4), the DAT container parse exe 0x40eb90(buffer,
// segment) of one segment (the credit pages' VRAM images), the buffer freed (0x6117e5) and the music channel start exe
// 0x40dc90(ch, slot, 0). A hook left empty is skipped (the script logic still runs; staffLoad empty -> 0xa59fa4 = 0).
struct StaffRollHost {
    std::function<uint32_t()> staffLoad;                       // the value stored in 0xa59fa4
    std::function<void(uint32_t buf, uint32_t seg)> staffSegment;
    std::function<void(uint32_t buf)> staffFree;
    std::function<void(uint32_t ch, uint32_t slot)> musicStart;
};
void staffRollSetHost(StaffRollHost host);

} // namespace od::port
