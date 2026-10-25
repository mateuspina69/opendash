// The class states of st28_lava_boss.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st28_lava_boss_state {

// The boss's bytes: in its header the red of its tint (its first parameter byte), a bomb out at the get-up, the
// charge's travel yaw; the bomb it has out (the rotation's spare lane), in the box (the velocity's spare lane), the
// spin sound's timer (the spin's x); its class block: its hit box and its body's, a timer (a saved yaw), the walk's
// phase, its ground now / before, the damage it took, a count, its pose, in the arena's box, its status, its fall's
// result
struct BossFields : ActorClassState {
    explicit BossFields(AnimatedActor& actor) : tintRed(actor.params[0]), bombOutAtGetUp(actor.params[1]), travelYaw(actor.params, 2), bombOut(actor.rotation.spare), inBoxWord(actor.velocity.spare), spinSoundTimer(actor.spin.x), count(actor.hopperBytes[4]), pose(actor.hopperBytes[5]), inBox(actor.townBirdHits), status(actor.townBirdHitsHigh), fallResult(actor.hopperTimer) {}
    uint8_t& tintRed;
    uint8_t& bombOutAtGetUp;
    BytesField<int16_t, 4> travelYaw;
    int16_t& bombOut;
    FieldAlias<uint16_t, int16_t> inBoxWord;
    FieldAlias<uint16_t, int16_t> spinSoundTimer;
    uint8_t& count;
    uint8_t& pose;
    uint8_t& inBox;
    uint8_t& status;
    uint8_t& fallResult;
    HitBoxHead hitBox;
    HitBoxHead bodyBox;
    uint16_t timer = 0;
    uint16_t walkPhase = 0;
    uint16_t groundNow = 0;
    uint16_t groundBefore = 0;
    uint16_t damageTaken = 0;

    void clearBlock() override {
        hitBox = {};
        bodyBox = {};
        timer = 0;
        walkPhase = 0;
        groundNow = 0;
        groundBefore = 0;
        damageTaken = 0;
    }
};

} // namespace od::port::st28_lava_boss_state
