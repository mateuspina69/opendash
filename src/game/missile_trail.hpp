// The missile trail (static type 6 of the global table, exe 0x5636b0): a ring of the last 0x1a positions of the actor
// it follows (the player's homing missile, the stages' missiles and shells), drawn as a fading additive ribbon; freed
// once that actor is gone. Moved with the in-stage warp's offset (kept through it), the whole ring follows.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port {

constexpr uint32_t kTrailPoints = 0x1a;

// The trail's fields (its static slot): the actor it follows, then from +0x28 its ring of positions (the newest at
// `head`, `length` of them); the variant's low bits are the ribbon's width
struct MissileTrailFields : ActorClassState {
    explicit MissileTrailFields(StaticActor& actor) : parent(actor.params, 0), length(actor.rotation.x), head(actor.rotation.y) {}
    BytesField<uint32_t, 4> parent; // the actor followed
    int16_t& length;
    int16_t& head;
    StateArray<int16_t, kTrailPoints> xs{};
    StateArray<int16_t, kTrailPoints> ys{};
    StateArray<int16_t, kTrailPoints> zs{};

    void clearBlock() override {
        for (size_t i = 20; i < 26; ++i) zs[i] = {};
    }
};

// exe 0x5636b0 (static type 6): the trail -- started at its actor, then each tick the ring advanced to the actor's
// position (shifted by its own warp offset) and the ribbon drawn; freed once the actor's slot is free
void missileTrailUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
