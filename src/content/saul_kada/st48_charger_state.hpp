// The class states of st48_charger.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"

namespace od::port::st48_charger_state {

// The charger's own bytes (0x20 cleared at the bind): its hit box and its body's, the walls of its last push, its two
// jets' slots, a timer, a target angle / count, its checks (walls, knock-down, ledges), its wheels' hit entries, its
// hit while spinning, a dust request; and in its header: its heading (the rotation's spare lane), lock-on off, the skid
// sound's timer (its spawn parameter bytes 2 / 3)
struct ChargerFields : ActorClassState {
    explicit ChargerFields(AnimatedActor& actor) : heading(actor.rotation.spare), noLockOn(actor.params[2]), skidSoundTimer(actor.params[3]), wallCheck(actor.hopperBytes[4]), knockDown(actor.hopperBytes[5]), wheelHits(actor.townBirdHits), ledgeCheck(actor.townBirdHitsHigh), hitWhileSpinning(actor.hopperTimer) {}
    int16_t& heading;
    uint8_t& noLockOn;
    uint8_t& skidSoundTimer;
    uint8_t& wallCheck;
    uint8_t& knockDown;
    uint8_t& wheelHits;
    uint8_t& ledgeCheck;
    uint8_t& hitWhileSpinning;
    HitBoxHead hitBox;
    HitBoxHead bodyBox;
    uint32_t walls = 0;
    union {
        StateArray<uint8_t, 2> jets{};
        uint16_t jetsWord;
    };
    int16_t timer = 0;
    int16_t target = 0;
    uint8_t dust = 0;

    void clearBlock() override {
        hitBox = {};
        bodyBox = {};
        walls = 0;
        jets = {};
        timer = 0;
        target = 0;
        dust = 0;
    }
};

} // namespace od::port::st48_charger_state
