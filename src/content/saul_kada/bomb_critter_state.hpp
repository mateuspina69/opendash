// The class states of bomb_critter.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::bomb_critter_state {

// The critter's own bytes: its hit box, the fuse lit, the ticks near the player before it lights, the fuse's ticks,
// the bob's phase, the smoke's ticks (hurt)
struct CritterFields : ActorClassState {
    explicit CritterFields(AnimatedActor&) {}
    HitBoxHead hitBox;
    uint16_t fuseLit = 0;
    uint16_t nearTicks = 0;
    uint16_t fuseTicks = 0;
    uint8_t bobPhase = 0;
    uint8_t smokeTicks = 0;
    PartField<uint8_t, uint16_t> fuseTicksLow{fuseTicks, 0};

    void clearBlock() override {
        hitBox = {};
        fuseLit = 0;
        nearTicks = 0;
        fuseTicks = 0;
        bobPhase = 0;
        smokeTicks = 0;
    }
};

} // namespace od::port::bomb_critter_state
