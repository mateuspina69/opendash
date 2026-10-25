// A run's start: the state part of the game mode's init (exe 0x541150, game mode 0) with the load flag
// (GameState::loadSave; the title sets it: 1 = apply the save image at kSaveBuf, 0 = a new game): the game state, the
// story flags, the visited rooms, the second flag block, the difficulty's stat tables and the player, each from the
// image or to a new run's values (a new run's by the difficulty and the entry mode). Bit-exact against
// tests/data/save_golden.bin (test_save).
#pragma once

#include <cstdint>

namespace od::port {

// exe 0x540d10: the game state (the records merge with the image's: the better kept)
void gameStateInit(int load);
// exe 0x512760: the story flags from the image (the story menu's flag cleared) / all cleared
void storyFlagsInit(int load);
// exe 0x55bfa0: the visited-room block from the image / a new run's
void visitedRoomsInit(int load);
// exe 0x4153b0: the second flag block from the image / cleared
void extraFlagsInit(int load);
// exe 0x573fd0: the player from the image / by the difficulty and the entry mode (its parts, weapons, levels, owned
// items), then the sound's listener
void playerInit(int load);
// The six above in exe 0x541150's order with the load flag and the difficulty (after the pools are reset: the caller
// zeroes the player struct, exe 0x5e00e0(1))
void gameModeInitState();

} // namespace od::port
