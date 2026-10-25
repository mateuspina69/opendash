// The class states of nino_ruin_spawners.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"

namespace od::port::nino_ruin_spawners_state {

// The spawners' fields: the bubbles' countdown, its limit and the entry it spawns from; the alive count of the script's
// slot (ST36); the trap (ST37): its pull strength and range, its point, the bursts' sound ticks, the room actor 2
// woken, the flag 0x599 set
struct RuinSpawnerFields : ActorClassState {
    explicit RuinSpawnerFields(ControllerActor& actor) : pullStrength(actor.params, 0), pullRange(actor.params, 2), scriptAlive(actor.params[3]) {}
    BytesField<int16_t, 4> pullStrength;
    BytesField<int16_t, 4> pullRange;
    uint8_t& scriptAlive;
    ShortVec3 trapPoint;
    int16_t countdown = 0;
    int16_t limit = 0;
    int16_t entry = 0;
    uint16_t soundTicks = 0;
    void clearCountdown() {
        countdown = 0;
        limit = 0;
        entry = 0;
        soundTicks = 0;
    }
};

} // namespace od::port::nino_ruin_spawners_state
