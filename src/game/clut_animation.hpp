// The CLUT animation (pool 0xa0 global type 2, exe 0x55da50): an effect that blends the stage's colour tables into the
// live ones over the keys of one of the stage's CLUT sequences (StageState::clutAnimations, the effect's parameter its
// index): each key blends its source table into its destination over its ticks (exe 0x55dae0, the colours blended by
// exe 0x55dcb0), then the next key, a jump back, or the end -- the live table restored from the stage's (variant 1)
// and the effect freed. The room-flag spawner (exe 0x425140) keeps one alive per set lock flag.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x55da50 (pool 0xa0 global type 2)
void clutAnimationUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x55dcb0(a, b, t): two words of two 15-bit colours each blended from a toward b by t / 4096 (each 5-bit channel
// widened to 8 bits, the step rounded), the semi-transparency bit of either word's colours kept
uint32_t blendColourPair(uint32_t a, uint32_t b, uint32_t t);
// exe 0x425140 (pool 0xe0 type 0x15, 52 rooms): the room-flag CLUT animations -- one CLUT animation (sequence +0xc + k)
// alive while the lock flag kFlagLocked + k is set (k = 0..15), ended (its restore state) when the flag is cleared
void roomFlagClutAnimationsUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
