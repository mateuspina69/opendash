// ST05's list of collected entries: script 0x74 (exe 0x53dd20) and its list menu (block 0xa59fd0;
// content/yosyonke/st05_entry_list.cpp).
#pragma once

#include <cstdint>
#include <vector>

#include "engine/routine_ids.hpp"
#include "engine/screen_prim.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// The script's 2D packets (their builder in content/yosyonke/st05_entry_list.cpp, through game/menu_draw), collected only
// into a set sink (null = not collected): script 0x74's entry list (exe 0x53e040: rows, hand, scroll bar; OT +0x7c,
// drawn as the shop's list)
void setEntryListDraw(std::vector<ScreenPrim>* draw);

// script 0x74, by its handler [0x89b6d0 + id * 4]
bool st05EntryListScriptPorted(RoutineId handler);
bool st05EntryListScriptTick(const PlayerEnv& env, RoutineId handler);

} // namespace od::port
