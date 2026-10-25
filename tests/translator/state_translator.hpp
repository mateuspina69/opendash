// The test-only translator between the native game state and dash2.exe's memory layout. The goldens
// a test loads a golden record into its image (a GameMem with the exe's
// layout), loadNativeState puts the image's values into the native state, the game runs, and storeNativeState writes
// the native state back into the image, which the test compares byte for byte with the next record. It is the
// ruler, never linked into the game: the game does not know the exe's layout.
//
// Each test keeps its image current at its boundaries: storeNativeState before it reads or overwrites the image (a
// compare, a reload of a record), loadNativeState after it wrote the image and before the game runs again. From the
// first load on the image is fenced (GameMem::setFences): a game access through the image to a byte the native state
// owns is an error, never a read of a stale copy (the test's own reads and writes of the image are not checked).
#pragma once

#include "translator/gamemem.hpp"
#include "game/collision.hpp"
#include "game/player/player_state.hpp"

namespace od::test {

// the native state read from the image (each native field whose bytes the image maps; the others keep their values);
// the image's native bytes fenced
// the actor pools as built and the translator's actor records dropped (a test starting another golden: the pools are
// the program's, so a new image does not rebuild them)
void resetNativeActors();
void loadNativeState(GameMem& image);
// the native state written into the image (each native field whose bytes the image maps)
void storeNativeState(GameMem& image);
// a player object (the local one or another: a remote player posed apart) read from / written into the image at the
// exe address `address` in the player's layout
void loadPlayerAt(const GameMem& image, uint32_t address, od::port::Player& player);
void storePlayerAt(GameMem& image, uint32_t address, od::port::Player& player);
// a room collision query state's 0x80 bytes in the exe's layout (the globals 0x6d00c0..: test_collision's goldens)
void collisionStateBytes(od::CollisionState& c, uint8_t out[0x80]);

} // namespace od::test
