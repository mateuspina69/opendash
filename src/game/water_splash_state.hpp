// The water splash's state (pool 0x60 type 0x48, a stage class: content/nino/water runs it): what its spawners write
// (the player's splashes and spray, game/player/player_effects; the bubble bursts, content/nino) and what the class
// keeps. Its parameters: the particles' size mask, life and count, the speed (the velocity's z); its source (the
// variant >> 6: its own position, an actor, or a point with an offset -- the source over the position's first word,
// the offset over its y / z words as the exe lays them); the particles spawned and alive and their life; the 8
// particles (an s16 point, its life in the spare lane; 0x40 bytes on, its rotation, its size in the spare lane).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port {

struct WaterSplashFields : ActorClassState {
    explicit WaterSplashFields(StaticActor& actor)
        : sizeMask(actor.params[0]), particleLife(actor.params[1]), count(actor.params[2]), speed(actor.velocity.z),
          offsetY(actor.pos.y.frac), offsetX(actor.pos.y.whole), offsetZ(actor.pos.z.whole) {}
    FieldAlias<int8_t, uint8_t> sizeMask;
    FieldAlias<int8_t, uint8_t> particleLife;
    FieldAlias<int8_t, uint8_t> count;
    int16_t& speed;
    ActorRef<Actor> followed;           // the source actor (variant 1 << 6)
    const ShortVec3* point = nullptr;   // the source point (variant 2 << 6: a bone point of the player)
    FieldAlias<int16_t, uint16_t> offsetY; // the point's offset (the x offset is +0x16, the y offset +0x14: as the exe)
    int16_t& offsetX;
    int16_t& offsetZ;
    int8_t spawned = 0;
    uint8_t alive = 0;
    uint8_t life = 0;
    StateArray<ShortVec4, 8> particlePoints{};
    StateArray<ShortVec4, 8> particleRotations{};

    // one particle: its point and life, its rotation and size
    struct Particle {
        ShortVec4& point;
        int16_t& life; // < 0: gone
        ShortVec4& rotation;
        int16_t& size;
    };
    struct Particles {
        WaterSplashFields& f;
        Particle operator[](size_t k) const {
            ShortVec4& p = f.particlePoints[k];
            ShortVec4& r = f.particleRotations[k];
            return {p, p.spare, r, r.spare};
        }
        static constexpr size_t size() { return 8; }
    } particles{*this};
};

} // namespace od::port
