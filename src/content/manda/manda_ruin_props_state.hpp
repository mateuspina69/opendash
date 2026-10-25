// The class states of manda_ruin_props.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::manda_ruin_props_state {

// The platform's (and its wheel's) own fields: the spawn parameters and the class block
struct PlatformFields : ActorClassState {
    explicit PlatformFields(StaticActor& actor) : runCells(actor.params[0]), runLength(actor.params, 0), driven(actor.params[0]), riderId(actor.params[2]), sparkStep(actor.params[2]), hitBox(actor.hitBox), landed(actor.gunshipPitchTarget()), hitFlags(actor.gunshipTimer()), rider(actor.gunshipCooldown()) {}
    uint8_t& runCells; // the spawn's run length in cells ...
    BytesField<int16_t, 4> runLength; // ... turned into units by the init
    uint8_t& driven; // the wheel: its platform carries the player (it spins up)
    FieldAlias<int8_t, uint8_t> riderId; // the platform: its rider's enemy id (0: none) ...
    FieldAlias<int8_t, uint8_t> sparkStep; // ... the wheel: the sparks' step while spinning up
    HitBoxHead& hitBox;
    uint8_t& landed; // cleared by every collision (its reader is not traced)
    uint8_t& hitFlags; // kProp*
    uint8_t& rider; // kRiderPresent | the rider's animated slot index
    int16_t distanceLeft = 0; // of the leg it runs
    int8_t companion = 0; // the wheel's static slot index
    uint8_t speed = 0;

    void clearBlock() override {
        distanceLeft = 0;
        companion = 0;
        speed = 0;
    }
};

} // namespace od::port::manda_ruin_props_state
