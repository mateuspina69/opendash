// The bomb-throwing Birdbots of Glyde's base (ST30, pool 0x20 type 0x2d sub 3; see glyde_base_bombers.hpp), transcribed
// from dash2.exe one function per exe function. States [0x892d1c]: 0 init 0x4968f0, 1 0x496a10 (the hurt test 0x497140,
// sub-states [0x892d2c]: 0 0x496b90 rise in, 1 0x496c40 take a bomb / aim / throw, 2 0x496f10 hurt, 3 0x496fc0 turn
// away and sink / reappear), 2 0x497080 (the wave counter - 1, freed). The timers and the reappearing place come from
// the CRT rand() (exe 0x611443).
#include "content/calbania/glyde_base_bombers.hpp"
#include "content/calbania/glyde_base_bombers_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/birdbot.hpp"
#include "game/bomb.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace glyde_base_bombers_state;

namespace {

constexpr const char* kBomberPort = "glyde base bomber port"; // the area of its port-gap errors

// +0x160
constexpr uint8_t kBomberThrown = 0x02;      // the bomb was thrown in this run
constexpr uint8_t kBomberNoHitEntry = 0x04;
constexpr uint8_t kBomberHitFlag = 0x08;     // an owner bit of its hit entry
constexpr uint8_t kBomberNoDamage = 0x20;
constexpr uint8_t kRisingBits = kBomberNoHitEntry | kBomberHitFlag | kBomberNoDamage;
constexpr uint8_t kSinkingBits = kBomberHitFlag | kBomberNoDamage;

enum class BomberState : uint8_t { Init = 0, Active = 1, Gone = 2 };
enum class BomberSub : uint8_t { RiseIn = 0, Throw = 1, Hurt = 2, Leave = 3 };
enum class RiseStep : uint8_t { Enter = 0, Wait = 1, Rise = 2 };
enum class ThrowStep : uint8_t { TakeBomb = 0, Lift = 1, Aim = 2, Throw = 3 };
enum class HurtStep : uint8_t { Enter = 0, Recoil = 1 };
enum class LeaveStep : uint8_t { Enter = 0, Sink = 1 };

// the places a variant reappears at (exe table 0x892d3c: {x, z, yaw})
struct ReappearPlace : DataRecord {
    int16_t x = 0;
    int16_t z = 0;
    int16_t yaw = 0;
    static ReappearPlace load(const TableReader& r) {
        ReappearPlace rec;
        rec.x = r.s16(0);
        rec.z = r.s16(2);
        rec.yaw = r.s16(4);
        return rec;
    }
};
const DataTable<RecordTable<ReappearPlace, 6, 6>> reappearPlaces{TableId::GlydeBaseBombersReappearPlaces};

// the key it drops (the stage's static 0x5b26d0) and where it is thrown from
constexpr uint8_t kKeyType = 0x42;
constexpr uint8_t kKeyFromBomber = 1; // the key's parameter
constexpr int16_t kKeyDropX = int16_t(0xf800), kKeyDropZ = 0xc00;
constexpr int16_t kKeyAimX = int16_t(0xff00), kKeyAimZ = 0x500; // the point its throw's distance is measured to
constexpr uint8_t kKnockedDownVariant = 1;                        // the knocked-down Birdbot's variant
constexpr uint8_t kKnockedDownWithKey = 3;                        // its parameter after a variant's
constexpr uint32_t kFlagKeyBomberDown = 0x21;
constexpr uint8_t kWaveNearlyBeaten = 2;
constexpr uint16_t kRiseSound = 0x193, kTakeSound = 0x18a, kVoiceA = 0x1a4, kVoiceB = 0x1a9, kHurtSound = 0x190;
constexpr uint8_t kAnimHold = 0x1c, kAnimLift = 0x1d, kAnimThrow = 0x21, kAnimHurt = 0x18;
constexpr uint32_t kDamageAttacks = 0xe0000;            // the attacks that count a hit without damage
constexpr uint32_t kKnockAttacks = 0x4400000;           // a strong push / a hit from far below: knocked at once
constexpr uint32_t kBoneLeftHand = 2, kBoneRightHand = 3;
// the parts hidden / shown at its init
constexpr size_t kShownPart = 2;
constexpr size_t kHiddenParts[6] = {6, 7, 8, 0, 4, 5};

void enterSub(AnimatedActor& actor, BomberSub s) {
    actor.subState = uint8_t(s);
    actor.restartStep();
}

// exe 0x4970a0: the bomb between the bones 2 and 3, facing the bomber
void holdBomb(AnimatedActor& actor, GlydeBomberFields& f) {
    if (!f.bomb) return;
    StaticActor& b = *f.bomb;
    b.rotation.y = actor.rotation.y;
    int16_t v[3] = {0, 0xa, 0};
    int16_t p2[3], p3[3];
    bonePointOffsetShorts(actor, kBoneLeftHand, v, p2); // exe 0x5e1970 shifts v in place: the second call sees y 0x50
    v[0] = -10;
    bonePointOffsetShorts(actor, kBoneRightHand, v, p3);
    b.pos.x.whole = int16_t((int(p3[0]) + int(p2[0])) >> 1);
    b.pos.y.whole = int16_t((int(p3[1]) + int(p2[1])) >> 1);
    b.pos.z.whole = int16_t((int(p3[2]) + int(p2[2])) >> 1);
}
// the held bomb let go, falling (rotated as the bomber)
void dropBomb(AnimatedActor& actor, StaticActor& b) {
    BombCarrierParams(b).carrierCommand = kCarrierLetGo;
    b.rotation.y = actor.rotation.y;
    b.velocity.y = int16_t(0xff00);
    b.velocity.z = int16_t(0xff00);
}
// exe 0x497140: 1 = knocked back (its 4th hit or a strong one), 2 = shot down
int damage(AnimatedActor& actor, GlydeBomberFields& f) {
    const uint32_t v = f.hitBox.result;
    if (f.bits & kBomberNoDamage) return 0;
    const uint32_t e = v & kHitDamageMask;
    int r = 0;
    if (e == 0 && !(v & kDamageAttacks)) return 0;
    --f.hitsLeft;
    if (f.hitsLeft == 0 || (v & kKnockAttacks)) r = 1;
    actor.tint = kTintFlash;
    if (hitPointsAdd(actor, -int(e)) != 0) {
        r = 2;
        countDefeat(); // exe 0x5e53e0
    }
    return r;
}
// 0x496a10 with 0x497140 == 2: the bomb let go, a knocked-down Birdbot in its place; a variant's: flag 0x21 and the
// key thrown from the gate's side toward the bomber
void shotDown(AnimatedActor& actor, GlydeBomberFields& f) {
    if (f.bomb) dropBomb(actor, *f.bomb);
    if (const uint32_t address = allocActor20()) { // exe 0x5e1160
        AnimatedActor& r = animatedActorAt(address);
        r.flags = kActorLive;
        r.type = kBirdbotType;
        r.subtype = 0;
        r.variant = kKnockedDownVariant;
        r.pos = actor.pos;
        r.rotation = actor.rotation;
        r.velocity.z = int16_t(0xfec0);
        r.velocity.x = 0;
        r.velocity.y = int16_t(0xfec0);
        r.params[0] = 0;
        if (actor.variant != 0) r.params[0] = kKnockedDownWithKey;
    }
    if (actor.variant != 0) {
        setStoryFlag(kFlagKeyBomberDown);                               // exe 0x510e10
        if (const uint32_t address = allocActor60(kActorStageClass)) { // exe 0x5e12f0
            StaticActor& s = staticActorAt(address);
            s.flags = kActorLive;
            s.type = kKeyType;
            s.subtype = 0;
            s.variant = 0;
            s.loopFlags |= kActorSweepGroup2;
            s.pos.x.whole = kKeyDropX;
            s.pos.y.whole = actor.pos.y.whole;
            s.pos.z.whole = kKeyDropZ;
            s.params[0] = kKeyFromBomber;
            // exe 0x5e3b00({0xff00, y, 0x500}, position): the x / z distance of the integer parts (16-bit differences)
            const int dx = int16_t(actor.pos.x.whole - kKeyAimX), dz = int16_t(actor.pos.z.whole - kKeyAimZ);
            const int32_t d = isqrt(int32_t(uint32_t(dx * dx) + uint32_t(dz * dz)));
            int32_t q = int32_t((int64_t(d) * 0x55555555) >> 32) - d; // imul / sub / sar 1 / round: -d / 3
            q >>= 1;
            q += int32_t(uint32_t(q) >> 31);
            s.velocity.x = 0;
            s.velocity.y = int16_t(0xff00);
            s.velocity.z = int16_t(q * 2);
            s.rotation.x = 0;
            s.rotation.y = 0xe00;
            s.rotation.z = 0;
        }
    }
    actor.enterState(uint8_t(BomberState::Gone));
}

// ---- sub-states ---------------------------------------------------------------------------------------------------
// exe 0x496b90: waits 0xf..0x1e ticks, then rises in for 5 (sound 0x193)
void riseIn(AnimatedActor& actor, GlydeBomberFields& f) {
    switch (RiseStep(actor.step)) {
    case RiseStep::Enter: {
        f.bits = kRisingBits;
        ++actor.step;
        const uint32_t r = crtRand(); // exe 0x611443
        f.hitsLeft = 4;
        f.runsLeft = 3;
        f.timer = uint8_t((r & 0xf) + 0xf);
        [[fallthrough]];
    }
    case RiseStep::Wait:
        --f.timer;
        if (f.timer == 0) {
            ++actor.step;
            f.timer = 5;
            soundPlayAtPos(kRiseSound, actor.pos);
        }
        return;
    case RiseStep::Rise:
        --f.timer;
        if (f.timer == 0) enterSub(actor, BomberSub::Throw);
        integrateVelocity(actor, 0, 0, -0xc0); // exe 0x5e3690
        return;
    default: return;
    }
}
void leave(AnimatedActor& actor, GlydeBomberFields& f);
// exe 0x496c40: takes a bomb, aims at the player (within 0x100 of its spawn yaw), throws it within reach
void throwBomb(AnimatedActor& actor, GlydeBomberFields& f) {
    const uint8_t var = actor.variant;
    switch (ThrowStep(actor.step)) {
    case ThrowStep::TakeBomb: {
        if (var == 0 && encounterState().waveEnemiesWord() <= kWaveNearlyBeaten) { // the wave nearly beaten: leave
            actor.subState = uint8_t(BomberSub::Leave);
            leave(actor, f);
            return;
        }
        f.bits = 0;
        actor.anim = kAnimHold;
        const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (address == 0) {
            f.bomb = nullptr;
            return;
        }
        actor.enterStep(uint8_t(ThrowStep::Lift));
        StaticActor& s = staticActorAt(address);
        f.bomb = &s;
        s.flags = kActorLive;
        s.type = kBombStageType;
        s.subtype = 0;
        s.variant = kBombVariantHeld;
        s.loopFlags |= kActorSweepGroup2;
        holdBomb(actor, f);
        soundPlayAtPos(kTakeSound, actor.pos);
        soundPlayAtPos((crtRand() & 1) ? kVoiceA : kVoiceB, actor.pos);
        return;
    }
    case ThrowStep::Lift: {
        holdBomb(actor, f);
        if (!actor.animEnded()) return;
        actor.enterStep(uint8_t(ThrowStep::Aim));
        actor.anim = kAnimLift;
        int ang = angleOf(player().pos, actor.pos); // exe 0x5e3bc0
        const int d = angleDiff(uint32_t(int32_t(f.spawnYaw)), uint32_t(ang)); // exe 0x5e3d80
        if (d < 0) {
            if (d < -0x100) ang += -0x100 - d;
        } else if (d > 0x100) {
            ang += 0x100 - d;
        }
        f.aimYaw = int16_t(ang);
        if (var == 0) f.timer = uint8_t((crtRand() & 0x1f) + 0x1e);
        else f.timer = uint8_t((crtRand() & 3) + 4);
        return;
    }
    case ThrowStep::Aim: {
        holdBomb(actor, f);
        if (turnYawTo(actor.rotation.y, uint32_t(int32_t(f.aimYaw)), var != 0 ? 0x40 : 0x20) != 0) return; // exe 0x5e3df0
        --f.timer;
        if (f.timer != 0) return;
        const int reach = var != 0 ? 0xc00 : 0xa00;
        const int d = planarDistance(player().pos, actor.pos); // exe 0x5e3b00
        if (d > reach) {
            actor.enterStep(uint8_t(ThrowStep::Aim));
            f.timer = 1;
            return;
        }
        actor.enterStep(uint8_t(ThrowStep::Throw));
        actor.anim = kAnimThrow;
        f.throwDistance = int16_t(d);
        f.timer = 0x12;
        f.bits |= kBomberThrown;
        return;
    }
    case ThrowStep::Throw:
        if (actor.stepCounter == 0) {
            holdBomb(actor, f);
            --f.timer;
            if (f.timer != 0) return;
            actor.stepCounter = 1;
            if (f.bomb) { // thrown: the distance gives the speed
                StaticActor& b = *f.bomb;
                BombCarrierParams(b).carrierCommand = kCarrierLetGo;
                b.rotation.y = actor.rotation.y;
                b.velocity.y = 0;
                const int32_t dist = f.throwDistance;
                int32_t q = int32_t((int64_t(dist) * 0x77777777) >> 32) - dist;
                q >>= 4;
                q += int32_t(uint32_t(q) >> 31);
                b.velocity.z = int16_t(uint32_t(q) << 4);
            }
            f.bomb = nullptr;
            return;
        }
        if (!actor.animEnded()) return;
        if (var != 0) {
            --f.runsLeft;
            if (f.runsLeft > 0) {
                enterSub(actor, BomberSub::Leave);
                return;
            }
        }
        enterSub(actor, BomberSub::Throw);
        return;
    default: return;
    }
}
// exe 0x496f10: knocked back (anim 0x18, sound 0x190); a thrown bomb let go, one still held freed
void hurtRecoil(AnimatedActor& actor, GlydeBomberFields& f) {
    const HurtStep s = HurtStep(actor.step);
    if (s == HurtStep::Enter) {
        actor.anim = kAnimHurt;
        actor.enterStep(uint8_t(HurtStep::Recoil));
        if (f.bomb) {
            StaticActor& b = *f.bomb;
            if (f.bits & kBomberThrown) dropBomb(actor, b);
            else freeActor(b); // exe 0x5e1540
            f.bomb = nullptr;
        }
        f.bits = kBomberNoDamage;
        soundPlayAtPos(kHurtSound, actor.pos);
    } else if (s == HurtStep::Recoil) {
        if (actor.animEnded()) enterSub(actor, actor.variant != 0 ? BomberSub::Leave : BomberSub::Throw);
    }
}
// exe 0x496fc0: turns away (yaw ^ 0x800) and sinks for 5 ticks; variant 0 is gone, the others reappear at one of six
// places
void leave(AnimatedActor& actor, GlydeBomberFields& f) {
    const LeaveStep s = LeaveStep(actor.step);
    if (s == LeaveStep::Enter) {
        f.bits = kSinkingBits;
        actor.step = uint8_t(uint8_t(s) + 1u);
        actor.rotation.y = int16_t(uint16_t(actor.rotation.y) ^ uint16_t(kHalfTurn));
        actor.anim = kAnimHold;
        f.timer = 5;
    } else if (s != LeaveStep::Sink) {
        return;
    }
    --f.timer;
    if (f.timer != 0) {
        integrateVelocity(actor, 0, 0, -0xc0);
        return;
    }
    if (actor.variant == 0) {
        actor.enterState(uint8_t(BomberState::Gone));
        return;
    }
    enterSub(actor, BomberSub::RiseIn);
    const ReappearPlace place = reappearPlaces[size_t(int32_t(crtRand()) % 6)];
    actor.pos.x.whole = place.x;
    actor.pos.z.whole = place.z;
    actor.rotation.y = place.yaw;
    integrateVelocity(actor, 0, 0, 0x8c0);
}
// exe 0x496a10
void active(AnimatedActor& actor, GlydeBomberFields& f) {
    const int r = damage(actor, f);
    if (r == 2) {
        shotDown(actor, f);
        return;
    }
    if (r == 1) enterSub(actor, BomberSub::Hurt);
    switch (BomberSub(actor.subState)) { // [0x892d2c]
    case BomberSub::RiseIn: riseIn(actor, f); break;
    case BomberSub::Throw: throwBomb(actor, f); break;
    case BomberSub::Hurt: hurtRecoil(actor, f); break;
    case BomberSub::Leave: leave(actor, f); break;
    default: notPortedAt(kBomberPort, "class 0x4967d0 sub-state", actor.subState);
    }
}
// exe 0x4968f0
void init(AnimatedActor& actor, GlydeBomberFields& f) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.setDrawn(true);
    actor.anim = kAnimHold;
    actor.prevAnim = 0xff;
    const uint16_t hp = hasAnimatedStats(actor.type) ? animatedStats(actor.type, actor.subtype).hitPoints : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::GlydeBaseBombersBomberBox);
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock();
    f.spawnYaw = actor.rotation.y;
    actor.pos.y.whole = int16_t(actor.pos.y.whole - 0x10);
    actor.parts[kShownPart].flags &= uint8_t(~kPartHidden);
    for (size_t k = 0; k < 3; ++k) actor.parts[kHiddenParts[k]].flags |= kPartHidden;
    actor.enterState(uint8_t(BomberState::Active));
    integrateVelocity(actor, 0, 0, 0x8c0);
    for (size_t k = 3; k < 6; ++k) actor.parts[kHiddenParts[k]].flags |= kPartHidden;
    active(actor, f);
}

// exe 0x4967d0's body
void bomber(AnimatedActor& actor) {
    GlydeBomberFields& f = classState<GlydeBomberFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (BomberState(actor.state)) { // [0x892d1c]
    case BomberState::Init: init(actor, f); break;
    case BomberState::Active: active(actor, f); break;
    case BomberState::Gone: // exe 0x497080
        encounterState().setWaveEnemiesWord(uint16_t(encounterState().waveEnemiesWord() - 1u));
        freeActor(actor);
        break;
    default: notPortedAt(kBomberPort, "class 0x4967d0 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) {
        animStep(actor); // exe 0x5e1e80
        animPose(actor);
    } else {
        animSet(actor, actor.anim, 0); // exe 0x5e1c30
        animPose(actor);
    }
    f.hitBox.result = 0;
    f.hitBox.previousX = actor.prevPos.x.whole;
    f.hitBox.previousZ = actor.prevPos.z.whole;
    const AnimatedActorStats stats = animatedStats(actor.type);
    const uint8_t b = f.bits;
    if (b & kBomberNoHitEntry) return;
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(actor.pos.y.whole - 0x40), actor.pos.z.whole};
    const uint32_t shape = ((((uint32_t(actor.slot) << 8) | (b & kBomberHitFlag)) << 7 | stats.hitHeight) << 9) | 0x404007u;
    hitListSubmit(pos, f.hitBox, shape, kHitAttackSolid); // exe 0x5e4120
}

} // namespace

void glydeBaseBomberUpdate(const PlayerEnv&, uint32_t actor) { bomber(animatedActorAt(actor)); }

} // namespace od::port
