// The equipment page's turning special weapon: its actor in the sub-screen's pool-0x20 slots (24..31: the class of the
// sub-screen's table, type kWeaponModelType = the exe's class 0x4ec4f0), set up when the weapon shown changes (exe
// 0x5edac0), updated with the sub-screen's slots (exe 0x55bbc0) and drawn (exe 0x5ed9b0's drawing: the drawing area,
// the actors through game/model_preview, the box it is clipped to).
#pragma once

#include <cstdint>

namespace od::port {

struct StatusScreen;

// exe 0x5edac0(block, weapon): on a change of the weapon shown the old model freed and, for a weapon, a model of the
// sub-screen's slots made at the weapon's place (exe table 0x91b578), turned toward the viewer
void weaponModelSet(StatusScreen& s, uint8_t weapon);
// exe 0x55bbc0: the sub-screen's pool-0x20 slots updated (the live ones, by the sub-screen's class table) with the
// update loop's cursor on them; an unported class is skipped (as game/actors updatePool)
void subScreenActorsUpdate();
// exe 0x5ed9b0(block, k, ot)'s drawing: in the ordering table's entry kOtModel + ot * 4, the drawing area, the
// sub-screen's actors (exe 0x5f40b0, the screen centre moved by the box k's offset) and the box k's drawing area, each
// inserted at the entry's head (walked: the box first, then the actors, then the full area)
void weaponModelDraw(int k, int ot);

} // namespace od::port
