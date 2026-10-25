// The effect actors the player spawns: the landing dust, the skid sparks, the water splashes and the jet skates' spray,
// the status effects' markers and the hit's sparks. Their classes live elsewhere (the global effect classes, the water
// class of content/nino); here only what the player hands each new actor.
#pragma once

#include <cstdint>

#include "game/global_class_types.hpp"

namespace od::port {

struct Player;

// exe 0x57b820(player, k): a dust puff (pool 0xa0 type 7): k = 0 at the feet (on the ground: the platform byte), 1 at
// the hips' bone point (a body landing); its size 0xa0 + k * 0x60
void dustPuff(Player& p, int k);
constexpr int kDustAtFeet = 0, kDustAtHips = 1;
// exe 0x57b8c0: skid sparks (pool 0xa0 type 3) at the feet's bone points on alternate frames
void skidSparks(Player& p);
// exe 0x57b990(player, k, x, y, z): a water splash (pool 0x60 type 0x48, the water class 0x5b3c40): k < 0 following the
// player; else at its bone point k offset by (x, y, z). Every ported caller: splash(p)
void waterSplash(Player& p, int k, int16_t x, int16_t y, int16_t z);
// the splash following the player (every ported caller of exe 0x57b990)
inline void splash(Player& p) { waterSplash(p, -1, 0, 0, 0); }
// the landings' n splashes
void splashes(Player& p, int n);
// exe 0x57ba60: the jet skates' water spray: two type-0x48 bursts at the feet's bone points, thrown backwards
void skateSpray(Player& p);
// exe 0x572470's status marker (pool 0x60, the global class type): 0 when the pool is full
uint32_t statusMarker(GlobalStaticType type, uint8_t variant);
// exe 0x57aa50's hit sparks (pool 0x60 type 13, game/hit_effects 0x566130) 0x58 above the player toward the hit's
// direction
void hitSparks(Player& p, int angle);

} // namespace od::port
