// The class states of st34_flyer_shell.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st34_flyer_shell_state {

// The shell's own bytes: its hit box (bone 0) and its core's (bone 3), the dying timer, a decaying count (nothing here
// sets it), its flags, a wall hit
struct ShellFields : ActorClassState {
    explicit ShellFields(AnimatedActor& actor) : flags(actor.shownParts) {}
    uint8_t& flags;
    HitBoxHead hitBox;
    HitBoxHead coreBox;
    uint16_t dyingTimer = 0;
    uint16_t decay = 0;
    uint8_t wallHit = 0;
    PartField<uint8_t, uint16_t> dyingTimerLow{dyingTimer, 0};

    void clearBlock() override {
        hitBox = {};
        coreBox = {};
        dyingTimer = 0;
        decay = 0;
        wallHit = 0;
    }
};

} // namespace od::port::st34_flyer_shell_state
