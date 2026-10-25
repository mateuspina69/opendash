// Tron Bonne's tank in Pokte Village (ST25 sub 6, story 3): the boss of the fight that follows script 0x1b, spawned by
// the sub-6 room handler 0x581bb0 (record 0x8c9220: pool 0x20, the ST25 stage table type 0x11 sub 0). Transcribed from
// dash2.exe one function per exe function (conventions as content/first_ruin/ruin_story.cpp), bit-exact against
// tests/data/story_ST25_boss*.bin. "Arm" / "missile" / "bomb" below are
// hypotheses from the code (bone groups, spawned pool-0x40 classes), not verified on screen. Render output (the shadow
// exe 0x5f20f0) is skipped with a comment.
//
// The classes the fight spawns live elsewhere: the ST25 stage table (*0xa6bc58 = 0x8ea758) maps pool 0x40 types
// 3 / 7 / 9 to the same functions as ST0F / the sky ship -- 0x5c3ae0 (the shock wave, variant 7:
// content/first_ruin/ruin_boss), 0x5c4a90 (the bombs: game/lobbed_shot), 0x5c52f0 (the missiles, variant
// 3: game/enemy_shot); its pool 0xa0 table (0x8ea798) types 3 / 7 = 0x42ec70 (the ring, content/first_ruin/ruin_boss) /
// 0x431120 (content/intro/crash_landing). Pool 0xa0 global type 3 (the dust: game/skid_sparks) and static global type
// 7 (the blast: game/explosions) are game classes. The story_ST25_boss / fight goldens run every pool ported
// (OD_LOG_UNPORTED=1 names none).
#include "content/calbania/tron_boss.hpp"
#include "content/calbania/tron_boss_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/distances.hpp"
#include "game/enemy_shot.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/matrix.hpp"
#include "game/message.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story_flags.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace tron_boss_state;

namespace {

const PlayerEnv* E = nullptr;

constexpr const char* kTronBossPort = "tron boss port"; // the area of its port-gap errors

int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }

constexpr uint32_t kTronFightBlockOffset = 0x20; // in its bone block


// +0x158
constexpr uint32_t kTronDead = 0x1;
constexpr uint32_t kTronLeftArmOff = 0x8, kTronRightArmOff = 0x10;
constexpr uint32_t kTronOnGround = 0x20;        // kept on the ground
constexpr uint32_t kTronRear = 0x1000;          // the missiles from the rear
constexpr uint32_t kTronFrontFired = 0x2000;    // the last missile attack was the front one
constexpr uint32_t kTronStrongBody = 0x4000;    // the body's attack strong (the charge, the jumps)
constexpr uint32_t kTronBounces = 0x8000;       // a wall bounces it away (the big jump)
constexpr uint32_t kTronJumpMedium = 0x20000, kTronJumpLong = 0x40000;
constexpr uint32_t kTronSecondTauntRound = 0x80000;
constexpr uint32_t kTronHalfMessage = 0x100000, kTronThirdMessage = 0x200000, kTronQuarterMessage = 0x400000,
                   kTronSixthMessage = 0x800000;
constexpr uint32_t kTronMessagesPending = 0x700000;
constexpr uint32_t kTronAirborne = 0x4000000;   // its hit entries hit from above
constexpr uint32_t kTronNoHeavyCount = 0x8000000;
constexpr uint32_t kTronStunned = 0x40000000;   // a heavy hit
constexpr uint32_t kTronNoDamage = kTronAirborne | kTronStunned;

enum class TronState : uint8_t { SetUp = 0, Fight = 1 }; // [0x890294]: exe 0x46d840 / 0x46d990
enum class TronSub : uint8_t { Idle = 0, Dying = 1, Charge = 2, Stomp = 3, FrontMissiles = 4, RearMissiles = 5,
                               Jump = 6, Bombs = 7, Stunned = 8, BigJump = 9 }; // [0x89029c]
enum class TronAttack : uint8_t { Charge = 0, Stomp = 1, Missiles = 2, Jump = 3, Bombs = 4, BigJump = 5, None = 0xff };
enum class ChargeStep : uint8_t { Face = 0, Rev = 1, SpinUp = 2, Spin = 3, SpinDown = 4, Pause = 5 };
enum class StompStep : uint8_t { Face = 0, Stomp = 1, Recover = 2 };
enum class MissileStep : uint8_t { Face = 0, Fire = 1, Recover = 2 };
enum class JumpStep : uint8_t { Face = 0, Crouch = 1, Rise = 2, Air = 3, Landed = 4 };
enum class BombStep : uint8_t { Face = 0, Throw = 1, Recover = 2 };
enum class BigJumpStep : uint8_t { Crouch = 0, Rise = 1, Flight = 2, Fall = 3 };

constexpr uint8_t kAnimIdle = 6, kAnimFrontRaise = 9, kAnimFrontFiring = 10, kAnimRearRaise = 0xb,
                  kAnimRearFiring = 0xc, kAnimStunned = 0xd, kAnimStomp = 0xe, kAnimRearLower = 0xf,
                  kAnimJumpCrouch = 0x1c, kAnimJumpRise = 0x1d,
                  kAnimJumpAir = 0x1e, kAnimLand = 0x1f, kAnimBigJump = 0x20, kAnimDying = 0x24, kAnimRev = 0x27,
                  kAnimSpinDown = 0x28;
constexpr uint32_t kMessageBank = 0x73c000;
const DataTable<StridedScalarTable<int16_t, 8, 4>> tauntMessages{TableId::TronBossTauntMessages};  // the taunts' messages
const DataTable<StridedScalarTable<int16_t, 3, 4>> hitPointMessages{TableId::TronBossHitPointMessages}; // the messages at 1/2, 1/3, 1/6 of the hit points
constexpr int8_t kTauntWindow = 4;
constexpr uint32_t kFlagTronDefeated = 0x589;
constexpr uint32_t kBreakAttack = 0x30200;
constexpr uint8_t kMissileType = 9, kMissileVariant = 3, kShockwaveType = 3, kShockwaveVariant = 7, kBombType = 7,
                  kBlastType = 7, kDustType = 3;

// bone k's point (exe 0x5e1810: a bone past the bone-block mask is the port gap)
ShortVec4& bone(const AnimatedActor& actor, uint32_t k) { return bonePoint(actor, k, kTronBossPort); }
// ... its colour (the point's spare lane)
BoneColour boneColour(const AnimatedActor& actor, uint32_t k) {
    return BoneColour(bone(actor, k).spare);
}
void integerPoint(const ShortVec4& p, int16_t out[3]) {
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}
// the player's chest (0x80 above his position, or another height below him), as a 16.16 point
void playerPoint(int32_t below, int32_t out[3]) {
    const Player& p = player();
    out[0] = p.pos.x;
    out[1] = int32_t(uint32_t(p.pos.y) - uint32_t(below));
    out[2] = p.pos.z;
}

// ---- the spawns ----------------------------------------------------------------------------------------------------
// exe 0x491c20(actor, variant, yaw, v): a dust puff (pool 0xa0 global type 3) at Ry(yaw) * (v << 4) from the actor, on
// its ground
void dust(AnimatedActor& actor, uint8_t variant, uint16_t yaw, const int16_t v[3]) {
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (!address) return;
    const int16_t rot[3] = {0, int16_t(yaw), 0};
    int32_t o[3];
    rotateVector(rot, int(v[0]) << 4, int(v[1]) << 4, int(v[2]) << 4, o);
    EffectActor& e = effectActorAt(address);
    e.setParamsLowWord(yaw); // the puff's yaw
    e.flags = kActorLiveDrawn;
    e.type = kDustType;
    e.variant = variant;
    e.loopFlags |= kActorSweepGroup2;
    e.pos.x = int16_t(uint32_t(int16_t(uint32_t(o[0]) >> 16)) + uint16_t(actor.pos.x.whole));
    e.pos.y = int16_t(actorGround(*E, actor));
    e.pos.z = int16_t(uint32_t(int16_t(uint32_t(o[2]) >> 16)) + uint16_t(actor.pos.z.whole));
}
// exe 0x46fc70 (exe 0x46fc00 with v = 0): a puff at a random yaw stepping round
void dustRandom(AnimatedActor& actor, TronFields& f, const int16_t v[3]) {
    auto& w = f.w();
    const uint32_t r = rngStep();
    dust(actor, 1, uint16_t((uint32_t(int32_t(w.dustCount)) * 0x200 + (r & 0x1ff)) & 0xfff), v);
    w.dustCount = int16_t(uint16_t(w.dustCount) + 1u);
}
// exe 0x46fcc0: two puffs 0x2c0 left / right of the tank
void dustTracks(AnimatedActor& actor, TronFields& f) {
    auto& w = f.w();
    const int16_t l[3] = {-0x2c0, 0, 0}, r[3] = {0x2c0, 0, 0};
    dust(actor, 4, uint16_t(actor.rotation.y), l);
    dust(actor, 4, uint16_t(actor.rotation.y), r);
    w.dustCount = int16_t(uint16_t(w.dustCount) + 1u);
}
// exe 0x46fa50: a missile (pool 0x40 type 9 variant 3) from bone 0, 0xf0 above it (or 0xc0 below and backwards with
// kTronRear; the rear missiles add a random yaw), pitched toward the player's chest
void missile(AnimatedActor& actor, TronFields& f) {
    int32_t target[3];
    playerPoint(0x640000, target);
    int16_t rot[3] = {actor.rotation.x, 0, actor.rotation.z};
    int k;
    if (!(f.flags & kTronRear)) {
        rot[1] = actor.rotation.y;
        k = int(f.missileSide) << 4;
    } else {
        int16_t s;
        if (TronSub(actor.subState) == TronSub::RearMissiles) {
            const uint32_t r = rngStep();
            s = int16_t(int16_t(int16_t(uint16_t((uint32_t(uint16_t(r)) & 0xffe3u) << 6)) >> 8) + actor.rotation.y);
        } else {
            s = actor.rotation.y;
        }
        rot[1] = int16_t(uint16_t((uint32_t(uint16_t(s)) - 0x800u) & 0xfff));
        k = int(f.missileSide) * 0x30;
    }
    int32_t o[3];
    rotateVector(rot, k, 0, -0x1400, o);
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return;
    ProjectileActor& e = projectileActorAt(address);
    EnemyShotFields shot(projectileActorAt(address));
    e.flags = kActorLiveDrawn;
    e.type = kMissileType;
    e.variant = kMissileVariant;
    e.loopFlags |= kActorSweepGroup2;
    shot.ticks = 0x40;
    shot.attack = f.row().missileAttack;
    const ShortVec4& b0 = bone(actor, 0);
    e.pos.x = int32_t(uint32_t(int32_t(b0.x) * 0x10000 + o[0]));
    const int y = (f.flags & kTronRear) ? b0.y - 0xc0 : b0.y + 0xf0;
    e.pos.y = int32_t(uint32_t(y * 0x10000 + o[1]));
    e.pos.z = int32_t(uint32_t(int32_t(b0.z) * 0x10000 + o[2]));
    const int16_t from[3] = {int16_t(uint32_t(target[0]) >> 16), int16_t(uint32_t(target[1]) >> 16),
                             int16_t(uint32_t(target[2]) >> 16)};
    const int16_t to[3] = {e.pos.x.whole, e.pos.y.whole, e.pos.z.whole};
    e.rotation.x = int16_t(uint32_t(0x800 - pitchOf(from, to)));
    e.rotation.y = rot[1];
    e.rotation.z = rot[2];
}
// the shock wave's parameters (pool 0x40 type 3 variant 7)
struct ShockwaveParams {
    explicit ShockwaveParams(const ProjectileActor& actor) : ShockwaveParams(const_cast<ProjectileActor&>(actor)) {}
    explicit ShockwaveParams(ProjectileActor& actor) : active(actor.params[0]), phase(actor.params[1]) {}
    uint8_t& active;
    uint8_t& phase;
};
// exe 0x46f8f0(actor, point): the shock wave of a stomp
void shockwave(const int16_t p[3]) {
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return;
    ProjectileActor& e = projectileActorAt(address);
    ShockwaveParams params(projectileActorAt(address));
    e.flags = kActorLive;
    e.type = kShockwaveType;
    e.loopFlags |= kActorSweepGroup2;
    e.variant = kShockwaveVariant;
    params.active = 1;
    params.phase = 0;
    e.pos.x.whole = p[0];
    e.pos.y.whole = p[1];
    e.pos.z.whole = p[2];
}
// the bomb's parameters (pool 0x40 type 7): its attack and its target slot
struct BombParams {
    explicit BombParams(const ProjectileActor& actor) : BombParams(const_cast<ProjectileActor&>(actor)) {}
    explicit BombParams(ProjectileActor& actor)
        : attack(actor.params, 0), target(actor.params, 2), spin(actor.rotation.spare) {}
    BytesField<uint16_t, 4> attack;
    BytesField<uint16_t, 4> target;
    int16_t& spin; // its spin (the rotation's spare lane)
};
// exe 0x46f940: a bomb (pool 0x40 type 7) thrown from bone 0 toward the player (a random height), RNG
void bomb(AnimatedActor& actor, TronFields& f) {
    const uint32_t r = rngStep();
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return;
    const ShortVec4& b0 = bone(actor, 0);
    int32_t target[3];
    playerPoint(0, target);
    target[1] = int32_t(uint32_t(player().pos.y) + (r & 0x7f) * 0xffff0000u);
    ProjectileActor& e = projectileActorAt(address);
    BombParams params(e);
    e.flags = kActorLive;
    e.type = kBombType;
    e.variant = 0;
    e.loopFlags |= kActorSweepGroup2;
    e.pos.x.whole = b0.x;
    e.pos.y.whole = b0.y;
    e.pos.z.whole = b0.z;
    e.velocity.x = 0;
    const int32_t from[3] = {e.pos.x, e.pos.y, e.pos.z};
    e.velocity.y = int16_t(launchSpeed(from, target, 0x300, 0x18, 0xffffffffu));
    e.velocity.z = int16_t(0xfd00);
    e.acceleration.x = 0;
    e.acceleration.y = 0x18;
    e.acceleration.z = 0;
    e.rotation.x = 0;
    e.rotation.z = 0;
    params.spin = 0x40;
    e.rotation.y = int16_t(uint32_t((r >> 8) & 0x7f) + uint16_t(f.playerAngle) - 0x40u);
    params.attack = f.row().bombAttack;
    params.target = 0xffff;
}
// exe 0x46fd30(actor, point): an explosion (static global type 7, the blast) where an arm breaks off
void blast(const ShortVec4& pt) {
    const uint32_t address = allocActor60(0); // exe 0x5e1300
    if (!address) return;
    StaticActor& e = staticActorAt(address);
    BlastParams params(actorHeaderAt(address));
    e.flags = kActorLive;
    e.loopFlags |= kActorSweepGroup2;
    e.type = kBlastType;
    e.variant = 0;
    e.pos.x.whole = pt.x;
    e.pos.y.whole = pt.y;
    e.pos.z.whole = pt.z;
    params.size = 0x100;
    params.attack = 0;
}

// ---- the helpers of the states ---------------------------------------------------------------------------------
// exe 0x46fe80(actor, id): a voice-like sound, unless a hit-point message waits while the taunt window is open
void voice(const TronFields& f, uint16_t id) {
    if ((f.flags & kTronMessagesPending) && windowStatus(kTauntWindow) != 0) return;
    soundPlayAtPos(id, kSoundAtListener);
}
// exe 0x46f160(actor, angle, max, shift): the yaw turned toward the angle with the accelerating speed (the spin's y
// lane; its spare lane the acceleration, 1 << shift, braking when the remaining angle is short); the signed difference
// before
int turn(AnimatedActor& actor, uint32_t angle, int maxSpeed, int shift) {
    int16_t& accel = actor.spin.spare;
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), angle);
    if (actor.spin.y < maxSpeed) {
        const int sp = int(actor.spin.y) >> shift;
        const int brake = ((sp + 1) * sp >> 1) * (1 << shift);
        const int ad = d < 0 ? -d : d;
        accel = int16_t(ad > brake ? uint32_t(1 << shift) : uint32_t(-1 << shift));
    } else {
        accel = 0;
    }
    if (d == 0) {
        actor.spin.y = 0;
        accel = 0;
        actor.rotation.y = int16_t(angle);
        return d;
    }
    actor.spin.y = int16_t(uint32_t(uint16_t(actor.spin.y)) + uint16_t(accel));
    const int sp = actor.spin.y;
    if (sp <= d) actor.rotation.y = int16_t(uint32_t(uint16_t(actor.rotation.y)) + uint16_t(sp));
    if (d <= -sp) actor.rotation.y = int16_t(uint32_t(uint16_t(actor.rotation.y)) - uint16_t(sp));
    const int ad = d < 0 ? -d : d;
    if (ad < sp) {
        accel = 0;
        actor.spin.y = int16_t(ad);
        actor.rotation.y = int16_t(angle);
    }
    return d;
}
// abs(exe 0x46f160(..)) as the exe evaluates it: the call runs twice
int turnAbs(AnimatedActor& actor, uint32_t angle) {
    const int d = turn(actor, angle, 0x100, 4);
    return d < 0 ? -turn(actor, angle, 0x100, 4) : turn(actor, angle, 0x100, 4);
}
// the spin's acceleration (its spare lane)
int16_t& spinAccel(AnimatedActor& actor) { return actor.spin.spare; }
// the sub-state s at its first step (the exe's byte stores)
void enterSub(AnimatedActor& actor, TronSub s) {
    actor.subState = uint8_t(s);
    actor.step = 0;
}
// the animation n from its first key
void playAnim(AnimatedActor& actor, TronFields& f, uint8_t n) {
    actor.anim = n;
    f.animKey = 0;
}
// the shared "dead" branch of the sub-states
void toDeath(AnimatedActor& actor, TronFields& f) {
    f.timer = 0xf;
    playAnim(actor, f, kAnimDying);
    actor.subState = uint8_t(TronSub::Dying);
}
// a heavy hit: voice 0x185, animation 0xd, stunned
bool stunCheck(AnimatedActor& actor, TronFields& f) {
    if (!(f.flags & kTronStunned)) return false;
    voice(f, 0x185);
    f.timer = 0xf;
    playAnim(actor, f, kAnimStunned);
    actor.subState = uint8_t(TronSub::Stunned);
    return true;
}
void toIdle(AnimatedActor& actor, TronFields& f, uint16_t timer) {
    f.timer = int16_t(timer);
    playAnim(actor, f, kAnimIdle);
    enterSub(actor, TronSub::Idle);
}
// the yaw += the spin's speed (12 bits) with the turret sound on each half turn
void spinTurret(AnimatedActor& actor) {
    const uint16_t y = uint16_t((uint32_t(uint16_t(actor.spin.y)) + uint16_t(actor.rotation.y)) & 0xfff);
    actor.rotation.y = int16_t(y);
    if ((uint16_t(actor.prevRotation.y) ^ y) & 0x800) soundPlayAtPos(0xe6, actor.pos);
}
// the timer counted down; true when it ran out
bool timerDone(TronFields& f) {
    f.timer = int16_t(uint16_t(f.timer) - 1u);
    return f.timer == 0;
}

// exe 0x46fd80: one of Roll's / Tron's taunts in the taunt window (the message bank's table 0x8902c4): four per round,
// the round by kTronSecondTauntRound, each one once
void taunt(TronFields& f) {
    auto& w = f.w();
    uint32_t k = rngStep() & 3;
    if (!(f.flags & kTronSecondTauntRound)) {
        while (int32_t(int16_t(w.tauntsSaid)) & (1 << k)) k = (k + 1) & 3;
        w.tauntsSaid = uint16_t(w.tauntsSaid | uint16_t(1 << k));
        if ((w.tauntsSaid & 0xf) == 0xf) f.flags |= kTronSecondTauntRound;
    } else {
        while (int32_t(int16_t(w.tauntsSaid)) & (1 << (k + 4))) k = (k + 1) & 3;
        k += 4;
        w.tauntsSaid = uint16_t(w.tauntsSaid | uint16_t(1 << k));
        if ((w.tauntsSaid & 0xf0) == 0xf0) {
            f.flags &= ~kTronSecondTauntRound;
            w.tauntsSaid = 0;
        }
    }
    windowOpen(kTauntWindow, kMessageBank, tauntMessages[k]);
}

// exe 0x46f720: the move's collision: breakable walls ahead (the vertical step, then the move yaw's step) queued with
// attack 0x30200 (the position restored), then the push-out; a bounce off the walls with kTronBounces turns the move
// yaw away (every third time toward the player again), a hard stop puts the tank back
void moveCollision(AnimatedActor& actor, TronFields& f) {
    auto& w = f.w();
    const uint32_t mask = collisionLayerMask(actor.slot); // exe 0x413240
    const int16_t cur[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const int32_t ly = actor.pos.y + int32_t(actor.velocity.y) * 0x1000;
    const int16_t up[3] = {cur[0], int16_t(uint32_t(ly) >> 16), cur[2]};
    uint32_t r = testMoveBlocked(*E, up, cur, actor.collisionBox, mask);
    if (r != 0) queueBreakableWallHit(r, kBreakAttack);
    const uint32_t keep[3] = {uint32_t(actor.pos.x), uint32_t(actor.pos.y), uint32_t(actor.pos.z)};
    moveAlongYaw(actor.pos, w.moveYaw, 0, 0, int(actor.velocity.z) << 4);
    const int16_t ahead[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const int16_t back[3] = {int16_t(keep[0] >> 16), int16_t(keep[1] >> 16), int16_t(keep[2] >> 16)};
    r = testMoveBlocked(*E, ahead, back, actor.collisionBox, mask);
    if (r != 0) queueBreakableWallHit(r, kBreakAttack);
    actor.pos.x = int32_t(keep[0]);
    actor.pos.y = int32_t(keep[1]);
    actor.pos.z = int32_t(keep[2]);
    r = actorTestMove(*E, actor);
    if (int32_t(r) >= 0) return;
    queueBreakableWallHit(r, kBreakAttack);
    r = actorPushOut(*E, actor);
    if ((f.flags & kTronBounces) && (r & 0xf00)) {
        int16_t pitch = 0, yaw = w.moveYaw;
        reflectRotationOffWalls(pitch, yaw, r);
        const uint8_t c = uint8_t(w.bouncesLeft - 1u);
        w.bouncesLeft = c;
        if (c == 0) {
            const uint32_t n = rngStep();
            w.bouncesLeft = 3;
            w.moveYaw = int16_t((n & 0xff) + uint32_t(uint16_t(f.playerAngle)) - 0x80u);
        } else {
            const uint32_t n = rngStep();
            w.moveYaw = int16_t(((n & 0xff) - 0x80u) + uint16_t(yaw));
        }
    }
    if (int32_t(r) < 0) {
        actor.pos.x = actor.prevPos.x;
        actor.pos.z = actor.prevPos.z;
        actor.velocity.z = 0;
        actor.velocity.x = 0;
        if (actor.airborne != 0) {
            actor.airborne = 0;
            actor.pos.y = actor.prevPos.y;
            actor.velocity.y = 0;
        }
    }
}
// one arm's damage: its box hit -> its bones flash, its hit points; at 0 it breaks off (its parts hidden, 4 blasts,
// voice 0x186 / 0x187 when the other one is gone)
void armDamage(AnimatedActor& actor, TronFields& f, const HitBoxHead& box, int16_t& hp, uint32_t firstBone,
               size_t firstPart, uint32_t offBit, uint32_t otherBit) {
    const uint32_t b = box.result;
    uint32_t ad;
    if (!(b & 0xc0000) || (ad = b & 0xfff) == 0 || hp <= 0) return;
    for (uint32_t k = firstBone; k < firstBone + 4; ++k) boneColour(actor, k) = kTintFlash;
    soundPlayAtPos(0x8d, actor.pos);
    hp = int16_t(uint32_t(uint16_t(hp)) - uint16_t(ad));
    if (hp >= 1) return;
    f.flags |= offBit;
    for (uint32_t k = 0; k < 4; ++k) {
        actor.parts[firstPart + k].flags |= kPartHidden;
        blast(bone(actor, firstBone + k));
    }
    voice(f, (f.flags & otherBit) ? 0x187 : 0x186);
}
// exe 0x46f230: the damage: the body box (or the queued damage, at most 0x20 a tick; the heavy-hit counter over 0x80
// -> stunned), the hit-point thresholds 1/2, 1/3, 1/4, 1/6 -> the message bits, death -> kTronDead; the arms' boxes
void damage(AnimatedActor& actor, TronFields& f) {
    auto& w = f.w();
    uint32_t d;
    const uint32_t box = w.body.result;
    if ((box & 0xc0000) == 0 || (d = box & 0xfff) == 0) {
        const int16_t q = w.queuedDamage;
        if (q < 0x21) {
            d = uint32_t(int32_t(q));
            w.queuedDamage = 0;
        } else {
            d = 0x20;
            w.queuedDamage = int16_t(q - 0x20);
        }
    } else {
        if (d > 0x20) d = 0x20;
        w.queuedDamage = 0;
    }
    const int16_t old = actor.hitPoints;
    bool clear = (f.flags & kTronNoHeavyCount) != 0;
    if (!clear) {
        w.heavyHits = int16_t(uint32_t(uint16_t(w.heavyHits)) + uint16_t(d) - 2u);
        clear = w.heavyHits < 0;
    }
    if (clear) w.heavyHits = 0;
    if (d != 0) {
        boneColour(actor, 0) = kTintFlash;
        for (uint32_t k = 9; k < 0xd; ++k) boneColour(actor, k) = kTintFlash;
        uint16_t id;
        if (w.heavyHits < 0x81) {
            id = 0x8d;
        } else {
            f.flags |= kTronStunned;
            w.heavyHits = 0;
            id = 0x8e;
        }
        soundPlayAtPos(id, actor.pos);
        if (hitPointsAdd(actor, int(t16(-int32_t(d)))) == 0) {
            const int16_t hp = actor.hitPoints, mx = actor.maxHitPoints;
            auto mark = [&](int16_t lim, uint32_t bit) {
                if (hp <= lim && lim < old) f.flags |= bit;
            };
            mark(int16_t(mx >> 1), kTronHalfMessage);
            mark(int16_t(int32_t(mx) / 3), kTronThirdMessage);
            mark(int16_t(mx >> 2), kTronQuarterMessage);
            mark(int16_t(int32_t(mx) / 6), kTronSixthMessage);
        } else {
            f.flags |= kTronDead;
        }
    }
    armDamage(actor, f, w.leftArm, w.leftArmHitPoints, 1, 1, kTronLeftArmOff, kTronRightArmOff);
    armDamage(actor, f, w.rightArm, w.rightArmHitPoints, 5, 5, kTronRightArmOff, kTronLeftArmOff);
}
// exe 0x46f550: the hit-list entries: the body (bone 0, its attack / the charge's), its top 0xb4 above, the arms as
// capsules (bones 2-3 / 6-7) unless broken off
void hitBoxes(AnimatedActor& actor, TronFields& f) {
    auto& w = f.w();
    w.leftArm.result = 0;
    w.rightArm.result = 0;
    w.body.result = 0;
    w.body.previousX = actor.prevPos.x.whole;
    w.body.previousZ = actor.prevPos.z.whole;
    const ShortVec4& b2 = bone(actor, 2);
    w.leftArm.previousX = b2.x;
    w.leftArm.previousZ = b2.z;
    const ShortVec4& b6 = bone(actor, 6);
    w.rightArm.previousX = b6.x;
    w.rightArm.previousZ = b6.z;
    uint32_t param = 0x20000;
    uint8_t dmg;
    if (!(f.flags & kTronStrongBody)) {
        dmg = f.row().bodyAttack;
    } else {
        param = 0xa0000;
        dmg = f.row().chargeAttack;
    }
    if (f.flags & kTronAirborne) param = (param & 0xfffdffffu) | 0x200000u;
    const ShortVec4& b0 = bone(actor, 0);
    const uint32_t slot = uint32_t(actor.slot) << 24;
    int16_t pt[3];
    integerPoint(b0, pt);
    hitListSubmit(pt, w.body, slot | 0x480050, dmg | param);
    pt[1] = int16_t(pt[1] + 0xb4);
    const uint32_t p2 = dmg | param | 0x800000;
    hitListSubmit(pt, w.body, slot | 0x400014, p2);
    if (!(f.flags & kTronLeftArmOff)) {
        int16_t a[3], b[3];
        integerPoint(bone(actor, 2), a);
        integerPoint(bone(actor, 3), b);
        hitListSubmitCapsule(a, 0x28, b, 0x2d, w.leftArm, slot | 0x481000, p2); // exe 0x5e4190
    }
    if (!(f.flags & kTronRightArmOff)) {
        int16_t a[3], b[3];
        integerPoint(b6, a);
        integerPoint(bone(actor, 7), b);
        hitListSubmitCapsule(a, 0x28, b, 0x2d, w.rightArm, slot | 0x481000, p2);
    }
}

// ---- the sub-states [0x89029c + +9] ------------------------------------------------------------------------------
// exe 0x46dc70: idle until the timer runs out, then the next attack by the distance and the RNG (never the same twice
// in a row, three tries): the charge, the stomp, the missiles front / rear alternately, the jump, the bombs, the big
// jump (once the hit points are under 1/4 and its cooldown ran out)
void idle(AnimatedActor& actor, TronFields& f) {
    auto& w = f.w();
    const uint32_t flags = f.flags;
    if (flags & kTronDead) {
        toDeath(actor, f);
        return;
    }
    f.timer = int16_t(uint32_t(uint16_t(f.timer)) - 1u);
    if (f.timer != 0) {
        f.flags = flags | kTronNoHeavyCount;
        return;
    }
    f.flags = flags & ~kTronNoHeavyCount;
    uint32_t v = rngStep() & 0xf;
    uint8_t c = 0;
    for (int n = 3; n != 0; --n) {
        if ((f.flags & kTronQuarterMessage) && w.bigJumpTicks == 0) {
            c = uint8_t(TronAttack::BigJump);
            break;
        }
        const int16_t d = f.playerDistance;
        if (d < 0x600) {
            if (v < 8) {
                c = uint8_t(TronAttack::Charge);
            } else if (v < 0xe) {
                if ((f.flags & (kTronLeftArmOff | kTronRightArmOff)) == (kTronLeftArmOff | kTronRightArmOff)) {
                    c = uint8_t(TronAttack::Missiles);
                    w.lastAttack = uint8_t(TronAttack::None);
                } else {
                    c = uint8_t(TronAttack::Stomp);
                }
            } else {
                c = uint8_t(TronAttack::Missiles);
                w.lastAttack = uint8_t(TronAttack::None);
            }
        } else if (d < 0xdf8) {
            if (v == 0) {
                c = uint8_t(TronAttack::Charge);
            } else if (v < 0xd) {
                c = uint8_t(TronAttack::Missiles);
                w.lastAttack = uint8_t(TronAttack::None);
            } else {
                c = uint8_t(TronAttack::Jump);
            }
        } else if (v < 4) {
            c = uint8_t(TronAttack::Missiles);
            w.lastAttack = uint8_t(TronAttack::None);
        } else {
            c = uint8_t((9 < v) + 3);
        }
        if (w.lastAttack != c) break;
        v = 0;
    }
    w.lastAttack = c;
    switch (TronAttack(c)) {
    case TronAttack::Charge:
        soundPlayAtPos(0xe8, kSoundAtListener);
        f.flags = (f.flags & 0xbfffdfdfu) | 0x8004000u;
        actor.acceleration.y = 0;
        actor.velocity.y = 0;
        enterSub(actor, TronSub::Charge);
        return;
    case TronAttack::Stomp:
        soundPlayAtPos(0xe8, kSoundAtListener);
        f.flags &= ~kTronFrontFired;
        enterSub(actor, TronSub::Stomp);
        return;
    case TronAttack::Missiles: {
        soundPlayAtPos(0xe8, kSoundAtListener);
        const uint32_t g = f.flags;
        if (g & kTronFrontFired) {
            f.flags = (g & ~kTronFrontFired) | kTronRear;
            playAnim(actor, f, kAnimRearRaise);
            actor.subState = uint8_t(TronSub::RearMissiles);
        } else {
            f.flags = g | kTronFrontFired;
            playAnim(actor, f, kAnimFrontRaise);
            actor.subState = uint8_t(TronSub::FrontMissiles);
        }
        actor.step = 0;
        return;
    }
    case TronAttack::Jump: {
        soundPlayAtPos(0xe8, kSoundAtListener);
        f.flags = (f.flags & 0xbfffdfffu) | 0xc004000u;
        w.moveYaw = f.playerAngle;
        int g, sp;
        if (f.playerDistance < 0xc01) {
            g = 0x48;
            sp = 0x380;
        } else if (f.playerDistance < 0x1801) {
            g = 0x34;
            sp = 0x480;
            f.flags |= kTronJumpMedium;
        } else {
            f.flags |= kTronJumpLong;
            g = 0x20;
            sp = 0x580;
        }
        f.jumpSpeed = int16_t(launchSpeed(actor.pos, player().pos, sp, g, 0xffffffffu));
        enterSub(actor, TronSub::Jump);
        return;
    }
    case TronAttack::Bombs:
        f.flags &= ~kTronFrontFired;
        playAnim(actor, f, kAnimRearRaise);
        enterSub(actor, TronSub::Bombs);
        return;
    case TronAttack::BigJump:
        f.flags = (f.flags & 0xbfffffffu) | 0x400c000u;
        actor.anim = kAnimBigJump;
        enterSub(actor, TronSub::BigJump);
        return;
    default: return;
    }
}
// exe 0x46dfa0: dying (animation 0x24): sound 0x188 at 5, flag 0x589 when the timer runs out (the player alive) -> the
// room handler starts script 0x1c
void dying(TronFields& f) {
    if (f.timer == 5) soundPlayAtPos(0x188, kSoundAtListener);
    f.timer = int16_t(uint32_t(uint16_t(f.timer)) - 1u);
    if (f.timer == 0 && player().hitPoints >= 0) setStoryFlag(kFlagTronDefeated);
}
// the two missiles of a charge's turn (either side)
void missilePair(AnimatedActor& actor, TronFields& f) {
    f.missileSide = int16_t(0xffc0);
    missile(actor, f);
    f.missileSide = 0x40;
    missile(actor, f);
}
const int16_t kNoOffset[3] = {0, 0, 0};
const int16_t kBelow[3] = {0, 0, -0x100};
// exe 0x46dff0: the charge: faces the player, then spins up (missile pairs while facing within 0x180 / backwards past
// 0x680), spins down, a pause
void charge(AnimatedActor& actor, TronFields& f) {
    if (f.flags & kTronDead) {
        toDeath(actor, f);
        return;
    }
    switch (ChargeStep(actor.step)) {
    case ChargeStep::Face:
        if (turnAbs(actor, uint32_t(int32_t(f.playerAngle))) > 0xf) {
            dustTracks(actor, f);
            return;
        }
        soundPlayAtPos(0xe5, actor.pos);
        f.timer = 8;
        playAnim(actor, f, kAnimRev);
        ++actor.step;
        return;
    case ChargeStep::Rev:
        if (timerDone(f)) {
            voice(f, 0x180);
            f.timer = 0x4b;
            actor.spin.y = 0;
            spinAccel(actor) = 4;
            ++actor.step;
        }
        return;
    case ChargeStep::SpinUp:
        dustRandom(actor, f, kNoOffset); // exe 0x46fc00
        actor.spin.y = int16_t(uint32_t(uint16_t(actor.spin.y)) + uint16_t(spinAccel(actor)));
        if (actor.spin.y > 0x85) actor.spin.y = 0x85;
        spinTurret(actor);
        if (timerDone(f)) {
            f.timer = 0x78;
            ++actor.step;
        }
        return;
    case ChargeStep::Spin: {
        dustRandom(actor, f, kNoOffset);
        spinTurret(actor);
        int16_t y = actor.rotation.y;
        if (y > 0x7ff) y = int16_t(y - 0x1000);
        int16_t t = f.playerAngle;
        if (t > 0x7ff) t = int16_t(t - 0x1000);
        int dd = int(y) - int(t);
        if (dd < 0) dd = int(t) - int(y);
        uint16_t u = uint16_t(dd) & 0xfff;
        if (u > 0x7ff) u = uint16_t(0x1000 - u);
        bool fire = false;
        if (int16_t(u) < 0x680) {
            if (int16_t(u) < 0x181) {
                f.flags &= ~kTronRear;
                fire = true;
            }
        } else {
            f.flags |= kTronRear;
            fire = true;
        }
        if (fire) missilePair(actor, f);
        if (timerDone(f)) {
            f.flags &= ~kTronRear;
            ++actor.step;
        }
        return;
    }
    case ChargeStep::SpinDown: {
        dustRandom(actor, f, kNoOffset);
        actor.spin.y = int16_t(uint32_t(uint16_t(actor.spin.y)) - uint16_t(spinAccel(actor)));
        if (actor.spin.y < 0) actor.spin.y = 0;
        spinTurret(actor);
        if (actor.spin.y == 0) {
            soundPlayAtPos(0xe7, actor.pos);
            playAnim(actor, f, kAnimSpinDown);
            const uint32_t flags = f.flags;
            f.flags = (flags & 0xf7ffbfffu) | kTronOnGround;
            if (!(flags & kTronStunned)) {
                f.timer = 0xf;
                ++actor.step;
                return;
            }
            stunCheck(actor, f);
        }
        return;
    }
    case ChargeStep::Pause:
        if (stunCheck(actor, f)) return;
        if (timerDone(f)) toIdle(actor, f, 0x1e);
        return;
    default: return;
    }
}
// a shock wave under bone b of an arm (at the tank's height)
void armShockwave(AnimatedActor& actor, uint32_t b) {
    const ShortVec4& p = bone(actor, b);
    const int16_t pt[3] = {p.x, actor.pos.y.whole, p.z};
    shockwave(pt);
}
// exe 0x46e3c0: the stomp: faces the player, then on the animation's event both arms send a shock wave
void stomp(AnimatedActor& actor, TronFields& f) {
    if (f.flags & kTronDead) {
        toDeath(actor, f);
        return;
    }
    if (stunCheck(actor, f)) return;
    const StompStep c = StompStep(actor.step);
    if (c == StompStep::Face) {
        if (turnAbs(actor, uint32_t(int32_t(f.playerAngle))) < 0x10) {
            soundPlayAtPos(0xe9, actor.pos);
            voice(f, 0x182);
            playAnim(actor, f, kAnimStomp);
            ++actor.step;
            return;
        }
        dustTracks(actor, f);
    } else if (c == StompStep::Stomp) {
        if (actor.animKey.events & 1) {
            soundPlayAtPos(0xea, actor.pos);
            cameraShakeAt(0x2000, 0x400, 0x100, 0x1000, actor.pos);
            if (!(f.flags & kTronLeftArmOff)) armShockwave(actor, 3);
            if (!(f.flags & kTronRightArmOff)) armShockwave(actor, 7);
            ++actor.step;
        }
    } else if (c == StompStep::Recover && actor.animEnded()) {
        toIdle(actor, f, 0x1e);
    }
}
// exe 0x46e5c0 / 0x46e710: the missiles (front: facing the player, animation 10 then 9; rear: the back turned (angle -
// 0x800), animation 0xc then 0xf): a missile every other tick for 0x1e ticks, alternating sides
void missiles(AnimatedActor& actor, TronFields& f, bool rear) {
    if (f.flags & kTronDead) {
        toDeath(actor, f);
        return;
    }
    if (stunCheck(actor, f)) return;
    const uint32_t angle =
        rear ? ((uint32_t(uint16_t(f.playerAngle)) - 0x800u) & 0xfff) : uint32_t(int32_t(f.playerAngle));
    int d = turn(actor, angle, 0x100, 4);
    const MissileStep c = MissileStep(actor.step);
    if (c == MissileStep::Face) {
        if (d < 0) d = -d;
        if (d < 0x20) {
            f.timer = 0x1e;
            actor.spin.y = 0;
            spinAccel(actor) = 4;
            playAnim(actor, f, rear ? kAnimRearFiring : kAnimFrontFiring);
            ++actor.step;
            return;
        }
        dustTracks(actor, f);
    } else if (c == MissileStep::Fire) {
        if (uint16_t(f.timer) & 1) {
            f.missileSide = int16_t(f.missileSide != 0x40 ? 0x40 : 0xffc0);
            missile(actor, f);
        }
        if (timerDone(f)) {
            playAnim(actor, f, rear ? kAnimRearLower : kAnimFrontRaise);
            ++actor.step;
        }
    } else if (c == MissileStep::Recover && actor.animEnded()) {
        if (rear) f.flags &= ~kTronRear;
        toIdle(actor, f, 5);
    }
}
// exe 0x46e870: the jump toward the player (the launch speed of the idle's pick, three ranges: forward speed 0x380 /
// 0x480 / 0x580 with gravity 0x48 / 0x34 / 0x20), dust while airborne, the landing (shake, sound 0xec)
void jump(AnimatedActor& actor, TronFields& f) {
    auto& w = f.w();
    switch (JumpStep(actor.step)) {
    case JumpStep::Face:
        if (turnAbs(actor, uint32_t(int32_t(f.playerAngle))) > 0xf) {
            dustTracks(actor, f);
            return;
        }
        voice(f, 0x181);
        playAnim(actor, f, kAnimJumpCrouch);
        ++actor.step;
        return;
    case JumpStep::Crouch: {
        if (!actor.animEnded()) return;
        soundPlayAtPos(0xeb, kSoundAtListener);
        f.flags &= ~kTronOnGround;
        w.moveYaw = f.playerAngle;
        playAnim(actor, f, kAnimJumpRise);
        actor.velocity.x = 0;
        actor.velocity.y = f.jumpSpeed;
        const uint32_t flags = f.flags;
        if (flags & kTronJumpLong) {
            f.flags = flags & ~kTronJumpLong;
            actor.velocity.z = int16_t(0xfa80);
            actor.acceleration.y = 0x20;
        } else if (!(flags & kTronJumpMedium)) {
            actor.velocity.z = int16_t(0xfc80);
            actor.acceleration.y = 0x48;
        } else {
            f.flags = flags & ~kTronJumpMedium;
            actor.velocity.z = int16_t(0xfb80);
            actor.acceleration.y = 0x34;
        }
        actor.airborne = 1;
        ++actor.step;
        return;
    }
    case JumpStep::Rise:
        if (actor.animEnded()) {
            playAnim(actor, f, kAnimJumpAir);
            ++actor.step;
        }
        return;
    case JumpStep::Air:
        if (actorGround(*E, actor) - 0x400 <= int(actor.pos.y.whole)) dustRandom(actor, f, kBelow);
        if (actor.airborne == 0) {
            soundPlayAtPos(0xec, kSoundAtListener);
            cameraShakeAt(0x2000, 0x400, 0x100, 0x1000, actor.pos);
            f.timer = 0x36;
            f.flags = (f.flags & 0xf3ffffffu) | kTronOnGround;
            actor.velocity.clearWords();
            actor.acceleration.y = 0;
            actor.anim = kAnimLand;
            if (!(f.flags & kTronStunned)) {
                f.animKey = 0;
                ++actor.step;
                return;
            }
            stunCheck(actor, f);
        }
        return;
    case JumpStep::Landed:
        if (stunCheck(actor, f)) return;
        if (f.timer > 0x26) dustRandom(actor, f, kBelow);
        if (timerDone(f)) {
            f.timer = 1;
            f.flags &= ~kTronStrongBody;
            enterSub(actor, TronSub::Idle);
        }
        return;
    default: return;
    }
}
// exe 0x46ebb0: the bombs (only while the player is at least 0xdf8 away): turns toward the player, then a bomb every 4
// ticks for 0x78 ticks
void bombs(AnimatedActor& actor, TronFields& f) {
    if (f.flags & kTronDead) {
        toDeath(actor, f);
        return;
    }
    if (stunCheck(actor, f)) return;
    if (f.playerDistance < 0xdf8) {
        playAnim(actor, f, kAnimRearLower);
        actor.step = uint8_t(BombStep::Recover);
    }
    const BombStep c = BombStep(actor.step);
    if (c == BombStep::Face) {
        turn(actor, uint32_t(int32_t(f.playerAngle)), 0x80, 2); // exe 0x46f120
        if (actor.animEnded()) {
            voice(f, 0x181);
            f.timer = 0x78;
            playAnim(actor, f, kAnimRearFiring);
            ++actor.step;
        }
    } else if (c == BombStep::Throw) {
        turn(actor, uint32_t(int32_t(f.playerAngle)), 0x80, 2);
        if ((uint16_t(f.timer) & 3) == 0) {
            soundPlayAtPos(0xed, kSoundAtListener);
            bomb(actor, f);
        }
        if (timerDone(f)) {
            playAnim(actor, f, kAnimRearLower);
            ++actor.step;
        }
    } else if (c == BombStep::Recover && actor.animEnded()) {
        toIdle(actor, f, 0x1e);
    }
}
// exe 0x46ecf0: stunned by a heavy hit (animation 0xd) for 0xf ticks
void stunned(AnimatedActor& actor, TronFields& f) {
    if (f.flags & kTronDead) {
        toDeath(actor, f);
        return;
    }
    if (timerDone(f)) {
        f.timer = 10;
        f.flags &= 0xbfffcfffu;
        playAnim(actor, f, kAnimIdle);
        enterSub(actor, TronSub::Idle);
    }
}
// exe 0x46ed60: the big jump (under 1/4 of the hit points): up, then the spinning flight toward the player for 600
// ticks (two move steps a tick, dust), the fall, the landing
void bigJumpSteps(AnimatedActor& actor, TronFields& f, bool falling) {
    auto& w = f.w();
    for (int n = 2; n != 0; --n) {
        actor.velocity.z = int16_t(uint32_t(uint16_t(actor.velocity.z)) + uint16_t(actor.acceleration.z));
        actor.velocity.y = int16_t(uint32_t(uint16_t(actor.velocity.y)) + uint16_t(actor.acceleration.y));
        actor.velocity.x = int16_t(uint32_t(uint16_t(actor.velocity.x)) + uint16_t(actor.acceleration.x));
        moveAlongYaw(actor.pos, w.moveYaw, actor.velocity.x, actor.velocity.y, actor.velocity.z);
        moveCollision(actor, f);
        actor.prevPos.x = actor.pos.x;
        actor.prevPos.y = actor.pos.y;
        actor.prevPos.z = actor.pos.z;
        actor.prevRotation = actor.rotation;
        if (!falling) {
            if (actor.velocity.z < -0x1ff) {
                actor.velocity.z = int16_t(0xfe00);
                actor.acceleration.z = 0;
            } else {
                actor.acceleration.z = int16_t(0xfff0);
            }
        } else if (actor.velocity.z < 0) {
            actor.acceleration.z = 0x10;
        } else {
            actor.acceleration.z = 0;
            actor.velocity.z = 0;
        }
    }
}
void bigJump(AnimatedActor& actor, TronFields& f) {
    auto& w = f.w();
    switch (BigJumpStep(actor.step)) {
    case BigJumpStep::Crouch:
        soundPlayAtPos(0xe5, actor.pos);
        f.timer = 0xe;
        f.flags &= ~kTronOnGround;
        actor.acceleration.y = 0x20;
        actor.anim = kAnimBigJump;
        actor.velocity.y = int16_t(0xfd00);
        ++actor.step;
        return;
    case BigJumpStep::Rise:
        if (timerDone(f)) {
            actor.velocity.y = 0x100;
            f.timer = 600;
            w.moveYaw = f.playerAngle;
            w.bouncesLeft = 3;
            actor.spin.y = 0;
            spinAccel(actor) = 4;
            actor.acceleration.y = 0;
            ++actor.step;
        }
        return;
    case BigJumpStep::Flight: {
        const int g = actorGround(*E, actor);
        if (g - 0x80 < int(actor.pos.y.whole)) actor.pos.y.whole = int16_t(g - 0x80);
        actor.spin.y = int16_t(uint32_t(uint16_t(actor.spin.y)) + uint16_t(spinAccel(actor)));
        if (actor.spin.y > 0x10a) actor.spin.y = 0x10a;
        spinTurret(actor);
        if (f.timer < 0x1fe) {
            if (actor.velocity.z < -0x1ff) {
                actor.velocity.z = int16_t(0xfe00);
                actor.acceleration.z = 0;
            } else {
                actor.acceleration.z = int16_t(0xfff0);
            }
            bigJumpSteps(actor, f, false);
            dustRandom(actor, f, kBelow);
        }
        if (timerDone(f)) {
            actor.velocity.y = 0;
            actor.anim = kAnimLand;
            actor.acceleration.y = 0x10;
            ++actor.step;
        }
        return;
    }
    case BigJumpStep::Fall: {
        actor.spin.y = int16_t(uint32_t(uint16_t(actor.spin.y)) - uint16_t(spinAccel(actor)));
        if (actor.spin.y < 0) actor.spin.y = 0;
        spinTurret(actor);
        if (actor.velocity.z < 0) {
            actor.acceleration.z = 0x10;
        } else {
            actor.acceleration.z = 0;
            actor.velocity.z = 0;
        }
        bigJumpSteps(actor, f, true);
        const int g = actorGround(*E, actor);
        if (g <= int(actor.pos.y.whole)) {
            actor.pos.y.whole = int16_t(g);
            actor.acceleration.y = 0;
            actor.velocity.y = 0;
        }
        if (actor.spin.y == 0) {
            f.timer = 0xf;
            f.flags = (f.flags & 0xfbff3fffu) | kTronOnGround;
            w.bigJumpTicks = 600;
            enterSub(actor, TronSub::Idle);
        }
        return;
    }
    default: return;
    }
}
void subState(AnimatedActor& actor, TronFields& f) {
    switch (TronSub(actor.subState)) {
    case TronSub::Idle: idle(actor, f); return;
    case TronSub::Dying: dying(f); return;
    case TronSub::Charge: charge(actor, f); return;
    case TronSub::Stomp: stomp(actor, f); return;
    case TronSub::FrontMissiles: missiles(actor, f, false); return;
    case TronSub::RearMissiles: missiles(actor, f, true); return;
    case TronSub::Jump: jump(actor, f); return;
    case TronSub::Bombs: bombs(actor, f); return;
    case TronSub::Stunned: stunned(actor, f); return;
    case TronSub::BigJump: bigJump(actor, f); return;
    default: notPortedAt(kTronBossPort, "0x46d990 sub-state", actor.subState);
    }
}

// ---- exe 0x46d7e0: states [0x890294]: 0 0x46d840 (set up), 1 0x46d990 (the fight) ---------------------------------
// The work block lives in the actor's 4th bone block (+0x20..+0x7f); its collision box is the work block's.
// exe 0x46d840
void setUp(AnimatedActor& actor, TronFields& f) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.flags |= kActorScenery;
    actor.clearMotion();
    actor.tint = 0;
    f.red = 0x10;
    f.green = 0x10;
    f.blue = 0x10;
    f.animKey = 0;
    const uint32_t blk = boneBlockAddress(actor, int(3));
    if (blk == 0) notPorted(kTronBossPort, "0x46d840: no 4th bone block");
    f.work = blk + kTronFightBlockOffset;
    f.clearState();
    auto& w = f.w();
    w.clearBytes();
    actor.collisionBox.pointTo(w.box);
    w.box.minZ = int16_t(0xff00);
    w.box.minX = int16_t(0xff00);
    w.box.maxZ = 0x100;
    w.box.maxX = 0x100;
    w.box.minY = int16_t(0xfe00);
    w.box.maxY = 0;
    f.stats = statTableRow(actorStatTables().animated, actor.type);
    const TronStats row = f.row();
    const uint16_t hp = uint16_t(uint32_t(row.hitPoints) << 5);
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    f.timer = 0x1e;
    f.flags |= kTronOnGround;
    w.leftArmHitPoints = int16_t(uint32_t(row.leftArmHitPoints) << 5);
    w.rightArmHitPoints = int16_t(uint32_t(row.rightArmHitPoints) << 5);
    w.lastAttack = uint8_t(TronAttack::None);
    w.tauntTicks = 300;
    hudAddBossGauge(actor, 0, 5, 0x9040a0); // exe 0x415d30
    playAnim(actor, f, kAnimIdle);
    animSet(actor, kAnimIdle, 0); // exe 0x5e1c30
    animPose(actor);
    actor.state = uint8_t(TronState::Fight);
    actor.subState = uint8_t(TronSub::Idle);
}
// exe 0x46d990
void fight(AnimatedActor& actor, TronFields& f) {
    int32_t chest[3]; // the player's chest (0x80 above his position)
    playerPoint(0x800000, chest);
    auto& w = f.w();
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    w.prevMoveYaw = w.moveYaw;
    if (w.bigJumpTicks != 0) w.bigJumpTicks = int16_t(uint32_t(uint16_t(w.bigJumpTicks)) - 1u);
    f.playerDistance = int16_t(planarDistance(actor.pos, chest));
    f.playerAngle = int16_t(angleOf(chest[0], chest[2], actor.pos.x, actor.pos.z));
    const int16_t chest16[3] = {int16_t(uint32_t(chest[0]) >> 16), int16_t(uint32_t(chest[1]) >> 16),
                                int16_t(uint32_t(chest[2]) >> 16)};
    const int16_t at16[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    f.playerPitch = int16_t(uint32_t(0x800 - pitchOf(chest16, at16)));
    if (int16_t(actor.maxHitPoints >> 1) < actor.hitPoints) {
        w.tauntTicks = int16_t(uint32_t(uint16_t(w.tauntTicks)) - 1u);
        if (w.tauntTicks == 0) {
            if (windowStatus(kTauntWindow) == 0) {
                w.tauntTicks = 300;
                taunt(f);
            } else {
                w.tauntTicks = 1;
            }
        }
    } else if (windowStatus(kTauntWindow) == 0) {
        static const uint32_t bits[3] = {kTronHalfMessage, kTronThirdMessage, kTronSixthMessage};
        for (uint32_t i = 0; i < 3; ++i) {
            if (!(f.flags & bits[i])) continue;
            f.flags &= ~bits[i];
            windowOpen(kTauntWindow, kMessageBank, hitPointMessages[i]);
        }
    }
    subState(actor, f);
    actor.velocity.y = int16_t(uint32_t(uint16_t(actor.velocity.y)) + uint16_t(actor.acceleration.y));
    actor.velocity.z = int16_t(uint32_t(uint16_t(actor.velocity.z)) + uint16_t(actor.acceleration.z));
    actor.velocity.x = int16_t(uint32_t(uint16_t(actor.velocity.x)) + uint16_t(actor.acceleration.x));
    moveAlongYaw(actor.pos, w.moveYaw, actor.velocity.x, actor.velocity.y, actor.velocity.z);
    bool clamp = false;
    if (actor.pos.z.whole >= 0x1000) {
        actor.pos.z.whole = 0x1000;
        clamp = true;
    } else if (actor.pos.z.whole <= -0xe00) {
        actor.pos.z.whole = int16_t(0xf200);
        clamp = true;
    }
    if (clamp && (f.flags & kTronBounces)) w.moveYaw = int16_t(uint32_t(uint16_t(w.moveYaw)) + 0x800u);
    if (w.moveYaw >= 0x800) w.moveYaw = int16_t(uint32_t(uint16_t(w.moveYaw)) - 0x1000u);
    if (w.moveYaw < -0x800) w.moveYaw = int16_t(uint32_t(uint16_t(w.moveYaw)) + 0x1000u);
    moveCollision(actor, f);
    if (f.flags & kTronOnGround) actor.pos.y.whole = int16_t(actorGround(*E, actor));
    if (actor.anim != actor.prevAnim) {
        animSet(actor, actor.anim, int(f.animKey)); // exe 0x5e1c30
        animPose(actor);
    } else {
        animStep(actor); // exe 0x5e1e80
        animPose(actor);
    }
    if (!(f.flags & kTronNoDamage)) {
        damage(actor, f);
        hitBoxes(actor, f);
    } else {
        w.heavyHits = 0;
        hitBoxes(actor, f);
    }
}
// exe 0x46d7e0's body: the bones' colour, the state
void tronTank(AnimatedActor& actor) {
    TronFields& f = classState<TronFields>(actor);
    const uint16_t tint = uint16_t(((uint32_t(int32_t(f.blue)) << 5 | uint32_t(int32_t(f.green))) << 5) |
                                   uint32_t(int32_t(f.red)));
    for (uint32_t k = 0; k < 0xd; ++k)
        if (BoneBlock* b = boneBlockOf(actor, int(k / kBonesPerBlock))) b->colour(k % kBonesPerBlock) = tint;
    switch (TronState(actor.state)) {
    case TronState::SetUp: setUp(actor, f); break;
    case TronState::Fight: fight(actor, f); break;
    default: notPortedAt(kTronBossPort, "0x46d7e0 state", actor.state);
    }
    // exe 0x5f20f0(actor, 0x100, 0x100, 0): the shadow (render output)
}

} // namespace

void tronBossUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    tronTank(animatedActorAt(actor));
}

} // namespace od::port
