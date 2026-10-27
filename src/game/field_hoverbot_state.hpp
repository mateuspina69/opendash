// The class states of field_hoverbot.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::field_hoverbot_state {

// The hoverbot's own fields: the yaw it aims its shot / its throw along (its rotation's spare lane; while leaving, the
// sink sound's flag) and its class block
struct FieldHoverbotFields : ActorClassState {
    explicit FieldHoverbotFields(AnimatedActor& actor) : aimYaw(actor.rotation.spare), sinkSounded(actor.rotation.spare), neighbourLow(actor.shownParts), bits(actor.hopperBytes[0]), cooldown(actor.hopperBytes[1]) {}
    int16_t& aimYaw;
    int16_t& sinkSounded; // leaving: the sink sound was played
    uint8_t& neighbourLow; // the neighbour word's low byte (its side bit, kHoverBumped)
    uint8_t& bits; // kHover* behaviour bits
    uint8_t& cooldown; // ticks before the next shot
    HitBox hitBox; // its shape asks for the contact output: the hit list writes it after the head
    int16_t timer = 0;
    uint8_t contact = 0; // kHover* contact bits of the dying fall (exe 0x44d290)
    uint16_t neighbour = 0; // game/enemy_neighbours (| kHoverBumped)
    int16_t fallTicks = 0; // variant 1: its fall back into the ground
    int16_t radius = 0; // its place around the player (exe 0x5e6140)
    int16_t pitch = 0;
    int16_t yaw = 0;

    void clearBlock() override {
        hitBox = {};
        timer = 0;
        contact = 0;
        neighbour = 0;
        fallTicks = 0;
        radius = 0;
        pitch = 0;
        yaw = 0;
    }
};

} // namespace od::port::field_hoverbot_state
