// The class states of st54_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"

namespace od::port::st54_cast_state {

constexpr uint8_t kEmittersSpawned = 0x01;

// The mover's own bytes: its kind (its first parameter), the two emitters it keeps, the position it froze, its spawn
// and ring timers (the low byte of the spawn timer read alone), its ticks, its ring index and its spawned flag
struct MoverFields : ActorClassState {
    explicit MoverFields(AnimatedActor& actor) : kind(actor.params[0]), ringIndex(actor.hopperBytes[0]), flags(actor.hopperBytes[1]) {}
    uint8_t& kind;
    uint8_t& ringIndex;
    uint8_t& flags; // kEmittersSpawned
    ActorRef<StaticActor> emitterA;
    ActorRef<StaticActor> emitterB;
    ShortVec3 frozen;
    uint16_t spawnTimer = 0;
    uint16_t ringTimer = 0;
    int16_t ticks = 0;
    PartField<uint8_t, uint16_t> spawnTimerLow{spawnTimer, 0};

    void clearBlock() override {
        emitterA = nullptr;
        emitterB = nullptr;
        frozen = {};
        spawnTimer = 0;
        ringTimer = 0;
        ticks = 0;
    }
};

} // namespace od::port::st54_cast_state
