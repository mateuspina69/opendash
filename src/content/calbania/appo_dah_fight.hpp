// The fields of Appo / Dah in the ST1F fight (exe 0x4c1b30, content/calbania/appo_dah_b), which the ST1F spawner
// (content/calbania/calbania_spawners) reads too.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

struct AppoDahFightFields : ActorClassState {
    explicit AppoDahFightFields(AnimatedActor& actor) : ridden(actor.params[0]), carried(actor.rotation.spare), collision(actor.shownParts), blinkTicks(actor.hopperBytes[4]), standOn(actor.hopperTimer) {}
    uint8_t& ridden; // at the gate: the player stands on it
    int16_t& carried; // the player carries it (the rotation's spare lane)
    uint8_t& collision; // kFight* collision bits of the tick
    int8_t waypoint = 0; // the waypoint row it walks from, the one it walks to, the last one
    int8_t nextWaypoint = 0;
    int8_t prevWaypoint = 0;
    uint8_t& blinkTicks; // the blink after getting up (no hit entry while it runs)
    int8_t grey = 0; // its tint's level (5:5:5 grey)
    int8_t emote = 0; // an emote effect asked this tick (1..3)
    int8_t animKey = 0; // the key a new animation starts at
    uint8_t& standOn; // a stand-on solid (script 0x1f's place)
    HitBox hitBox; // its contact angle: the hit's direction
    int16_t timer = 0;
    uint16_t knockYaw = 0; // the direction it was hit from (the throw's ticks while it throws)
    uint8_t flags = 0; // kFight* flags

    void clearBlock() override {
        waypoint = 0;
        nextWaypoint = 0;
        prevWaypoint = 0;
        grey = 0;
        emote = 0;
        animKey = 0;
        hitBox = {};
        timer = 0;
        knockYaw = 0;
        flags = 0;
    }
};

} // namespace od::port
