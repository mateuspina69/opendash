// The class states of pirate_servbots.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::pirate_servbots_state {

// The Servbot's own bytes (0x20 cleared at the bind): its hit box (its contact motion: the hit's yaw / distance), its
// collision result, the side it runs around the player on, a timer, the yaw it faces (the hit's / the watched
// player's), its flags, the wall's timer, what a ledge does to it, whether it has reacted, its off-screen ticks,
// whether it hit something thrown
struct ServbotFields : ActorClassState {
    explicit ServbotFields(AnimatedActor& actor) : flags(actor.hopperBytes[0]), wallTimer(actor.hopperBytes[1]), ledge(actor.hopperBytes[2]), reacted(actor.hopperBytes[3]), thrownHit(actor.hopperBytes[5]) {}
    uint8_t& flags;
    uint8_t& wallTimer;
    uint8_t& ledge;
    uint8_t& reacted;
    int8_t offScreen = 0;
    uint8_t& thrownHit;
    // the reporting variant's spawner byte it marks when freed (its spawn word holds the byte's exe address)
    uint8_t* goneFlag = nullptr;
    HitBox hitBox;
    uint8_t contact = 0;
    uint8_t side = 0;
    int16_t timer = 0;
    int16_t faceYaw = 0;

    void clearBlock() override {
        offScreen = 0;
        hitBox = {};
        contact = 0;
        side = 0;
        timer = 0;
        faceYaw = 0;
    }
};

// The bomber's own bytes (0x20 cleared at the bind): its hit box, whether it is thrown out, a wait, the bombs left of
// the volley, its home yaw
struct BomberFields : ActorClassState {
    explicit BomberFields(AnimatedActor&) {}
    HitBox hitBox;
    uint8_t thrownOut = 0;
    uint16_t wait = 0;
    uint16_t bombsLeft = 0;
    int16_t homeYaw = 0;

    void clearBlock() override {
        hitBox = {};
        thrownOut = 0;
        wait = 0;
        bombsLeft = 0;
        homeYaw = 0;
    }
};

} // namespace od::port::pirate_servbots_state
