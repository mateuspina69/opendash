// The class states of wall_debris.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::wall_debris_state {

constexpr uint32_t kPieces = 20;
// One piece: how far it has dropped below the break, its distance from the break, and their speeds
struct DebrisPiece {
    int16_t drop = 0;
    int16_t radius = 0;
    int16_t dropSpeed = 0;
    int16_t radiusSpeed = 0;
};

// The debris' fields (it has no model: the slot past its header is its own): the broken segment's pool entry (the
// spawn's parameter), replaced at the start by the segment's texture list; the pieces alive (a bit each), the ticks,
// the floor, the shapes' phase, the pieces
struct WallDebrisFields : ActorClassState {
    explicit WallDebrisFields(StaticActor& actor) : poolEntry(actor.params, 0), textureList(actor.params, 0) {}
    BytesField<uint32_t, 4> poolEntry;   // before the start
    BytesField<uint32_t, 4> textureList; // after it (per-state alternative of the same word)
    uint32_t livePieces = 0;
    int16_t ticks = 0;
    int16_t floor = 0;
    uint8_t shapePhase = 0;
    StateArray<DebrisPiece, kPieces> pieces{};

    void clearBlock() override {
        pieces[18].dropSpeed = 0;
        pieces[18].radiusSpeed = 0;
        pieces[19] = {};
    }
};

} // namespace od::port::wall_debris_state
