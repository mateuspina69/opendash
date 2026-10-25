// The class states of st28_gunner_boss.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st28_gunner_boss_state {

// The boss's own bytes (0x20 cleared at the bind): its body's and its arm's hit boxes, its last beam, the damage since
// its last retreat, its flags, its wall contact, a timer, a count, dead
struct BossFields : ActorClassState {
    explicit BossFields(AnimatedActor& actor) : flags(actor.hopperBytes[4]), wallContact(actor.hopperBytes[5]), timer(actor.townBirdHits), count(actor.townBirdHitsHigh), dead(actor.hopperTimer) {}
    uint8_t& flags;
    uint8_t& wallContact;
    uint8_t& timer;
    uint8_t& count;
    uint8_t& dead;
    HitBoxHead bodyBox;
    HitBoxHead armBox;
    uint32_t beam = 0;
    uint16_t damageTaken = 0;

    void clearBlock() override {
        bodyBox = {};
        armBox = {};
        beam = 0;
        damageTaken = 0;
    }
};

} // namespace od::port::st28_gunner_boss_state
