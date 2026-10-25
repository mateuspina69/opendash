// The class states of appo_dah_b.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "content/calbania/appo_dah_face.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::appo_dah_b_state {

// the bomb's fields (pool 0x40 type 0x15, exe 0x5c8220): its life, its attack, its hit box, its spin
struct AppoDahBombFields : ActorClassState {
    explicit AppoDahBombFields(ProjectileActor& actor) : ticks(actor.params, 0), attack(actor.params[2]), hitBox(actor.hitBox) {}
    BytesField<int16_t, 4> ticks;
    FieldAlias<int8_t, uint8_t> attack;
    HitBoxHead& hitBox;
    uint16_t spin = 0;
};

// ==== the script puppets: exe 0x4c5f90 (script 0x1d) / 0x4c6ae0 (script 0x1f) ======================================
struct ScriptPuppetFields : ActorClassState {
    explicit ScriptPuppetFields(AnimatedActor& actor) : routine(actor.params[0]), speaker(actor.params[1]), noGround(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> routine; // the variant routine's index
    uint8_t& speaker; // script 0x1f: it speaks first
    uint8_t& noGround; // not kept on the ground
    uint8_t castFlags = 0; // kPuppetHold
    uint8_t animKey = 0;
    AppoDahMouth mouth;

    void clearBlock() override {
        castFlags = 0;
        animKey = 0;
        mouth = {};
    }
};

} // namespace od::port::appo_dah_b_state
