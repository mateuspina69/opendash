// The class states of st12_14.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"

namespace od::port::st12_14_state {

struct TumbleFields : ActorClassState {
    explicit TumbleFields(StaticActor& actor) : facingBits(actor.params[0]), rockAmplitude(actor.rotation.spare), hitBox(actor.hitBox), active(actor.gunshipTimer()) {}
    uint8_t& facingBits; // the spawn's facing: bit 0 a half turn, bit 1 a quarter turn
    int16_t& rockAmplitude; // the rotation's spare lane: the rocking's step (its sign alternates)
    HitBoxHead& hitBox;
    uint8_t& active;
    uint16_t facing = 0;
    int16_t slideLeft = 0;
    uint8_t timer = 0;

    void clearBlock() override {
        facing = 0;
        slideLeft = 0;
        timer = 0;
    }
};

// The dust cloud's spawn parameters (the stage's controller type 0xf; the class is not ported: the names are the
// spawner's, hypotheses), several in the header's sub-state / step bytes
struct BlockDustParams : ActorClassState {
    explicit BlockDustParams(ControllerActor& actor) : angle(actor.subState), rate(actor.step), fade(actor.stepCounter), size(actor.params, 0), life(actor.params[2]), spread(actor.params[3]), owner(actor.recordPoint.spare) {}
    uint8_t& angle;
    uint8_t& rate;
    uint8_t& fade;
    BytesField<uint16_t, 4> size;
    uint8_t& life;
    uint8_t& spread;
    FieldAlias<uint16_t, int16_t> owner; // the block's slot
    ShortVec3 point;
};

struct WalkerFields : ActorClassState {
    explicit WalkerFields(AnimatedActor& actor) : contact(actor.hopperBytes[1]), mode(actor.hopperBytes[2]) {}
    uint8_t& contact; // kWalkerBounced / kWalkerBlocked of this tick
    uint8_t& mode; // kWalker* behaviour bits
    HitBox hitBox; // its contact.x: the hit's direction
    uint16_t blinkTicks = 0;
    uint16_t knockYaw = 0;
    uint16_t vanishTicks = 0;

    void clearBlock() override {
        hitBox = {};
        blinkTicks = 0;
        knockYaw = 0;
        vanishTicks = 0;
    }
};

struct BoulderFields : ActorClassState {
    explicit BoulderFields(StaticActor& actor) : companionSlot(actor.stepCounter), endCellX(actor.params[0]), endCellZ(actor.params[1]), modelVariant(actor.params[3]), pathYaw(actor.rotation.spare), returning(actor.velocity.spare), hitBox(actor.hitBox) {}
    uint8_t& companionSlot; // the companion's slot id (in the header's step counter byte)
    FieldAlias<int8_t, uint8_t> endCellX; // the far point's cells from the spawn point
    FieldAlias<int8_t, uint8_t> endCellZ;
    uint8_t& modelVariant; // its model; non-zero: it stays put (the companion)
    int16_t& pathYaw; // the rotation's spare lane: the heading it rolls along
    FieldAlias<uint16_t, int16_t> returning; // the velocity's spare lane: rolling back to the spawn point
    HitBoxHead& hitBox;
    int16_t startX = 0;
    int16_t startZ = 0;
    int16_t endX = 0;
    int16_t endZ = 0;

    void clearBlock() override {
        startX = 0;
        startZ = 0;
        endX = 0;
        endZ = 0;
    }
};

struct TurretFields : ActorClassState {
    explicit TurretFields(AnimatedActor& actor)
        : unused(actor.params[1]), bursts(actor.params[2]), startKey(actor.params[3]) {}
    uint8_t& unused;                 // cleared at the init (its reader is not traced)
    uint8_t& bursts;                 // the bursts fired
    FieldAlias<int8_t, uint8_t> startKey; // the next animation's first key
    uint16_t flags = 0;              // kTurret*
    uint16_t timer = 0;
    uint32_t playerDistance = 0;     // planar, from its bone 1
    int16_t playerYaw = 0;           // the player's direction relative to its yaw
    int16_t playerPitch = 0;
    int16_t headYaw = 0;
    int16_t headPitch = 0;
    HitBoxHead bodyBox;
    HitBoxHead barrelBox;

    // flag bits of the low byte set / tested as the exe does, a byte store
    void setFlagBits(uint8_t bits) { flags = uint16_t((flags & 0xff00u) | uint8_t(uint8_t(flags) | bits)); }
    uint8_t flagByte() const { return uint8_t(flags); }
    // the head's yaw and pitch cleared (the exe's dword store)
    void clearHead() {
        headYaw = 0;
        headPitch = 0;
    }

    void clearBlock() override {
        flags = 0;
        timer = 0;
        playerDistance = 0;
        playerYaw = 0;
        playerPitch = 0;
        headYaw = 0;
        headPitch = 0;
        bodyBox = {};
        barrelBox = {};
    }
};

} // namespace od::port::st12_14_state
