// The class states of st5c_fx2.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "content/late_game/st5c_fx2.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st5c_fx2_state {

// The explosion's own bytes: the boss it was spawned with (the parameter word), its hit box, the boss, its shock-ring
// spawner, its radius and timer
struct BlastFields : ActorClassState {
    explicit BlastFields(ProjectileActor& actor) : spawnBoss(actor.params, 0), hitBox(actor.hitBox) {}
    BytesField<uint32_t, 4> spawnBoss;
    HitBoxHead& hitBox;
    ActorRef<AnimatedActor> boss;
    ActorRef<EffectActor> shockRings;
    uint16_t radius = 0;
    uint16_t timer = 0;
};

constexpr uint32_t kBallRays = 0x18;

// The visuals' own bytes: the ball they were spawned with (the parameter word), then the block: a timer, a word, the
// rays' angles, timers, lengths and spins, the ball
struct BallFxFields : ActorClassState, BallVisualsParams {
    explicit BallFxFields(StaticActor& actor) : BallVisualsParams(actor), timer(actor.rotation.x), word(actor.rotation.y) {}
    FieldAlias<uint16_t, int16_t> timer;
    FieldAlias<uint16_t, int16_t> word;
    StateArray<uint16_t, kBallRays> angles{};
    StateArray<int8_t, kBallRays> timers{};
    StateArray<uint8_t, kBallRays> lengths{};
    StateArray<int8_t, kBallRays> spins{};
    ActorRef<ProjectileActor> ball;

    void clearBlock() override {
    }
};

// The dust's own bytes: its spawn bytes, four random dwords
struct LandingDustFields : ActorClassState, LandingDustParams {
    explicit LandingDustFields(EffectActor& actor) : LandingDustParams(actor) {}
    union {
        StateArray<uint32_t, 4> randoms{};
        StateArray<uint16_t, 8> randomHalves;
    };
};

constexpr uint32_t kBeamSegments = 0x18;

// The beam's own bytes: the boss it was spawned with (its parameter word), its loop sound's handle, then its block: the
// timer, the hit box, the boss, the segments
struct BeamFields : ActorClassState {
    explicit BeamFields(ProjectileActor& actor) : spawnBoss(actor.params, 0), loopHandle(actor.params[3]), timer(actor.wallHit, 0) {}
    BytesField<uint32_t, 4> spawnBoss;
    uint8_t& loopHandle;
    PartField<uint16_t, uint32_t> timer;
    HitBoxHead hitBox;
    ActorRef<AnimatedActor> boss;
    StateArray<ActorRef<ProjectileActor>, kBeamSegments> segments{};
};

} // namespace od::port::st5c_fx2_state
