// The room's territory rectangles (the table *0xa71808 the room setup points at, game/spawn): the cells an enemy may
// not enter (kind 0) or may not leave (any other kind), in units of a grid cell; the classes that keep to their ground
// test their position against them and are pushed back across the edge they crossed.
#pragma once

#include <cstdint>

#include "game/loaded_data.hpp"
#include "game/vectors.hpp"

namespace od::port {

struct Actor;

// One rectangle (8 bytes; a negative kind ends the table)
struct TerritoryRect : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    int32_t kind = 0; // +0x0: 0: inside is forbidden, else outside is
    int8_t cellX = 0; // +0x4: the first cell (x / z, in 0x200 units)
    int8_t cellZ = 0; // +0x5
    int8_t width = 0; // +0x6: the cells it spans
    int8_t depth = 0; // +0x7
    template <class R> static TerritoryRect load(const R& r) {
        TerritoryRect v;
        v.kind = r.template read<int32_t>(0x0);
        v.cellX = r.template read<int8_t>(0x4);
        v.cellZ = r.template read<int8_t>(0x5);
        v.width = r.template read<int8_t>(0x6);
        v.depth = r.template read<int8_t>(0x7);
        return v;
    }
};
constexpr uint32_t kTerritoryRectSize = 8;

struct TerritoryState {
    uint32_t table = 0; // the room's rectangles (their address in the game state; 0: none)
};
inline TerritoryState& territory() {
    static TerritoryState s;
    return s;
}

// exe 0x5e6ab0(pos): the first rectangle (1-based) the position violates; 0 = none
int territoryViolated(const Fixed16Vec3& pos);
// ... on a point's integer x / z (the exe's stack points)
int territoryViolated(int16_t x, int16_t z);
// exe 0x5e6b40(actor, rect): the actor pushed back across the edges of rectangle `rect` (1-based) its last move
// crossed; the kWallOn* bits of the edges (game/collision)
uint32_t territoryPushBack(Actor& actor, int rect);

} // namespace od::port
