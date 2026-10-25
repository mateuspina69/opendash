// The class states of st41_enemy_generator.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st41_enemy_generator_state {

// The generator's own bytes: its hit box, the hits left before a flinch, its timer, its flags, the key entry of its
// next animation; the flyer family's sway / bob / turn bytes (set at the bind, not read by the generator)
struct GeneratorFields : ActorClassState {
    explicit GeneratorFields(AnimatedActor& actor) : flagsLow(actor.hopperBytes[4]), bobCarry(actor.townBirdHits), animEntry(actor.hopperTimer) {}
    uint8_t& flagsLow;
    uint8_t& bobCarry;
    uint8_t& animEntry;
    HitBox hitBox; // with its contact output (+0x154: the hit entry asks for the other's last move)
    uint16_t flinchHits = 0;
    uint16_t timer = 0;
    uint16_t swayAngle = 0;
    uint16_t bobAngle = 0;
    uint16_t spin = 0;
    uint16_t flags = 0;
    PartField<uint8_t, uint16_t> timerLow{timer, 0};

    void clearBlock() override {
        hitBox = {};
        flinchHits = 0;
        timer = 0;
        swayAngle = 0;
        bobAngle = 0;
        spin = 0;
        flags = 0;
    }
};

} // namespace od::port::st41_enemy_generator_state
