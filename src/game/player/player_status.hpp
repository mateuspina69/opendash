// The player's status effects (the status word +0xb8: the timed effects, their floor forms, their marker actors), the
// model's light colour from the good / bad balance, and the drag of a wade.
#pragma once

#include <cstdint>

namespace od::port {

struct Player;
struct PlayerEnv;

// the story flags of the armours that stop a status: the burn's, the slow's
constexpr uint32_t kFlagBurnArmour = 0x403;
constexpr uint32_t kFlagSlowArmour = 0x404;

// exe 0x572470: the armours end their statuses; the drain / burn hurt (the floor forms harder unless the legs resist),
// the slow and the orbit set the drag / spawn their markers, the timers run (shortened by moving: the pad's directions
// pressed); without a status the light colour by the balance; in a wade the drag by its depth
void updateStatusEffects(Player& p);

// The floors that give the player a status while it stands on them (the stages call these on the player): its height
// (the lower of its y and its ground, exe 0x40f190, asked twice as the exe does) below the floor's limit, not between
// two airborne ticks, no script running.
// exe 0x572090(player, limit): the drain floor -- kStatusDrain and its floor form, the drain's timer kFloorStatusTicks;
// 1 when it took
int drainFloor(const PlayerEnv& env, Player& p, int limit);
// exe 0x5720f0(player, limit): the burning floor -- its floor form, and the burn with its timer unless the burn armour
// (kFlagBurnArmour); the player sinks kBurnFloorWade into it unless its legs resist (kLegsNoBurn); 1 when it took
int burnFloor(const PlayerEnv& env, Player& p, int limit);
// exe 0x571ee0(player): the slippery floor (no script running, the legs that grip excepted)
void slipperyFloor(Player& p);

} // namespace od::port
