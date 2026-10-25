// The class states of st41_hover_gunner.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st41_hover_gunner_state {

// Its own bytes (0x20 cleared at the bind): a timer, the yaw it turns to off a wall, its hit box, its stagger meter,
// its attack cooldown, its flags, this tick's walls' contact, its aim's offsets (x / z, and one not read), the wall's
// side, the key entry of its next animation
struct GunnerFields : ActorClassState {
    explicit GunnerFields(AnimatedActor& actor) : flags(actor.shownParts), animEntry(actor.hopperTimer) {}
    uint8_t& flags;
    uint8_t& animEntry;
    uint16_t timer = 0;
    int16_t turnYaw = 0;
    HitBoxHead hitBox;
    uint16_t stagger = 0;
    uint16_t cooldown = 0;
    uint8_t contact = 0;
    uint16_t aimX = 0;
    uint16_t aimZ = 0;
    uint16_t wallSide = 0;
    uint16_t aimSpare = 0;
    PartField<uint8_t, uint16_t> timerLow{timer, 0};

    void clearBlock() override {
        timer = 0;
        turnYaw = 0;
        hitBox = {};
        stagger = 0;
        cooldown = 0;
        contact = 0;
        aimX = 0;
        aimZ = 0;
        wallSide = 0;
        aimSpare = 0;
    }
};

} // namespace od::port::st41_hover_gunner_state
