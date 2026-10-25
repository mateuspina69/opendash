// The burrowing reaverbot of the Manda ruins, class 0x482f50 (see manda_ruin_burrower.hpp).
//
// Each tick: the actor-collision result into the contact bits (exe 0x484880), the previous pose, the state [0x891ffc]
// (0 bind 0x4830f0, 1 the crawler's main 0x483220 (variant 0), 2 the guard's main 0x483c70 (variant 1), 3 carried
// 0x4842b0, 4 dying 0x484510); then (live) the animation, the head's turn added to the decoded frame's part 1, the pose,
// the hit entry at bone 0 (none while carried; harmless while dying; a thrown body's when thrown; a strong one's while
// it leaps / sways).
// The crawler's sub-states [0x892010]: 0 0x4832b0 reset, 1 0x4832f0 crawls until the player is near (behind: it rears
// up and spins round, 2; in front: 3), 2 0x4833a0 rears up and spins round, 3 0x483520 rises (two glow trails) and
// leaps, 4 0x483790 sways toward the player, bouncing off walls, 5 0x483a50 lies stunned, 6 0x483ab0 gets up, 7
// 0x483b10 idle, 8 0x483b30 falls (the ground lost), 9 0x483ba0 knocked over. The guard's [0x892038]: 0 0x483cb0
// reset, 1 0x483cf0 watches, 2 0x483da0 fires (pool 0x40 stage type 0: exe 0x5c2830 on ST12 / ST13) every 0x3c ticks,
// 3 0x483f80 / 4 0x4840c0 hops round, 5 0x484200 knocked back. Carried [0x892050]: 0 0x4842d0 in the hands, 1 0x4843e0
// flying (the first solid hit: a bounce, a spark, its hit points to -1).
//
// A port gap where the exe would read through a null pointer (a missing bone point) or index past a dispatch table.
// Left out (render output): the shadow (exe 0x5f20f0(actor, 0x30, 0x160, 0), not in states 2 / 3).
#include "content/manda/manda_ruin_burrower.hpp"
#include "content/manda/manda_ruin_burrower_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/glow_trails.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/territory.hpp"

namespace od::port {

using namespace manda_ruin_burrower_state;

namespace {

inline constexpr const char* kEnemyPort = "manda ruin burrower port"; // the area of its port-gap errors

enum class BurrowerState : uint8_t {
    Bind = 0,    // exe 0x4830f0
    Crawler = 1, // exe 0x483220 (variant 0)
    Guard = 2,   // exe 0x483c70 (variant 1)
    Carried = 3, // exe 0x4842b0
    Dying = 4,   // exe 0x484510
};
enum class CrawlerSub : uint8_t {
    Reset = 0,   // exe 0x4832b0
    Crawl = 1,   // exe 0x4832f0
    Spin = 2,    // exe 0x4833a0
    Leap = 3,    // exe 0x483520
    Sway = 4,    // exe 0x483790
    Stunned = 5, // exe 0x483a50
    GetUp = 6,   // exe 0x483ab0
    Idle = 7,    // exe 0x483b10
    Fall = 8,    // exe 0x483b30
    Knocked = 9, // exe 0x483ba0
};
enum class GuardSub : uint8_t {
    Reset = 0,     // exe 0x483cb0
    Watch = 1,     // exe 0x483cf0
    Fire = 2,      // exe 0x483da0
    HopWide = 3,   // exe 0x483f80
    HopNarrow = 4, // exe 0x4840c0
    Knocked = 5,   // exe 0x484200
};
enum class CarriedSub : uint8_t { Hands = 0, Flying = 1 }; // exe 0x4842d0 / 0x4843e0
enum class BurrowerVariant : uint8_t { Crawler = 0, Guard = 1 };

// +0x167 (exe 0x484740 / 0x484880)
constexpr uint8_t kBurrowerContactWallX = 0x01, kBurrowerContactWallZ = 0x02, kBurrowerContactLanded = 0x04;
constexpr uint8_t kBurrowerContactGroundLost = 0x08;
constexpr uint8_t kBurrowerContactWalls = kBurrowerContactWallX | kBurrowerContactWallZ;
// +0x168
constexpr uint8_t kBurrowerNoHitEntry = 0x01, kBurrowerNoHurt = 0x02, kBurrowerKeepsFalling = 0x08;
constexpr uint8_t kBurrowerThrown = 0x10, kBurrowerStrong = 0x20, kBurrowerBounces = 0x40, kBurrowerNotTarget = 0x80;

// The shot the guard fires (pool 0x40 stage type 0, exe 0x5c2830: its spawn parameters as the guard writes them)
struct GuardShotParams {
    explicit GuardShotParams(const ProjectileActor& actor) : GuardShotParams(const_cast<ProjectileActor&>(actor)) {}
    explicit GuardShotParams(ProjectileActor& actor) : ticks(actor.params, 0), attack(actor.params[2]) {}
    BytesField<uint16_t, 4> ticks;
    uint8_t& attack;
};

const DataTable<CollisionBoxRecord> burrowerBox{TableId::MandaRuinBurrowerBox};
const DataTable<InteractionBoxRecord> burrowerLiftBox{TableId::MandaRuinBurrowerLiftBox};
constexpr uint16_t kTintHurtBlink = 0x4214;
constexpr uint16_t kSoundLeap = 0x11e, kSoundSway = 0x11f, kSoundHurt = 0x8d;
constexpr int16_t kGravity = 0x40;
constexpr int16_t kHeadSlow = 0x60, kHeadFast = 0x100; // the head's turn a tick
constexpr uint8_t kGuardShotType = 0;
constexpr uint16_t kGuardShotTicks = 0x96;

AnimatedActorStats statsOf(const AnimatedActor& a) { return animatedStats(a.type); }
void toSub(AnimatedActor& a, uint8_t s) { // the sub-state at its step 0 (the exe's byte store at +9, word store at +0xa)
    a.subState = s;
    a.restartStep();
}
void toSub(AnimatedActor& a, CrawlerSub s) { toSub(a, uint8_t(s)); }
void toSub(AnimatedActor& a, GuardSub s) { toSub(a, uint8_t(s)); }
void turnHead(BurrowerFields& f, int16_t d) { f.headYaw = int16_t(f.headYaw + d); }
// the player's direction seen from the burrower, relative to its yaw
int playerBearing(const AnimatedActor& a) {
    return angleDiff(uint32_t(angleOf(player().pos, a.pos)), uint32_t(int32_t(a.rotation.y)));
}
int playerDistance(const AnimatedActor& a) { return planarDistance(player().pos, a.pos); }

// the death explosion (global static type 8) at the actor, 0x60 up
void explosion(const AnimatedActor& a, uint16_t spread, uint16_t flashes) {
    const uint32_t address = allocActor60(0); // exe 0x5e1300
    if (!address) return;
    StaticActor& x = staticActorAt(address);
    x.flags = kActorLive;
    x.type = typeByte(GlobalStaticType::Explosion);
    x.variant = 0;
    x.loopFlags |= kActorSweepGroup2;
    x.pos.x.whole = a.pos.x.whole;
    x.pos.y.whole = int16_t(a.pos.y.whole - 0x60);
    x.pos.z.whole = a.pos.z.whole;
    ExplosionParams params(x);
    params.spread = spread;
    params.flashes = flashes;
}
// the global hit spark, variant 3, at the actor (its dust)
void dustSpark(const AnimatedActor& a) {
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    e.flags = kActorLive;
    e.type = typeByte(GlobalEffectType::HitSpark);
    e.variant = 3;
    e.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
    e.pos.x = a.pos.x.whole;
    e.pos.y = a.pos.y.whole;
    e.pos.z = a.pos.z.whole;
    classState<HitSparkFields>(e).size = 0x40;
}
// the throw's velocity: Rx(the player's pitch) * (0, y, z) (exe 0x5e3e50)
void throwVelocity(AnimatedActor& a, int y, int z) {
    int32_t v[3];
    playerAimVector(0, y, z, v);
    a.velocity.x = 0;
    a.velocity.y = int16_t(v[1]);
    a.velocity.z = int16_t(v[2]);
}
// exe 0x4844b0(actor, yaw): landed / bounced: the speeds halved, a bounce up, true; else the move along the yaw and the
// gravity
bool airStep(AnimatedActor& a, BurrowerFields& f, int yaw) {
    if (f.contact & kBurrowerContactLanded) {
        a.velocity.z = int16_t(a.velocity.z >> 1);
        a.velocity.y = int16_t(uint32_t(-int(a.velocity.y)) >> 1);
        a.rotation.x = 0;
        a.airborne = 1;
        return true;
    }
    moveAlongYaw(a.pos, yaw, 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
    a.velocity.y = int16_t(a.velocity.y + a.acceleration.y);
    return false;
}
// exe 0x484740: the walls / the territory / the ground -> the contact bits; true: the ground was lost (more than 0x10
// below)
bool collide(const PlayerEnv& env, AnimatedActor& a, BurrowerFields& f) {
    f.contact = 0;
    uint32_t r = 0;
    if (int32_t(actorTestMove(env, a)) >= 0) { // exe 0x40f570
        const int rect = territoryViolated(a.pos); // exe 0x5e6ab0
        if (rect != 0) {
            r = territoryPushBack(a, rect); // exe 0x5e6b40
            if (r & 0x300) f.contact |= kBurrowerContactWallX;
            if (r & 0xc00) f.contact |= kBurrowerContactWallZ;
        }
    } else {
        r = actorPushOut(env, a); // exe 0x410960
        if (int32_t(r) < 0) a.pos = a.prevPos;
        if (r & 0x300) f.contact |= kBurrowerContactWallX;
        if (r & 0xc00) f.contact |= kBurrowerContactWallZ;
        if ((f.mode & kBurrowerBounces) && reflectRotationOffWalls(a.rotation, r) != 0) // exe 0x410740
            f.contact |= kBurrowerContactLanded;
    }
    const int g = actorGround(env, a); // exe 0x40f190
    if (a.airborne != 0) {
        if (!(f.mode & kBurrowerBounces) && (r & 0x3000)) {
            a.airborne = 0;
            a.pos.y.whole = int16_t(g);
            f.contact |= kBurrowerContactLanded;
        }
        actorCollisionSubmitMover(a);
        return false;
    }
    if (g - a.pos.y.whole >= 0x11) {
        f.contact |= kBurrowerContactGroundLost;
        return true;
    }
    a.pos.y.whole = int16_t(g);
    actorCollisionSubmitMover(a);
    return false;
}
// exe 0x484660(actor, 0): the hit box's damage; a special weapon's push knocks it over (the crawler: sub-state 9, the
// guard: 5); dead -> state 4 (the crawler knocked over first)
void hurt(AnimatedActor& a, BurrowerFields& f) {
    const uint32_t v = f.hitBox.result;
    const uint32_t d = v & kHitDamageMask;
    if (d == 0) return;
    uint32_t take = 0;
    if (v & (kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong)) {
        a.tint = kTintFlash;
        take = d;
    }
    if (v & (kHitLevelAbove | kHitLevelWithin | kHitLevelBelow)) {
        a.tint = kTintFlash;
        take = d;
    }
    if (v & kHitAttackStrongPush) {
        resetModelScale(a); // exe 0x4e5cd0
        f.savedYaw = f.hitBox.contact.x;
        a.tint = kTintFlash;
        if (BurrowerVariant(a.variant) == BurrowerVariant::Crawler) toSub(a, CrawlerSub::Knocked);
        else if (BurrowerVariant(a.variant) == BurrowerVariant::Guard) toSub(a, GuardSub::Knocked);
        take = d;
    }
    if (hitPointsAdd(a, -int(take)) == 0) return;
    if (a.variant != uint8_t(BurrowerVariant::Crawler)) {
        if (a.variant != uint8_t(BurrowerVariant::Guard)) return;
    } else if (v & kHitAttackStrongPush) {
        toSub(a, CrawlerSub::Knocked);
        return;
    }
    a.enterState(uint8_t(BurrowerState::Dying));
}
// exe 0x4830f0: the model, the stats, the boxes (the crawler can be lifted), its bounces; the crawler on the ground;
// tracked by its enemy id
void bind(const PlayerEnv& env, AnimatedActor& a, BurrowerFields& f) {
    a.flags |= kActorDrawn;
    if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
        freeActor(a);            // exe 0x5e1490
        return;
    }
    f.clearPitchAndRollBytes();
    a.anim = 0;
    a.prevAnim = 0xff;
    a.runFlags |= kActorRunMarked;
    const uint16_t hp = hasAnimatedStats(a.type) ? statsOf(a).hitPoints : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.collisionBox.pointAt(burrowerBox.stateAddress());
    a.interactionBox.pointAt(burrowerLiftBox.stateAddress());
    if (a.variant == uint8_t(BurrowerVariant::Crawler)) a.interaction = kInteractLift;
    else if (a.variant == uint8_t(BurrowerVariant::Guard)) a.interaction = 0;
    a.airborne = 0;
    a.onPlatform = 0;
    a.clearClassBlock(); // exe 0x40dc40
    f.hitBox.previousX = a.pos.x.whole;
    f.hitBox.previousZ = a.pos.z.whole;
    f.hitBox.result = 0;
    const uint32_t r = rngStep();
    f.startKey = 0;
    f.bouncesLeft = uint8_t((r & 3) + 1);
    if (a.variant == uint8_t(BurrowerVariant::Crawler)) {
        a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190
        a.enterState(uint8_t(BurrowerState::Crawler));
    } else if (a.variant == uint8_t(BurrowerVariant::Guard)) {
        a.enterState(uint8_t(BurrowerState::Guard));
    }
    if (f.enemyId == 0) return;
    encounterTrackInFieldSlotWhenOut(a, uint16_t(int16_t(f.enemyId))); // exe 0x5e64b0
}

// ---- the crawler (state 1) --------------------------------------------------------------------------------------
// exe 0x4832b0 (with 0x4832d0): the walk animation, still, the scale 1.0 -> crawl
void crawlerReset(AnimatedActor& a, BurrowerFields& f) {
    a.anim = 1;
    a.velocity.setXY(0, 0);
    a.velocity.z = 0;
    a.acceleration.z = 0;
    resetModelScale(a);
    f.mode = 0;
    toSub(a, CrawlerSub::Crawl);
}
// exe 0x4832f0: crawls (the head turning) until the player is near: in front within 0x800 -> the leap, behind within
// 0x400 -> the spin
void crawl(AnimatedActor& a, BurrowerFields& f) {
    if (a.step == 0) {
        f.mode = 0;
        a.anim = 0;
        a.enterStep(1);
    }
    turnHead(f, kHeadSlow);
    const int d = playerBearing(a);
    if (uint32_t(d + 0x400) <= 0x7ffu) {
        if (playerDistance(a) >= 0x800) return;
        toSub(a, CrawlerSub::Leap);
        return;
    }
    if (playerDistance(a) >= 0x400) return;
    toSub(a, CrawlerSub::Spin);
}
enum class SpinStep : uint8_t { Start = 0, Rear = 1, Face = 2, Spin = 3, Land = 4 };
// exe 0x4833a0: rears up, turns to the player, spins round a full turn (-0x155 a tick) in the air, lands -> the leap
void spin(AnimatedActor& a, BurrowerFields& f) {
    switch (SpinStep(a.step)) {
    case SpinStep::Start:
        f.mode = 0;
        a.enterStep(uint8_t(SpinStep::Rear));
        f.timer = 0x20;
        a.anim = 0;
        f.startKey = int8_t(uint8_t(a.animKey.frame));
        [[fallthrough]];
    case SpinStep::Rear:
        f.timer = int16_t(uint16_t(f.timer) - 1u);
        a.prevAnim = 0xff;
        a.anim = 0;
        if (f.timer == 0) {
            a.anim = 1;
            f.timer = 0x20;
            ++a.step;
            turnHead(f, kHeadSlow);
            f.startKey = 0;
            return;
        }
        break;
    case SpinStep::Face:
        turnYawTo(a.rotation.y, uint32_t(angleOf(player().pos, a.pos)), 0x80);
        f.timer = int16_t(uint16_t(f.timer) - 1u);
        if (f.timer == 0) {
            a.velocity.y = -0x190;
            a.acceleration.y = kGravity;
            ++a.step;
            turnHead(f, kHeadSlow);
            f.mode |= kBurrowerBounces | kBurrowerKeepsFalling;
            f.turnLeft = 0x1000;
            return;
        }
        break;
    case SpinStep::Spin:
        f.turnLeft = int16_t(uint16_t(f.turnLeft) - 0x155u);
        a.rotation.y = int16_t(uint16_t(a.rotation.y) - 0x155u);
        if (f.turnLeft <= -1) {
            a.rotation.y = int16_t(a.rotation.y - f.turnLeft);
            f.turnLeft = 0;
            ++a.step;
        }
        soundPlayAtPos(kSoundLeap, a.pos);
        [[fallthrough]];
    case SpinStep::Land:
        if (airStep(a, f, a.rotation.y)) toSub(a, CrawlerSub::Leap);
        break;
    default: break;
    }
    turnHead(f, kHeadSlow);
}
// a glow trail (stage effect type 6, variant 1) riding the burrower at (x, y, -0x20)
const EffectActor* leapGlow(const AnimatedActor& a, int16_t x, int16_t y) {
    const uint32_t address = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!address) return nullptr;
    EffectActor& e = effectActorAt(address);
    GlowTrailFields& glow = classState<GlowTrailFields>(e);
    e.flags = kActorLive;
    e.type = kGlowTrailStageType;
    e.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
    e.variant = uint8_t(GlowTrailVariant::Trail);
    glow.size = 0x10;
    glow.mode = 0;
    e.pos.x = x;
    e.pos.y = y;
    e.pos.z = -0x20;
    glow.bone = 0;
    glow.parent = &a;
    return &e;
}
enum class LeapStep : uint8_t { Start = 0, Crouch = 1, Air = 2 };
// exe 0x483520: crouches 0x1e ticks facing the player (the scale pulsing), leaps with two glow trails, lands -> the sway
void leap(AnimatedActor& a, BurrowerFields& f) {
    const uint8_t s = a.step;
    if (s == uint8_t(LeapStep::Start)) {
        f.mode = 0;
        a.enterStep(uint8_t(LeapStep::Crouch));
        a.anim = 1;
        f.timer = 0x1e;
        if (gameState().frame & 1) dustSpark(a);
    } else if (s == uint8_t(LeapStep::Crouch)) {
        turnYawTo(a.rotation.y, uint32_t(angleOf(player().pos, a.pos)), 0x80);
        const uint32_t odd = uint8_t(f.timer) & 1;
        a.scale.x = int16_t((odd + 0x10) * 0x20);
        a.scale.y = int16_t(uint32_t(int(odd) * -0x20 + 0x200));
        a.scale.z = int16_t((odd + 0x10) * 0x20);
        f.timer = int16_t(uint16_t(f.timer) - 1u);
        if (f.timer == 0) {
            resetModelScale(a);
            a.anim = 2;
            ++a.step;
            a.velocity.y = -0x12c;
            a.acceleration.y = kGravity;
            f.mode |= kBurrowerBounces | kBurrowerKeepsFalling;
            f.glowSlots = 0;
            if (const EffectActor* e = leapGlow(a, -0x14, -4)) f.glowSlots = uint16_t(uint32_t(e->slot) << 8);
            if (const EffectActor* e = leapGlow(a, 0x14, 4)) {
                f.glowSlots = uint16_t(f.glowSlots | e->slot);
                turnHead(f, kHeadFast);
                return;
            }
        }
    } else if (s == uint8_t(LeapStep::Air)) {
        if (a.velocity.y <= -1) a.anim = 3;
        if (airStep(a, f, a.rotation.y)) {
            a.airborne = 0;
            a.velocity.y = 0;
            a.velocity.z = 0;
            if (f.firstGlow != 0) freeActor(effectPool()[size_t(f.firstGlow) - kEffectClass]);
            if (f.secondGlow != 0) freeActor(effectPool()[size_t(f.secondGlow) - kEffectClass]);
            f.glowSlots = 0;
            toSub(a, CrawlerSub::Sway);
            turnHead(f, kHeadFast);
            return;
        }
    }
    turnHead(f, kHeadFast);
}
enum class SwayStep : uint8_t { Start = 0, Charge = 1, Bounce = 2, Wobble = 3, Tumble = 4, TumbleLand = 5 };
// exe 0x483790: sways toward the player; a wall: it bounces (and, its bounces spent, tumbles over and lies stunned)
void sway(AnimatedActor& a, BurrowerFields& f) {
    switch (SwayStep(a.step)) {
    case SwayStep::Start: {
        f.mode = kBurrowerStrong | 0x04;
        a.enterStep(uint8_t(SwayStep::Charge));
        a.anim = 1;
        f.sway = 0;
        const uint32_t r = rngStep();
        f.swayStep = int8_t((r & 1) ? 0x40 : 0xc0);
        a.velocity.z = -0x380;
        soundPlayAtPos(kSoundSway, a.pos);
        return;
    }
    case SwayStep::Charge: {
        turnHead(f, kHeadFast);
        if (gameState().frame & 1) dustSpark(a);
        if (f.contact & kBurrowerContactWalls) {
            const uint8_t n = uint8_t(f.bouncesLeft - 1u);
            f.bouncesLeft = n;
            if (n != 0) {
                a.step = uint8_t(SwayStep::Bounce);
            } else {
                a.anim = 6;
                f.bouncesLeft = 3;
                a.step = uint8_t(SwayStep::Tumble);
                a.stepCounter = 1;
            }
            a.velocity.z = 0x140;
            a.velocity.y = -0x140;
            a.acceleration.y = kGravity;
            a.airborne = 1;
            f.mode = uint8_t((f.mode & uint8_t(~kBurrowerStrong)) | kBurrowerBounces | kBurrowerKeepsFalling);
            soundPlayAtPos(kSoundHurt, a.pos);
            return;
        }
        const int16_t v = f.sway;
        bool flip;
        if (f.swayStep >= 1) flip = v > 0x100;
        else flip = v < -0x100;
        if (flip) f.swayStep = int8_t(uint32_t(-int(f.swayStep)));
        f.sway = int16_t(v + f.swayStep);
        a.rotation.y = int16_t(a.rotation.y + f.swayStep);
        integrateVelocity(a, 0, 0, a.velocity.z);
        return;
    }
    case SwayStep::Bounce:
        turnHead(f, kHeadFast);
        if (!airStep(a, f, a.rotation.y)) return;
        a.anim = 0;
        a.airborne = 0;
        a.velocity.z = 0;
        a.velocity.y = 0;
        f.timer = 0x3c;
        a.velocity.setXY(0, 0);
        ++a.step;
        return;
    case SwayStep::Wobble:
        turnHead(f, kHeadSlow);
        switch (int(f.timer) / 15) {
        case 0:
        case 3: a.rotation.z = int16_t(uint16_t(a.rotation.z) - 0x10u); break;
        case 1:
        case 2: a.rotation.z = int16_t(uint16_t(a.rotation.z) + 0x10u); break;
        default: break;
        }
        f.timer = int16_t(uint16_t(f.timer) - 1u);
        if (f.timer != 0) return;
        a.rotation.z = 0;
        toSub(a, CrawlerSub::Leap);
        return;
    case SwayStep::Tumble:
        if (a.animEnded()) {
            a.anim = 7;
            ++a.step;
        }
        [[fallthrough]];
    case SwayStep::TumbleLand:
        turnHead(f, kHeadFast);
        if (!airStep(a, f, a.rotation.y)) return;
        if (a.stepCounter == 0) {
            a.velocity.y = 0;
            a.airborne = 0;
            a.velocity.z = 0;
            a.velocity.setXY(0, 0);
            toSub(a, CrawlerSub::Stunned);
            return;
        }
        --a.stepCounter;
        return;
    default: return;
    }
}
// exe 0x483a50: lies stunned 0x5a ticks (still, its spin set) -> gets up
void stunned(AnimatedActor& a, BurrowerFields& f) {
    if (a.step == 0) {
        f.headYaw = 0;
        f.mode = 0x04 | kBurrowerKeepsFalling;
        a.enterStep(1);
        a.anim = 7;
        a.velocity.setXY(0, 0);
        a.velocity.setZSpare(0, 0);
        a.spin.x = 0;
        a.spin.z = 0x10;
        f.timer = 0x5a;
    } else if (a.step != 1) {
        return;
    }
    f.timer = int16_t(uint16_t(f.timer) - 1u);
    if (f.timer != 0) return;
    toSub(a, CrawlerSub::GetUp);
}
// exe 0x483ab0: the get-up animation -> the leap
void getUp(AnimatedActor& a, BurrowerFields& f) {
    if (a.step == 0) {
        f.headYaw = 0;
        f.mode = 0x04;
        a.enterStep(1);
        a.anim = 8;
        a.animKey.flags = 0;
        a.rotation.x = 0;
        a.rotation.z = 0;
        a.velocity.z = 0;
    }
    integrateVelocity(a, 0, 0, a.velocity.z);
    if (!a.animEnded()) return;
    toSub(a, CrawlerSub::Leap);
}
// exe 0x483b10
void idle(AnimatedActor& a, BurrowerFields& f) {
    if (a.step != 0) return;
    f.mode = 0;
    a.enterStep(1);
    a.anim = 0;
}
// exe 0x483b30: the ground lost: falls until it lands -> the leap
void fall(AnimatedActor& a, BurrowerFields& f) {
    turnHead(f, kHeadFast);
    if (a.step == 0) {
        f.mode = 0x04 | kBurrowerKeepsFalling | kBurrowerBounces;
        a.step = 1;
        a.anim = 1;
        a.acceleration.y = kGravity;
        resetModelScale(a);
    } else if (a.step != 1) {
        return;
    }
    if (!airStep(a, f, a.rotation.y)) return;
    a.airborne = 0;
    a.velocity.y = 0;
    toSub(a, CrawlerSub::Leap);
}
// exe 0x483ba0: knocked over (thrown up flipping away from the hit), lands -> the leap, or dead -> dying
void crawlerKnocked(AnimatedActor& a, BurrowerFields& f) {
    if (a.step == 0) {
        f.mode = 0x04 | kBurrowerKeepsFalling | kBurrowerBounces | kBurrowerNoHurt;
        a.anim = 3;
        a.rotation.z = 0;
        a.rotation.y = int16_t(f.savedYaw + kHalfTurn);
        a.rotation.x = 0x1000;
        f.timer = 0x1000;
        a.velocity.x = 0;
        a.velocity.y = -0x400;
        a.acceleration.y = kGravity;
        a.velocity.z = -0x40;
        a.airborne = 1;
        soundPlayAtPos(kSoundHurt, a.pos);
        resetModelScale(a);
        a.enterStep(1);
    }
    f.timer = int16_t(uint16_t(f.timer) - 0x80u);
    turnHead(f, kHeadFast);
    a.rotation.x = f.timer;
    if (!airStep(a, f, f.savedYaw)) return;
    a.rotation.x = 0;
    a.airborne = 0;
    a.velocity.y = 0;
    if (a.hitPoints >= 0) {
        toSub(a, CrawlerSub::Leap);
        return;
    }
    a.enterState(uint8_t(BurrowerState::Dying));
}
// exe 0x483220: the crawler's state -- the hurt test, the sub-state, the collision (the ground lost: the fall), the lift
void crawlerState(const PlayerEnv& env, AnimatedActor& a, BurrowerFields& f) {
    a.tint = kTintNeutral;
    if (!(f.mode & kBurrowerNoHurt)) hurt(a, f);
    switch (CrawlerSub(a.subState)) {
    case CrawlerSub::Reset: crawlerReset(a, f); break;
    case CrawlerSub::Crawl: crawl(a, f); break;
    case CrawlerSub::Spin: spin(a, f); break;
    case CrawlerSub::Leap: leap(a, f); break;
    case CrawlerSub::Sway: sway(a, f); break;
    case CrawlerSub::Stunned: stunned(a, f); break;
    case CrawlerSub::GetUp: getUp(a, f); break;
    case CrawlerSub::Idle: idle(a, f); break;
    case CrawlerSub::Fall: fall(a, f); break;
    case CrawlerSub::Knocked: crawlerKnocked(a, f); break;
    default: notPortedAt(kEnemyPort, "0x482f50 state 1 sub-state (table 0x892010 has 10)", a.subState);
    }
    if (collide(env, a, f)) {
        a.airborne = 1;
        if (!(f.mode & kBurrowerKeepsFalling)) toSub(a, CrawlerSub::Fall);
    }
    if (a.hitPoints >= 0 && playerLiftTargetCheck(a, 0)) { // exe 0x573a60
        a.enterState(uint8_t(BurrowerState::Carried));
        f.mode |= kBurrowerNoHitEntry;
        a.runFlags |= kActorRunMarked;
    }
}

// ---- the guard (state 2) ----------------------------------------------------------------------------------------
// exe 0x483cb0 (with 0x483cd0)
void guardReset(AnimatedActor& a, BurrowerFields& f) {
    a.anim = 1;
    a.velocity.setXY(0, 0);
    a.acceleration.setXY(0, 0);
    resetModelScale(a);
    f.mode = 0;
    toSub(a, GuardSub::Watch);
}
// exe 0x483cf0: watches; the player in front within 0xc00 -> fire, else a hop round toward it
void watch(AnimatedActor& a, BurrowerFields& f) {
    if (a.step == 0) {
        f.mode = 0;
        a.anim = 1;
        a.enterStep(1);
    }
    turnHead(f, kHeadSlow);
    const int d = playerBearing(a);
    if (uint32_t(d + 0x400) <= 0x7ffu) {
        if (playerDistance(a) >= 0xc00) return;
        toSub(a, GuardSub::Fire);
        return;
    }
    f.turnLeft = int16_t(d);
    toSub(a, GuardSub::HopWide);
}
// exe 0x483da0: a shot every 0x3c ticks; the player off its front -> a hop round, beyond 0xc00 -> watch
void fire(AnimatedActor& a, BurrowerFields& f) {
    if (a.step == 0) {
        f.mode = 0;
        a.anim = 1;
        a.enterStep(1);
        f.timer = 1;
    }
    turnHead(f, kHeadFast);
    f.timer = int16_t(uint16_t(f.timer) - 1u);
    if (f.timer == 0) {
        if (const uint32_t address = allocActor40(kActorStageClass)) { // exe 0x5e1280: pool 0x40 stage type 0
            ProjectileActor& s = projectileActorAt(address);
            s.loopFlags |= kActorSweepGroup2;
            s.flags = kActorLive;
            s.type = kGuardShotType;
            s.pos = a.pos;
            s.pos.y.whole = int16_t(uint16_t(s.pos.y.whole) - 0x50u);
            const uint32_t i = (uint32_t(uint16_t(a.rotation.y)) >> 6) & 0x3f;
            s.pos.x.whole = int16_t(s.pos.x.whole - int16_t((int32_t(sin64(i)) << 6) >> 12));
            s.pos.z.whole = int16_t(s.pos.z.whole - int16_t((int32_t(cos64(i)) << 6) >> 12));
            s.velocity.setXY(0, 0);
            s.velocity.z = -0x300;
            s.rotation.x = int16_t(pitchOf(s.pos, player().pos) + kHalfTurn);
            s.rotation.y = a.rotation.y;
            s.rotation.z = 0;
            GuardShotParams params(s);
            params.ticks = kGuardShotTicks;
            params.attack = statsOf(a).shotAttack;
        }
        f.timer = 0x3c;
    }
    const int d = playerBearing(a);
    if (uint32_t(d + 0x400) >= 0x800u) {
        f.turnLeft = int16_t(d);
        toSub(a, GuardSub::HopWide);
        return;
    }
    if (uint32_t(d + 0x80) >= 0x100u) {
        f.turnLeft = int16_t(d);
        toSub(a, GuardSub::HopNarrow);
        return;
    }
    if (playerDistance(a) <= 0xc00) return;
    toSub(a, GuardSub::Watch);
}
enum class HopStep : uint8_t { Start = 0, TurnNegative = 1, TurnPositive = 2, Done = 3 };
// exe 0x483f80 (div 6, -0x190) / 0x4840c0 (div 3, -0xc8): a hop turning by the turn left in div steps -> fire
void guardHop(AnimatedActor& a, BurrowerFields& f, int div, int16_t vy, bool fixPlus) {
    const uint8_t s = a.step;
    if (s == uint8_t(HopStep::Start)) {
        f.mode = 0;
        a.anim = 2;
        a.velocity.y = vy;
        a.acceleration.y = kGravity;
        f.hopHeight = 0;
        const int16_t v = f.turnLeft;
        f.savedYaw = v;
        if (v >= 0) {
            a.step = uint8_t(HopStep::TurnPositive);
        } else {
            f.savedYaw = int16_t(-int(v));
            a.step = uint8_t(HopStep::TurnNegative);
        }
        soundPlayAtPos(kSoundLeap, a.pos);
    } else if (s == uint8_t(HopStep::TurnNegative) || s == uint8_t(HopStep::TurnPositive)) {
        const int16_t st = int16_t(int(f.savedYaw) / div);
        bool done;
        if (s == uint8_t(HopStep::TurnNegative)) {
            f.turnLeft = int16_t(f.turnLeft + st);
            a.rotation.y = int16_t(a.rotation.y + st);
            done = f.turnLeft > 0;
        } else {
            f.turnLeft = int16_t(f.turnLeft - st);
            a.rotation.y = int16_t(a.rotation.y - st);
            done = f.turnLeft < 0;
        }
        if (done) {
            const int16_t rem = f.turnLeft;
            a.rotation.y = int16_t(fixPlus ? a.rotation.y + rem : a.rotation.y - rem);
            f.turnLeft = 0;
            a.step = uint8_t(HopStep::Done);
        }
    }
    if (a.velocity.y <= -1) a.anim = 3;
    turnHead(f, kHeadFast);
    f.hopHeight = int16_t(f.hopHeight + (a.velocity.y >> 4));
    a.velocity.y = int16_t(a.acceleration.y + a.velocity.y);
    if (f.hopHeight <= 0) return;
    f.hopHeight = 0;
    toSub(a, GuardSub::Fire);
}
// exe 0x484200: knocked back (a hop on the spot) 0x12 ticks -> fire, or dead -> dying
void guardKnocked(AnimatedActor& a, BurrowerFields& f) {
    if (a.step == 0) {
        f.mode = 0x04 | kBurrowerNoHurt;
        a.anim = 4;
        soundPlayAtPos(kSoundHurt, a.pos);
        resetModelScale(a);
        a.enterStep(1);
        f.timer = 0x12;
        f.headYaw = 0;
    }
    if (a.step == 1) {
        f.hopHeight = int16_t(f.hopHeight + (a.velocity.y >> 4));
        a.velocity.y = int16_t(a.acceleration.y + a.velocity.y);
        if (f.hopHeight >= 1) {
            f.hopHeight = 0;
            ++a.step;
        }
    }
    f.timer = int16_t(uint16_t(f.timer) - 1u);
    if (f.timer != 0) return;
    if (a.hitPoints >= 0) {
        toSub(a, GuardSub::Fire);
        return;
    }
    a.enterState(uint8_t(BurrowerState::Dying));
}
// exe 0x483c70: the guard's state -- the hurt test, the sub-state
void guardState(AnimatedActor& a, BurrowerFields& f) {
    a.tint = kTintNeutral;
    if (!(f.mode & kBurrowerNoHurt)) hurt(a, f);
    switch (GuardSub(a.subState)) {
    case GuardSub::Reset: guardReset(a, f); break;
    case GuardSub::Watch: watch(a, f); break;
    case GuardSub::Fire: fire(a, f); break;
    case GuardSub::HopWide: guardHop(a, f, 6, -0x190, false); break;
    case GuardSub::HopNarrow: guardHop(a, f, 3, -0xc8, true); break;
    case GuardSub::Knocked: guardKnocked(a, f); break;
    default: notPortedAt(kEnemyPort, "0x482f50 state 2 sub-state (table 0x892038 has 6)", a.subState);
    }
}

// ---- state 3: carried / thrown ----------------------------------------------------------------------------------
// exe 0x4843e0: flying; the first solid hit bounces it (a spark, its hit points to -1); landed: the bounces left, then
// stunned (alive) or dying
void flying(const PlayerEnv& env, AnimatedActor& a, BurrowerFields& f) {
    if ((f.hitBox.result & kHitAttackSolid) && a.hitPoints >= 0) {
        a.velocity.z = int16_t(uint32_t(-int(a.velocity.z)) >> 1);
        a.hitPoints = -1;
        spawnHitSpark(kActorSweepGroup2, a.pos.x.whole, int16_t(uint16_t(a.pos.y.whole - 0x40)), a.pos.z.whole);
    }
    if (airStep(a, f, a.rotation.y)) {
        if (a.stepCounter != 0) {
            --a.stepCounter;
            integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
            collide(env, a, f);
            return;
        }
        if (a.hitPoints >= 0) {
            a.rotation.y = int16_t(uint16_t(a.rotation.y) - 0x400u);
            a.rotation.x = 0;
            a.anim = 7;
            a.enterState(uint8_t(BurrowerState::Crawler), uint8_t(CrawlerSub::Stunned));
        } else {
            a.enterState(uint8_t(BurrowerState::Dying));
        }
        a.velocity.y = 0;
        a.airborne = 0;
    }
    collide(env, a, f);
}
// exe 0x4842d0: at the player's hands until thrown (along the player's aim) / dropped, then flying
void inHands(const PlayerEnv& env, AnimatedActor& a, BurrowerFields& f) {
    if (a.step == 0) {
        a.tint = kTintNeutral;
        f.mode = kBurrowerBounces | kBurrowerNoHitEntry;
        a.step = 1;
        a.anim = 5;
        resetModelScale(a);
        a.rotation.x = 0;
        a.rotation.z = 0;
    }
    carryAtHands(a, 0, 0, 0);                 // exe 0x573ab0
    const CarryRelease r = carryReleased();   // exe 0x573950
    if (r == CarryRelease::Held) return;
    f.mode &= uint8_t(~kBurrowerNoHitEntry);
    f.savedYaw = player().rotation.y;
    if (r != CarryRelease::Thrown) {
        a.velocity.y = -0x300;
        a.velocity.z = -0x40;
    } else {
        throwVelocity(a, -0x100, -0x380);
        f.mode |= kBurrowerThrown;
    }
    a.acceleration.y = kGravity;
    a.rotation.y = f.savedYaw;
    a.airborne = 1;
    carryDropFromPlayer(a); // exe 0x573990
    a.subState = uint8_t(CarriedSub::Flying);
    a.step = 0;
    a.stepCounter = 3; // the bounces before it lands
    flying(env, a, f);
}
// exe 0x4842b0
void carriedState(const PlayerEnv& env, AnimatedActor& a, BurrowerFields& f) {
    switch (CarriedSub(a.subState)) {
    case CarriedSub::Hands: inHands(env, a, f); break;
    case CarriedSub::Flying: flying(env, a, f); break;
    default: notPortedAt(kEnemyPort, "0x482f50 state 3 sub-state (table 0x892050 has 2)", a.subState);
    }
}
// exe 0x484510: the defeat and its explosion, 0x1e ticks blinking (not a target near the end), the drops, freed
void dying(const PlayerEnv& env, AnimatedActor& a, BurrowerFields& f) {
    a.tint = kTintFlash;
    if (a.step == 0) {
        a.step = 1;
        f.timer = 0x1e;
        if (a.airborne == 0) {
            a.velocity.x = 0;
            a.velocity.y = 0;
            a.velocity.z = 0;
        }
        a.acceleration.y = kGravity;
        a.anim = 4;
        countDefeat();
        explosion(a, 0x30, uint16_t(f.timer));
    }
    if (uint8_t(f.timer) & 1) a.tint = kTintHurtBlink;
    if (f.timer <= 0xe) f.mode |= kBurrowerNotTarget;
    f.timer = int16_t(uint16_t(f.timer) - 1u);
    if (f.timer == 0) {
        spawnDrops(a.pos, statsOf(a).dropTable); // exe 0x5f1270
        freeActor(a);
        return;
    }
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
    if (a.airborne != 0) a.velocity.y = int16_t(a.velocity.y + a.acceleration.y);
    collide(env, a, f);
    if (a.wasAirborne != 0 && a.airborne == 0) {
        a.velocity.z = 0;
        a.velocity.x = 0;
    }
}
// exe 0x482f50
void update(const PlayerEnv& env, AnimatedActor& a) {
    auto&& f = classState<BurrowerFields>(a);
    const uint32_t c = a.actorHit; // exe 0x484880
    if (c != 0) {
        if (c & 0x300) f.contact |= kBurrowerContactWallX;
        if (c & 0xc00) f.contact |= kBurrowerContactWallZ;
        if (c & kSolidAboveOrBelow) f.contact |= kBurrowerContactLanded;
    }
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (BurrowerState(a.state)) {
    case BurrowerState::Bind: bind(env, a, f); break;
    case BurrowerState::Crawler: crawlerState(env, a, f); break;
    case BurrowerState::Guard: guardState(a, f); break;
    case BurrowerState::Carried: carriedState(env, a, f); break;
    case BurrowerState::Dying: dying(env, a, f); break;
    default: notPortedAt(kEnemyPort, "0x482f50 state (table 0x891ffc has 5)", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim != a.prevAnim) animSet(a, a.anim, f.startKey); // exe 0x5e1c50
    else animStep(a);                                         // exe 0x5e1ea0
    LaneVec4 head = decodedFrame().entries[2];               // part 1's angles
    head.y = int16_t(f.headYaw + head.y);
    animPose(a); // exe 0x5e2d90
    const uint8_t mode = f.mode;
    const uint32_t bits = uint32_t(int32_t(int8_t(mode)));
    const AnimatedActorStats stats = statsOf(a);
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    if (bits & kBurrowerNoHitEntry) return;
    uint32_t owner = (bits & kBurrowerNotTarget) << 0xc, attack; // kHitNoLockOn
    const uint32_t slot = a.slot;
    if (a.state == uint8_t(BurrowerState::Dying)) {
        attack = kHitAttackBump;
        owner = hitOwner(uint8_t(slot)) | owner | hitGroup(kHitGroupBodies) | kHitContactMotion | hitRadius(0x48);
    } else if (mode & kBurrowerThrown) {
        attack = uint32_t(stats.bodyAttack) | kHitAttackByPlayer;
        owner = hitOwner(uint8_t(slot)) | hitGroup(kHitGroupPlayerAttacks) | kHitContactMotion | hitRadius(0x48);
    } else {
        attack = (mode & kBurrowerStrong) ? (kHitAttackThrown | kHitAttackStrong | stats.strongAttack)
                                          : (kHitAttackSolid | stats.attack);
        owner = ((slot << 15 | stats.hitHeight) << 9) | owner | hitGroup(kHitGroupBodies) | kHitCompareHeights |
                kHitContactMotion | hitRadius(0x48);
    }
    const ShortVec4& bone = bonePoint(a, 0, kEnemyPort); // exe 0x5e1810
    const int16_t pos[3] = {bone.x, bone.y, bone.z};
    hitListSubmit(pos, f.hitBox, owner, attack); // exe 0x5e4120
    // exe 0x5f20f0(actor, 0x30, 0x160, 0) unless the state word is 3 / the state 2: the shadow (render output)
}

} // namespace

void mandaBurrowerUpdate(const PlayerEnv& env, uint32_t actor) { update(env, animatedActorAt(actor)); }

} // namespace od::port
