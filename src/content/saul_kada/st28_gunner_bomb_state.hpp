// The class states of st28_gunner_bomb.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st28_gunner_bomb_state {

// A flame seed (4 bytes): its shrink delay, its size, its phase
struct FlameSeed {
    uint16_t delay = 0;
    uint8_t size = 0;
    uint8_t phase = 0;
};

// The bomb's own bytes: its hit box, its timer, its five flame seeds
struct BombFields : ActorClassState {
    explicit BombFields(ProjectileActor& actor) : hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
    int16_t timer = 0;
    StateArray<FlameSeed, 5> seeds{};
};

} // namespace od::port::st28_gunner_bomb_state
