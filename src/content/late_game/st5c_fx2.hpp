// ST5C: the remaining attack classes of the boss 0x50b9c0 (content/late_game/st5c, content/late_game/st5c_fx): attack
// 7's beam (pool 0x40 type 0x2b, exe 0x5cf370), attack 10's ball visuals (static type 0x5e, exe 0x5bd320), the ball's
// explosion (pool 0x40 type 2, exe 0x5c3690) and the rock effects of attack 9 (pool 0xa0 type 0x24, exe 0x43ff80
// variants 3 0x440d40 / 4 0x4416a0, routed by content/late_game/st5c's dust class). See content/late_game/st5c_fx2.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

// the rock effects' class (pool 0xa0 stage type 0x24) and its variants of the rocks
constexpr uint8_t kRockEffectType = 0x24, kWarningRingVariant = 3, kLandingDustVariant = 4;
// The warning ring's spawn word: the rock it warns of (its report byte ends the hold)
struct WarningRingParams : ActorClassState {
    explicit WarningRingParams(ActorHeader&) {}
    ActorRef<AnimatedActor> rock;
    // the rock is the exe's parameter word (its identity word)
    HeadPointer headPointer() const override { return HeadPointer::toSlot(kHeadParamsWord, rock ? rock->at : 0); }
    void clearHeadPointer() override { rock = nullptr; }
};
// The landing dust's spawn bytes: its rise speed, its brake, its spread (s8), its size (s8), its radius
struct LandingDustParams {
    explicit LandingDustParams(const EffectActor& actor) : LandingDustParams(const_cast<EffectActor&>(actor)) {}
    explicit LandingDustParams(EffectActor& actor) : rise(actor.params[0]), brake(actor.params[1]), spread(actor.params[2]), size(actor.params[3]), radius(actor.posSpare) {}
    uint8_t& rise;
    uint8_t& brake;
    FieldAlias<int8_t, uint8_t> spread;
    FieldAlias<int8_t, uint8_t> size;
    int16_t& radius;
};
// the ball visuals' spawn word: the ball they follow
struct BallVisualsParams {
    explicit BallVisualsParams(const StaticActor& actor) : BallVisualsParams(const_cast<StaticActor&>(actor)) {}
    explicit BallVisualsParams(StaticActor& actor) : spawnBall(actor.params, 0) {}
    BytesField<uint32_t, 4> spawnBall;
};
constexpr uint8_t kBallVisualsType = 0x5e, kBallExplosionType = 2, kBeamType = 0x2b;

void bossBeamUpdate(const PlayerEnv& env, uint32_t actor);          // exe 0x5cf370
void ballVisualsUpdate(const PlayerEnv& env, uint32_t actor);   // exe 0x5bd320
void ballExplosionUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x5c3690
// exe 0x43ff80's [0x88d998] variants 3 / 4 (called by content/late_game/st5c's dust class)
void rockWarningRingUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x440d40
void rockLandingDustUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x4416a0

} // namespace od::port
