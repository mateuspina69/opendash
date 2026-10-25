// The class states of st3c_statue.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st3c_statue_state {

// The statue's own bytes: its pose held
struct StatueFields : ActorClassState {
    explicit StatueFields(AnimatedActor&) {}
    uint8_t holdPose = 0;

    void clearBlock() override {
        holdPose = 0;
    }
};

constexpr uint16_t kPartFlagHalf = 0x04;

// Its own bytes: its hit box and its flags (kPartFlag*: the word's low bits as bytes, kPartFlagHalf as a word)
struct PartFields : ActorClassState {
    explicit PartFields(StaticActor& actor) : hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
    uint16_t flagsWord = 0;
    PartField<uint8_t, uint16_t> flags{flagsWord, 0};

    void clearBlock() override {
        flagsWord = 0;
    }
};

// the exe global the part publishes its hit box in (its reader is not traced)
struct StatuePartLink {
    uint32_t hitBox = 0; // 0xa5a928
};
inline StatuePartLink& partLink() {
    static StatuePartLink s;
    return s;
}

} // namespace od::port::st3c_statue_state
