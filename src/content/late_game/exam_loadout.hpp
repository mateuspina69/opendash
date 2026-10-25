// The digger's licence exam (ST55) and its loadout: the player's own equipment kept in the loadout save
// (game/player/event_loadout) and the exam's given (exe 0x542730), and where a game over during the exam rescues the
// player to (exe 0x5428b0's places, asked by game/title through the registry).
#pragma once

#include "game/content_registry.hpp"

namespace od::port {

// exe 0x542730: the player's equipment saved, the exam's given (life 0x50, no special weapon, the exam's helmet and
// armour, no hit-height level, the buster's levels 2 / 0 / 0 / 2)
void examLoadoutGive();
// exe 0x5428b0's places: back to the office the exam was entered from (ST1B or ST0B)
GameOverRescuePlace examRescuePlace();
// the exam was entered from ST1B (story flag 0x248; else from ST0B)
bool examEnteredFromSt1b();

} // namespace od::port
