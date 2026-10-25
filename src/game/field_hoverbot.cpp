// The field hoverbot class 0x44bea0 (see field_hoverbot.hpp). States [0x88e894]: 0 0x44bf40 bind, 1 0x44c080 appear
// (by the variant [0x88e8b0]: variant 0 [0x88e8b8] 0x44c140 / 0x44c1a0 / 0x44c260, variant 1 [0x88e8c4] 0x44c330 /
// 0x44c3e0), 2 0x44c450 main (by the variant [0x88e8cc]: variant 0 0x44c4c0 hovers, sub-states [0x88e8d4] 0x44c550
// circle / 0x44c6f0 give way / 0x44c810 spin / 0x44c8b0 shoot; variant 1 0x44ca40 falls back), 3 0x44cb20 carried
// (sub-states [0x88e8f4]: 0x44cb40 at the hands, 0x44cc10 thrown, 0x44cd20 landed), 4 0x44cdc0 dying, 5 0x44cf80
// leaving. Its stat row is the type's row of (spawn byte * 3 + variant) (game/actor_stats).
// Deviations: notPorted where the exe would read through a null pointer (a bone point missing, a variant past the
// two-entry tables).
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/field_hoverbot.hpp"
#include "game/field_hoverbot_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/enemy_motion.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/explosions.hpp"
#include "game/fixed_point.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/matrix.hpp"
#include "game/pickups.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/stage_smoke.hpp"
#include "game/step_dust.hpp"

namespace od::port {

using namespace field_hoverbot_state;

namespace {

constexpr const char* kFieldHoverbotPort = "field hoverbot port"; // the area of its port-gap errors

// +0x15e
constexpr uint8_t kHoverWall = 0x01, kHoverReflected = 0x02, kHoverMoveFlag = 0x08, kHoverFloor = 0x10;
// +0x160
constexpr uint8_t kHoverBumped = 0x40; // already turned away from a wall once
// +0x162
constexpr uint8_t kHoverThrownEntry = 0x01, kHoverNoHeightFollow = 0x02, kHoverBounces = 0x04;

enum class HoverState : uint8_t {
    Bind = 0,    // exe 0x44bf40
    Appear = 1,  // exe 0x44c080
    Main = 2,    // exe 0x44c450
    Carried = 3, // exe 0x44cb20
    Dying = 4,   // exe 0x44cdc0
    Leaving = 5, // exe 0x44cf80
};
enum class HoverVariant : uint8_t { AroundPlayer = 0, FromGround = 1 };
enum class HoverAppearSub : uint8_t { Start = 0, Follow = 1, Wait = 2 };       // exe 0x44c140 / 0x44c1a0 / 0x44c260
enum class HoverPopSub : uint8_t { Burst = 0, Rise = 1 };                      // exe 0x44c330 / 0x44c3e0
enum class HoverSub : uint8_t { Circle = 0, GiveWay = 1, Spin = 2, Shoot = 3 }; // exe 0x44c550 / 0x44c6f0 / 0x44c810 / 0x44c8b0
enum class HoverFallSub : uint8_t { Start = 0, Rise = 1, Down = 2 };           // exe 0x44ca40
enum class HoverCarriedSub : uint8_t { InHands = 0, Flying = 1, Landed = 2 };  // exe 0x44cb40 / 0x44cc10 / 0x44cd20

// The aimed shot it fires (pool 0x40 stage type 0, exe 0x5c2830: its spawn parameters as the hoverbot writes them)
struct HoverShotParams {
    explicit HoverShotParams(const ProjectileActor& actor) : HoverShotParams(const_cast<ProjectileActor&>(actor)) {}
    explicit HoverShotParams(ProjectileActor& actor) : ticks(actor.params, 0), attack(actor.params[2]) {}
    BytesField<uint16_t, 4> ticks;
    uint8_t& attack;
};
constexpr uint8_t kHoverShotStageType = 0;

const DataTable<CollisionBoxRecord> hoverBox{TableId::FieldHoverbotHoverBox};
const DataTable<InteractionBoxRecord> hoverLiftBox{TableId::FieldHoverbotHoverLiftBox};
const DataTable<ScalarTable<uint8_t, 8>> hoverModels{TableId::FieldHoverbotHoverModels}; // by the spawn byte
const DataTable<ScalarTable<uint16_t, 8>> circleTicks{TableId::FieldHoverbotCircleTicks};
constexpr int16_t kHoverScale = 0x300;
constexpr int16_t kHoverHeight = 0xb4;   // the least height above the ground while it hovers
constexpr int16_t kFallBackHeight = 100; // ... while variant 1 falls back
constexpr int16_t kGravity = 0x40;
constexpr uint16_t kSoundHurt = 0x8d, kSoundBurst = 0xa7, kSoundRise = 0xa3, kSoundFallBack = 0xa8, kSoundSink = 0xa4;
constexpr uint8_t kSparkLoops = 2;
constexpr uint8_t kSmokeVariant = 8;
constexpr uint32_t kAppearShape = 0x80000; // the owner-word bit of its hit entry while it appears around the player

// the row of (spawn byte * 3 + variant) of its type's stats
uint32_t statsRowAddress(const AnimatedActor& a) {
    return statTableRow(actorStatTables().animated, a.type) + (uint32_t(a.spawnByte) * 3 + a.variant) * 0x10;
}
AnimatedActorStats statsOf(const AnimatedActor& a) { return animatedStatsAt(statsRowAddress(a)); }

// exe 0x44d240: the damage of the player's / a solid's hits, the sound; dead -> -1
int hurt(AnimatedActor& a, FieldHoverbotFields& f) {
    const uint32_t d = f.hitBox.result & kHitDamageMask;
    if (d != 0 && (f.hitBox.result & (kHitAttackSolid | kHitAttackByPlayer))) {
        a.tint = kTintFlash;
        soundPlayAtPos(kSoundHurt, a.pos);
        if (hitPointsAdd(a, -int(d)) != 0) return -1;
    }
    return 0;
}
// exe 0x44d140: the height toward the player's by an eighth of the gap, at most 0x20 a tick
void followPlayerHeight(AnimatedActor& a) {
    const int16_t y = a.pos.y.whole;
    int d = (int(player().pos.y.whole) - y) >> 3;
    if (d >= 0x21) {
        a.pos.y.whole = wrap16(y + 0x20);
        return;
    }
    if (d <= -0x21) d = -0x20;
    a.pos.y.whole = wrap16(y + d);
}
// exe 0x44d180(actor, h): not lower than h above the ground; 1 = held up
int keepAboveGround(const PlayerEnv& env, AnimatedActor& a, int h) {
    const int g = actorGround(env, a); // exe 0x40f190
    if (g - h < a.pos.y.whole) {
        a.pos.y.whole = wrap16(g - h);
        return 1;
    }
    return 0;
}
// exe 0x44d1c0(actor, attack bits, shape bits): the hit entry at bone 0
void submitHit(AnimatedActor& a, FieldHoverbotFields& f, uint32_t attackBits, uint32_t shapeBits) {
    const ShortVec4& b = bonePoint(a, 0, kFieldHoverbotPort); // exe 0x5e1810
    const int16_t pos[3] = {b.x, b.y, b.z};
    hitListSubmit(pos, f.hitBox, uint32_t(a.slot) << 24 | shapeBits | 0x408010u,
                  uint32_t(statsOf(a).attack) | attackBits); // exe 0x5e4120
    f.hitBox.result = 0;
    f.hitBox.previousX = b.x;
    f.hitBox.previousZ = b.z;
}
// the move test and the push-out, the position restored when the push-out fails
void collideKeep(const PlayerEnv& env, AnimatedActor& a) {
    if (int32_t(actorTestMove(env, a)) < 0 && int32_t(actorPushOut(env, a)) < 0) a.pos = a.prevPos;
}
// exe 0x44d290: the move test and the push-out -> the contact bits
void collide(const PlayerEnv& env, AnimatedActor& a, FieldHoverbotFields& f) {
    f.contact = 0;
    uint32_t r = actorTestMove(env, a); // exe 0x40f570
    if (r & 0x20000000u) f.contact |= kHoverMoveFlag;
    if (int32_t(r) >= 0) return;
    r = actorPushOut(env, a); // exe 0x410960
    if (int32_t(r) < 0) a.pos = a.prevPos;
    if (r & 0xf00) f.contact |= kHoverWall;
    if (r & 0x3000) f.contact |= kHoverFloor;
    if ((f.bits & kHoverBounces) && reflectRotationOffWalls(a.rotation, r) != 0) f.contact |= kHoverReflected;
}
// exe 0x40be30 on an identity (Rx(pitch)) then exe 0x40bbb0 (game/matrix transformVectorFloat) of (0, 0, z): the out z
int32_t pitchedZ(int pitch, int z) {
    const int32_t v[3] = {0, 0, z};
    int32_t o[3];
    transformVectorFloat(rotationX(pitch), v, o);
    return o[2];
}

// ---- state 0 --------------------------------------------------------------------------------------------------------
// exe 0x5e6040 with its outputs in the hoverbot's place fields (untouched without a field slot)
int fieldBearing(const AnimatedActor& a, FieldHoverbotFields& f) {
    if (encounterState().slotOfKind[uint32_t(EncounterKind::Field)] < 0) return 0;
    int16_t pitch = 0, yaw = 0;
    const int d = encounterFieldBearing(a, pitch, yaw);
    f.pitch = pitch;
    f.yaw = yaw;
    return d;
}
void bind(const PlayerEnv& env, AnimatedActor& a, FieldHoverbotFields& f) { // exe 0x44bf40
    if (!bindActorModel(a, hoverModels[a.spawnByte])) { // exe 0x5e0bc0
        freeActor(a);
        return;
    }
    a.runFlags |= kActorWarpKept;
    a.anim = 0;
    a.prevAnim = 0xff;
    a.scale.z = kHoverScale;
    a.scale.y = kHoverScale;
    a.scale.x = kHoverScale;
    a.rotation.z = 0;
    a.rotation.x = 0;
    const uint16_t hp = hasAnimatedStats(a.type) ? uint16_t(statsOf(a).hitPoints) : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.collisionBox.pointAt(hoverBox.stateAddress());
    a.interactionBox.pointAt(hoverLiftBox.stateAddress());
    a.interaction = kInteractLift;
    a.airborne = 1;
    a.onPlatform = 0;
    a.clearClassBlock(); // exe 0x40dc40
    registerEnemy(a.slot, 0);
    const uint8_t v = a.variant;
    if (v == uint8_t(HoverVariant::AroundPlayer)) {
        const int d = fieldBearing(a, f); // exe 0x5e6040
        f.radius = int16_t(d);
        encounterPlaceAroundPlayer(a, int16_t(uint16_t(d)), f.pitch, f.yaw); // exe 0x5e6140
    } else if (v == uint8_t(HoverVariant::FromGround)) {
        a.pos.y.whole = wrap16(actorGround(env, a) + 200);
        f.fallTicks = 0;
        a.enterState(uint8_t(HoverState::Appear));
        return;
    }
    a.enterState(uint8_t(HoverState::Appear));
}

// ---- state 1 --------------------------------------------------------------------------------------------------------
void follow(AnimatedActor& a, FieldHoverbotFields& f);
void appearStart(AnimatedActor& a, FieldHoverbotFields& f) { // exe 0x44c140 (variant 0)
    a.subState = uint8_t(HoverAppearSub::Follow);
    a.enterStep(0);
    const uint32_t r = rngStep();
    animSetPose(a, 2, int(r & 3)); // exe 0x5e1c30(a, 2, r & 3)
    a.velocity.setXY(0, 0);
    a.velocity.z = -0x400;
    a.runFlags |= kActorRunMarked;
    f.bits = 0;
    follow(a, f);
}
// exe 0x44c1a0 (variant 0): kept around the player (its place offsets settling) until on the screen
void follow(AnimatedActor& a, FieldHoverbotFields& f) {
    const Player& p = player();
    if (p.rotation.x > 0) {
        a.rotation.y = int16_t(uint16_t(p.rotation.y) - uint16_t(p.directionOffset.y));
        f.radius -= 8u;
        settleTowardZero(f.pitch, f.yaw, 8, 0); // exe 0x5e60f0
        encounterPlaceAroundPlayer(a, f.radius, f.pitch, f.yaw);
        if (a.runFlags & kActorOffScreen) return;
        a.enterState(uint8_t(HoverState::Main));
        return;
    }
    a.rotation.y = p.rotation.y;
    settleTowardZero(f.pitch, f.yaw, 8, 2);
    encounterPlaceAroundPlayer(a, f.radius, f.pitch, f.yaw);
    if (a.runFlags & kActorOffScreen) return;
    a.subState = uint8_t(HoverAppearSub::Wait);
    a.enterStep(0);
}
void appearWait(AnimatedActor& a, FieldHoverbotFields& f) { // exe 0x44c260 (variant 0): 0x1e ticks -> the main state
    if (a.step == 0) {
        a.enterStep(1);
        f.timer = 0x1e;
    }
    --f.timer;
    if (f.timer != 0) return;
    a.enterState(uint8_t(HoverState::Main));
}
// exe 0x44c3e0 (variant 1): a wait, the rise slowing to its top, the animation 1 -> the main state
void popRise(AnimatedActor& a, FieldHoverbotFields& f) {
    switch (a.step) {
    case 0:
        --f.timer;
        if (f.timer != 0) return;
        a.step = 1;
        return;
    case 1:
        a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
        if (a.velocity.y < 0) return;
        a.step = 2;
        return;
    case 2:
        a.step = 3;
        a.anim = 1;
        [[fallthrough]];
    case 3:
        if (a.animKey.events == 0) return;
        a.enterState(uint8_t(HoverState::Main));
        return;
    default: return;
    }
}
// exe 0x44c330 (variant 1): the burst out of the ground (dust, ground effects, sounds)
void popBurst(const PlayerEnv& env, AnimatedActor& a, FieldHoverbotFields& f) {
    a.subState = uint8_t(HoverPopSub::Rise);
    a.enterStep(0);
    a.anim = 0;
    a.velocity.x = 0;
    a.velocity.y = -0x200;
    a.velocity.z = 0;
    a.acceleration.y = 0x80;
    f.timer = 9;
    const int32_t kind = groundKindUnder(a.pos); // exe 0x5e6240
    const int g = actorGround(env, a);
    spawnDustSpark(a.pos.x.whole, wrap16(g - 10), a.pos.z.whole);  // exe 0x5e62c0
    spawnStepDust(a.pos.x.whole, wrap16(g), a.pos.z.whole, kind); // exe 0x5e6300
    spawnStepDust(a.pos.x.whole, wrap16(g), a.pos.z.whole, kind);
    soundPlayAtPos(kSoundBurst, a.pos);
    soundPlayAtPos(kSoundRise, a.pos);
    popRise(a, f);
}
void dying(const PlayerEnv& env, AnimatedActor& a, FieldHoverbotFields& f);
void appear(const PlayerEnv& env, AnimatedActor& a, FieldHoverbotFields& f) { // exe 0x44c080
    a.tint = kTintNeutral;
    const uint8_t v = a.variant;
    if (v > uint8_t(HoverVariant::FromGround))
        notPortedAt(kFieldHoverbotPort, "0x44bea0 state 1 variant (table 0x88e8b0 has 2)", v);
    if (hurt(a, f)) {
        a.enterState(uint8_t(HoverState::Dying));
        dying(env, a, f);
        return;
    }
    if (v == uint8_t(HoverVariant::AroundPlayer)) { // exe 0x44c0a0
        switch (HoverAppearSub(a.subState)) {
        case HoverAppearSub::Start: appearStart(a, f); break;
        case HoverAppearSub::Follow: follow(a, f); break;
        case HoverAppearSub::Wait: appearWait(a, f); break;
        default:
            notPortedAt(kFieldHoverbotPort, "0x44bea0 variant 0 state 1 sub-state (table 0x88e8b8 has 3)", a.subState);
        }
    } else { // exe 0x44c2a0
        switch (HoverPopSub(a.subState)) {
        case HoverPopSub::Burst: popBurst(env, a, f); break;
        case HoverPopSub::Rise: popRise(a, f); break;
        default:
            notPortedAt(kFieldHoverbotPort, "0x44bea0 variant 1 state 1 sub-state (table 0x88e8c4 has 2)", a.subState);
        }
    }
    moveAlongRotation(a.pos, a.rotation, a.velocity.x, a.velocity.y, a.velocity.z); // exe 0x5e37b0
    collideKeep(env, a);
    if (v == uint8_t(HoverVariant::AroundPlayer)) {
        keepAboveGround(env, a, kHoverHeight);
        submitHit(a, f, kHitAttackSolid, kAppearShape);
    } else {
        submitHit(a, f, kHitAttackSolid, 0);
    }
}

// ---- state 2 --------------------------------------------------------------------------------------------------------
// exe 0x44c550: around the player at 800..900; the shot when in range and faced (one shooter at a time)
void circle(AnimatedActor& a, FieldHoverbotFields& f) {
    if (a.step == 0) {
        a.step = 1;
        a.anim = 2;
        a.acceleration.z = -0x40;
        uint32_t r = rngStep();
        f.timer = int16_t(circleTicks[r & 7]);
        r = rngStep();
        f.bits = 0;
        f.neighbour = uint16_t(r & 1);
        a.runFlags &= uint8_t(~kActorRunMarked);
    }
    const int head = headingFromPlayer(a, neighbourSide(f.neighbour), 800, 900); // exe 0x5e5cf0
    if (speedToward(a, 0x200) != 0) a.velocity.z = -0x200;                     // exe 0x5e55a0
    turnYawTo(a.rotation.y, uint32_t(head), 0x40);
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
    if (neighbourFind(a, f.neighbour, 0) != 0) { // exe 0x5e5e00
        a.subState = uint8_t(HoverSub::GiveWay);
        a.enterStep(0);
    }
    if (f.cooldown != 0) {
        --f.cooldown;
        return;
    }
    EncounterState& enc = encounterState();
    if (enc.attackLocksRecent() & kAttackLockLunge) return;
    if (!((1u << (rngStep() & 0xf)) & 0x590u)) return;
    const Player& p = player();
    const int d = distance3(a.pos, p.pos); // exe 0x5e3a10
    if (d <= 0x320 || d >= 0x4b0) return;
    const int ang = angleOf(a.pos, p.pos);
    if (uint32_t(angleDiff(uint32_t(int32_t(p.rotation.y)), uint32_t(ang)) + 0x100) >= 0x200u) return;
    a.subState = uint8_t(HoverSub::Shoot);
    a.enterStep(0);
    enc.markAttackLock(kAttackLockLunge);
}
void giveWay(AnimatedActor& a, FieldHoverbotFields& f) { // exe 0x44c6f0: by how the neighbour moves (exe 0x5e5f70)
    Actor& other = neighbourActor(uint16_t(f.neighbour)); // the id before this tick's classification
    int target = 0;
    const uint8_t st = a.step;
    if (st == 2) {
        if (speedToward(a, 0x200) != 0) {
            a.subState = uint8_t(HoverSub::Circle);
            a.enterStep(0);
        }
    } else if (st <= 1) {
        if (st == 0) {
            a.enterStep(1);
            neighbourClassify(a, f.neighbour);
            f.bits = 0;
            a.acceleration.z = -0x40;
        }
        const uint16_t n = f.neighbour;
        if (n & kNeighbourOpposite) target = headingFromOther(a, other, neighbourSide(f.neighbour), 400); // exe 0x5e5d90
        if (n & kNeighbourCrossingSlow) accelerateToward(a, 0x280);
        if (n & kNeighbourCatching) decelerateToward(a, 0x180);
        if (other.flags == 0 || ((uint16_t(other.pos.x.whole) ^ uint16_t(a.pos.x.whole)) & 0xff00) ||
            ((uint16_t(other.pos.z.whole) ^ uint16_t(a.pos.z.whole)) & 0xff00))
            a.enterStep(2);
    }
    turnYawTo(a.rotation.y, uint32_t(target), 0x40);
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
}
// exe 0x44c810: bumped -- a spin of 0x10 ticks, then the other side, back to circling
void spin(AnimatedActor& a, FieldHoverbotFields& f) {
    switch (a.step) {
    case 0:
        a.enterStep(1);
        a.anim = 3;
        f.timer = 0x10;
        f.bits = 0;
        [[fallthrough]];
    case 1:
        a.rotation.y += 0x80u;
        --f.timer;
        if (f.timer != 0) return;
        a.enterStep(2);
        return;
    case 2:
        a.enterStep(3);
        a.anim = 1;
        [[fallthrough]];
    case 3:
        if (a.animKey.events == 0) return;
        f.neighbour = uint16_t((uint32_t(f.neighbour) ^ kNeighbourSideRight) & ~uint32_t(kHoverBumped));
        a.subState = uint8_t(HoverSub::Circle);
        a.enterStep(0);
        return;
    default: return;
    }
}
// exe 0x44c8b0: face the player, rise, the aimed shot (pool 0x40 stage type 0)
void shoot(AnimatedActor& a, FieldHoverbotFields& f) {
    EncounterState& enc = encounterState();
    switch (a.step) {
    case 0:
        a.enterStep(1);
        f.aimYaw = int16_t(angleOf(player().pos, a.pos));
        a.anim = 3;
        f.bits = kHoverNoHeightFollow;
        [[fallthrough]];
    case 1:
        turnYawTo(a.rotation.y, uint32_t(int32_t(f.aimYaw)), 0x40);
        a.pos.y.whole -= 4u;
        if (a.animKey.flags != 0) {
            a.enterStep(2);
            a.anim = 0;
            enc.markAttackLock(kAttackLockLunge);
            return;
        }
        break;
    case 2: {
        a.enterStep(3);
        const uint32_t shot = allocActor40(kActorStageClass); // exe 0x5e1280
        if (shot) {
            ProjectileActor& s = projectileActorAt(shot);
            HoverShotParams params(s);
            s.loopFlags |= kActorSweepGroup2;
            s.flags = kActorLive;
            s.type = kHoverShotStageType;
            s.pos.x = a.pos.x;
            s.pos.y = a.pos.y;
            s.velocity.setXY(0, 0);
            s.velocity.z = -0x300;
            s.pos.z = a.pos.z;
            // the target: the player's position with y - 100 (integer part)
            const Player& p = player();
            const int16_t target[3] = {p.pos.x.whole, wrap16(p.pos.y.whole - 100), p.pos.z.whole};
            s.rotation.x = int16_t(pitchOf(a.pos, target) + 0x800); // exe 0x5e3ca0
            s.rotation.y = f.aimYaw;
            s.rotation.z = 0;
            params.ticks = 0x96;
            params.attack = statsOf(a).strongAttack;
        }
        a.anim = 1;
    }
        [[fallthrough]];
    case 3:
        if (a.animKey.events != 0) {
            f.cooldown = 0x78;
            a.anim = 2;
            a.subState = uint8_t(HoverSub::Circle);
            a.enterStep(0);
        }
        break;
    default: break;
    }
    enc.markAttackLock(kAttackLockLunge);
}
// exe 0x44ca40 (variant 1): up a little, then back down into the ground -> leaving
void fallBack(const PlayerEnv& env, AnimatedActor& a, FieldHoverbotFields& f) {
    const uint8_t s = a.subState;
    bool rise = false;
    if (s == uint8_t(HoverFallSub::Start)) {
        a.subState = uint8_t(HoverFallSub::Rise);
        a.anim = 2;
        a.velocity.setXY(0, 0);
        a.velocity.z = -0x600;
        f.bits = 0;
        soundPlayAtPos(kSoundFallBack, a.pos);
        rise = true;
    } else if (s == uint8_t(HoverFallSub::Rise)) {
        rise = true;
    } else if (s == uint8_t(HoverFallSub::Down)) {
        keepAboveGround(env, a, kFallBackHeight);
    }
    if (rise) {
        a.pos.y.whole += 0x20u;
        if (keepAboveGround(env, a, kFallBackHeight)) a.subState = uint8_t(HoverFallSub::Down);
    }
    moveAlongRotation(a.pos, a.rotation, a.velocity.x, a.velocity.y, a.velocity.z); // exe 0x5e37b0
    int32_t r = 0;
    if (int32_t(actorTestMove(env, a)) < 0) {
        r = int32_t(actorPushOut(env, a));
        if (r < 0) a.pos = a.prevPos;
    }
    f.fallTicks += 0x60u;
    if (f.fallTicks <= 0x3c00 && r == 0) return;
    a.enterState(uint8_t(HoverState::Leaving));
}
void hover(const PlayerEnv& env, AnimatedActor& a, FieldHoverbotFields& f) { // exe 0x44c4c0 (variant 0)
    switch (HoverSub(a.subState)) {
    case HoverSub::Circle: circle(a, f); break;
    case HoverSub::GiveWay: giveWay(a, f); break;
    case HoverSub::Spin: spin(a, f); break;
    case HoverSub::Shoot: shoot(a, f); break;
    default: notPortedAt(kFieldHoverbotPort, "0x44bea0 variant 0 state 2 sub-state (table 0x88e8d4 has 4)", a.subState);
    }
    if (int32_t(actorTestMove(env, a)) < 0) {
        const int32_t r = int32_t(actorPushOut(env, a));
        if (r != 0) {
            if (r < 0) a.pos = a.prevPos;
            if (!(f.neighbourLow & kHoverBumped)) {
                a.subState = uint8_t(HoverSub::Spin);
                a.enterStep(0);
                f.neighbour |= kHoverBumped;
            }
        }
    }
    if (f.bits & kHoverNoHeightFollow) return;
    followPlayerHeight(a);
    keepAboveGround(env, a, kHoverHeight);
}
void mainState(const PlayerEnv& env, AnimatedActor& a, FieldHoverbotFields& f) { // exe 0x44c450
    a.tint = kTintNeutral;
    if (hurt(a, f)) {
        a.enterState(uint8_t(HoverState::Dying));
        dying(env, a, f);
        return;
    }
    switch (HoverVariant(a.variant)) {
    case HoverVariant::AroundPlayer: hover(env, a, f); break;
    case HoverVariant::FromGround: fallBack(env, a, f); break;
    default: notPortedAt(kFieldHoverbotPort, "0x44bea0 state 2 variant (table 0x88e8cc has 2)", a.variant);
    }
    if (playerLiftTargetCheck(a, 1)) { // exe 0x573a60
        a.enterState(uint8_t(HoverState::Carried));
        a.runFlags |= kActorRunMarked;
        return;
    }
    submitHit(a, f, kHitAttackSolid, 0);
}

// ---- state 3 --------------------------------------------------------------------------------------------------------
void thrown(const PlayerEnv& env, AnimatedActor& a, FieldHoverbotFields& f);
void inHands(const PlayerEnv& env, AnimatedActor& a, FieldHoverbotFields& f) { // exe 0x44cb40: until released
    if (a.step == 0) {
        a.step = 1;
        a.anim = 2;
        encounterUntrack(a); // exe 0x5e6540
    }
    carryAtHands(a, 0, 0, 0);               // exe 0x573ab0
    const CarryRelease r = carryReleased(); // exe 0x573950
    if (r == CarryRelease::Held) return;
    f.aimYaw = player().rotation.y;
    f.hitBox.result = 0;
    a.subState = uint8_t(HoverCarriedSub::Flying);
    a.enterStep(0);
    if (r != CarryRelease::Thrown) {
        a.velocity.y = -0x200;
        a.velocity.z = -0x80;
        f.bits = 0;
    } else {
        int32_t v[3];
        playerAimVector(0, -0x100, -0x500, v); // exe 0x5e3e50
        a.velocity.x = 0;
        a.velocity.y = int16_t(v[1]);
        a.velocity.z = int16_t(v[2]);
        f.bits = kHoverThrownEntry;
    }
    carryDropFromPlayer(a); // exe 0x573990
    thrown(env, a, f);
}
// exe 0x44cc10: flying under gravity; a solid hit -> dying; a wall it turns off -> landed; thrown: it hurts the enemies
void thrown(const PlayerEnv& env, AnimatedActor& a, FieldHoverbotFields& f) {
    if (f.hitBox.result & kHitAttackSolid) {
        a.enterState(uint8_t(HoverState::Dying));
        a.velocity.z = 0;
        a.velocity.x = 0;
        a.hitPoints = -1;
        dying(env, a, f);
        return;
    }
    if (a.step == 0) {
        a.step = 1;
        a.rotation.y = f.aimYaw;
        a.anim = 4;
        a.runFlags &= uint8_t(~kActorRunMarked);
    }
    a.velocity.y += uint32_t(kGravity);
    integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
    if (int32_t(actorTestMove(env, a)) < 0) {
        const uint32_t r = actorPushOut(env, a);
        if (int32_t(r) >= 0) {
            if (reflectRotationOffWalls(a.rotation, r) != 0) { // exe 0x410740
                a.subState = uint8_t(HoverCarriedSub::Landed);
                a.enterStep(0);
            }
        } else {
            a.pos = a.prevPos;
        }
    }
    if (!(f.bits & kHoverThrownEntry)) return;
    const ShortVec4& b = bonePoint(a, 0, kFieldHoverbotPort); // exe 0x5e1810
    const int16_t pos[3] = {b.x, b.y, b.z};
    hitListSubmit(pos, f.hitBox, uint32_t(a.slot) << 24 | 0x208010u, 0x400ff); // exe 0x5e4120
    f.hitBox.result = 0;
    f.hitBox.previousX = b.x;
    f.hitBox.previousZ = b.z;
}
void landed(const PlayerEnv& env, AnimatedActor& a, FieldHoverbotFields& f) { // exe 0x44cd20: -> leaving
    if (a.step != 0) {
        if (a.animEnded()) a.enterState(uint8_t(HoverState::Leaving));
    } else {
        a.step = 1;
        a.anim = 2;
        a.velocity.setXY(0, 0);
        a.velocity.z = -0x600;
        f.bits = 0;
    }
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
    if (int32_t(actorTestMove(env, a)) < 0) {
        const int32_t r = int32_t(actorPushOut(env, a));
        if (r != 0) {
            if (r < 0) a.pos = a.prevPos;
            a.enterState(uint8_t(HoverState::Leaving));
        }
    }
    keepAboveGround(env, a, kHoverHeight);
}
void carried(const PlayerEnv& env, AnimatedActor& a, FieldHoverbotFields& f) { // exe 0x44cb20
    switch (HoverCarriedSub(a.subState)) {
    case HoverCarriedSub::InHands: inHands(env, a, f); break;
    case HoverCarriedSub::Flying: thrown(env, a, f); break;
    case HoverCarriedSub::Landed: landed(env, a, f); break;
    default: notPortedAt(kFieldHoverbotPort, "0x44bea0 state 3 sub-state (table 0x88e8f4 has 3)", a.subState);
    }
}

// ---- states 4 / 5 ---------------------------------------------------------------------------------------------------
// exe 0x44cdc0: a spark, the fall trailing smoke until a wall / the floor -> an explosion, the drops, freed
void dying(const PlayerEnv& env, AnimatedActor& a, FieldHoverbotFields& f) {
    const uint8_t s = a.subState;
    if (s <= 1) {
        if (s == 0) {
            f.bits = 0;
            a.anim = 4;
            const int32_t z = pitchedZ(a.rotation.x, a.velocity.z); // exe 0x40be30 + 0x40bbb0
            a.rotation.x = 0;
            a.velocity.z = int16_t(z);
            a.velocity.y = -0x100;
            a.subState = 1;
            spawnHitSpark(kSparkLoops, a.pos.x.whole, a.pos.y.whole, a.pos.z.whole); // exe 0x5e5400
            countDefeat();                                                       // exe 0x5e53e0
        }
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        a.velocity.y += 0x20u;
        if (f.contact & (kHoverWall | kHoverFloor)) {
            const uint32_t address = allocActor60(0); // exe 0x5e1300: the explosion (game/explosions)
            if (address) {
                StaticActor& x = staticActorAt(address);
                ExplosionParams e(x);
                x.flags = kActorLive;
                x.type = typeByte(GlobalStaticType::Explosion);
                x.variant = 0;
                x.loopFlags |= kActorSweepGroup2;
                x.pos.x.whole = a.pos.x.whole;
                x.pos.y.whole = wrap16(a.pos.y.whole - 0x60);
                x.pos.z.whole = a.pos.z.whole;
                e.spread = 0x40;
                e.flashes = 4;
            }
            spawnDrops(a.pos, statsOf(a).dropTable); // exe 0x5f1270
            freeActor(a);
            return;
        }
    }
    if (!(uint8_t(f.timer) & 1)) { // a smoke puff (static stage type 3 variant 8) every other tick
        const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (address) {
            StaticActor& x = staticActorAt(address);
            SmokeParams smoke(x);
            x.flags = kActorLive;
            x.type = kSmokeType;
            x.variant = kSmokeVariant;
            x.loopFlags |= kActorSweepGroup2;
            x.pos.x.whole = a.pos.x.whole;
            x.pos.y.whole = a.pos.y.whole;
            x.pos.z.whole = a.pos.z.whole;
            smoke.driftX = 0;
            smoke.driftZ = 0;
            smoke.life = 4;
        }
    }
    ++f.timer;
    collide(env, a, f);
}
// exe 0x44cf80: up and away (the 64-step cosine / sine curves of its velocity), the dust while above the ground, freed
// 300 above it
void leaving(const PlayerEnv& env, AnimatedActor& a, FieldHoverbotFields& f) {
    const Trig64Tables& trig = trig64Tables();
    switch (a.subState) {
    case 0:
        a.subState = 1;
        f.timer = 0;
        f.sinkSounded = 0;
        a.velocity.x = 0;
        a.velocity.z = 0;
        a.anim = 3;
        f.bits = 0;
        [[fallthrough]];
    case 1: {
        const int16_t t = trig.cosine[uint32_t(int32_t(f.timer) >> 1)];
        a.velocity.y = int16_t((-int32_t(t) << 8) >> 0xc);
        ++f.timer;
        if (a.animKey.flags != 0) {
            a.subState = 2;
            a.anim = 1;
            f.timer = 0;
        }
        break;
    }
    case 2: {
        const int16_t t = trig.sine[uint32_t(int32_t(f.timer) >> 1)];
        a.velocity.y = int16_t((int32_t(t) << 10) >> 0xc);
        ++f.timer;
        if (a.animKey.flags != 0) a.subState = 3;
        break;
    }
    case 3: ++f.timer; break;
    default: break;
    }
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
    if (a.subState <= 2) collideKeep(env, a);
    const int g = actorGround(env, a);
    if (g < a.pos.y.whole && !(uint8_t(f.timer) & 3)) {
        const int32_t kind = groundKindUnder(a.pos);
        spawnDustSpark(a.pos.x.whole, wrap16(g), a.pos.z.whole);
        spawnStepDust(a.pos.x.whole, wrap16(g), a.pos.z.whole, kind);
        spawnStepDust(a.pos.x.whole, wrap16(g), a.pos.z.whole, kind);
        if (f.sinkSounded == 0) {
            soundPlayAtPos(kSoundSink, a.pos);
            f.sinkSounded = 1;
        }
    }
    if (int(a.pos.y.whole) <= g + 300) return;
    freeActor(a);
}

void fieldHoverbot(const PlayerEnv& env, AnimatedActor& a) { // exe 0x44bea0
    FieldHoverbotFields& f = classState<FieldHoverbotFields>(a);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (HoverState(a.state)) {
    case HoverState::Bind: bind(env, a, f); break;
    case HoverState::Appear: appear(env, a, f); break;
    case HoverState::Main: mainState(env, a, f); break;
    case HoverState::Carried: carried(env, a, f); break;
    case HoverState::Dying: dying(env, a, f); break;
    case HoverState::Leaving: leaving(env, a, f); break;
    default: notPortedAt(kFieldHoverbotPort, "0x44bea0 state (table 0x88e894 has 6)", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim != a.prevAnim) animSetPose(a, a.anim, 0); // exe 0x5e1c30
    else animStepPose(a);                                // exe 0x5e1e80
    // exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow (render output), skipped while state / sub (u16 +8) == 3
}

} // namespace

void fieldHoverbotUpdate(const PlayerEnv& env, uint32_t actor) { fieldHoverbot(env, animatedActorAt(actor)); }

} // namespace od::port
