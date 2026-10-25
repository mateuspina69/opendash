// The class states of blob_boss.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/field_alias.hpp"

namespace od::port::blob_boss_state {

// The boss's working block (0x40 bytes at +0x40 of its bone block 0)
struct BlobFightBlock {
    HitBoxHead bodyHit;   // the prop's point
    HitBoxHead reachHit;  // the capsule from the body's top
    uint32_t stats = 0;   // its type's row of its spawn byte (exe data)
    int16_t bodyYaw = 0;
    int16_t wanderYaw = 0;    // the small form's heading
    int16_t queuedDamage = 0; // damage others queue on it (drained 0x20 a tick)
    int16_t damageTaken = 0;  // since the last attack
    int16_t height = 0;       // the body's height (base height * the y scale, at least 0x40)
    int16_t baseHeight = 0;
    int16_t topHeight = 0;    // the y scale << 7 >> 9: where its top is
    int16_t lastAimYaw = 0;   // the aim yaw of the last tick
    int16_t shots = 0;        // the ring's shots fired / the lobs left
    int16_t ringRounds = 0;   // the rings of the second phase before a spin
    int16_t farTicks = 0;     // the player kept far along z
    int16_t phaseTicks = 0;   // the second phase's ticks before a spin
    uint16_t messageTicks = 0;
    uint8_t messageStep = 0;  // the tutorial's message step
    StateArray<uint8_t, 0x11> unread{}; // cleared with the area (no reader)

    AnimatedActorStats statsRow() const { return animatedStatsAt(stats); }
};

// Its own fields: its colour (the tint's RGB555 components) and its pulse counter (spawn parameters), the prop, the CLUT
// animation, the working block, the flags, the timers and the aim
struct BlobFields : ActorClassState {
    explicit BlobFields(AnimatedActor& actor) : red(actor.params[0]), green(actor.params[1]), blue(actor.params[2]), pulse(actor.params[3]), flags(actor.bossFlags) {}
    FieldAlias<int8_t, uint8_t> red;
    FieldAlias<int8_t, uint8_t> green;
    FieldAlias<int8_t, uint8_t> blue;
    uint8_t& pulse;
    uint32_t& flags; // kBlob*
    ActorRef<StaticActor> prop;
    uint32_t clutEffect = 0; // the pool-0xa0 CLUT animation's address
    BoneBlockArea<BlobFightBlock> work;
    int16_t timer = 0;
    int16_t counter = 0; // the lob's lead / the sequences' ticks
    uint32_t aimWord = 0; // the aim yaw and pitch as the exe copies them (one dword)
    int16_t topYaw = 0; // from the player to its top
    int16_t topPitch = 0;
    int16_t aimDistance = 0;
    int16_t reach = 0; // the capsule's length (the x scale * 0x160)
    PartField<int16_t, uint32_t> aimYaw{aimWord, 0}; // from the player to its bone 0
    PartField<int16_t, uint32_t> aimPitch{aimWord, 2};
    void clearOwnBytesBytes() {
        timer = 0;
        counter = 0;
        aimWord = 0;
        topYaw = 0;
        topPitch = 0;
        aimDistance = 0;
        reach = 0;
        flags = 0;
    }

    void clearBlock() override {
        prop = nullptr;
        clutEffect = 0;
        work = nullptr;
        timer = 0;
        counter = 0;
        aimWord = 0;
        topYaw = 0;
        topPitch = 0;
        aimDistance = 0;
        reach = 0;
    }
};

} // namespace od::port::blob_boss_state
