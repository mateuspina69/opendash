// The class states of st0a_fx.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st0a_fx_state {

// the effect's bytes: its spawn parameters (+0xc..+0xf), the short after its position and its own bytes (+0x18..),
// each variant naming them its way
struct StageEffectFields : ActorClassState {
    explicit StageEffectFields(EffectActor& actor) : firstFrame(actor.params[0]), trailRow(actor.params[0]), radiusY(actor.params[0]), radiusX(actor.params[1]), trailBaseQuarter(actor.params[1]), released(actor.stepCounter), size(actor.params, 2), vertical(actor.params[2]), instant(actor.params[2]), trailShrink(actor.params[3]), tilePage(actor.params[3]), rawSize(actor.posSpare), yaw(actor.posSpare), frame(actor.trail[0]), trailBase(actor.trail[1]), fadeTicks(actor.trail[0]), fade(actor.trail[2]), radius(actor.trail[0], actor.trail[1]), tileColour(actor.trail[0], actor.trail[1]), frameLow(actor.trail[0], 0), cycle(actor.trail[1], 0), tileRising(actor.trail[2], 0), trail_(actor.trail) {}
    uint8_t& firstFrame; // 0: the first u frame / 0x20
    FieldAlias<int8_t, uint8_t> trailRow; // 3: the trail's row
    FieldAlias<int8_t, uint8_t> radiusY; // 5 / 8: the glow's radii / 4
    FieldAlias<int8_t, uint8_t> radiusX;
    FieldAlias<int8_t, uint8_t> trailBaseQuarter; // 3: the trail's base / 4
    uint8_t& released; // 5: the hold ends (set by its spawner; the step counter's byte)
    BytesField<int16_t, 4> size; // 0: the billboard's size
    uint8_t& vertical; // 3: the trail steps along y (the second row table)
    uint8_t& instant; // 5: starts at full brightness; 8: steady (no flicker)
    uint8_t& trailShrink; // 3: & 0xf, added to the size's shrink
    uint8_t& tilePage; // 7: the TILE's tpage (<< 5 | 0x1e; & 3 its blend mode)
    int16_t& rawSize; // 2: the billboard's size (the yaw's slot)
    int16_t& yaw; // 3: the trail's yaw
    FieldAlias<uint16_t, int16_t> frame; // 0 / 2 / 3: the frame counter (0 / 2: its low byte the u frame)
    int16_t& trailBase; // 3: set from its parameter (its reader is not traced)
    int16_t& fadeTicks; // 5: the fade's ticks
    int16_t& fade; // 6: the fade out's ticks left
    PiecesField<int32_t> radius;       // 6: the burst's radius (the trail's first two shorts)
    PiecesField<uint32_t> tileColour;  // 7
    PartField<uint8_t, int16_t> frameLow; // the frame counter's low byte
    PartField<uint8_t, int16_t> cycle;    // 4: the frame cycle (>> 2: the frame)
    PartField<uint8_t, int16_t> tileRising; // 7: the colour rises
    // its own bytes (the trail's eight shorts) cleared one byte at a time (exe 0x40dc40)
    void clearBlockBytes() { trail_ = {}; }

private:
    StateArray<int16_t, 8>& trail_;
};

} // namespace od::port::st0a_fx_state
