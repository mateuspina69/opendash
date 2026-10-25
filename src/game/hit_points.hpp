// The actors' hit points and the defeat tally: the hit points an actor gains or loses (clamped to its maximum; below
// 0 it is dead), the damage the player takes (counted for the HUD, none outside play or with the no-damage flag) and
// the count of the enemies defeated.
#pragma once

#include <cstdint>

namespace od::port {

struct Actor;

// exe 0x5e52f0(actor, delta): hit points += delta (16-bit), at most the maximum; below 0: set to -1, returns -1 (dead),
// else 0
int hitPointsAdd(Actor& actor, int delta);
// exe 0x5e5320(actor, delta): a damage (delta < 0) only while the field plays and without the no-damage flag, recorded
// for the HUD; the hit points of a living actor drop to 0 at most (returns 1 when they reach below 0, 0 else); a heal
// or a dead actor goes through hitPointsAdd
int hitPointsChange(Actor& actor, int delta);
// exe 0x5e53e0: an enemy defeated (both tallies + 1)
void countDefeat();

} // namespace od::port
