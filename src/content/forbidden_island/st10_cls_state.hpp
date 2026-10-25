// The class states of st10_cls.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st10_cls_state {

// The roller's own values: the wobble's counter (parameter 0), its group bit (parameter 1), the ticks far from the
// player, its lifetime (the acceleration's spare lane), and the class block: its hit box, the push-out's result, the
// neighbour word, its heading, a timer, its flags, the hit counter (< 0: the cooldown), the hit mode, its encounter
// group's index and bits, the hum's countdown
struct RollerFields : ActorClassState {
    explicit RollerFields(AnimatedActor& actor) : wobble(actor.params[0]), groupBit(actor.params[1]), farTicks(actor.params, 2), lifetime(actor.acceleration.spare), flags(actor.hopperBytes[4]), hitMode(actor.townBirdHits), groupIndex(actor.townBirdHitsHigh), groupBits(actor.hopperTimer) {}
    uint8_t& wobble;
    FieldAlias<int8_t, uint8_t> groupBit;
    BytesField<int16_t, 4> farTicks;
    FieldAlias<uint16_t, int16_t> lifetime;
    uint8_t& flags; // kRoller*
    int8_t hitCount = 0;
    uint8_t& hitMode; // HitMode
    uint8_t& groupIndex;
    uint8_t& groupBits; // the marks it leaves on its group (set by the morph)
    HitBoxHead hit;
    uint32_t pushResult = 0;
    uint16_t neighbour = 0; // game/enemy_neighbours' word
    uint16_t heading = 0;
    uint16_t timer = 0;
    uint8_t humTicks = 0;

    void clearBlock() override {
        hitCount = 0;
        hit = {};
        pushResult = 0;
        neighbour = 0;
        heading = 0;
        timer = 0;
        humTicks = 0;
    }
};

} // namespace od::port::st10_cls_state
