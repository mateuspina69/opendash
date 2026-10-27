// The class states of glyde_base_bombers.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::glyde_base_bombers_state {

struct GlydeBomberFields : ActorClassState {
    explicit GlydeBomberFields(AnimatedActor& actor) : bits(actor.shownParts), hitsLeft(actor.hopperBytes[4]) {}
    uint8_t& bits; // kBomber*
    uint8_t& hitsLeft; // the hits before it is knocked back
    int8_t runsLeft = 0; // a variant's throws before it sinks
    HitBox hitBox; // its shape asks for the contact output: the hit list writes it after the head
    ActorRef<StaticActor> bomb; // the bomb it holds
    uint8_t timer = 0;
    union {
        int16_t aimYaw = 0; // the yaw it aims the throw at ...
        int16_t throwDistance; // ... then the player's distance (the throw's speed)
    };
    int16_t spawnYaw = 0; // its aim stays within 0x100 of it

    void clearBlock() override {
        runsLeft = 0;
        hitBox = {};
        bomb = nullptr;
        timer = 0;
        aimYaw = 0;
        spawnYaw = 0;
    }
};

} // namespace od::port::glyde_base_bombers_state
