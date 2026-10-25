// The class states of two_form_reaverbot.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::two_form_reaverbot_state {

// Its own bytes (0x20 cleared at the bind): its hit box, the last push-out's result, its neighbour word, its target
// yaw, a timer, its flags, its stagger counter, its capsule's kind, its enemy group and the mark it sets, a sound
// timer; its animation's entry key (the header's step counter), its spawn point (the acceleration's lanes), a life
// count (the acceleration's spare lane: 0x258 retires it)
struct FormFields : ActorClassState {
    explicit FormFields(AnimatedActor& actor) : flags(actor.hopperBytes[4]), stagger(actor.hopperBytes[5]), capsuleKind(actor.townBirdHits), group(actor.townBirdHitsHigh), groupMark(actor.hopperTimer), animEntry(actor.stepCounter), spawnX(actor.acceleration.x), spawnY(actor.acceleration.y), spawnZ(actor.acceleration.z), lifeCount(actor.acceleration.spare) {}
    uint8_t& flags;
    FieldAlias<int8_t, uint8_t> stagger;
    uint8_t& capsuleKind;
    uint8_t& group;
    uint8_t& groupMark;
    uint8_t& animEntry;
    int16_t& spawnX;
    int16_t& spawnY;
    int16_t& spawnZ;
    int16_t& lifeCount;
    HitBoxHead hitBox;
    StateArray<uint8_t, 8> clearedPad0{}; // bytes the block clear covers (no reader)
    uint32_t wallResult = 0;
    uint16_t neighbour = 0;
    uint16_t targetYaw = 0;
    uint16_t timer = 0;
    uint8_t soundTimer = 0;
    void clearBlock() override {
        hitBox = {};
        clearedPad0 = {};
        wallResult = 0;
        neighbour = 0;
        targetYaw = 0;
        timer = 0;
        soundTimer = 0;
    }
};

} // namespace od::port::two_form_reaverbot_state
