// The blob boss of ST2F / ST58 (pool 0x20 class 0x4d6b40, see blob_boss.hpp; hypothesis names from the observed
// behaviour). A body that scales itself (stretching up, squashing flat) with its steered prop (static type 0x39) riding
// on its top: it drives the prop's colour, its aim and its flash. Its working block lives inside its own bone block 0
// (exe 0x5e1780(actor, 0) + 0x40, kept in +0x154): the two hit boxes, its stat row, the body's yaw, the damage, the
// heights and the counters.
//
// States [0x8956b0]: 0 init 0x4d6b60, 1 fight 0x4d6e80, 2 free 0x4d7510, 3 the small form 0x4d8550, 4 rise 0x4d8ce0
// (sequence 0x4d8fb0), 5 sink 0x4d9350 (sequence 0x4d9600). Fight sub-states [0x8956c8]: 0 wait 0x4d7540, 1 chase
// 0x4d75c0, 2 slam 0x4d7750, 3 jump 0x4d7a30, 4 shoot 0x4d7e90, 5 second phase 0x4d8100, 6 spin 0x4d81e0, 7 die
// 0x4d83f0. Small-form sub-states [0x8956e8]: 0 wander 0x4d89f0, 1 flee 0x4d8ab0, 2 die 0x4d8bf0.
//
// Spawns: its prop (static stage type 0x39, content/saul_kada/steered_prop), the blasts (static global type 7, game/
// explosions), hit sparks (game/hit_effects), the CLUT animation (game/clut_animation), its falling shots and lobbed
// blobs (pool 0x40 stage types 0xd / 0x1d, content/subruins/blob_boss_shots).
#include "content/subruins/blob_boss.hpp"
#include "content/subruins/blob_boss_state.hpp"

#include "content/saul_kada/steered_prop.hpp"
#include "content/subruins/blob_boss_shots.hpp"
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
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/explosions.hpp"
#include "game/fixed_point.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/message.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_status.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace blob_boss_state;

namespace {

// +0x158
constexpr uint32_t kBlobDead = 0x1, kBlobFlashProp = 0x2, kBlobClutRunning = 0x4, kBlobClutStart = 0x8;
constexpr uint32_t kBlobAimFromBody = 0x10, kBlobPulseScale = 0x20, kBlobPulseRun = 0x40;
constexpr uint32_t kBlobPulse = kBlobPulseScale | kBlobPulseRun;
constexpr uint32_t kBlobSlammed = 0x80;    // the last attack was the slam (the next is the jump)
constexpr uint32_t kBlobShootNext = 0x100; // the wait ends in the shoot
constexpr uint32_t kBlobLeadRight = 0x200, kBlobLeadLeft = 0x400;
constexpr uint32_t kBlobLowLife = 0x800, kBlobSecondPhase = 0x1000, kBlobNoDamage = 0x2000;
constexpr uint32_t kBlobPlayerFarZ = 0x4000, kBlobRingShots = 0x8000, kBlobZClamped = 0x10000;
constexpr uint32_t kBlobSpinning = 0x20000, kBlobShooting = 0x40000, kBlobHealing = 0x80000;
constexpr uint32_t kBlobHusk = 0x20000000, kBlobAlerted = 0x40000000, kBlobFleeing = 0x80000000;
constexpr uint32_t kBlobPhaseBits = kBlobLowLife | kBlobSecondPhase;

enum class BlobVariant : uint8_t { Small = 0, Fight = 1, Rise = 2, Sink = 3 };
enum class BlobState : uint8_t { Init = 0, Fight = 1, Free = 2, Small = 3, Rise = 4, Sink = 5 };
enum class FightSub : uint8_t { Wait = 0, Chase = 1, Slam = 2, Jump = 3, Shoot = 4, SecondPhase = 5, Spin = 6, Die = 7 };
enum class SmallSub : uint8_t { Wander = 0, Flee = 1, Die = 2 };

// The view of its prop's targets as one word (the exe's dword store of the yaw and the pitch)
struct PropTargetsWord {
    explicit PropTargetsWord(const StaticActor& actor) : PropTargetsWord(const_cast<StaticActor&>(actor)) {}
    explicit PropTargetsWord(StaticActor& actor) : word(actor.hitBox.result) {}
    uint32_t& word;
};
// The CLUT animation it spawns, as it reads it: its state and its key word (0: the sequence ended)
struct BlobClutView {
    explicit BlobClutView(EffectActor& clut) : state(clut.state), progress(clut.trail[4]) {}
    uint8_t& state;    // +0x8
    int16_t& progress; // +0x20
};
constexpr uint8_t kClutRestoreState = 2;

constexpr uint8_t kPropType = 0x39;              // the steered prop (static stage type)
const DataTable<ScalarTable<uint16_t, 0x10>> ringYaws{TableId::BlobBossRingYaws}; // the falling shots' yaws around the aim
constexpr uint32_t kBlocksGroupClass = 0x60, kBlocksGroupType = 0x1f, kBlocksGroupVariant = 3; // the object-state blocks
constexpr int8_t kMessageWindow = 4;
constexpr uint32_t kMessageBank = 0x73c000;
constexpr uint32_t kFlagTutorialDone = 0x75, kFlagBossGone = 0x179;
constexpr uint32_t kBossGaugeColour = 0x6040c0;

StaticActor& propOf(const AnimatedActor& a, BlobFields& f) { // +0x14c (the exe dereferences it unchecked)
    if (!f.prop) notPortedAt(kStoryPort, "0x4d6b40: the prop +0x14c is null (the exe would crash), actor", a.at);
    return *f.prop;
}
void toSub(AnimatedActor& a, uint8_t s) {
    a.subState = s;
    a.step = 0;
}
template <class T> void incIfLt(T&& v, int lim) {
    if (v < lim) ++v;
}
template <class T> void decIfGt(T&& v, int lim) {
    if (v > lim) --v;
}
// the scale steps: x = (x * k0) >> 10, y = (y * k2) >> 10, z = x
void scaleBy(AnimatedActor& a, int32_t k0, int32_t k2) {
    const int32_t v0 = (int32_t(a.scale.x) * k0) >> 10;
    const int32_t v2 = (int32_t(a.scale.y) * k2) >> 10;
    a.scale.x = int16_t(v0);
    a.scale.y = int16_t(v2);
    a.scale.z = int16_t(v0);
}
template <class T> void wrapUp(T&& v) { // >= 0x800 -> -= 0x1000
    if (v >= 0x800) v -= 0x1000u;
}
void wrapBoth(int16_t& v) {
    wrapUp(v);
    if (v < -0x800) v += 0x1000u;
}
void message(int16_t id) { // exe 0x5576b0(4, 1) + 0x557390(4, 0x73c000, id)
    windowClose(kMessageWindow, WindowClose::Force);
    windowOpen(kMessageWindow, kMessageBank, id);
}
// the 64-step sine / cosine of the game state's tables at the index (unchecked as the exe)
int16_t sine64At(int i) { return trig64Tables().sine[size_t(i)]; }
int16_t cosine64At(int i) { return trig64Tables().cosine[size_t(i)]; }

// ---- class helpers ------------------------------------------------------------------------------------------------
// exe 0x4d9880(actor, target): the body yaw toward the target by 0x20 (0x80 alerted / in its second phase / aiming
// from the body)
void turn(BlobFields& f, BlobFightBlock& w, uint16_t target) {
    const int16_t t = (f.flags & (kBlobAlerted | kBlobSecondPhase | kBlobAimFromBody)) ? 0x80 : 0x20;
    const uint16_t si = uint16_t(w.bodyYaw);
    int16_t d = int16_t(uint16_t(si - target));
    if (d >= 0x800) d = int16_t(uint16_t(d) - 0x1000);
    if (d < -0x800) d = int16_t(uint16_t(d) + 0x1000);
    if (d >= t) w.bodyYaw = int16_t(uint16_t(si - uint16_t(t)));
    else if (int(d) > -int(t)) w.bodyYaw = int16_t(target);
    else w.bodyYaw = int16_t(uint16_t(si + uint16_t(t)));
    wrapBoth(w.bodyYaw);
}
// exe 0x4d9920: x / z (integer parts) toward 0 by 2
void centre(AnimatedActor& a) {
    for (Fixed16* c : {&a.pos.x, &a.pos.z}) {
        const int16_t v = c->whole;
        if (v > 2) c->whole = int16_t(v - 2);
        else if (v < -2) c->whole = int16_t(v + 2);
        else c->whole = 0;
    }
}
// exe 0x4d9980: the fight's damage -- a hit (0x40000) flashes it (not while spinning) and the prop, its damage (at most
// 0x20) is taken; else the queued damage is drained 0x20 a tick. Dead -> kBlobDead; below a fifth of its life ->
// kBlobLowLife; the damage summed into damageTaken
void fightDamage(AnimatedActor& a, BlobFields& f, BlobFightBlock& w) {
    uint32_t r = w.bodyHit.result;
    int32_t d = 0;
    bool queued = true;
    if (r & kHitAttackByPlayer) {
        r &= kHitDamageMask;
        if (!(f.flags & kBlobSpinning)) a.tint = kTintFlash;
        f.flags |= kBlobFlashProp;
        if (r != 0) {
            queued = false;
            d = int32_t(r);
            if (d > 0x20) d = 0x20;
            w.queuedDamage = 0;
        }
    }
    if (queued) {
        const int16_t q = w.queuedDamage;
        if (q > 0x20) {
            w.queuedDamage = int16_t(q - 0x20);
            d = 0x20;
        } else {
            d = q;
            w.queuedDamage = 0;
        }
    }
    if (d != 0)
        if (hitPointsAdd(a, -d) != 0) f.flags |= kBlobDead; // exe 0x5e52f0
    if (int32_t(a.hitPoints) < int32_t(a.maxHitPoints) / 5) f.flags |= kBlobLowLife;
    w.damageTaken = int16_t(uint16_t(w.damageTaken) + uint16_t(d));
}
// exe 0x4d9a50: the small form's damage -- a hit alerts it (unless it is already alerted / fleeing) and flashes it
void smallDamage(AnimatedActor& a, BlobFields& f, BlobFightBlock& w) {
    const uint32_t r = w.bodyHit.result;
    if (!(r & kHitAttackByPlayer)) return;
    if (!(f.flags & (kBlobAlerted | kBlobFleeing))) f.flags |= kBlobAlerted;
    a.tint = kTintFlash;
    f.flags |= kBlobFlashProp;
    const uint32_t d = r & kHitDamageMask;
    if (d != 0)
        if (hitPointsAdd(a, -int(d)) != 0) f.flags |= kBlobDead;
}
// exe 0x4d9ac0: the hit entries -- the prop's point (its attack; deflecting in the second phase / while it takes no
// damage) or, as a husk, a solid to stand on (exe 0x414050); unless a husk / spinning the capsule from the body's top
// along the top's pitch / yaw over its reach
void submitHits(AnimatedActor& a, BlobFields& f, BlobFightBlock& w) {
    w.bodyHit.result = 0;
    w.bodyHit.previousX = a.prevPos.x.whole;
    w.bodyHit.previousZ = a.prevPos.z.whole;
    w.reachHit.result = 0;
    w.reachHit.previousX = a.prevPos.x.whole;
    w.reachHit.previousZ = a.prevPos.z.whole;
    const StaticActor& p = propOf(a, f);
    const AnimatedActorStats row = w.statsRow();
    if (!(f.flags & kBlobHusk)) {
        const int16_t pt[3] = {p.pos.x.whole, p.pos.y.whole, p.pos.z.whole};
        const uint32_t kind = (f.flags & (kBlobSecondPhase | kBlobNoDamage)) ? kHitAttackDeflects : kHitAttackSolid;
        hitListSubmit(pt, w.bodyHit, uint32_t(a.slot) << 24 | 0x400010u, uint32_t(row.attack) | kind); // exe 0x5e4120
    } else {
        actorCollisionSubmitSolid(a, SolidType::StandOn, kNoBone, 0); // exe 0x414050(actor, 1, 0xffff, 0)
    }
    if (f.flags & (kBlobHusk | kBlobSpinning)) return;
    const int16_t x = a.pos.x.whole, z = a.pos.z.whole;
    const int16_t y = int16_t(uint16_t(uint16_t(a.pos.y.whole) - uint16_t(w.topHeight)));
    const int pitch = uint16_t(f.topPitch), yaw = uint16_t(f.topYaw);
    const int32_t cosP = fixedCos(pitch);
    const int32_t yOff = (int32_t(fixedSin(pitch)) * w.topHeight) >> 12;
    const int32_t len = f.reach;
    const int32_t dx = (int32_t(int16_t((int32_t(fixedSin(yaw)) * len) >> 12)) * cosP) >> 12;
    const int32_t dz = (int32_t(int16_t((int32_t(fixedCos(yaw)) * len) >> 12)) * cosP) >> 12;
    const int16_t pa[3] = {int16_t(x - dx), int16_t(uint16_t(y) + uint16_t(yOff)), int16_t(z - dz)};
    const int16_t pb[3] = {x, y, z};
    hitListSubmitCapsule(pa, 0xc, pb, 0xc, w.reachHit, uint32_t(a.slot) << 24 | 0x683000u,
                         uint32_t(row.attack) | kHitAttackThrown | kHitAttackSolid); // exe 0x5e4190
}
// exe 0x4d9ca0: a blast (static global type 7 variant 0, size 0x40) at a random point around it
void blastNear(const AnimatedActor& a) {
    const uint32_t r1 = rngStep();
    const uint32_t r2 = rngStep();
    const uint32_t r3 = rngStep();
    const uint16_t x = uint16_t(uint16_t(a.pos.x.whole) - (r1 & 0xff) + 0x80u);
    const uint16_t y = uint16_t(uint16_t(a.pos.y.whole) - ((r2 >> 8) & 0xff));
    const uint16_t z = uint16_t(uint16_t(a.pos.z.whole) - ((r3 >> 16) & 0xff) + 0x80u);
    const uint32_t address = allocActor60(0); // exe 0x5e1300
    if (!address) return;
    StaticActor& b = staticActorAt(address);
    BlastParams params(b);
    b.loopFlags |= kActorSweepGroup2;
    b.flags = kActorLive;
    b.type = typeByte(GlobalStaticType::Blast);
    b.variant = 0;
    b.pos.x.whole = int16_t(x);
    b.pos.y.whole = int16_t(y);
    b.pos.z.whole = int16_t(z);
    params.size = 0x40;
    params.attack = 0;
}
// exe 0x4d9d60: a blast (static global type 7 variant 1, size 0x100) at it
void blastAt(const AnimatedActor& a) {
    const uint32_t address = allocActor60(0); // exe 0x5e1300
    if (!address) return;
    StaticActor& b = staticActorAt(address);
    BlastParams params(b);
    b.flags = kActorLive;
    b.loopFlags |= kActorSweepGroup2;
    b.type = typeByte(GlobalStaticType::Blast);
    b.variant = 1;
    b.pos.x.whole = a.pos.x.whole;
    b.pos.y.whole = a.pos.y.whole;
    b.pos.z.whole = a.pos.z.whole;
    params.size = 0x100;
    params.attack = 0;
}
// exe 0x4d9db0(actor, shift): a hit spark (pool 0xa0 global type 0 variant 0) at a random x / z (+-0x80 << shift)
void sparkNear(const AnimatedActor& a, int shift) {
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (!address) return;
    EffectActor& s = effectActorAt(address);
    s.type = typeByte(GlobalEffectType::HitSpark);
    s.variant = 0;
    s.flags = kActorLive;
    s.loopFlags |= kActorSweepGroup2;
    const uint32_t r1 = rngStep();
    const uint32_t dx = ((r1 & 0xff) - 0x80u) << shift;
    s.pos.x = int16_t(uint16_t(uint16_t(a.pos.x.whole) - dx));
    s.pos.y = int16_t(uint16_t(uint16_t(a.pos.y.whole) - 0x80u));
    const uint32_t r2 = rngStep();
    const uint32_t dz = (((r2 >> 16) & 0xff) - 0x80u) << shift;
    classState<HitSparkFields>(s).size = 0x100;
    s.pos.z = int16_t(uint16_t(uint16_t(a.pos.z.whole) - dz));
}
// exe 0x4d9e70: the CLUT animation (pool 0xa0 global type 2) of sequence 5 (the tutorial) or 4, kept in clutEffect
void startClutEffect(const AnimatedActor& a, BlobFields& f) {
    const uint32_t address = allocActorA0(kActorStageClass); // exe 0x5e13a0
    f.clutEffect = address;
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    e.flags = kActorLive;
    e.type = typeByte(GlobalEffectType::ClutAnimation);
    e.variant = 0;
    e.loopFlags = kActorSweepGroup2;
    e.setParamsWord(a.spawnByte == 0 ? 5u : 4u);
    e.enterState(0);
}
// exe 0x4d9ee0: a falling shot (big) from the body's top, at the ring's yaw around the aim
void ringShot(const AnimatedActor& a, BlobFields& f, BlobFightBlock& w) {
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return;
    ProjectileActor& s = projectileActorAt(address);
    s.loopFlags |= kActorSweepGroup2;
    s.flags = kActorLive;
    s.type = kFallingShotType;
    s.variant = 0;
    s.pos.x = a.pos.x;
    s.pos.y = int32_t(uint32_t(a.pos.y) - (uint32_t(int32_t(w.topHeight)) << 16));
    s.pos.z = a.pos.z;
    s.rotation.x = 0;
    s.rotation.y = int16_t(uint16_t(ringYaws[uint8_t(w.shots) & 0xfu] + uint16_t(f.aimYaw)));
    s.rotation.z = 0;
    s.velocity.x = 0;
    s.velocity.y = 0;
    s.velocity.z = -0x400;
    s.acceleration.x = 0;
    s.acceleration.y = 0;
    s.acceleration.z = 0;
    s.setParamsWord(1); // big
}
// exe 0x4d9f70: a lobbed blob at the player; its yaw leads by counter << 5 (the lead, +-10, grows while the aim keeps
// turning the same way: kBlobLeadRight / kBlobLeadLeft)
void lobBlob(const AnimatedActor& a, BlobFields& f, BlobFightBlock& w) {
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return;
    ProjectileActor& s = projectileActorAt(address);
    LobbedBlobParams params(s);
    s.loopFlags |= kActorSweepGroup2;
    s.flags = uint8_t(kActorLiveDrawn | kActorMeshModel);
    s.type = kLobbedBlobType;
    s.variant = 0;
    params.hitSize = 0x40;
    params.attack = 4;
    s.pos.x = a.pos.x;
    s.pos.y = int32_t(uint32_t(a.pos.y) - (uint32_t(int32_t(w.topHeight)) << 17));
    s.pos.z = a.pos.z;
    const Player& p = player();
    const int32_t target[3] = {p.pos.x, int32_t(uint32_t(p.pos.y) + 0xffc00000u), p.pos.z};
    int d = int(f.aimYaw) - w.lastAimYaw;
    if (d < -0x800) d += 0x1000;
    if (d >= 0x800) d -= 0x1000;
    uint32_t fl = f.flags;
    if (d > 0) {
        if (fl & kBlobLeadRight) {
            if (f.counter < 0xa) ++f.counter;
        } else {
            f.counter = 0;
        }
        fl = (fl & ~kBlobLeadLeft) | kBlobLeadRight;
    } else if (d < 0) {
        if (fl & kBlobLeadLeft) {
            if (f.counter > -0xa) --f.counter;
        } else {
            f.counter = 0;
        }
        fl = (fl & ~kBlobLeadRight) | kBlobLeadLeft;
    } else {
        f.counter = 0;
        fl &= ~(kBlobLeadRight | kBlobLeadLeft);
    }
    f.flags = fl;
    s.rotation.x = 0;
    s.rotation.y = int16_t(uint16_t((uint16_t(f.counter) << 5) + uint16_t(f.aimYaw)));
    s.rotation.z = 0;
    s.acceleration.y = 0x20;
    s.velocity.z = 0x400;
    const int32_t from[3] = {s.pos.x, s.pos.y, s.pos.z};
    s.velocity.y = int16_t(launchSpeed(from, target, 0x400, 0x20, 0xa00)); // exe 0x5e3ee0
}
// exe 0x4da0f0: a lobbed blob (variant 1 in the tutorial) thrown up in a random direction
void sprayBlob(const AnimatedActor& a, BlobFightBlock& w) {
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return;
    ProjectileActor& s = projectileActorAt(address);
    LobbedBlobParams params(s);
    s.flags = uint8_t(kActorLiveDrawn | kActorMeshModel);
    s.type = kLobbedBlobType;
    s.variant = a.spawnByte == 0 ? 1 : 0;
    s.loopFlags |= kActorSweepGroup2;
    const uint32_t r1 = rngStep();
    params.attack = 0x10;
    params.hitSize = uint16_t(((r1 >> 24) & 0x3f) + 0x20u);
    s.pos.x = a.pos.x;
    s.pos.y = int32_t(uint32_t(a.pos.y) - (uint32_t(int32_t(w.topHeight)) << 17));
    s.pos.z = a.pos.z;
    s.rotation.x = 0;
    const uint32_t r2 = rngStep();
    s.rotation.y = int16_t(r2 & 0xfff);
    s.rotation.z = 0;
    s.acceleration.y = 0x40;
    const uint32_t r3 = rngStep();
    s.velocity.z = 0x100;
    s.velocity.y = int16_t(uint16_t(0xfffffe00u - ((r3 >> 12) & 0x3ff)));
}

// the planar distance / angle / pitch of two 16.16 points (exe 0x5e3b00 / 0x5e3bc0 / 0x5e3ca0 on the exe's stack points)
void wholes(const int32_t p[3], int16_t out[3]) {
    for (int k = 0; k < 3; ++k) out[k] = wholePart(p[k]);
}
int aimDistance(const int32_t from[3], const int32_t to[3]) {
    int16_t a[3], b[3];
    wholes(from, a);
    wholes(to, b);
    return planarDistance16(a, b);
}
int aimPitch(const int32_t from[3], const int32_t to[3]) {
    int16_t a[3], b[3];
    wholes(from, a);
    wholes(to, b);
    return pitchOf(a, b);
}
// the shared head of states 1 / 3 / 4 / 5: the heights / the reach from the scale, the previous pose, the tint of its
// colour, the aim from the player (y - 0x80; the fight aiming from the body: from the origin at the body's height) to
// bone 0 (distance, yaw, 0x800 - pitch), then from the player to the body's top (yaw, 0x800 - pitch). Returns the
// player point's z (integer part) the fight reads after it
int16_t aimHead(AnimatedActor& a, BlobFields& f, BlobFightBlock& w, bool relative) {
    w.topHeight = int16_t((int32_t(a.scale.y) << 7) >> 9);
    w.height = int16_t((int32_t(w.baseHeight) * a.scale.y) >> 9);
    if (w.height < 0x40) w.height = 0x40;
    f.reach = int16_t((int32_t(a.scale.x) * 0x160) >> 9);
    const ShortVec4& bone = bonePoint(a, 0, kStoryPort); // exe 0x5e1810(a, 0)
    const Player& player0 = player();
    int32_t from[3];
    auto fromPlayer = [&] {
        from[0] = player0.pos.x;
        from[1] = int32_t(uint32_t(player0.pos.y) + 0xff800000u);
        from[2] = player0.pos.z;
    };
    const bool fromBody = relative && (f.flags & kBlobAimFromBody);
    if (fromBody) {
        from[0] = 0;
        from[1] = int32_t(uint32_t(int32_t(bone.y) - w.height) << 16);
        from[2] = 0;
    } else {
        fromPlayer();
    }
    int32_t to[3] = {int32_t(uint32_t(int32_t(bone.x)) << 16), int32_t(uint32_t(int32_t(bone.y) - w.height) << 16),
                     int32_t(uint32_t(int32_t(bone.z)) << 16)};
    a.prevAnim = a.anim;
    a.savePreviousPose();
    w.lastAimYaw = f.aimYaw;
    const int v = ((int(f.blue) << 5 | int(f.green)) << 5) | int(f.red);
    a.tint = uint16_t(v);
    f.aimDistance = int16_t(aimDistance(from, to));                   // exe 0x5e3b00
    f.aimYaw = int16_t(angleOf(from[0], from[2], to[0], to[2]));      // exe 0x5e3bc0
    f.aimPitch = int16_t(0x800 - aimPitch(from, to));                 // exe 0x5e3ca0
    if (relative && (f.flags & kBlobAimFromBody)) fromPlayer();
    to[1] = int32_t(uint32_t(int32_t(bone.y) - w.topHeight) << 16);
    f.topYaw = int16_t(angleOf(from[0], from[2], to[0], to[2]));
    f.topPitch = int16_t(0x800 - aimPitch(from, to));
    return wholePart(from[2]);
}
// the velocities gain the accelerations, then the position moves by (0, vy, -vz) turned by the body yaw (exe 0x5e36f0)
void moveBody(AnimatedActor& a, BlobFightBlock& w) {
    a.velocity.x = int16_t(uint16_t(a.velocity.x) + uint16_t(a.acceleration.x));
    a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    a.velocity.z = int16_t(uint16_t(a.velocity.z) + uint16_t(a.acceleration.z));
    moveAlongYaw(a.pos, w.bodyYaw, 0, a.velocity.y, -int(a.velocity.z));
}
void clampZ(AnimatedActor& a, BlobFields& f) { // z within +-0x700 (kBlobZClamped when clamped)
    if (a.pos.z.whole >= 0x700) {
        a.pos.z.whole = 0x700;
        f.flags |= kBlobZClamped;
    } else if (a.pos.z.whole <= -0x700) {
        a.pos.z.whole = -0x700;
        f.flags |= kBlobZClamped;
    } else {
        f.flags &= ~kBlobZClamped;
    }
}
void propFollow(AnimatedActor& a, BlobFields& f, BlobFightBlock& w) { // the prop at the body's top
    StaticActor& p = propOf(a, f);
    p.pos = a.pos;
    p.pos.y.whole = int16_t(uint16_t(p.pos.y.whole) - uint16_t(w.height));
}
void propWarm(AnimatedActor& a, BlobFields& f) { // the prop's colour toward (0x1f, 0xc, 0xc)
    SteeredPropParams p(propOf(a, f));
    incIfLt(p.red, 0x1f);
    decIfGt(p.green, 0xc);
    decIfGt(p.blue, 0xc);
}
void propCool(AnimatedActor& a, BlobFields& f, int r) { // the prop's colour toward (r, 0x10, 0x10)
    SteeredPropParams p(propOf(a, f));
    decIfGt(p.red, r);
    incIfLt(p.green, 0x10);
    incIfLt(p.blue, 0x10);
}
void brighten(BlobFields& f) { // its own colour up to 0x10 (all three components with the red)
    if (f.red < 0x10) {
        ++f.red;
        ++f.green;
        ++f.blue;
    }
}
void dim(BlobFields& f) { // its own colour down to 0xb
    if (f.red > 0xb) {
        --f.red;
        --f.green;
        --f.blue;
    }
}
void markBlocksGroup() { // the object-state blocks' group (static type 0x1f variant 3) marked: they fade
    enemyGroupMark(enemyGroupFind(kBlocksGroupClass, kBlocksGroupType, kBlocksGroupVariant), 1); // exe 0x5e6d00 / 0x5e6d40
}

// ---- the fight's sub-states [0x8956c8] ------------------------------------------------------------------------------
void fightWait(AnimatedActor& a, BlobFields& f, BlobFightBlock& w) { // exe 0x4d7540
    turn(f, w, uint16_t(f.aimYaw));
    --f.timer;
    if (f.timer != 0) return;
    const uint32_t fl = f.flags;
    if ((fl & kBlobPhaseBits) == kBlobLowLife) {
        toSub(a, uint8_t(FightSub::SecondPhase));
        return;
    }
    if (!(fl & kBlobShootNext)) {
        a.subState = uint8_t(FightSub::Chase);
        a.velocity.z = int16_t((f.flags & kBlobSecondPhase) ? 0x1a0 : 0x100);
        return;
    }
    f.flags = fl & ~kBlobShootNext;
    toSub(a, uint8_t(FightSub::Shoot));
}
void fightChase(AnimatedActor& a, BlobFields& f, BlobFightBlock& w) { // exe 0x4d75c0
    turn(f, w, uint16_t(f.aimYaw));
    if (w.damageTaken > 0x258) {
        if (!(f.flags & kBlobSlammed)) {
            f.flags |= kBlobSlammed;
            a.subState = uint8_t(FightSub::Slam);
        } else {
            markBlocksGroup();
            soundPlayAtPos(0x260, kSoundAtListener);
            f.flags &= ~kBlobSlammed;
            a.subState = uint8_t(FightSub::Jump);
        }
        a.step = 0;
        const uint32_t fl = f.flags | kBlobAimFromBody;
        f.flags = fl;
        if (!(fl & kBlobPhaseBits)) f.flags = fl | kBlobNoDamage;
        f.counter = 0;
        a.velocity.z = 0x1a0;
    }
    if ((f.flags & kBlobPhaseBits) == kBlobLowLife) {
        f.timer = 1;
        a.velocity.z = 0;
        toSub(a, uint8_t(FightSub::Wait));
    }
    if (f.flags & kBlobRingShots) {
        if (!(uint8_t(w.shots) & 3)) soundPlayAtPos(0x25e, a.pos);
        ringShot(a, f, w);
        ++w.shots;
        if (w.shots >= 0x40) {
            w.shots = 0;
            if (f.flags & kBlobSecondPhase) {
                ++w.ringRounds;
                if (w.ringRounds >= 3) {
                    w.ringRounds = 0;
                    toSub(a, uint8_t(FightSub::Spin));
                }
            }
            w.farTicks = 0;
            f.flags &= ~kBlobRingShots;
        }
    } else {
        w.shots = 0;
    }
    if (!(f.flags & kBlobPlayerFarZ) && (f.flags & kBlobSecondPhase)) {
        --w.phaseTicks;
        if (uint16_t(w.phaseTicks) == 0) toSub(a, uint8_t(FightSub::Spin));
    }
    if (a.subState != uint8_t(FightSub::Chase)) {
        w.shots = 0;
        w.farTicks = 0;
        f.flags &= ~kBlobRingShots;
    }
}
// the common step 0 of the slam / the jump: close enough -> stop, aim from the player again
bool closeIn(AnimatedActor& a, BlobFields& f) {
    if (f.aimDistance > 0x68) return false;
    f.flags &= ~kBlobAimFromBody;
    ++a.step;
    a.velocity.z = 0;
    return true;
}
void fightSlam(AnimatedActor& a, BlobFields& f, BlobFightBlock& w) { // exe 0x4d7750 (jump table 0x4d7a10)
    turn(f, w, uint16_t(f.aimYaw));
    if (a.step != 0) centre(a);
    ++f.counter;
    if (a.step <= 2 && !(uint16_t(f.counter) & 7)) propWarm(a, f);
    const uint8_t s = a.step;
    switch (s) {
    case 0: closeIn(a, f); return;
    case 1:
        if (f.pulse != 0) return;
        soundPlayAtPos(0x258, a.pos);
        f.flags &= ~kBlobPulse;
        a.step = uint8_t(s + 1);
        return;
    case 2:
        if (a.scale.x > 0x100) {
            if (w.baseHeight < 0xf0) w.baseHeight += 2u;
            scaleBy(a, 0x3f8, 0x410);
        } else {
            a.step = uint8_t(s + 1);
            f.timer = 0x1e;
        }
        return;
    case 3:
        --w.baseHeight;
        --f.timer;
        if (f.timer != 0) return;
        a.step = uint8_t(s + 1);
        soundPlayAtPos(0x259, a.pos);
        return;
    case 4:
        if (a.scale.x >= 0x800) {
            a.step = uint8_t(s + 1);
            return;
        }
        if (w.baseHeight > 0x80) w.baseHeight -= 8u;
        else w.baseHeight = 0x80;
        scaleBy(a, 0x480, 0x390);
        return;
    case 5:
        if (a.scale.x > 0x200) {
            scaleBy(a, 0x3f0, 0x40d);
        } else {
            f.timer = 0x1e;
            f.flags = (f.flags & ~kBlobNoDamage) | kBlobPulse;
            w.damageTaken = 0;
            toSub(a, uint8_t(FightSub::Wait));
        }
        if (!(uint8_t(f.counter) & 3)) propCool(a, f, 0x10);
        return;
    default: return;
    }
}
void fightJump(AnimatedActor& a, BlobFields& f, BlobFightBlock& w) { // exe 0x4d7a30 (jump table 0x4d7e68)
    turn(f, w, uint16_t(f.aimYaw));
    if (a.step != 0) centre(a);
    ++f.counter;
    if (a.step <= 3 && !(uint16_t(f.counter) & 7)) propWarm(a, f);
    const uint8_t s = a.step;
    auto widen = [&a] { scaleBy(a, 0x480, 0x380); }; // exe 0x4d7d40
    switch (s) {
    case 0: closeIn(a, f); return;
    case 1:
        if (f.pulse != 0) return;
        f.timer = 0x12c;
        f.flags &= ~kBlobPulse;
        a.step = uint8_t(s + 1);
        return;
    case 2:
        --f.timer;
        if (f.timer == 0) {
            soundPlayAtPos(0x259, a.pos);
            a.step = uint8_t(s + 1);
            a.scale.y = 0x280;
            a.scale.x = 0x200;
            a.scale.z = 0x200;
            return;
        }
        a.scale.x = int16_t((rngStep() & 0xf) * 2 + 0x1f0u);
        a.scale.y = int16_t(((rngStep() >> 3) & 0x1e) + 0x270u);
        a.scale.z = int16_t(((rngStep() >> 7) & 0x1e) + 0x1f0u);
        return;
    case 3:
        if (a.scale.x >= 0x400) {
            a.step = uint8_t(s + 1);
            return;
        }
        widen();
        return;
    case 4:
        if (w.baseHeight < 0xf0) w.baseHeight += 8u;
        if (a.scale.x > 0x100) {
            a.velocity.y = -0x300;
            a.acceleration.y = 0x30;
            a.airborne = 1;
            scaleBy(a, 0x380, 0x4c0);
        } else {
            soundPlayAtPos(0x25a, a.pos);
            a.step = uint8_t(s + 1);
        }
        return;
    case 5:
        if (w.baseHeight > 0x80) w.baseHeight -= 2u;
        if (a.scale.x < 0x200) scaleBy(a, 0x41c, 0x3dd);
        if (a.airborne != 0) return;
        soundPlayAtPos(0x25b, a.pos);
        a.step = uint8_t(s + 1);
        a.velocity.y = 0;
        a.acceleration.y = 0;
        [[fallthrough]];
    case 6: // exe 0x4d7d22
        if (w.baseHeight > 0x40) w.baseHeight -= 8u;
        if (a.scale.x < 0x400) {
            widen();
            return;
        }
        f.flags |= kBlobClutStart;
        ++a.step;
        return;
    case 7:
        if (w.baseHeight < 0x80) w.baseHeight += 8u;
        else w.baseHeight = 0x80;
        if (a.scale.x > 0x200) {
            scaleBy(a, 0x3f0, 0x414);
        } else {
            f.timer = 0x1e;
            f.flags = (f.flags & ~kBlobNoDamage) | kBlobShootNext | kBlobPulse;
            w.damageTaken = 0;
            toSub(a, uint8_t(FightSub::Wait));
        }
        if (!(uint8_t(f.counter) & 1)) propCool(a, f, 0x10);
        return;
    default: return;
    }
}
void fightShoot(AnimatedActor& a, BlobFields& f, BlobFightBlock& w) { // exe 0x4d7e90 (jump table 0x4d80e8)
    turn(f, w, uint16_t(f.aimYaw));
    if (a.step != 0) centre(a);
    const uint8_t s = a.step;
    auto next = [&a, &f, s] { // exe 0x4d8010
        a.step = uint8_t(s + 1);
        f.flags |= kBlobNoDamage;
    };
    switch (s) {
    case 0:
        if (f.pulse != 0) return;
        soundPlayAtPos(0x258, a.pos);
        f.flags = (f.flags & ~kBlobPulse) | kBlobNoDamage;
        a.step = uint8_t(s + 1);
        return;
    case 1:
        if (a.scale.x > 0x100) {
            if (w.baseHeight < 0xd0) w.baseHeight += 2u;
            scaleBy(a, 0x3f8, 0x410);
        } else {
            a.step = uint8_t(s + 1);
            f.flags &= ~kBlobNoDamage;
            f.timer = 0xf;
            f.counter = 0;
            w.shots = 0x20;
        }
        return;
    case 2:
        if (player().pos.y.whole > -0x300 || int32_t(a.hitPoints) < int32_t(a.maxHitPoints) / 5) {
            next();
            return;
        }
        --f.timer;
        if (f.timer != 0) return;
        f.flags |= kBlobShooting;
        soundPlayAtPos(0x25c, a.pos);
        lobBlob(a, f, w);
        --w.shots;
        if (w.shots != 0) {
            f.timer = 0xf;
            return;
        }
        next();
        return;
    case 3:
        if (a.scale.x < 0x200) {
            if (w.baseHeight > 0x80) w.baseHeight -= 4u;
            scaleBy(a, 0x410, 0x3e9);
            return;
        }
        markBlocksGroup();
        soundPlayAtPos(0x261, kSoundAtListener);
        f.flags = (f.flags & ~(kBlobHealing | kBlobNoDamage)) | kBlobPulse | kBlobClutStart;
        f.timer = 0x1e;
        w.damageTaken = 0;
        toSub(a, uint8_t(FightSub::Wait));
        return;
    default: return;
    }
}
void fightSecondPhase(AnimatedActor& a, BlobFields& f, BlobFightBlock& w) { // exe 0x4d8100
    turn(f, w, uint16_t(f.aimYaw));
    brighten(f);
    switch (a.step) {
    case 0:
        if (f.pulse != 0) return;
        f.flags |= kBlobSecondPhase;
        w.phaseTicks = 0x12c;
        ++a.step;
        return;
    case 1: {
        SteeredPropParams p(propOf(a, f));
        for (auto* c : {&p.green, &p.blue}) {
            if (*c > 0xc) --*c;
            else if (*c < 0xc) ++*c;
        }
        if (p.red < 0x1f) {
            ++p.red;
            return;
        }
        f.timer = 0x5a;
        f.flags &= ~kBlobAimFromBody;
        w.damageTaken = 0;
        toSub(a, uint8_t(FightSub::Wait));
        return;
    }
    default: return;
    }
}
void fightSpin(AnimatedActor& a, BlobFields& f, BlobFightBlock& w) { // exe 0x4d81e0 (jump table 0x4d83d4)
    const uint8_t s0 = a.step;
    if (s0 >= 1 && s0 <= 2) {
        SteeredPropParams p(propOf(a, f));
        decIfGt(p.red, 0xc);
        decIfGt(p.green, 4);
        decIfGt(p.blue, 4);
        dim(f);
    }
    if (a.velocity.z > 0) {
        a.acceleration.z = -4;
    } else {
        a.acceleration.z = 0;
        a.velocity.z = 0;
    }
    const uint8_t s = a.step;
    switch (s) {
    case 0:
        if (f.pulse != 0) return;
        f.flags = (f.flags & ~kBlobPulse) | kBlobSpinning;
        soundPlayAtPos(0x25f, a.pos);
        a.step = uint8_t(s + 1);
        return;
    case 1:
        if (a.scale.x < 0x600) {
            scaleBy(a, 0x420, 0x3e0);
            return;
        }
        f.flags &= ~kBlobPhaseBits;
        f.timer = 0x78;
        classState<SteeredPropFields>(propOf(a, f)).targetPitch = 0x200;
        a.step = uint8_t(s + 1);
        return;
    case 2:
        --f.timer;
        if (f.timer == 0) a.step = uint8_t(s + 1);
        return;
    case 3:
        if (a.scale.x > 0x200) {
            scaleBy(a, 0x3f0, 0x414);
            return;
        }
        f.flags &= ~kBlobSpinning;
        if (!(f.flags & kBlobDead)) {
            f.flags |= kBlobPhaseBits | kBlobPulse;
            w.phaseTicks = 0x12c;
            a.subState = uint8_t(FightSub::SecondPhase);
            a.step = 1;
        } else {
            f.timer = 0x78;
            toSub(a, uint8_t(FightSub::Die));
        }
        return;
    default: return;
    }
}
void fightDie(AnimatedActor& a, BlobFields& f) { // exe 0x4d83f0
    SteeredPropParams p(propOf(a, f));
    incIfLt(p.red, 0x10);
    incIfLt(p.green, 0x10);
    incIfLt(p.blue, 0x10);
    brighten(f);
    switch (a.step) {
    case 0: {
        --f.timer;
        const int16_t t = f.timer;
        if (t < 0x3c && !(t & 7)) sparkNear(a, 1);
        if (f.timer == 0) {
            a.scale.y = 0x280;
            ++a.step;
        } else {
            a.scale.y = int16_t(((rngStep() >> 3) & 0x1e) + 0x270u);
        }
        a.scale.x = int16_t((rngStep() & 0xf) * 2 + 0x1f8u);
        a.scale.z = int16_t(((rngStep() >> 7) & 0x1e) + 0x1f8u);
        return;
    }
    case 1:
        if (a.spawnByte == 0) {
            setStoryFlag(kFlagTutorialDone); // exe 0x510e10
            return;
        }
        f.timer = 0x78;
        f.counter = 0;
        a.enterState(uint8_t(BlobState::Sink));
        return;
    default: return;
    }
}

// ---- the small form's sub-states [0x8956e8] -------------------------------------------------------------------------
void smallWander(AnimatedActor& a, BlobFields& f, BlobFightBlock& w) { // exe 0x4d89f0
    turn(f, w, uint16_t(w.wanderYaw));
    --f.timer;
    if (f.timer == 0) {
        f.timer = 0x3c;
        if (rngStep() & 1) {
            const uint32_t r = rngStep();
            w.wanderYaw = int16_t(uint16_t(uint16_t(w.wanderYaw) + ((r & 0x7ff) - 0x400u)));
            wrapBoth(w.wanderYaw);
        }
    }
    if (f.flags & kBlobAlerted) toSub(a, uint8_t(SmallSub::Flee));
}
void smallFlee(AnimatedActor& a, BlobFields& f, BlobFightBlock& w) { // exe 0x4d8ab0
    switch (a.step) {
    case 0:
        f.flags &= ~kBlobPulse;
        f.timer = 0x2d;
        a.scale.x = 0x100;
        a.scale.z = 0x100;
        ++a.step;
        f.pulse = 0;
        a.velocity.z = 0;
        a.scale.y = 0x1c0;
        return;
    case 1:
        turn(f, w, uint16_t(f.aimYaw));
        --f.timer;
        if (f.timer != 0) return;
        f.flags |= kBlobFleeing | kBlobPulse;
        ++a.step;
        a.velocity.z = 0x1a0;
        return;
    case 2: {
        int t = int(f.aimYaw) + 0x800;
        if (t >= 0x800) t -= 0x1000;
        turn(f, w, uint16_t(t));
        ++f.timer;
        if (f.pulse != 0 || f.aimDistance < 0x800 || f.timer <= 0x96) return;
        f.timer = 0;
        f.flags &= ~(kBlobAlerted | kBlobFleeing);
        w.wanderYaw = w.bodyYaw;
        a.velocity.z = 0x80;
        toSub(a, uint8_t(SmallSub::Wander));
        return;
    }
    default: return;
    }
}
void smallDie(AnimatedActor& a, BlobFields& f) { // exe 0x4d8bf0
    const uint8_t s = a.step;
    switch (s) {
    case 0:
        if (f.pulse == 0) {
            a.acceleration.z = -0x10;
            a.step = uint8_t(s + 1);
        }
        break;
    case 1:
        if (!(f.pulse & 7)) blastNear(a);
        if (f.pulse == 0) {
            f.flags = (f.flags & ~kBlobPulse) | kBlobHusk;
            ++a.step;
        }
        break;
    case 2:
        if (a.scale.x < 0x200) scaleBy(a, 0x420, 0x3e0);
        else a.step = uint8_t(s + 1);
        break;
    default: break;
    }
    if ((f.flags & kBlobHusk) && f.red > 0xb) {
        --f.red;
        --f.green;
        --f.blue;
    }
    if (a.velocity.z <= 0) {
        a.velocity.z = 0;
        a.acceleration.z = 0;
    }
}

// ---- the rise / sink sequences --------------------------------------------------------------------------------------
void riseSequence(AnimatedActor& a, BlobFields& f, BlobFightBlock& w) { // exe 0x4d8fb0 (jump table 0x4d9334)
    ++f.counter;
    const uint8_t s = a.step;
    switch (s) {
    case 0:
        --f.timer;
        if (f.timer == 0) {
            soundPlayAtPos(0x258, a.pos);
            a.step = uint8_t(s + 1);
            a.scale.y = 0x1c0;
        } else {
            a.scale.y = int16_t(((rngStep() >> 3) & 0x1e) + 0x1b0u);
        }
        a.scale.x = int16_t((rngStep() & 0xf) * 2 + 0xf0u);
        a.scale.z = int16_t(((rngStep() >> 7) & 0x1e) + 0xf0u);
        if (!(uint8_t(f.counter) & 7)) propWarm(a, f);
        break;
    case 1:
        if (a.scale.y < 0x7ba) {
            if (w.baseHeight < 0xf0) w.baseHeight += 2u;
            a.scale.y = int16_t((int32_t(a.scale.y) * 0x410) >> 10);
            a.scale.x = int16_t((rngStep() & 0xf) + 0xf8u);
            a.scale.z = int16_t(((rngStep() >> 8) & 0xf) + 0xf8u);
        } else {
            a.step = uint8_t(s + 1);
            f.timer = 0x1e;
            a.scale.x = 0x100;
            a.scale.z = 0x100;
        }
        break;
    case 2:
        --w.baseHeight;
        --f.timer;
        if (f.timer == 0) {
            a.step = uint8_t(s + 1);
            soundPlayAtPos(0x259, a.pos);
        }
        break;
    case 3:
        if (a.scale.x >= 0x800) {
            a.step = uint8_t(s + 1);
            break;
        }
        if (w.baseHeight > 0x80) w.baseHeight -= 8u;
        else w.baseHeight = 0x80;
        scaleBy(a, 0x480, 0x390);
        break;
    case 4:
        if (a.scale.x > 0x200) {
            scaleBy(a, 0x3f0, 0x40d);
        } else {
            f.timer = 0x2d;
            f.flags |= kBlobPulse;
            a.step = uint8_t(s + 1);
        }
        if (!(uint8_t(f.counter) & 3)) propCool(a, f, 0x11);
        break;
    case 5:
        --f.timer;
        if (f.timer == 0) {
            a.acceleration.z = 8;
            a.step = uint8_t(s + 1);
        }
        break;
    case 6:
        if (a.velocity.z > 0x80) {
            a.acceleration.z = 0;
            a.velocity.z = 0x80;
        }
        break;
    default: break;
    }
    if (f.counter >= 0xc8) a.pos.y.whole = -0x280;
}
void sinkSequence(AnimatedActor& a, BlobFields& f, BlobFightBlock& w) { // exe 0x4d9600 (jump table 0x4d985c)
    ++f.counter;
    const uint8_t s = a.step;
    const uint16_t t = uint16_t(f.counter);
    switch (s) {
    case 0:
        if (int16_t(t) < 0x14) return;
        a.step = uint8_t(s + 1);
        f.timer = int16_t((rngStep() & 3) + 2u);
        return;
    case 1:
        --f.timer;
        if (f.timer == 0) {
            sparkNear(a, 2);
            f.timer = int16_t(rngStep() % 7u + 2u);
        }
        if (a.scale.x < 0x5a0) {
            scaleBy(a, 0x406, 0x3fa);
        } else {
            f.timer = 0x1e;
            ++a.step;
        }
        return;
    case 2:
        --f.timer;
        if (f.timer == 0) a.step = uint8_t(s + 1);
        return;
    case 3:
        if (!(t & 3)) {
            soundPlayAtPos(0x25c, a.pos);
            sprayBlob(a, w);
        }
        if (a.scale.x > 0) {
            int v = int(a.scale.x) - 0x10;
            if (v <= 0) v = 0;
            a.scale.x = int16_t(v);
            a.scale.z = int16_t(v);
            return;
        }
        ++a.step;
        f.timer = 0x3c;
        return;
    case 4:
        --f.timer;
        if (f.timer != 0) return;
        a.velocity.z = 0x80;
        a.step = uint8_t(s + 1);
        w.bodyYaw = int16_t(classState<SteeredPropFields>(propOf(a, f)).targetYaw);
        f.timer = 0x10;
        return;
    case 5: {
        SteeredPropFields& p = classState<SteeredPropFields>(propOf(a, f));
        p.targetPitch += 0x20u;
        --f.timer;
        if (f.timer != 0) return;
        a.velocity.z = 0;
        a.step = uint8_t(s + 1);
        f.timer = 0x1e;
        return;
    }
    case 6: {
        --f.timer;
        if (f.timer != 0) return;
        StaticActor& p = propOf(a, f);
        p.flags &= uint8_t(~kActorDrawn);
        a.flags &= uint8_t(~kActorDrawn);
        a.step = uint8_t(s + 1);
        blastAt(a);
        return;
    }
    case 7:
        if (a.spawnByte != 0) a.enterState(uint8_t(BlobState::Free));
        return;
    default: return;
    }
}

// ---- states [0x8956b0] ----------------------------------------------------------------------------------------------
void init(AnimatedActor& a, BlobFields& f) { // exe 0x4d6b60 (jump table 0x4d6e64)
    if (!bindActorModel(a, a.spawnByte)) { // exe 0x5e0bc0(a, &+7)
        freeActor(a);                      // exe 0x5e1490
        return;
    }
    a.flags |= kActorScenery;
    a.clearMotion(); // exe 0x40dc40
    const uint32_t block0 = boneBlockAddress(a, 0);
    if (!block0) notPorted(kStoryPort, "0x4d6b60: exe 0x5e1780(a, 0) returned null");
    const uint32_t workAddress = block0 + 0x40;
    f.work.pointAt(workAddress);
    f.clearOwnBytesBytes();
    BlobFightBlock& w = *f.work;
    w = {};
    setBoneSemiTransparency(a, 0, 1, 3); // exe 0x5e1a20
    setBoneSemiTransparency(a, 1, 0, 1);
    a.clearContactWord();
    a.platform = nullptr;
    a.collisionBox.pointAt(TableId::BlobBossBlobBox);
    a.setParamsWord(0x101010);
    w.stats = animatedStatRow(a.type, a.spawnByte);
    w.shots = 0;
    w.baseHeight = 0x80;
    w.farTicks = 0;
    switch (BlobVariant(a.variant)) {
    case BlobVariant::Small: // exe 0x4d6c35
        a.velocity.z = 0x80;
        a.maxHitPoints = int16_t(uint32_t(w.statsRow().hitPoints) << 5);
        a.hitPoints = int16_t(uint32_t(w.statsRow().hitPoints) << 5);
        a.scale.y = 0x1c0;
        a.scale.x = 0x100;
        a.scale.z = 0x100;
        a.state = uint8_t(BlobState::Small);
        a.subState = uint8_t(SmallSub::Wander);
        a.restartStep();
        f.flags |= kBlobPulse;
        f.timer = 0x3c;
        w.bodyYaw = 0x400;
        w.wanderYaw = 0x400;
        break;
    case BlobVariant::Fight: // exe 0x4d6c95
        a.maxHitPoints = int16_t(uint32_t(w.statsRow().hitPoints) << 6);
        a.hitPoints = int16_t(uint32_t(w.statsRow().hitPoints) << 6);
        hudAddBossGauge(a, 1, 5, kBossGaugeColour); // exe 0x415d30
        a.scale.y = 0x280;
        a.scale.x = 0x200;
        a.scale.z = 0x200;
        a.state = uint8_t(BlobState::Fight);
        a.subState = uint8_t(FightSub::Wait);
        a.restartStep();
        f.flags |= kBlobPulse;
        f.timer = 1;
        w.bodyYaw = 0;
        if (a.spawnByte == 0) {
            w.messageTicks = 0x78;
            w.messageStep = 0;
            windowOpen(kMessageWindow, kMessageBank, 0x30); // exe 0x557390
        }
        break;
    case BlobVariant::Rise: // exe 0x4d6d20
        a.scale.y = 0x1c0;
        a.scale.x = 0x100;
        a.scale.z = 0x100;
        a.enterState(uint8_t(BlobState::Rise));
        a.setParamsWord(0x111010);
        f.timer = 0x78;
        f.counter = 0;
        w.bodyYaw = 0;
        break;
    case BlobVariant::Sink: // exe 0x4d6d60
        a.scale.y = 0x280;
        a.scale.x = 0x200;
        a.scale.z = 0x200;
        a.enterState(uint8_t(BlobState::Sink));
        f.timer = 0x78;
        f.counter = 0;
        w.bodyYaw = 0;
        break;
    default:
        freeActor(a); // exe 0x5e1490 (the exe then carries on below)
        break;
    }
    a.anim = 0;
    animSetPose(a, 0, 0); // exe 0x5e1c30
    const uint32_t propAddress = allocActor60(kActorStageClass); // exe 0x5e12f0
    f.prop = propAddress ? &staticActorAt(propAddress) : nullptr;
    if (propAddress) {
        StaticActor& p = staticActorAt(propAddress);
        p.flags = kActorLiveDrawn;
        p.type = kPropType;
        p.variant = 0;
        p.spawnByte = a.spawnByte;
        p.loopFlags |= uint8_t(kActorSweepGroup2 | kActorLateLoop);
        p.setParamsWord(0x101010);
        p.enterState(0);
        p.pos = a.pos;
        p.pos.y.whole -= 0x80u;
        p.rotation.x = 0;
        p.rotation.y = w.bodyYaw;
    }
    if (a.variant == uint8_t(BlobVariant::Sink)) PropTargetsWord(propOf(a, f)).word = 0;
}

void fight(const PlayerEnv& env, AnimatedActor& a, BlobFields& f) { // exe 0x4d6e80 (sub table 0x8956c8)
    BlobFightBlock& w = *f.work;
    const int16_t playerZ = aimHead(a, f, w, true);
    if (f.flags & kBlobPulseScale) {
        int sv, cv;
        if (!(f.flags & kBlobSecondPhase)) {
            const int i = int8_t(f.pulse);
            sv = sine64At(i) >> 6;
            cv = cosine64At(i) >> 5;
        } else {
            const int i = int(f.pulse & 0x1fu) * 2;
            sv = sine64At(i) >> 5;
            cv = cosine64At(i) >> 5;
        }
        a.scale.x = int16_t(sv + 0x200);
        a.scale.y = int16_t(cv + 0x200);
        a.scale.z = int16_t(sv + 0x200);
    }
    switch (FightSub(a.subState)) {
    case FightSub::Wait: fightWait(a, f, w); break;
    case FightSub::Chase: fightChase(a, f, w); break;
    case FightSub::Slam: fightSlam(a, f, w); break;
    case FightSub::Jump: fightJump(a, f, w); break;
    case FightSub::Shoot: fightShoot(a, f, w); break;
    case FightSub::SecondPhase: fightSecondPhase(a, f, w); break;
    case FightSub::Spin: fightSpin(a, f, w); break;
    case FightSub::Die: fightDie(a, f); break;
    default: notPortedAt(kStoryPort, "0x4d6e80: sub-state out of the table 0x8956c8", a.subState);
    }
    wrapUp(f.aimYaw);
    wrapBoth(a.rotation.y);
    if (f.flags & kBlobPulseRun) {
        if (f.pulse == 0) f.pulse = 0x40;
        const uint8_t pulse = f.pulse;
        if (!(f.flags & kBlobSecondPhase)) {
            if (pulse == 0x20) soundPlayAtPos(0x256, a.pos);
        } else if (pulse == 0x10 || pulse == 0x30) {
            soundPlayAtPos(0x257, a.pos);
        }
        --f.pulse;
    }
    moveBody(a, w);
    clampZ(a, f);
    if (playerZ <= 0x7e0 && playerZ >= -0x7e0) {
        w.farTicks = 0;
        w.ringRounds = 0;
        f.flags &= ~(kBlobPlayerFarZ | kBlobRingShots);
    } else if (f.flags & kBlobZClamped) {
        f.flags |= kBlobPlayerFarZ;
        ++w.farTicks;
        if (w.farTicks >= 0x5a) {
            w.farTicks = 0x5a;
            f.flags |= kBlobRingShots;
        }
    } else {
        f.flags &= ~kBlobPlayerFarZ;
    }
    if (int32_t(actorTestMove(env, a)) < 0) actorPushOut(env, a); // exe 0x40f570 / 0x410960
    animStepPose(a);                                               // exe 0x5e1e80
    if (!(f.flags & (kBlobSecondPhase | kBlobNoDamage))) fightDamage(a, f, w);
    propFollow(a, f, w);
    StaticActor& p = propOf(a, f);
    if (!(f.flags & kBlobSpinning)) {
        if (!(f.flags & kBlobPlayerFarZ)) PropTargetsWord(p).word = f.aimWord;
        else PropTargetsWord(p).word = uint32_t(int32_t(f.aimYaw) + 0x800);
    }
    if (f.flags & kBlobFlashProp) {
        f.flags &= ~kBlobFlashProp;
        SteeredPropParams(p).flash = 1;
    }
    if (f.flags & kBlobClutStart) {
        if (!(f.flags & kBlobClutRunning)) {
            f.flags = (f.flags & ~kBlobClutStart) | kBlobHealing | kBlobClutRunning;
            startClutEffect(a, f);
        } else {
            BlobClutView clut(effectActorAt(f.clutEffect));
            if (clut.progress == 0) {
                f.flags &= ~(kBlobClutRunning | kBlobClutStart);
                clut.state = kClutRestoreState;
            }
        }
    }
    if (f.flags & kBlobHealing) {
        ++a.hitPoints;
        if (a.hitPoints > a.maxHitPoints) a.hitPoints = a.maxHitPoints;
        drainFloor(env, player(), -0x300); // exe 0x572090
    }
    submitHits(a, f, w);
    if (a.spawnByte != 0) return;
    const uint8_t step = w.messageStep;
    auto say = [&w, step](int16_t id) {
        message(id);
        w.messageStep = uint8_t(step + 1);
    };
    switch (step) {
    case 0:
        --w.messageTicks;
        if (w.messageTicks == 0) say(0x31);
        return;
    case 1:
        if (a.subState == uint8_t(FightSub::Wait)) say(0x32);
        return;
    case 2:
        if (a.subState == uint8_t(FightSub::Jump)) say(0x33);
        return;
    case 3:
        if (f.flags & kBlobClutRunning) say(0x34);
        return;
    case 4:
        if (f.flags & kBlobShooting) say(0x35);
        else if (f.flags & kBlobSecondPhase) w.messageStep = uint8_t(step + 1);
        return;
    case 5:
        if (f.flags & kBlobSecondPhase) say(0x36);
        return;
    case 6:
        if (a.subState == uint8_t(FightSub::Spin) && (f.flags & kBlobSpinning)) {
            message(0x37);
            w.messageTicks = 0x82;
            w.messageStep = uint8_t(step + 1);
        }
        return;
    case 7:
        if (w.messageTicks != 0) --w.messageTicks;
        if (a.hitPoints <= int16_t(a.maxHitPoints >> 3) && w.messageTicks == 0) say(0x38);
        return;
    default: return;
    }
}

void freeAll(AnimatedActor& a, BlobFields& f) { // exe 0x4d7510
    setStoryFlag(kFlagBossGone); // exe 0x510e10
    freeActor(propOf(a, f));     // exe 0x5e1540(+0x14c)
    freeActor(a);                // exe 0x5e1490
}

void small(const PlayerEnv& env, AnimatedActor& a, BlobFields& f) { // exe 0x4d8550 (sub table 0x8956e8)
    BlobFightBlock& w = *f.work;
    aimHead(a, f, w, false);
    if (f.flags & kBlobPulseScale) {
        const int i = (f.flags & kBlobFleeing) ? int(f.pulse & 0x1fu) * 2 : int(int8_t(f.pulse));
        const int sv = sine64At(i) >> 7, cv = cosine64At(i) >> 6;
        a.scale.x = int16_t(sv + 0x100);
        a.scale.y = int16_t(cv + 0x180);
        a.scale.z = int16_t(sv + 0x100);
    }
    switch (SmallSub(a.subState)) {
    case SmallSub::Wander: smallWander(a, f, w); break;
    case SmallSub::Flee: smallFlee(a, f, w); break;
    case SmallSub::Die: smallDie(a, f); break;
    default: notPortedAt(kStoryPort, "0x4d8550: sub-state out of the table 0x8956e8", a.subState);
    }
    wrapUp(f.aimYaw);
    wrapBoth(a.rotation.y);
    if (f.flags & kBlobPulseRun) {
        if (f.pulse == 0) f.pulse = 0x40;
        --f.pulse;
    }
    moveBody(a, w);
    clampZ(a, f);
    bool bounced = false;
    if (int32_t(actorTestMove(env, a)) < 0) {   // exe 0x40f570
        const uint32_t r = actorPushOut(env, a); // exe 0x410960
        if (r & 0xf00) {
            bounced = true;
            int16_t pitch = 0, yaw = w.bodyYaw;
            reflectRotationOffWalls(pitch, yaw, r); // exe 0x410740 (on the exe's stack rotation {0, body yaw, 0})
            w.wanderYaw = yaw;
            w.bodyYaw = yaw;
        }
    }
    if ((f.flags & kBlobZClamped) && !bounced) {
        const int16_t v = int16_t(uint16_t(0xf800u - uint16_t(w.bodyYaw)) & 0xfff);
        w.wanderYaw = v;
        w.bodyYaw = v;
    }
    animStepPose(a); // exe 0x5e1e80
    if (a.subState != uint8_t(SmallSub::Die)) {
        smallDamage(a, f, w);
        if (f.flags & kBlobDead) {
            f.flags |= kBlobPulse;
            f.pulse = 0;
            toSub(a, uint8_t(SmallSub::Die));
        }
    }
    propFollow(a, f, w);
    StaticActor& p = propOf(a, f);
    SteeredPropParams colour(p);
    const uint32_t fl = f.flags;
    if (!(fl & kBlobHusk)) {
        const uint32_t hi = fl & (kBlobAlerted | kBlobFleeing);
        int32_t yaw;
        if (hi == 0) {
            yaw = w.bodyYaw;
        } else {
            yaw = f.aimYaw;
            if (fl & kBlobFleeing) {
                yaw += 0x800;
                if (yaw >= 0x800) yaw -= 0x1000;
            }
        }
        const uint32_t lo = uint32_t(yaw) & 0xffff;
        if (hi == 0) {
            const int i = int8_t(f.pulse);
            PropTargetsWord(p).word = lo | ((uint32_t(int32_t(cosine64At(i))) & ~0x3fu) << 10);
        } else {
            const int i = int(f.pulse & 0x1fu) * 2;
            PropTargetsWord(p).word = lo + uint32_t(int32_t(sine64At(i)) >> 4);
        }
    } else {
        PropTargetsWord(p).word = 0x2000000;
        if (colour.red > 8) {
            --colour.red;
            --colour.green;
            --colour.blue;
        }
    }
    if (f.flags & kBlobFlashProp) {
        f.flags &= ~kBlobFlashProp;
        colour.flash = 1;
    }
    submitHits(a, f, w);
}

// the scale pulse of the rise / the sink
void sequencePulse(AnimatedActor& a, BlobFields& f) {
    if (!(f.flags & kBlobPulseScale)) return;
    const int i = int8_t(f.pulse);
    const int sv = sine64At(i) >> 6, cv = cosine64At(i) >> 5;
    a.scale.x = int16_t(sv + 0x200);
    a.scale.y = int16_t(cv + 0x200);
    a.scale.z = int16_t(sv + 0x200);
}
void rise(AnimatedActor& a, BlobFields& f) { // exe 0x4d8ce0
    BlobFightBlock& w = *f.work;
    aimHead(a, f, w, false);
    sequencePulse(a, f);
    riseSequence(a, f, w);
    wrapUp(f.aimYaw);
    wrapBoth(a.rotation.y);
    if (f.flags & kBlobPulseRun) {
        if (f.pulse == 0) f.pulse = 0x40;
        if (f.pulse == 0x20) soundPlayAtPos(0x256, a.pos);
        --f.pulse;
    }
    moveBody(a, w);
    animStepPose(a); // exe 0x5e1e80
    propFollow(a, f, w);
    PropTargetsWord(propOf(a, f)).word = f.aimWord;
}
void sink(AnimatedActor& a, BlobFields& f) { // exe 0x4d9350
    BlobFightBlock& w = *f.work;
    aimHead(a, f, w, false);
    sequencePulse(a, f);
    sinkSequence(a, f, w);
    wrapUp(f.aimYaw);
    wrapBoth(a.rotation.y);
    if (f.flags & kBlobPulseRun) {
        if (f.pulse == 0) f.pulse = 0x40;
        --f.pulse;
    }
    moveBody(a, w);
    animStepPose(a); // exe 0x5e1e80
    propFollow(a, f, w);
}

void blobBoss(const PlayerEnv& env, AnimatedActor& a) { // exe 0x4d6b40: [0x8956b0 + state * 4](actor)
    BlobFields& f = classState<BlobFields>(a);
    switch (BlobState(a.state)) {
    case BlobState::Init: init(a, f); break;
    case BlobState::Fight: fight(env, a, f); break;
    case BlobState::Free: freeAll(a, f); break;
    case BlobState::Small: small(env, a, f); break;
    case BlobState::Rise: rise(a, f); break;
    case BlobState::Sink: sink(a, f); break;
    default: notPortedAt(kStoryPort, "0x4d6b40: state out of the table 0x8956b0", a.state);
    }
}

} // namespace

void blobBossUpdate(const PlayerEnv& env, uint32_t actor) { blobBoss(env, animatedActorAt(actor)); }

} // namespace od::port
