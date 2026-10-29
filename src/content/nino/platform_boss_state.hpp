// The class states of platform_boss.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port::platform_boss_state {

// The bolt (pool 0xe0 stage type 0xf, exe 0x423f40): its parent's pool and index, its length, the spawn's width /
// amplitude parameters and its point (+0x10, where its spawners write it: the orb, ST12's sliding blocks, ST4C's
// pillars; the header's state bytes carry its start angle, its angle step and its fade)
struct BoltFields : ActorClassState {
    explicit BoltFields(ControllerActor& actor) : length(actor.params, 0), widthParam(actor.params[2]), amplitudeParam(actor.params[3]), parent(actor.recordPoint.spare), pos(actor.recordPoint) {}
    BytesField<uint16_t, 4> length;
    FieldAlias<int8_t, uint8_t> widthParam;
    FieldAlias<int8_t, uint8_t> amplitudeParam;
    int16_t& parent;
    ShortVec3& pos;
    uint16_t halfWidth = 0;
    uint16_t amplitude = 0;
    uint8_t fade = 0;
    uint8_t startAngle = 0;
    uint8_t angleStep = 0;
};

// The shots' class block: the hit box, the billboard's spin (the shot) / the life (the orb)
struct ShotFields : ActorClassState {
    explicit ShotFields(ProjectileActor& actor) : fade(actor.params[0]), hit(actor.hitBox) {}
    uint8_t& fade; // the orb's fade-in count (its spawn's life word's low byte)
    HitBoxHead& hit;
    int16_t orbTicks = 0;
    int16_t spin = 0;
};

// The boss's own values: the dive's target x / z (the velocity's and the rotation's spare lanes), its spark points
// (the tail's at +0x40, the head's at +0x48: the acceleration and spin lanes), its class block
struct BossFields : ActorClassState {
    explicit BossFields(AnimatedActor& actor) : diveZ(actor.rotation.spare), diveX(actor.velocity.spare) {}
    int16_t& diveZ;
    int16_t& diveX;
    ShortVec3 tailPoint;
    ShortVec3 headPoint;
    HitBoxHead hit;
    uint16_t ticks = 0;
    uint8_t contact = 0; // kTouched*
    uint8_t mode = 0; // kBoss*
    int8_t waypoint = 0;
    int8_t fromWaypoint = 0;
    uint8_t startKey = 0;
    int8_t sparkKind = 0;
    uint8_t sparkCooldown = 0;

    void clearBlock() override {
        hit = {};
        ticks = 0;
        contact = 0;
        mode = 0;
        waypoint = 0;
        fromWaypoint = 0;
        startKey = 0;
        sparkKind = 0;
        sparkCooldown = 0;
    }
};

// A spark particle (8 bytes): an s16 point and its life (the bursts: its direction)
struct SparkParticle {
    ShortVec3 point;
    int16_t life = 0;
    // x | y << 16 as the exe reads the point's first dword
    uint32_t xyWord() const { return uint32_t(uint16_t(point.x)) | uint32_t(uint16_t(point.y)) << 16; }
};
// The spark's own values: its particles (16 x 8 bytes from +0x40, over the slot's motion fields: the class keeps them
// there), the shots left
struct SparkFields : ActorClassState {
    explicit SparkFields(StaticActor&) {}
    StateArray<SparkParticle, 16> particles{};
    uint8_t shotsLeft = 0;
};

} // namespace od::port::platform_boss_state
