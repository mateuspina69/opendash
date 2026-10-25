// Roll's development room: in the Flutter (ST04; Roll's menu 0x4a, "Development Room", sets kFlagDevRoomAsked, the ST04
// room handler starts script 0x30, whose handler runs the stage's script-0x30 callback: ST04's stage setup points it at
// exe 0x545e60) and Nino Island's copy (ST19, exe 0x548650, the same menu over its own block). The menu's pages:
// Roll's opening message and her menu, develop (an item's recipe), change the special weapon, improve a weapon's stat
// (its price by the tool kit tier); the list / stats panel (the item list, the stat bars, the weapon's turning model).
//
// The script block's states (exe tables 0x8aef80 / 0x8af5d0): the set-up (Roll's: windows closed, the sub-screen's
// layout, the player scripted, the field position saved, an in-stage request to sub 2; Nino's: the player placed for
// the menu in the room), the step runner (Roll's record, then the menu), the end (the special weapon chosen applied,
// the player back where he was).
//
// Nino's copy lives here, with Roll's, and not under content/nino: it is the same room of the Flutter (the same menu,
// messages, price tables and panel over the ST19 stage's block), one subject in one file.
#pragma once

#include <cstdint>
#include <vector>

#include "engine/screen_prim.hpp"
#include "game/content_registry.hpp"

namespace od::content {

constexpr uint32_t kFlagDevRoomAsked = 0x6fd;    // Roll's menu chose "Development Room" (cleared by its start)
constexpr uint8_t kScriptDevRoom = 0x30;         // the script whose handler runs the stage's development room

// where the development room puts the list / stats panel primitives this tick; null = none collected
void setDevRoomDraw(std::vector<od::port::ScreenPrim>* draw);
// exe 0x545ab0(state, weapon, stat): the price of the stat's next level (the room copy's price table by level * 5 +
// stat; the tool kit tier: High -10 %, Low +10 %), -1 at the maximum
int32_t devUpgradePrice(int weapon, int stat);

// the two rooms as the stage script-0x30 callbacks of ST04 / ST19
void registerDevRoom(od::port::ContentRegistry& r);

} // namespace od::content
