// Town walker NPCs (Maybe_TownWalkerUpdate 0x441950: ST09 mailman & co.) -- the route-following state
// machine, bit-exact with the game in 16.16 fixed point. Player reaction: stop when the player stands
// close in front (state 3). Not ported yet: talk / bumped substates, wall collision (FUN_0040f570),
// ground snapping (FUN_0040f190), head look (FUN_004425d0) and the idle animation of state 3.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include "game/angles.hpp"

namespace od {

// Global LFSR DAT_006d0000 (the game shares it between all systems; the port gives walkers their own).
struct Rng {
    uint32_t x = 0x932dd99b;
    uint32_t next() {
        x = (1u - (uint32_t(int32_t(x) >> 31))) + x * 2u ^ 0x873ca9e5u;
        return x;
    }
};

// One waypoint of a route (DAT_00a72244[route] + index * 8): next[k] >= 0x80 = no link.
struct Waypoint {
    std::array<uint8_t, 4> next{0xFF, 0xFF, 0xFF, 0xFF};
    int16_t x = 0, z = 0;
};
using Route = std::map<int, Waypoint>;

struct Walker {
    int route = 0;
    uint8_t cur = 0, prev = 0; // actor +0x15f / +0x160
    uint8_t flags = 0;         // actor +0xd: 0x10 = may pause on arrival
    int16_t speed = 0, turn = 0;
    int32_t x = 0, y = 0, z = 0; // 16.16
    int16_t yaw = 0;             // 4096 = 360 deg; forward = -(sin, cos) * speed
    uint8_t sub = 0;             // path sub-state (table 0x88dc4c), 5 = port: one idle tick after a pause
    int16_t tx = 0, tz = 0;      // current target
    int16_t pause = 0;           // remaining pause ticks (sub 4)
    uint8_t state = 0;           // actor +9: 0 = follow the route (FUN_00441cb0), 3 = stopped for the player
};

// The player as the walkers see it (player struct 0xa71d20 +0x10..: 16.16 position).
struct WalkerPlayer {
    int32_t x = 0, y = 0, z = 0;
};

// Angle (4096 units) from b to a, like FUN_005e3bc0: atan2(a - b) with the game's 2048 / 3.14 scale.
int angleTo(int32_t ax, int32_t az, int32_t bx, int32_t bz);

// FUN_004446d0: next waypoint (random among the links, avoiding the previous one).
uint8_t chooseNext(const Waypoint& wp, uint8_t prev, Rng& rng);

// One 30 Hz tick of the path sub-state (FUN_00441cb0). forcedNext >= 0 replaces the random choice
// (tests replay the game's choices, which depend on the shared global RNG).
void stepWalker(Walker& w, const Route& route, Rng& rng, int forcedNext = -1);

// One tick of the walker's actor update with the player present: state 0 = the route step, then
// FUN_00441cb0's check (closer than 0x100 and within +-0x200 of the walker's facing -> state 3); state 3 =
// FUN_00442470 (stand while closer than 0x180 and within +-0x280, then resume the route).
void stepWalker(Walker& w, const Route& route, Rng& rng, const WalkerPlayer& player);

} // namespace od
