// The class states of manda_ruin_burrower.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "content/manda/manda_ruin_burrower.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::manda_ruin_burrower_state {

// The burrower's own fields: its spawn parameters (BurrowerParams), the two glow slots in its rotation's spare lane,
// its class block
struct BurrowerFields : ActorClassState, BurrowerParams {
    explicit BurrowerFields(AnimatedActor& actor) : BurrowerParams(actor), glowSlots(actor.rotation.spare), secondGlow(actor.rotation.spare, 0), firstGlow(actor.rotation.spare, 1), swayStep(actor.hopperBytes[4]), contact(actor.hopperBytes[5]), mode(actor.townBirdHits), bouncesLeft(actor.townBirdHitsHigh), startKey(actor.hopperTimer), rotation(actor.rotation) {}
    FieldAlias<uint16_t, int16_t> glowSlots; // the rotation's spare lane: the leap's two glow trails (slot ids) ...
    PartField<uint8_t, int16_t> secondGlow; // ... the second in the low byte,
    PartField<uint8_t, int16_t> firstGlow; // ... the first in the high one
    FieldAlias<int8_t, uint8_t> swayStep;
    uint8_t& contact; // kBurrowerContact* of this tick
    uint8_t& mode; // kBurrower* behaviour bits
    uint8_t& bouncesLeft;
    FieldAlias<int8_t, uint8_t> startKey; // the next animation's first key
    ShortVec4& rotation; // the slot's rotation (its pitch and roll cleared byte by byte)
    HitBox hitBox; // its contact.x: the hit's direction
    int16_t timer = 0;
    int16_t savedYaw = 0; // the knock's direction / the hop's turn
    int16_t turnLeft = 0;
    int16_t headYaw = 0; // the head's turn (added to the decoded frame each tick)
    int16_t sway = 0;
    void clearPitchAndRollBytes() {
        rotation.x = 0;
        rotation.z = 0;
    }

    void clearBlock() override {
        hitBox = {};
        timer = 0;
        savedYaw = 0;
        turnLeft = 0;
        headYaw = 0;
        sway = 0;
    }
};

} // namespace od::port::manda_ruin_burrower_state
