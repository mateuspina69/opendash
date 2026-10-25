// The class states of st34_ruin_flyer.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"

namespace od::port::st34_ruin_flyer_state {

// The flyer's own bytes: alerted and rising first (its first / last spawn parameters), its hit box, its home point and
// yaw, its timer, its pitch, its stagger gauge, the yaw reflected off a wall, its flags, this tick's wall contact, its
// enemy group
struct FlyerFields : ActorClassState {
    explicit FlyerFields(AnimatedActor& actor) : alerted(actor.params[0]), risesFirst(actor.params[3]), flags(actor.hopperBytes[2]), contact(actor.hopperBytes[3]), group(actor.hopperBytes[4]) {}
    uint8_t& alerted;
    uint8_t& risesFirst;
    uint8_t& flags;
    uint8_t& contact;
    uint8_t& group;
    HitBoxHead hitBox;
    ShortVec3 home;
    int16_t homeYaw = 0;
    uint16_t timer = 0;
    int16_t pitch = 0;
    uint16_t gauge = 0;
    int16_t reflectedYaw = 0;
    PartField<uint8_t, uint16_t> timerLow{timer, 0};

    void clearBlock() override {
        hitBox = {};
        home = {};
        homeYaw = 0;
        timer = 0;
        pitch = 0;
        gauge = 0;
        reflectedYaw = 0;
    }
};

} // namespace od::port::st34_ruin_flyer_state
