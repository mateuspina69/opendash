// The ST22 fire projectiles exe 0x5c2a60 (see st22_fire_shots.hpp), transcribed from dash2.exe one function per exe
// function. Shadows (exe 0x5f20f0) are render output and left out.
#include "content/calbania/st22_fire_shots.hpp"
#include "content/calbania/st22_fire_shots_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/address_forms.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st22_fire_shots_state;

namespace {

using address_forms::E;

int16_t t16(int v) { return int16_t(uint16_t(uint32_t(v))); }

enum class FireShotState : uint8_t { Init = 0, Fly = 1, End = 2 };
enum class FireShotVariant : uint8_t { Fireball = 0, HomingLob = 1, Ember = 2, RisingBomb = 3, Shard = 4 };
enum class FireballSub : uint8_t { Launch = 0, Bounce = 1, Fly = 2 };
enum class FlightSub : uint8_t { Launch = 0, Fly = 1 };

constexpr uint32_t kFireShotBoxSize = 0xc;
const DataTable<StridedScalarTable<int16_t, 4, 4>> fireballBounceSpeeds{TableId::St22FireShotsFireballBounceSpeeds}; // the fireball's vy after each bounce
constexpr int8_t kFireballFrames = 5;
constexpr int8_t kFireballBounces = 2;
constexpr uint32_t kHitTaken = 0x10000; // the hit box's result: the shot hit something
constexpr int16_t kGravity = 0x20;
constexpr uint8_t kFlameEffectType = 1;  // the stage's effect type 1 (ST22 0x42dde0): a flame
constexpr uint8_t kSmokeEmitterType = 3, kSmokeEmitterVariant = 3;
constexpr uint8_t kFireShotType = 1;
constexpr uint16_t kBurstSound = 0x8b;
constexpr int kEmbers = 8, kShards = 0x10;

// exe 0x5c3470(point, colour, size, abr): a 0x2c billboard of the flame cell (uv 0x20..0x3f, CLUT 0x7c50, tpage abr)
void flameBillboard(const int16_t p[3], uint32_t colour, uint32_t size, uint32_t abr, uint32_t owner) {
    dc::emit(
             {0, colour, (uint32_t(uint16_t(p[1])) << 16) | uint16_t(p[0]), 0x7c502020u, uint32_t(int32_t(p[2])),
              ((abr & 3) << 0x15) | 0xf203fu, size, 0x3f20u, 0, 0x3f3fu},
             owner, false);
}
// exe 0x5c34f0: the fireball's 6-frame billboard (the frame counts 5..0)
void fireballBillboard(ProjectileActor& actor, FireShotFields& f) {
    const uint32_t frame = uint32_t(int32_t(f.frame)) * 0x20;
    dc::emit(
             {0, 0x2c808080u, (uint32_t(uint16_t(actor.pos.y.whole)) << 16) | uint16_t(actor.pos.x.whole),
              frame | 0x7c926000u, uint32_t(int32_t(actor.pos.z.whole)), (frame + 0x1f) | 0xf6000u, 0x20,
              frame | 0x7f00u, 0, (frame + 0x1f) | 0x7f00u},
             actor.at, false);
    const int8_t n = int8_t(uint8_t(f.frame) - 1u);
    f.frame = n < 0 ? kFireballFrames : n;
}
// exe 0x5c3600: a hit spark (pool 0xa0 global type 0, size 0x80; exe 0x5e5400's spark)
void hitSpark(const int16_t p[3]) { spawnHitSpark(kActorSweepGroup2, p[0], p[1], p[2]); }
// the stage's flame effect's colour (its parameters)
struct FlameEffectFields {
    explicit FlameEffectFields(const EffectActor& actor) : FlameEffectFields(const_cast<EffectActor&>(actor)) {}
    explicit FlameEffectFields(EffectActor& actor) : red(actor.params[0]), green(actor.params[1]), blue(actor.params[2]) {}
    uint8_t& red;
    uint8_t& green;
    uint8_t& blue;
};
// exe 0x5c3640: a stage type-1 flame effect (ST22 0x42dde0) of colour rgb (0xRRGGBB)
void flameEffect(const int16_t p[3], uint32_t rgb) {
    const uint32_t address = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    FlameEffectFields colour(e);
    e.flags = kActorLive;
    e.loopFlags |= kActorSweepGroup2;
    e.type = kFlameEffectType;
    e.variant = 0;
    e.pos.x = p[0];
    e.pos.y = p[1];
    e.pos.z = p[2];
    colour.blue = uint8_t(rgb & 0xff);
    colour.red = uint8_t((rgb >> 16) & 0xff);
    colour.green = uint8_t((rgb >> 8) & 0xff);
}
// exe 0x5c3590(actor, -, rate) with exe 0x5c35d0: the yaw turned toward the player by rate
void turnToPlayer(ProjectileActor& actor, int rate) {
    const uint32_t to = uint32_t(angleOf(player().pos, actor.pos)); // exe 0x5e3bc0
    int32_t d = int32_t((to & 0xfff) - (uint32_t(int32_t(actor.rotation.y)) & 0xfff));
    if (uint32_t(d + 0x800) > 0x1000) d = ~d;
    if (d > rate) actor.rotation.y += uint32_t(rate);
    if (d < -rate) actor.rotation.y -= uint32_t(rate);
}
void integerPosition(const ProjectileActor& actor, int16_t p[3]) {
    p[0] = actor.pos.x.whole;
    p[1] = actor.pos.y.whole;
    p[2] = actor.pos.z.whole;
}
// the velocity (exe 0x5e3690), then the gravity added
void moveWithGravity(ProjectileActor& actor) {
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
    actor.velocity.y += uint16_t(actor.acceleration.y);
}
uint32_t hitOwnerOf(const ProjectileActor& actor, uint32_t shape) { return (uint32_t(actor.slot) << 24) | shape; }

// exe 0x5c2b10: the bouncing fireball
void fireball(ProjectileActor& actor, FireShotFields& f) {
    switch (FireballSub(actor.subState)) {
    case FireballSub::Launch:
        actor.subState = uint8_t(FireballSub::Bounce);
        actor.velocity.x = 0;
        actor.velocity.z = int16_t(0xfd00);
        actor.acceleration.y = kGravity;
        f.phase = 0;
        [[fallthrough]];
    case FireballSub::Bounce:
        actor.subState = uint8_t(FireballSub::Fly);
        actor.velocity.y = fireballBounceSpeeds[size_t(int32_t(f.phase))];
        [[fallthrough]];
    case FireballSub::Fly:
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        actor.velocity.y += uint16_t(actor.acceleration.y);
        break;
    default: break;
    }
    if (int32_t(actorTestMove(*E, actor)) < 0) { // exe 0x40f570
        const uint32_t r = actorPushOut(*E, actor); // exe 0x410960
        if (reflectRotationOffWalls(actor.rotation, r) != 0) { // exe 0x410740
            actor.velocity.z = int16_t(actor.velocity.z >> 2);
            const int8_t n = int8_t(uint8_t(f.phase) + 1u);
            f.phase = n;
            if (n > kFireballBounces) {
                actor.enterState(uint8_t(FireShotState::End));
                return;
            }
            actor.subState = uint8_t(FireballSub::Bounce);
        }
    }
    if (f.hitBox.result & kHitTaken) {
        actor.enterState(uint8_t(FireShotState::End));
        return;
    }
    int16_t p[3];
    integerPosition(actor, p);
    hitListSubmit(p, f.hitBox, hitOwnerOf(actor, 0x604008u), 0x100008u); // exe 0x5e4120
    f.keepHitPoint(actor);
    fireballBillboard(actor, f);
}
// exe 0x5c2c40: the homing lob at the player
void homingLob(ProjectileActor& actor, FireShotFields& f) {
    if (FlightSub(actor.subState) == FlightSub::Launch) {
        actor.subState = uint8_t(FlightSub::Fly);
        f.phase = 0;
        actor.velocity.x = 0;
        const int32_t d = planarDistance(player().pos, actor.pos); // exe 0x5e3b00
        int32_t n = (d + ((d >> 31) & 0x1ff)) >> 9;
        if (n == 0) n = 1;
        actor.velocity.z = int16_t(0xe000);
        actor.acceleration.y = kGravity;
        actor.velocity.y = int16_t(uint32_t(
            (int32_t(player().pos.y.whole - actor.pos.y.whole) * 16 - ((n * n * 0x20) >> 1)) / n));
    }
    soundUpdateLoop(f.loopSound, actor.pos, 0); // exe 0x5d55c0
    turnToPlayer(actor, 1);
    moveWithGravity(actor);
    const uint32_t r = actorTestMove(*E, actor);
    if (int32_t(r) < 0) {
        queueBreakableWallHit(r, 0x81); // exe 0x58d4e0
        if (actorPushOut(*E, actor) != 0) {
            actor.enterState(uint8_t(FireShotState::End));
            return;
        }
    }
    if (f.hitBox.result & kHitTaken) {
        actor.enterState(uint8_t(FireShotState::End));
        return;
    }
    int16_t pa[3], pb[3] = {actor.prevPos.x.whole, actor.prevPos.y.whole, actor.prevPos.z.whole};
    integerPosition(actor, pa);
    hitListSubmitCapsule(pa, 0x60, pb, 0x60, f.hitBox, hitOwnerOf(actor, 0xc05000u), 0x100040u); // exe 0x5e4190
    f.keepHitPoint(actor);
    flameBillboard(pa, 0x2e0000ffu, 0x180, 0, actor.at);
    const int16_t s = t16(int32_t(int32_t(sin64(uint32_t(int32_t(f.phase)))) << 8) >> 12);
    pa[0] = t16(actor.pos.x.whole + s);
    pa[1] = t16(s + actor.pos.y.whole);
    pa[2] = actor.pos.z.whole;
    flameBillboard(pa, 0x2e0080ffu, 0x60, 1, actor.at);
    pa[0] = actor.pos.x.whole;
    pa[1] = t16(actor.pos.y.whole - s);
    pa[2] = t16(actor.pos.z.whole + s);
    flameBillboard(pa, 0x2e0080ffu, 0x60, 1, actor.at);
    f.phase = int8_t((uint8_t(f.phase) + 4u) & 0x3fu);
    pa[0] = actor.pos.x.whole;
    pa[1] = t16(actorGround(*E, actor)); // exe 0x40f190
    pa[2] = t16(actor.pos.z.whole - 0x50);
    // exe 0x5e6240(position): the ground kind under it, result unused
    flameEffect(pa, 0xff4040);
}
// exe 0x5c2ea0: an ember (a random arc)
void ember(ProjectileActor& actor, FireShotFields& f) {
    if (FlightSub(actor.subState) == FlightSub::Launch) {
        actor.subState = uint8_t(FlightSub::Fly);
        actor.velocity.x = 0;
        actor.velocity.y = int16_t((0xffffffe0u - (rngStep() & 7)) << 4);
        const uint32_t r2 = rngStep();
        actor.acceleration.y = kGravity;
        actor.velocity.z = int16_t(uint32_t(-int32_t(r2 & 0xf)) << 4);
        actor.rotation.y = int16_t((rngStep() & 0xf) << 8);
        const uint32_t r4 = rngStep();
        f.unreadD = 0;
        f.phase = int8_t(r4 & 0x1f);
    }
    int16_t p[3];
    integerPosition(actor, p);
    flameBillboard(p, 0x2e40c0ffu, uint32_t(int32_t(f.phase) + 0x18), 1, actor.at);
    moveWithGravity(actor);
    const uint32_t r = actorPushOut(*E, actor); // exe 0x410960
    if (reflectRotationOffWalls(actor.rotation, r) != 0) {
        actor.enterState(uint8_t(FireShotState::End));
        return;
    }
    f.keepHitPoint(actor);
    flameBillboard(p, 0x2e4040ffu, uint32_t(int32_t(f.phase) + 0x30), 1, actor.at);
}
// the smoke emitter that rides the rising bomb (the static stage type 3): the actor it follows
struct RidingSmokeFields {
    explicit RidingSmokeFields(const StaticActor& actor) : RidingSmokeFields(const_cast<StaticActor&>(actor)) {}
    explicit RidingSmokeFields(StaticActor& actor) : rider(actor.hitBox.result) {}
    uint32_t& rider;
};
// exe 0x5c3020: the rising bomb
void risingBomb(ProjectileActor& actor, FireShotFields& f) {
    if (FlightSub(actor.subState) == FlightSub::Launch) {
        actor.subState = uint8_t(FlightSub::Fly);
        f.phase = 0;
        actor.velocity.x = 0;
        actor.velocity.y = 0;
        actor.velocity.z = int16_t(0xf800);
        integrateVelocity(actor, 0, 0, -0x800);
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(0xfd00);
        actor.velocity.z = int16_t(0xfb80);
        actor.acceleration.y = kGravity;
        if (const uint32_t address = allocActor60(kActorStageClass)) { // exe 0x5e12f0: the smoke emitter riding it
            StaticActor& smoke = staticActorAt(address);
            smoke.type = kSmokeEmitterType;
            smoke.variant = kSmokeEmitterVariant;
            smoke.flags = kActorLive;
            smoke.loopFlags |= kActorSweepGroup2;
            smoke.pos.x = actor.pos.x;
            smoke.pos.y = actor.pos.y;
            RidingSmokeFields(smoke).rider = actor.at;
            smoke.pos.z = actor.pos.z;
        }
    }
    moveWithGravity(actor);
    const uint32_t r = actorPushOut(*E, actor);
    if (reflectRotationOffWalls(actor.rotation, r) == 0 && actor.velocity.y <= 0x300 && !(f.hitBox.result & kHitTaken)) {
        int16_t p[3];
        integerPosition(actor, p);
        hitListSubmit(p, f.hitBox, hitOwnerOf(actor, 0xc04020u), 0x20008u);
        f.keepHitPoint(actor);
        flameBillboard(p, 0x2e0000ffu, 0x80, 0, actor.at);
        return;
    }
    actor.enterState(uint8_t(FireShotState::End));
}
// exe 0x5c3170: a shard (a random spread)
void shard(ProjectileActor& actor, FireShotFields& f) {
    if (FlightSub(actor.subState) == FlightSub::Launch) {
        actor.subState = uint8_t(FlightSub::Fly);
        f.phase = 0;
        actor.velocity.x = 0;
        actor.velocity.y = 0x400;
        actor.velocity.z = int16_t(uint32_t(int32_t(rngStep() & 0xf) * -0x80));
        actor.rotation.y += uint32_t((int32_t(rngStep() & 0xf) - 7) * 0x40);
    }
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
    const uint32_t r = actorPushOut(*E, actor);
    if (reflectRotationOffWalls(actor.rotation, r) != 0 || (f.hitBox.result & kHitTaken)) {
        actor.enterState(uint8_t(FireShotState::End));
        return;
    }
    int16_t p[3];
    integerPosition(actor, p);
    hitListSubmit(p, f.hitBox, hitOwnerOf(actor, 0xc04008u), 0x20001u);
    f.keepHitPoint(actor);
    flameBillboard(p, 0x2e0000ffu, 0x20, 0, actor.at);
}
// a fire shot of variant v at the actor's position (the end's embers and shards)
ProjectileActor* spawnFireShot(const ProjectileActor& actor, FireShotVariant v) {
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return nullptr;
    ProjectileActor& e = projectileActorAt(address);
    e.flags = kActorLive;
    e.loopFlags |= kActorSweepGroup2;
    e.type = kFireShotType;
    e.variant = uint8_t(v);
    e.pos.x = actor.pos.x;
    e.pos.y = actor.pos.y;
    e.pos.z = actor.pos.z;
    return &e;
}
// exe 0x5c32b0: the end, jump table 0x5c345c by the variant
void fireShotEnd(ProjectileActor& actor) {
    int16_t p[3];
    integerPosition(actor, p);
    switch (FireShotVariant(actor.variant)) {
    case FireShotVariant::Fireball:
    case FireShotVariant::Shard: hitSpark(p); break;
    case FireShotVariant::HomingLob:
        soundPlayAtPos(kBurstSound, actor.pos); // exe 0x5d5380
        for (int k = 0; k < kEmbers; ++k) {
            spawnFireShot(actor, FireShotVariant::Ember);
            int16_t q[3];
            q[0] = t16(int32_t(((rngStep() & 0x1f) - 0xf) << 5) + actor.pos.x.whole);
            q[1] = actor.pos.y.whole;
            q[2] = t16(int32_t(((rngStep() & 0x1f) - 0xf) << 5) + actor.pos.z.whole);
            hitSpark(q);
        }
        break;
    case FireShotVariant::RisingBomb:
        hitSpark(p);
        for (int k = 0; k < kShards; ++k)
            if (ProjectileActor* e = spawnFireShot(actor, FireShotVariant::Shard)) e->rotation = actor.rotation;
        break;
    default: break; // the ember and past the table: freed only
    }
    freeActor(actor); // exe 0x5e1500
}
// exe 0x5c2a60's body
void fireShot(ProjectileActor& actor) {
    FireShotFields& f = classState<FireShotFields>(actor);
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    switch (FireShotState(actor.state)) {
    case FireShotState::Init: // exe 0x5c2aa0
        actor.enterState(uint8_t(FireShotState::Fly));
        actor.collisionBox.pointAt(uint32_t(actor.variant) * kFireShotBoxSize + stateAddress(TableId::St22FireShotsFireShotBoxes));
        f.frame = kFireballFrames;
        f.hitBox.previousX = actor.pos.x.whole;
        f.hitBox.previousZ = actor.pos.z.whole;
        f.hitBox.result = 0;
        return;
    case FireShotState::Fly: // exe 0x5c2af0: by the variant [0x8d0f68]
        switch (FireShotVariant(actor.variant)) {
        case FireShotVariant::Fireball: fireball(actor, f); return;
        case FireShotVariant::HomingLob: homingLob(actor, f); return;
        case FireShotVariant::Ember: ember(actor, f); return;
        case FireShotVariant::RisingBomb: risingBomb(actor, f); return;
        case FireShotVariant::Shard: shard(actor, f); return;
        default: notPortedAt(kStoryPort, "0x5c2a60 variant (table 0x8d0f68)", actor.variant);
        }
    case FireShotState::End: fireShotEnd(actor); return;
    default: notPortedAt(kStoryPort, "0x5c2a60 state (table 0x8d0f5c)", actor.state);
    }
}

} // namespace

void st22FireShotUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    fireShot(projectileActorAt(actor));
}

} // namespace od::port
