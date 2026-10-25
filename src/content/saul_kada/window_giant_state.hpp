// The class states of window_giant.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::window_giant_state {

// The giant's own bytes: its side (over the header's step counter), the step to come back to after a reaction (its
// animation, key entry and push), its hit box, its timer, its flags, the key entry of its next animation, the player
// seen in its box, the walk's step a pause returns to
struct GiantFields : ActorClassState {
    explicit GiantFields(AnimatedActor& actor) : side(actor.stepCounter), reactReturn(actor.params[0]), reactAnim(actor.params[1]), reactEntry(actor.params[2]), reactPush(actor.params[3]) {}
    uint8_t& side;
    uint8_t& reactReturn;
    uint8_t& reactAnim;
    uint8_t& reactEntry;
    FieldAlias<int8_t, uint8_t> reactPush;
    HitBoxHead hitBox;
    uint16_t timer = 0;
    uint8_t flags = 0;
    uint8_t animEntry = 0;
    uint8_t seen = 0;
    uint8_t resumeStep = 0;

    void clearBlock() override {
        hitBox = {};
        timer = 0;
        flags = 0;
        animEntry = 0;
        seen = 0;
        resumeStep = 0;
    }
};

} // namespace od::port::window_giant_state
