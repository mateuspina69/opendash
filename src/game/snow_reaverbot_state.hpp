// The class states of snow_reaverbot.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"

namespace od::port::snow_reaverbot_state {

// The snow reaverbot's own fields: its spawn parameter and its class block
struct SnowReaverbotFields : ActorClassState {
    explicit SnowReaverbotFields(AnimatedActor& actor) : spawnKind(actor.params[0]), paceDistance(actor.params, 0) {}
    FieldAlias<int8_t, uint8_t> spawnKind;    // sub 0: its wake range's row; kSnowPlacedAwake: placed awake
    BytesField<uint16_t, 4> paceDistance;     // sub 4: the pace's length (<< 4)
    HitBox hitBox;                            // its contact output's bytes are the fields below
    int16_t& targetYaw = hitBox.contact.x;
    PartField<int8_t, int16_t> hits{hitBox.contact.y, 0};        // the hits counted (< 0: the stagger's cool-down)
    PartField<uint8_t, int16_t> bits{hitBox.contact.y, 1};       // kSnow*
    int16_t& timer = hitBox.contact.z;
    PartField<uint8_t, int16_t> hitEntryMode{hitBox.contactKind, 0}; // 0: it hurts; else a harmless entry (dead)
    uint16_t neighbour = 0;                   // game/enemy_neighbours
    ShortVec3 home;                           // sub 4: its spawn point ...
    int16_t homeYaw = 0;                      // ... and its spawn yaw

    void clearBlock() override {
        hitBox = {};
        neighbour = 0;
        home = {};
        homeYaw = 0;
    }
};

} // namespace od::port::snow_reaverbot_state
