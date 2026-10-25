// The hovering reaverbot of the Manda ruin, class 0x44d320 (see st14_enemy.hpp). States [0x88e924]: 0 0x44d490 init,
// 1 0x44d5b0 rising (sub-states [0x88e93c]: the rise, then the shared ones one index up), 2 0x44d6a0 main (the shared
// sub-states [0x88e940]: hover, notice, dive, home, knock-back), 3 0x44dfd0 carried (sub-states [0x88e964]: held,
// thrown), 4 0x44e160 dying (it falls; an explosion and its drops where it lands).
//
// Left out (render output): the shadow (exe 0x5f20f0(actor, 0x30, 0x160, 0), not drawn in state 3).
#include "content/manda/st14_enemy.hpp"
#include "content/manda/st14_enemy_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/enemy_motion.hpp"
#include "game/explosions.hpp"
#include "game/vectors.hpp"
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
#include "game/spark_emitter.hpp"
#include "game/story.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace st14_enemy_state;

namespace {

enum class HoverState : uint8_t {
    Init = 0,    // exe 0x44d490
    Rising = 1,  // exe 0x44d5b0
    Main = 2,    // exe 0x44d6a0
    Carried = 3, // exe 0x44dfd0
    Dying = 4,   // exe 0x44e160
};
// the shared sub-states (state 2's table; state 1's runs them one index up, after its rise)
enum class HoverSub : uint8_t {
    Hover = 0,  // exe 0x44d730
    Notice = 1, // exe 0x44d950
    Dive = 2,   // exe 0x44da80
    Home = 3,   // exe 0x44dd20
    Knock = 4,  // exe 0x44dea0
};
constexpr uint8_t kRisingRise = 0; // state 1's sub-state 0: exe 0x44d5d0
enum class CarriedSub : uint8_t { Held = 0, Thrown = 1 }; // exe 0x44dff0 / 0x44e0c0
enum class DyingSub : uint8_t { Start = 0, Falling = 1 };

// +0x154
constexpr uint8_t kHoverAltEntry = 0x01;   // its hit entry is a thrown body's
constexpr uint8_t kHoverDiving = 0x02;     // its touch is a thrown attack; not knocked back by the level hits
constexpr uint8_t kHoverBounces = 0x04;    // reflected off the walls
constexpr uint8_t kHoverNoHitEntry = 0x08;
constexpr uint8_t kHoverNoDamage = 0x10;
// +0x155
constexpr uint8_t kHoverContactWall = 0x01, kHoverContactBounced = 0x02, kHoverContactBlocked = 0x08;
constexpr uint8_t kHoverContactFloor = 0x10;
constexpr uint8_t kHoverContactLanding = kHoverContactWall | kHoverContactFloor;
// the test move's bit 0x20000000 (its meaning is not traced)
constexpr uint32_t kTestMoveBit29 = 0x20000000u;

// exe 0x44e450's result
enum class HitResult : uint8_t { None = 0, Hit = 1, Knocked = 2, Dead = 3 };

const DataTable<CollisionBoxRecord> hoverBox{TableId::St14EnemyHoverBox};
const DataTable<InteractionBoxRecord> hoverLiftBox{TableId::St14EnemyHoverLiftBox};
const DataTable<ScalarTable<uint8_t, 4>> hoverModels{TableId::St14EnemyHoverModels}; // by the spawn byte
const DataTable<ScalarTable<int16_t, 0x10>> noticeRanges{TableId::St14EnemyNoticeRanges}; // by the range parameter
const DataTable<ScalarTable<int8_t, 0x10>> hoverPauses{TableId::St14EnemyHoverPauses}; // the hover's pauses (by the RNG)
constexpr uint16_t kSoundHurt = 0x8d, kSoundDive = 0xa8;
constexpr int16_t kRiseSpeed = 0x200, kRiseBrake = 0x20;
constexpr uint32_t kStatsStride = 0x30, kStatsOffset = 0x20; // the type's rows: 0x30 per spawn byte, its stats at +0x20

// its stats row (the type's table, 0x30 bytes per spawn byte, the row at +0x20)
AnimatedActorStats hoverStats(const AnimatedActor& a) {
    return animatedStatsAt(statTableRow(actorStatTables().animated, a.type) + uint32_t(a.spawnByte) * kStatsStride +
                              kStatsOffset);
}
// the actor's position as 16.16 values (the exe's points on its stack)
void positionValues(const AnimatedActor& a, int32_t out[3]) {
    out[0] = a.pos.x;
    out[1] = a.pos.y;
    out[2] = a.pos.z;
}
// a stack point with only its integer words written (the exe leaves the fraction words uninitialised; 0 here)
void wholePoint(int16_t x, int16_t y, int16_t z, int32_t out[3]) {
    out[0] = int32_t(uint32_t(uint16_t(x)) << 16);
    out[1] = int32_t(uint32_t(uint16_t(y)) << 16);
    out[2] = int32_t(uint32_t(uint16_t(z)) << 16);
}
// exe 0x5a0770(angle, target, step): the angle snapped onto the target within step, else turned by step toward it
void pitchStep(int16_t& angle, int target, int step) {
    const int d = angleDiff(uint32_t(int32_t(angle)), uint32_t(target)); // exe 0x5e3d80
    if (uint32_t(d + step) < uint32_t(step * 2)) {
        angle = int16_t(target);
        return;
    }
    angle = int16_t(angle + (d < 1 ? -step : step));
}
// exe 0x44e430(a, b): b * b / (a << 5), 0 when a == 0
int swingAcceleration(int a, int b) {
    if (a == 0) return 0;
    return (b * b) / (a << 5);
}
// exe 0x44e3c0(actor, point, step): the yaw toward the point, snapped when within step
void turnTowardPoint(AnimatedActor& a, const int32_t point[3], int step) {
    const int ang = angleOf(point[0], point[2], a.pos.x, a.pos.z); // exe 0x5e3bc0(point, position)
    const int d = turnYawTo(a.rotation.y, uint32_t(ang), step);    // exe 0x5e3df0
    if (uint32_t(d + step) < uint32_t(step * 2)) a.rotation.y = int16_t(ang);
}
// exe 0x44e400(actor, point, step): the pitch toward the point
void pitchTowardPoint(AnimatedActor& a, const int32_t point[3], int step) {
    int32_t from[3];
    positionValues(a, from);
    pitchStep(a.rotation.x, pitchOf(from, point) + kHalfTurn, step); // exe 0x5e3ca0, 0x5a0770
}
// exe 0x40be30 on an identity (Rx(pitch)) then exe 0x40bbb0 (game/matrix transformVectorFloat) of (0, 0, z): the out z
int32_t pitchedZ(int pitch, int z) {
    const int32_t v[3] = {0, 0, z};
    int32_t o[3];
    transformVectorFloat(rotationX(pitch), v, o);
    return o[2];
}
// the move rotation kept from the rotation (the sub-states' tail)
void keepMoveRotation(AnimatedActor& a, HoverFields& f) {
    f.movePitch = a.rotation.x;
    f.moveYaw = a.rotation.y;
}
// exe 0x5e3810 along the move rotation (the exe's stack angles {pitch, yaw, 0})
void moveAlongMoveRotation(AnimatedActor& a, const HoverFields& f) {
    const int16_t rotation[3] = {f.movePitch, f.moveYaw, 0};
    moveAlongRotation(a.pos, rotation, 0, 0, a.velocity.z);
}

// exe 0x44e330: the walls and the ground -> the contact bits
void collide(const PlayerEnv& env, AnimatedActor& a, HoverFields& f) {
    f.contact = 0;
    const uint32_t r = actorTestMove(env, a); // exe 0x40f570
    if (r & kTestMoveBit29) f.contact |= kHoverContactBlocked;
    if (int32_t(r) >= 0) return;
    const uint32_t q = actorPushOut(env, a); // exe 0x410960
    if (int32_t(q) < 0) a.pos = a.prevPos;
    if (q & 0xf00) f.contact |= kHoverContactWall;
    if (q & 0x3000) f.contact |= kHoverContactFloor;
    if ((f.mode & kHoverBounces) && reflectRotationOffWalls(a.rotation, q) != 0) // exe 0x410740
        f.contact |= kHoverContactBounced;
}
// exe 0x44e450: the hit box's damage -- a level hit knocks it back (not while diving), a hit of the player's
HitResult takeDamage(AnimatedActor& a, HoverFields& f) {
    const uint32_t v = f.hitBox.result;
    const uint32_t d = v & kHitDamageMask;
    if (d == 0) return HitResult::None;
    HitResult r = HitResult::None;
    if ((v & (kHitLevelWithin | kHitLevelBelow)) && !(f.mode & kHoverDiving)) r = HitResult::Knocked;
    else if (v & (kHitAttackByPlayer | kHitAttackStrong)) r = HitResult::Hit;
    a.tint = kTintFlash;
    soundPlayAtPos(kSoundHurt, a.pos);                       // exe 0x5d5380
    if (hitPointsAdd(a, -int(d)) != 0) return HitResult::Dead; // exe 0x5e52f0
    return r;
}

// ---- state 4 (and the kill from states 2 / 3): exe 0x44e160 -------------------------------------------------------
void dying(const PlayerEnv& env, AnimatedActor& a, HoverFields& f) {
    const uint8_t sub = a.subState;
    if (sub == uint8_t(DyingSub::Start)) { // exe 0x44e1db
        f.mode = kHoverNoDamage;
        a.anim = 4;
        a.rotation.x = f.movePitch;
        a.rotation.y = f.moveYaw;
        const int32_t z = pitchedZ(a.rotation.x, a.velocity.z); // exe 0x40be30 + 0x40bbb0
        a.rotation.x = 0;
        a.velocity.z = int16_t(z);
        a.velocity.y = -0x100;
        a.subState = uint8_t(DyingSub::Falling);
        spawnHitSpark(kActorSweepGroup2, a.pos.x.whole, a.pos.y.whole, a.pos.z.whole); // exe 0x5e5400
        countDefeat();                                                                 // exe 0x5e53e0
    }
    if (sub <= uint8_t(DyingSub::Falling)) { // exe 0x44e28c: falling
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        a.velocity.y = int16_t(a.velocity.y + 0x20);
        if (f.contact & kHoverContactLanding) {
            if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
                StaticActor& x = staticActorAt(address);
                x.variant = 0;
                x.loopFlags |= kActorSweepGroup2;
                x.flags = kActorLive;
                x.type = typeByte(GlobalStaticType::Explosion);
                x.pos.x.whole = a.pos.x.whole;
                x.pos.y.whole = int16_t(a.pos.y.whole - 0x60);
                x.pos.z.whole = a.pos.z.whole;
                ExplosionParams params(x);
                params.spread = 0x40;
                params.flashes = 4;
            }
            spawnDrops(a.pos, hoverStats(a).dropTable); // exe 0x5f1270
            freeActor(a);                                // exe 0x5e1490
            return;
        }
    }
    // exe 0x44e17b: a smoke emitter (stage static type 3 variant 8) every other tick
    if (!(uint16_t(f.timer) & 1)) {
        if (const uint32_t address = allocActor60(kActorStageClass)) { // exe 0x5e12f0
            StaticActor& smoke = staticActorAt(address);
            SparkEmitterParams& params = classState<SparkEmitterParams>(smoke);
            smoke.flags = kActorLive;
            smoke.type = kSparkEmitterStageType;
            smoke.variant = 8;
            smoke.loopFlags |= kActorSweepGroup2;
            smoke.pos.x.whole = a.pos.x.whole;
            smoke.pos.y.whole = a.pos.y.whole;
            smoke.pos.z.whole = a.pos.z.whole;
            params.driftX = 0;
            params.driftZ = 0;
            params.lifetime = 4;
        }
    }
    ++f.timer;
    collide(env, a, f);
}

// ---- the sub-states ---------------------------------------------------------------------------------------------------
// exe 0x5e4040(v, -0x20) as content/manda/st12_14's brakingDistance inlines it (16.16): the distance the rise covers
// while it brakes
int32_t riseBrakingDistance(int32_t v) {
    int32_t acc = -kRiseBrake;
    if (v < acc) {
        v = -v;
        acc = -acc;
    }
    const int32_t n = int32_t(uint32_t(v) << 12);
    return int32_t(0u - uint32_t(n / int32_t(uint32_t(acc) << 12)) * uint32_t(n >> 1));
}
enum class RiseStep : uint8_t { Start = 0, Rise = 1, Brake = 2, Wait = 3 };
// exe 0x44d5d0 (state 1 sub 0, variant 1): risen from below to the home height, then state 2 at the animation's mark
void rise(AnimatedActor& a, HoverFields& f) {
    const int dy = f.home.y - a.pos.y.whole;
    switch (RiseStep(a.step)) {
    case RiseStep::Start:
        a.anim = 0;
        a.velocity.y = kRiseSpeed;
        a.enterStep(uint8_t(RiseStep::Rise));
        [[fallthrough]];
    case RiseStep::Rise:
        integrateVelocity(a, 0, a.velocity.y, 0);
        if (riseBrakingDistance(a.velocity.y) >= int32_t(uint32_t(dy) << 16)) a.enterStep(uint8_t(RiseStep::Brake));
        return;
    case RiseStep::Brake:
        integrateVelocity(a, 0, a.velocity.y, 0);
        a.velocity.y = int16_t(a.velocity.y - kRiseBrake);
        if (dy <= 0 || a.velocity.y <= 0) {
            a.anim = 1;
            a.pos.y.whole = f.home.y;
            a.enterStep(uint8_t(RiseStep::Wait));
        }
        return;
    case RiseStep::Wait:
        if (a.animKey.events & 1) a.enterState(uint8_t(HoverState::Main));
        return;
    default: return;
    }
}
// noticed: the sub-state s at its step 0
void toSub(AnimatedActor& a, HoverSub s) {
    a.subState = uint8_t(s);
    a.restartStep();
}
// the player within the notice range of the home's range parameter
bool playerInRange(const AnimatedActor& a) {
    return planarDistance(a.pos, player().pos) < noticeRanges[size_t(int32_t(HoverReaverbotParams(a).range))];
}
enum class HoverStep : uint8_t { Start = 0, Pause = 1, Wait = 2, SwingStart = 3, Swing = 4, SwingBack = 5 };
// exe 0x44d730: drifts (the yaw spinning, a vertical swing between pauses) until the player comes near or a hit
void hover(AnimatedActor& a, HoverFields& f) {
    switch (HoverStep(a.step)) { // table 0x44d934
    case HoverStep::Start:
        f.mode = 0;
        a.velocity.y = 0;
        a.velocity.x = 0;
        a.anim = 2;
        a.spin.y = HoverReaverbotParams(a).reverseCircle != 0 ? 0x20 : -0x20;
        a.enterStep(uint8_t(HoverStep::Pause));
        [[fallthrough]];
    case HoverStep::Pause: { // exe 0x44d77e
        const uint32_t r = rngStep();
        f.timer = int16_t(int32_t(hoverPauses[r & 0xf]));
        a.enterStep(uint8_t(HoverStep::Wait));
        [[fallthrough]];
    }
    case HoverStep::Wait: // exe 0x44d7ac
        --f.timer;
        if (uint16_t(f.timer) == 0) a.enterStep(uint8_t(HoverStep::SwingStart));
        break;
    case HoverStep::SwingStart: { // exe 0x44d7cb
        a.velocity.y = 0;
        int v = swingAcceleration(0x40, 0x100); // exe 0x44e430
        const uint32_t r = rngStep();
        if (r & 1) v = -v;
        a.acceleration.y = int16_t(v);
        a.enterStep(uint8_t(HoverStep::Swing));
        [[fallthrough]];
    }
    case HoverStep::Swing:
    case HoverStep::SwingBack: { // exe 0x44d80a
        const int16_t acc = a.acceleration.y;
        a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(acc));
        const int16_t vy = a.velocity.y;
        if (a.stepCounter == 0) {
            if ((vy < 0 ? -int(vy) : int(vy)) < 0x100) break;
            a.acceleration.y = int16_t(-int(acc));
            a.stepCounter = 1;
            a.velocity.y = a.velocity.y <= 0 ? -0x100 : 0x100;
        } else {
            const int aa = acc < 0 ? -int(acc) : int(acc);
            if (uint32_t(int(vy) + aa) >= uint32_t(aa * 2)) break;
            a.velocity.y = 0;
            a.velocity.x = 0;
            if (a.step == uint8_t(HoverStep::Swing)) {
                a.step = uint8_t(HoverStep::SwingBack);
                a.stepCounter = 0;
            } else {
                a.enterStep(uint8_t(HoverStep::Pause));
            }
        }
        break;
    }
    default: break;
    }
    // exe 0x44d887
    stepTowardLimit(a.velocity.z, 0xfff8, -int(f.cruiseSpeed)); // exe 0x5e5510
    integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
    a.rotation.y = int16_t(uint16_t(a.rotation.y) + uint16_t(a.spin.y));
    if (a.rotation.x != 0) pitchStep(a.rotation.x, 0, 0x20); // exe 0x5a0770
    if (f.noticeCooldown != 0) --f.noticeCooldown;
    if (f.noticeCooldown == 0 && playerInRange(a)) toSub(a, HoverSub::Notice); // exe 0x5e3b00
    if (f.hitResult != 0) toSub(a, HoverSub::Notice);
    keepMoveRotation(a, f);
}
enum class NoticeStep : uint8_t { Start = 0, Look = 1, TurnStart = 2, Turn = 3, BackOff = 4, BackingOff = 5 };
// exe 0x44d950: notices the player, turns to it 0x3c ticks; near -> the dive, far -> backs off to the hover
void notice(AnimatedActor& a, HoverFields& f) {
    switch (NoticeStep(a.step)) { // table 0x44da64
    case NoticeStep::Start:
        f.mode = 0;
        a.anim = 3;
        a.enterStep(uint8_t(NoticeStep::Look));
        break;
    case NoticeStep::Look:
        if (a.animEnded()) {
            a.anim = 0;
            a.enterStep(uint8_t(NoticeStep::TurnStart));
        }
        break;
    case NoticeStep::TurnStart:
        f.timer = 0x3c;
        a.enterStep(uint8_t(NoticeStep::Turn));
        [[fallthrough]];
    case NoticeStep::Turn: { // exe 0x44d9b1
        const int ang = angleOf(player().pos, a.pos);    // exe 0x5e3bc0(player, position)
        turnYawTo(a.rotation.y, uint32_t(ang), 0x20);    // exe 0x5e3df0
        --f.timer;
        if (uint16_t(f.timer) == 0) {
            if (playerInRange(a)) toSub(a, HoverSub::Dive);
            else a.enterStep(uint8_t(NoticeStep::BackOff));
        }
        if (f.hitResult != 0) toSub(a, HoverSub::Dive);
        break;
    }
    case NoticeStep::BackOff: // exe 0x44da20
        a.anim = 1;
        f.noticeCooldown = 0x3c;
        a.enterStep(uint8_t(NoticeStep::BackingOff));
        break;
    case NoticeStep::BackingOff: // exe 0x44da36
        if (a.animKey.events != 0) toSub(a, HoverSub::Hover);
        break;
    default: break;
    }
    keepMoveRotation(a, f);
}
enum class DiveStep : uint8_t { Start = 0, Aim = 1, Dive = 2, PullUp = 3, LevelOut = 4 };
constexpr int16_t kDiveAbovePlayer = 0xa0, kPullUpHeight = 0x64;
// exe 0x44da80: dives at the player's position (the sound), pulls up, levels out -> home
void dive(AnimatedActor& a, HoverFields& f) {
    switch (DiveStep(a.step)) { // table 0x44dd08
    case DiveStep::Start:
        f.mode = kHoverDiving;
        a.anim = 1;
        a.velocity.setXY(0, 0);
        a.velocity.z = 0;
        a.enterStep(uint8_t(DiveStep::Aim));
        break;
    case DiveStep::Aim: {
        if (!a.animEnded()) break;
        const Player& p = player();
        f.target.x = p.pos.x.whole;
        f.target.y = int16_t(uint16_t(p.pos.y.whole) - uint16_t(kDiveAbovePlayer));
        f.target.z = p.pos.z.whole;
        if (pointVisibleOrNear(a.pos, 0) != 0) f.target.y = p.pos.y.whole; // exe 0x5e59a0
        a.anim = 2;
        a.rotation.x = 0x400;
        soundPlayAtPos(kSoundDive, a.pos); // exe 0x5d5380
        a.enterStep(uint8_t(DiveStep::Dive));
        break;
    }
    case DiveStep::Dive: { // exe 0x44db42
        const int16_t v = a.velocity.z;
        a.velocity.z = v >= -0x400 ? int16_t(v - 0x20) : int16_t(-0x400);
        int32_t point[3];
        wholePoint(f.target.x, f.target.y, f.target.z, point);
        turnTowardPoint(a, point, 0x40);  // exe 0x44e3c0
        pitchTowardPoint(a, point, 0x40); // exe 0x44e400
        const int d = a.pos.y.whole - f.target.y;
        if (((d < 0 ? -d : d) << 4) < -int(a.velocity.z) || (f.contact & kHoverContactLanding)) {
            f.target.y = f.home.y;
            a.enterStep(uint8_t(DiveStep::PullUp));
        }
        keepMoveRotation(a, f);
        break;
    }
    case DiveStep::PullUp: { // exe 0x44dbf8
        const int16_t v = a.velocity.z;
        a.velocity.z = v < -0x200 ? int16_t(v + 0x20) : int16_t(-0x200);
        // the point above it: its x / z dwords, its height's integer word (the fraction word: the exe's stack, 0 here)
        const int32_t point[3] = {a.pos.x, int32_t(uint32_t(uint16_t(a.pos.y.whole - kPullUpHeight)) << 16), a.pos.z};
        pitchTowardPoint(a, point, 0x80); // exe 0x44e400
        const int d = a.pos.y.whole - f.target.y;
        if (((d < 0 ? -d : d) << 4) < -int(a.velocity.z) || (f.contact & kHoverContactLanding)) {
            f.noticeCooldown = 0x3c;
            a.enterStep(uint8_t(DiveStep::LevelOut));
        }
        keepMoveRotation(a, f);
        break;
    }
    case DiveStep::LevelOut: { // exe 0x44dc91
        const int16_t v = a.velocity.z;
        a.velocity.z = v < 0 ? int16_t(v + 0x30) : int16_t(0);
        pitchStep(a.rotation.x, 0, 0x40); // exe 0x5a0770
        if (a.velocity.z == 0 && a.rotation.x == 0) toSub(a, HoverSub::Home);
        break;
    }
    default: break;
    }
    // exe 0x44dcc9
    moveAlongMoveRotation(a, f); // exe 0x5e3810
}
enum class HomeStep : uint8_t { Start = 0, Fly = 1 };
// exe 0x44dd20: flies back to the home point (around a wall it touches), then the hover
void home(AnimatedActor& a, HoverFields& f) {
    int32_t point[3];
    switch (HomeStep(a.step)) {
    case HomeStep::Start:
        f.mode = 0;
        a.velocity.y = 0;
        a.velocity.x = 0;
        a.anim = 2;
        a.enterStep(uint8_t(HomeStep::Fly));
        [[fallthrough]];
    case HomeStep::Fly: { // exe 0x44dd58
        wholePoint(f.home.x, f.home.y, f.home.z, point);
        if (f.contact & kHoverContactWall) {
            const int dist = distance3(a.pos, point);                       // exe 0x5e3a60
            const int ang = angleOf(point[0], point[2], a.pos.x, a.pos.z); // exe 0x5e3bc0
            const int d = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(ang)); // exe 0x5e3d80
            if (uint32_t(d + 0x400) < 0x800u) {
                const int32_t e = (HoverReaverbotParams(a).reverseCircle != 0 ? 0x800 : 0) - 0x400 + a.rotation.y;
                // the exe's index of the 64-step tables, its literal shifts: 0 or -1 (the short before each table)
                const int32_t i = (e >> 0xb) >> 6;
                const Trig64Tables& t = trig64Tables();
                // (-1: the short before each table, alignment padding no code writes: 0)
                const int32_t sx = i < 0 ? 0 : t.cosine[size_t(i)];
                const int32_t sz = i < 0 ? 0 : t.sine[size_t(i)];
                point[0] = int32_t(uint32_t(uint16_t(uint32_t((sx * dist) >> 0xb) + uint16_t(a.pos.x.whole))) << 16);
                point[2] = int32_t(uint32_t(uint16_t(uint32_t((sz * dist) >> 0xb) + uint16_t(a.pos.z.whole))) << 16);
            }
        }
        turnTowardPoint(a, point, 0x20);  // exe 0x44e3c0
        pitchTowardPoint(a, point, 0x20); // exe 0x44e400
        break;
    }
    default: // the exe reads its uninitialised stack point (the sub-state's steps are only 0 and 1)
        notPortedAt(kStoryPort, "exe 0x44dd20 past its step 1 (an uninitialised stack point)", a.step);
    }
    // exe 0x44de29
    stepTowardLimit(a.velocity.z, 0xfff8, -int(f.cruiseSpeed));  // exe 0x5e5510
    moveAlongRotation(a.pos, a.rotation, 0, 0, a.velocity.z); // exe 0x5e37b0(actor, 0, 0, speed)
    if (distance3(a.pos, point) < -int(f.cruiseSpeed)) toSub(a, HoverSub::Hover); // exe 0x5e3a60
    if (f.hitResult != 0) toSub(a, HoverSub::Notice);
    keepMoveRotation(a, f);
}
enum class KnockStep : uint8_t { Start = 0, Fly = 1 };
// exe 0x44dea0: knocked back away from the player, spinning, until it stops -> home
void knock(AnimatedActor& a, HoverFields& f) {
    switch (KnockStep(a.step)) {
    case KnockStep::Start: {
        f.mode = kHoverNoDamage;
        a.velocity.y = 0;
        a.velocity.x = 0;
        a.velocity.z = 0x400;
        a.anim = 2;
        const int ang = angleOf(player().pos, a.pos); // exe 0x5e3bc0(player, position)
        const uint32_t r = rngStep();
        f.moveYaw = int16_t((r & 0x3ff) + uint32_t(ang) - 0x1ffu);
        f.movePitch = a.rotation.x;
        const int d = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(ang)); // exe 0x5e3d80
        a.enterStep(uint8_t(KnockStep::Fly));
        a.spin.y = d <= 0 ? 0x80 : -0x80;
        f.spinAccel = d <= 0 ? -0x10 : 0x10;
        [[fallthrough]];
    }
    case KnockStep::Fly: { // exe 0x44df63
        moveAlongMoveRotation(a, f); // exe 0x5e3810
        a.velocity.z = int16_t(uint16_t(a.velocity.z) - 0x80u);
        const uint16_t spin = uint16_t(a.spin.y);
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + spin);
        a.spin.y = int16_t(uint16_t(f.spinAccel) + spin);
        if (a.velocity.z <= 0) {
            a.velocity.z = 0;
            toSub(a, HoverSub::Home);
        }
        return;
    }
    default: return;
    }
}
// the shared sub-states
void hoverSub(AnimatedActor& a, HoverFields& f, uint32_t k) {
    switch (HoverSub(k)) {
    case HoverSub::Hover: hover(a, f); return;
    case HoverSub::Notice: notice(a, f); return;
    case HoverSub::Dive: dive(a, f); return;
    case HoverSub::Home: home(a, f); return;
    case HoverSub::Knock: knock(a, f); return;
    default: notPortedAt(kStoryPort, "0x44d320 sub-state (runs into data)", k);
    }
}

// ---- the states -----------------------------------------------------------------------------------------------------
// exe 0x44d490: the model of its spawn byte, the stats, the boxes, the home; the cruise speed; state 2 (variant 1: 1,
// rising from its range's depth below)
void init(AnimatedActor& a, HoverFields& f) {
    if (!bindActorModel(a, hoverModels[a.spawnByte])) { // exe 0x5e0bc0
        freeActor(a);                                   // exe 0x5e1490
        return;
    }
    a.anim = 0;
    a.prevAnim = 0xff;
    const uint32_t row = statTableRow(actorStatTables().animated, a.type);
    const uint32_t hp = row != 0 ? uint32_t(hoverStats(a).hitPoints) : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.velocity.setXY(0, 0);
    a.velocity.setZSpare(0, 0);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.collisionBox.pointAt(hoverBox.stateAddress());
    a.interactionBox.pointAt(hoverLiftBox.stateAddress());
    a.interaction = kInteractLift;
    a.airborne = 0;
    a.onPlatform = 0;
    a.platform = nullptr;
    a.actorHit = 0;
    a.clearClassBlock(); // exe 0x40dc40(+0x14c, +0x168)
    f.home.x = a.pos.x.whole;
    f.home.y = a.pos.y.whole;
    f.home.z = a.pos.z.whole;
    const int32_t sn40 = fixedSin(0x40), sn3e0 = fixedSin(0x3e0); // exe 0x40b920 (the sine)
    const int32_t q = int32_t(uint32_t(-sn40) << 7) / sn3e0;
    a.enterState(uint8_t(HoverState::Main));
    f.cruiseSpeed = int16_t(uint32_t(q) << 4);
    if (a.variant == 1) {
        f.home.y = int16_t(uint16_t(int32_t(HoverReaverbotParams(a).range) << 7) + uint16_t(a.pos.y.whole));
        a.enterState(uint8_t(HoverState::Rising));
    }
}
// exe 0x44d6a0: the damage (dead: dying at once; knocked: the knock-back), the sub-state, the collision, the lift
void mainState(const PlayerEnv& env, AnimatedActor& a, HoverFields& f) {
    if (!(f.mode & kHoverNoDamage)) {
        const HitResult r = takeDamage(a, f); // exe 0x44e450
        if (r == HitResult::Dead) {
            a.enterState(uint8_t(HoverState::Dying));
            dying(env, a, f); // exe 0x44e160
            return;
        }
        if (r == HitResult::Knocked) toSub(a, HoverSub::Knock);
        f.hitResult = uint8_t(r);
    } else {
        f.hitResult = 0;
    }
    hoverSub(a, f, a.subState); // table 0x88e940
    collide(env, a, f);         // exe 0x44e330
    if (playerLiftTargetCheck(a, 1)) { // exe 0x573a60
        a.enterState(uint8_t(HoverState::Carried));
        f.mode |= kHoverNoHitEntry;
    }
}
// exe 0x44e0c0 (state 3 sub 1): thrown / dropped, it falls; a solid hit kills it; a bounce sends it home
void thrown(const PlayerEnv& env, AnimatedActor& a, HoverFields& f) {
    if (f.hitBox.result & kHitAttackSolid) {
        a.enterState(uint8_t(HoverState::Dying));
        a.velocity.z = 0;
        a.velocity.x = 0;
        a.hitPoints = -1;
        dying(env, a, f); // exe 0x44e160
        return;
    }
    if (a.step == 0) {
        a.enterStep(1);
        a.anim = 4;
    }
    a.velocity.y = int16_t(uint16_t(a.velocity.y) + 0x40u);
    integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
    if (f.contact & kHoverContactBounced) {
        a.velocity.y = 0;
        a.velocity.z = -0x400;
        a.state = uint8_t(HoverState::Main);
        toSub(a, HoverSub::Home);
    }
    collide(env, a, f); // exe 0x44e330
}
// exe 0x44dff0 (state 3 sub 0): at the player's hands until thrown / dropped
void held(const PlayerEnv& env, AnimatedActor& a, HoverFields& f) {
    if (a.step == 0) {
        f.mode = kHoverNoHitEntry;
        a.step = 1;
        a.anim = 2;
        a.rotation.x = 0;
    }
    carryAtHands(a, 0, 0, 0);                 // exe 0x573ab0
    const CarryRelease rel = carryReleased(); // exe 0x573950
    if (rel == CarryRelease::Held) return;
    a.subState = uint8_t(CarriedSub::Thrown);
    a.restartStep();
    if (rel == CarryRelease::Thrown) {
        int32_t v[3];
        playerAimVector(0, -0x100, -0x500, v); // exe 0x5e3e50
        a.velocity.x = 0;
        a.velocity.y = int16_t(v[1]);
        a.velocity.z = int16_t(v[2]);
        f.mode = kHoverAltEntry | kHoverBounces;
    } else {
        a.velocity.y = -0x200;
        a.velocity.z = -0x80;
        f.mode = kHoverBounces;
    }
    a.rotation.y = player().rotation.y;
    carryDropFromPlayer(a); // exe 0x573990
    thrown(env, a, f);      // exe 0x44e0c0
}
// exe 0x44d320
void update(const PlayerEnv& env, AnimatedActor& a) {
    auto&& f = classState<HoverFields>(a);
    a.prevAnim = a.anim;
    a.prevPos = a.pos;
    a.tint = kTintNeutral;
    a.prevRotation = a.rotation;
    a.savePreviousContact();
    switch (HoverState(a.state)) { // table 0x88e924
    case HoverState::Init: init(a, f); break;
    case HoverState::Rising: // exe 0x44d5b0: table 0x88e93c
        if (a.subState == kRisingRise) rise(a, f);
        else if (a.subState <= 5) hoverSub(a, f, a.subState - 1u);
        else notPortedAt(kStoryPort, "0x44d5b0 sub-state (table 0x88e93c runs into data)", a.subState);
        break;
    case HoverState::Main: mainState(env, a, f); break;
    case HoverState::Carried: // exe 0x44dfd0: table 0x88e964
        switch (CarriedSub(a.subState)) {
        case CarriedSub::Held: held(env, a, f); break;
        case CarriedSub::Thrown: thrown(env, a, f); break;
        default: notPortedAt(kStoryPort, "0x44dfd0 sub-state (table 0x88e964 runs into other code)", a.subState);
        }
        break;
    case HoverState::Dying: dying(env, a, f); break;
    default: notPortedAt(kStoryPort, "0x44d320 state (table 0x88e924 runs into data)", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    const uint8_t mode = f.mode;
    if (a.anim == a.prevAnim) animStepPose(a); // exe 0x5e1e80
    else animSetPose(a, a.anim, 0);            // exe 0x5e1c30
    if (!(a.flags & kActorDrawn)) return;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    f.hitBox.result = 0;
    const AnimatedActorStats stats = hoverStats(a);
    if (a.hitPoints < 0 || (mode & kHoverNoHitEntry)) return;
    uint32_t owner, attack;
    if (!(mode & kHoverAltEntry)) {
        attack = (uint32_t(mode & kHoverDiving) << 0x12) | stats.attack | kHitAttackSolid;
        owner = hitOwner(a.slot) | hitHeightLevel(stats.hitHeight) | hitGroup(kHitGroupBodies) | kHitCompareHeights |
                hitRadius(0x40);
    } else {
        attack = uint32_t(stats.bodyAttack) | kHitAttackByPlayer;
        owner = hitOwner(a.slot) | hitGroup(kHitGroupPlayerAttacks) | hitRadius(0x40);
    }
    const ShortVec4& bone = bonePoint(a, 0, kStoryPort); // exe 0x5e1810
    const int16_t pos[3] = {bone.x, bone.y, bone.z};
    hitListSubmit(pos, f.hitBox, owner, attack); // exe 0x5e4120
    // the state's word != 3: exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow (render output)
}

} // namespace

void hoverReaverbotUpdate(const PlayerEnv& env, uint32_t actor) { update(env, animatedActorAt(actor)); }

} // namespace od::port
