// The class states of st2b_props.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st2b_props_state {

// The plates' own bytes: their home height
struct PlateFields : ActorClassState {
    explicit PlateFields(StaticActor&) {}
    int16_t homeY = 0;

    void clearBlock() override {
        homeY = 0;
    }
};

// The pot's own bytes: its hit box, its state flags (kPot*), a word
struct PotFields : ActorClassState {
    explicit PotFields(StaticActor& actor) : rotationSpare(actor.rotation.spare), hitBox(actor.hitBox) {}
    int16_t& rotationSpare; // the yaw it was thrown along (the rotation's spare lane)
    HitBoxHead& hitBox;
    uint32_t word = 0;
    PartField<uint8_t, uint32_t> potFlags{word, 0};

    void clearBlock() override {
        word = 0;
    }
};

} // namespace od::port::st2b_props_state
