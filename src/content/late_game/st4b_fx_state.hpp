// The class states of st4b_fx.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st4b_fx_state {



// The shot's own bytes: its spawn damage, its hit box, its timer and its damage
struct VolleyFields : ActorClassState {
    explicit VolleyFields(ProjectileActor& actor) : spawnDamage(actor.params, 0), hitBox(actor.hitBox), timer(actor.debrisFlags) {}
    BytesField<int16_t, 4> spawnDamage;
    HitBoxHead& hitBox;
    uint32_t& timer;
    uint32_t damage = 0;
};

// The piece's own bytes: its hit box and the yaw it moves along
struct PieceFields : ActorClassState {
    explicit PieceFields(StaticActor& actor) : hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
    int16_t travelYaw = 0;

    void clearBlock() override {
        travelYaw = 0;
    }
};

constexpr int kTrailPoints = 0x1a;

// The trail's own bytes: the eye point, then (the slot's body) the ring of its past positions
struct EyeTrailFields : ActorClassState {
    explicit EyeTrailFields(StaticActor& actor) : length(actor.rotation.x), head(actor.rotation.y) {}
    int16_t& length;
    int16_t& head;
    // the boss's eye point it follows (in the boss's work block; its spawn parameter word is the point's address):
    // the point and, in its 4th lane, the word that ends the trail
    const ShortVec4* eye = nullptr;
    StateArray<int16_t, kTrailPoints> xs{};
    StateArray<int16_t, kTrailPoints> ys{};
    StateArray<int16_t, kTrailPoints> zs{};

    void clearBlock() override {
        for (size_t i = 20; i < 26; ++i) zs[i] = {};
    }
};

} // namespace od::port::st4b_fx_state
