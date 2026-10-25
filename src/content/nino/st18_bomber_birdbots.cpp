// The ST18 bomber Birdbot, class 0x495af0 (see st18_bomber_birdbots.hpp). States [0x892cd8]: 0 init 0x495cd0, 1
// 0x495e10 (sub-states [0x892ce4]: 0 0x495e50 dropped in, 1 0x496040 take the bomb, 2 0x4960c0 walk in, 3 0x496170
// throw, 4 0x496290 spin and hop off, 5 0x4963d0 fall, 6 0x52ef00 nothing), 2 0x4964b0 shot down. Its walls / ground
// step is the Birdbots' (game/birdbot, exe 0x496580 = 0x4958f0); its airborne arc clamps the fall speed. The shadow's
// entry (exe 0x5f20f0) is render output, its ground probe is kept.
#include "content/nino/st18_bomber_birdbots.hpp"
#include "content/nino/st18_bomber_birdbots_state.hpp"

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
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/shadows.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace st18_bomber_birdbots_state;

namespace {

constexpr const char* kBomberPort = "st18 bomber port"; // the area of its port-gap errors

// +0x15e (besides kBirdbotBounces)
constexpr uint8_t kBomberHoldsBomb = 0x02, kBomberNoHitEntry = 0x04, kBomberNoFall = 0x08, kBomberThrown = 0x10;
constexpr uint8_t kBomberNoDamage = 0x20, kBomberStays = 0x40, kBomberNoLockOn = 0x80;

enum class BomberState : uint8_t { Init = 0, Main = 1, Down = 2 };
enum class BomberSub : uint8_t { DropIn = 0, TakeBomb = 1, WalkIn = 2, Throw = 3, SpinOff = 4, Fall = 5, Gone = 6 };

const DataTable<CollisionBoxRecord> bomberBox{TableId::St18BomberBirdbotsBox};
const DataTable<InteractionBoxRecord> bomberLiftBox{TableId::St18BomberBirdbotsLiftBox};
const DataTable<ScalarTable<int16_t, 3>> bombHold{TableId::St18BomberBirdbotsBombHold}; // the bomb's offset from the bones 2 and 3
constexpr uint8_t kStatsRow = 2;               // its 0x10-byte row of the Birdbots' stats
constexpr int kWalkInRange = 0x480;
constexpr int16_t kMaxFall = 0x800;
constexpr uint16_t kSoundHit = 0x8d, kSoundHitHard = 0x8e, kSoundDrop = 0x193;

void toSub(AnimatedActor& a, BomberSub s) {
    a.subState = uint8_t(s);
    a.enterStep(0);
}
AnimatedActorStats statsOf(const AnimatedActor& a) { return animatedStats(a.type, kStatsRow); }
int centreDistance(const AnimatedActor& a) { // exe 0x5e3b00(position, {0, *, 0})
    const int32_t centre[3] = {0, 0, 0};
    return planarDistance(a.pos, centre);
}
// exe 0x496440: game/birdbot birdbotArc with the fall speed clamped to kMaxFall
int arc(AnimatedActor& a, BomberFields& f, int yaw) {
    if (f.contact & (kBirdbotLanded | kBirdbotOnPlatform)) {
        a.velocity.z = int16_t(a.velocity.z >> 2);
        a.velocity.y = int16_t(uint32_t(-int(a.velocity.y)) >> 1);
        a.rotation.x = 0;
        return 1;
    }
    moveAlongYaw(a.pos, yaw, 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
    a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    if (a.velocity.y > kMaxFall) a.velocity.y = kMaxFall;
    return 0;
}
// exe 0x496670(actor, bomb, 0x892cd0): the bomb between the bones 2 and 3, facing the bomber
void holdBomb(const AnimatedActor& a, StaticActor& b) {
    const int16_t v[3] = {bombHold[0], bombHold[1], bombHold[2]};
    int16_t p2[3], p3[3];
    bonePointOffset(a, 2, v, p2); // exe 0x5e1970
    bonePointOffset(a, 3, v, p3);
    b.savePreviousPose();
    b.pos.x.whole = int16_t((int(p3[0]) + int(p2[0])) >> 1);
    b.pos.y.whole = int16_t((int(p3[1]) + int(p2[1])) >> 1);
    b.pos.z.whole = int16_t((int(p3[2]) + int(p2[2])) >> 1);
    b.rotation.y = a.rotation.y;
}
// exe 0x496730: 1 = knocked (a strong hit, on the ground), 2 = shot down
int damage(AnimatedActor& a, BomberFields& f) {
    const uint32_t v = f.hitBox.result;
    if (f.bits & kBomberNoDamage) return 0;
    f.hitKind = 0;
    int r = 0;
    if (v & kHitDamageMask) {
        uint16_t snd = kSoundHit;
        if (a.airborne == 0) {
            r = (v & (kHitLevelWithin | kHitAttackThrown | kHitAttackWater)) != 0;
            if (r) f.hitKind = 1;
            if (v & (kHitLevelBelow | kHitAttackStrongPush)) {
                r = 1;
                f.hitKind = 2;
                snd = kSoundHitHard;
            }
        }
        a.tint = kTintFlash;
        soundPlayAtPos(snd, a.pos);
        if (hitPointsAdd(a, -int(v & kHitDamageMask)) != 0) r = 2;
    }
    return r;
}
// exe 0x4964b0: a knocked-down Birdbot in its place (its encounter id handed over, its bomb set off), freed
void shotDown(AnimatedActor& a, BomberFields& f) {
    if (a.subState == 0) {
        countDefeat(); // exe 0x5e53e0
        const uint32_t address = allocActor20(); // exe 0x5e1160
        if (!address)
            notPorted(kBomberPort,
                      "class 0x495af0: no free pool-0x20 slot for the knocked-down Birdbot (the exe reads null + 2)");
        AnimatedActor& r = animatedActorAt(address);
        r.flags = kActorLive;
        r.type = kBirdbotType;
        r.subtype = 0;
        r.variant = 0;
        r.params[0] = 0;
        r.pos = a.pos;
        r.rotation.y = int16_t((rngStep() & 0xf) << 8);
        if (f.trackId != 0) {
            encounterUntrack(a);                                         // exe 0x5e6540
            encounterTrackInScriptSlot(r, uint16_t(int16_t(f.trackId))); // exe 0x5e6500
        }
        if ((f.bits & kBomberHoldsBomb) && f.bomb) BombCarrierParams(staticActorAt(f.bomb->at)).carrierCommand = kCarrierBlowUp;
        if (r.slot <= a.slot) { // the new actor already ran this loop: freed next tick
            a.subState = 1;
            return;
        }
    }
    freeActor(a); // exe 0x5e1490
}

// ---- sub-states ---------------------------------------------------------------------------------------------------
void dropIn(AnimatedActor& a, BomberFields& f) { // exe 0x495e50: dropped in, drifting to the circle around the centre
    switch (a.step) {
    case 0:
        a.anim = 0xd;
        a.acceleration.y = 0x18;
        a.velocity.y = 0x100;
        f.bits = uint8_t(kBomberStays | kBomberNoFall | kBirdbotBounces);
        a.airborne = 1;
        soundPlayAtPos(kSoundDrop, a.pos);
        a.enterStep(1);
        return;
    case 1:
        if (a.animEnded()) {
            a.anim = 0xe;
            a.enterStep(2);
        }
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        return;
    case 2: {
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        if (f.groundHeight - 0x2e < int(a.pos.y.whole)) {
            a.anim = 0xf;
            a.enterStep(3);
        }
        // toward the point of its distance from the centre straight behind it (its integer parts)
        const int d = centreDistance(a);
        int16_t dx = 0, dz = 0;
        angleVector(int16_t(uint16_t(a.rotation.y) + uint16_t(kHalfTurn)), d, dx, dz); // exe 0x5e40c0
        const int32_t lx = int32_t(uint32_t(int32_t(dx)) << 16), lz = int32_t(uint32_t(int32_t(dz)) << 16);
        const int ddx = wrap16(int16_t(uint32_t(lx) >> 16) - a.pos.x.whole);
        const int ddz = wrap16(int16_t(uint32_t(lz) >> 16) - a.pos.z.whole);
        if (isqrt(int32_t(uint32_t(ddx * ddx) + uint32_t(ddz * ddz))) != 0) // exe 0x5e3b00(position, point)
            moveAlongYaw(a.pos, angleOf(lx, lz, a.pos.x, a.pos.z), 0, 0, -0x10);
        return;
    }
    case 3:
        a.anim = 0xf;
        f.timer = 0xf;
        a.enterStep(4);
        [[fallthrough]];
    case 4:
        a.velocity.y = int16_t(uint16_t(a.velocity.y) - 0x80u);
        if (a.velocity.y < -0x40) a.velocity.y = -0x40;
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        if (a.velocity.y < 1) {
            --f.timer;
            if (f.timer == 0) a.enterStep(5);
        }
        return;
    case 5:
        a.acceleration.y = 0x40;
        a.enterStep(6);
        [[fallthrough]];
    case 6:
        if (arc(a, f, a.rotation.y) != 0) {
            toSub(a, BomberSub::TakeBomb);
            a.airborne = 0;
        }
        return;
    default: return;
    }
}
void takeBomb(AnimatedActor& a, BomberFields& f) { // exe 0x496040: a held bomb (game/bomb)
    if (a.step == 0) {
        f.bits = kBomberStays;
        a.anim = 0x1c;
        f.bomb = nullptr;
        if (const uint32_t address = allocActor60(kActorStageClass)) { // exe 0x5e12f0
            StaticActor& s = staticActorAt(address);
            s.flags = kActorLive;
            s.type = kBombStageType;
            s.subtype = 0;
            s.variant = kBombVariantHeld;
            BombCarrierParams(s).carrierCommand = 0;
            f.bomb = &s;
            f.bits |= kBomberHoldsBomb;
        }
        a.enterStep(1);
    } else if (a.animEnded()) {
        toSub(a, BomberSub::WalkIn);
    }
    if (f.bomb) holdBomb(a, *f.bomb);
}
void walkIn(AnimatedActor& a, BomberFields& f) { // exe 0x4960c0: until within kWalkInRange of the centre
    const uint8_t s = a.step;
    if (s == 0) {
        f.bits = uint8_t(kBomberStays | kBomberHoldsBomb);
        a.anim = 0x1e;
        a.enterStep(1);
    } else if (s == 1) {
        if (a.animEnded()) {
            a.anim = 0x1f;
            a.enterStep(2);
        }
    } else if (s == 2) {
        integrateVelocity(a, 0, 0, -0x30);
        if (centreDistance(a) < kWalkInRange) toSub(a, BomberSub::Throw);
    }
    if (f.bomb) holdBomb(a, *f.bomb);
}
// exe 0x496170: the throw (anims 0x20 / 0x21; the bomb let go at frame 0x11). `bomb`: the bomb at the state's start
void throwBomb(AnimatedActor& a, BomberFields& f, StaticActor* bomb) {
    switch (a.step) {
    case 0:
        f.bits = uint8_t(kBomberStays | kBomberHoldsBomb);
        a.anim = 0x20;
        a.enterStep(1);
        return;
    case 1:
        if (a.animEnded()) {
            f.timer = 3;
            a.enterStep(2);
        }
        return;
    case 2:
        --f.timer;
        if (f.timer == 0) {
            a.anim = 0x21;
            a.enterStep(3);
            soundPlayAtPos((rngStep() & 1) ? 0x1a4 : 0x1a9, a.pos);
        }
        return;
    case 3:
        if (bomb) holdBomb(a, *bomb);
        if (a.animKey.frame == 0x11) {
            if (bomb) {
                BombCarrierParams(staticActorAt(bomb->at)).carrierCommand = kCarrierLetGo;
                bomb->velocity.y = -0x300;
                bomb->velocity.z = -0x200;
            }
            f.bits &= uint8_t(~kBomberHoldsBomb);
            a.enterStep(4);
        }
        [[fallthrough]];
    case 4:
        if (a.animEnded()) toSub(a, BomberSub::SpinOff);
        return;
    default: return;
    }
}
void spinOff(AnimatedActor& a, BomberFields& f) { // exe 0x496290: spins 0x20 ticks, then hops off (anim 0xf)
    switch (a.step) {
    case 0:
        f.bits = kBomberStays;
        a.anim = 0x14;
        a.spin.y = 0x40;
        if (rngStep() & 1) a.spin.y = int16_t(-int(a.spin.y));
        f.timer = 0x20;
        a.enterStep(1);
        [[fallthrough]];
    case 1:
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + uint16_t(a.spin.y));
        --f.timer;
        if (f.timer == 0) {
            a.enterStep(2);
            soundPlayAtPos((rngStep() & 1) ? 0x1a5 : 0x1aa, a.pos);
        }
        return;
    case 2:
        f.bits = uint8_t(kBomberNoLockOn | kBomberNoFall | kBirdbotBounces);
        a.anim = 0xf;
        a.acceleration.y = 0x18;
        a.velocity.y = -0x100;
        a.velocity.z = -0x140;
        a.airborne = 1;
        a.enterStep(3);
        [[fallthrough]];
    case 3:
        if (arc(a, f, a.rotation.y) != 0) { // a hop on from what it lands on
            a.velocity.z = -0x140;
            moveAlongYaw(a.pos, a.rotation.y, 0, a.velocity.y, -0x140); // exe 0x5e36f0
            a.velocity.y = -0x100;
            a.airborne = 1;
        }
        return;
    default: return;
    }
}
void fall(AnimatedActor& a, BomberFields& f) { // exe 0x4963d0: (anims 0xf / 0x10)
    const uint8_t s = a.step;
    if (s == 0) {
        a.anim = 0xf;
        a.acceleration.y = 0x18;
        f.bits = uint8_t(kBomberNoLockOn | kBomberStays | kBomberNoFall | kBirdbotBounces);
        a.enterStep(1);
    } else if (s != 1) {
        if (s == 2 && a.animEnded()) toSub(a, BomberSub::Gone);
        return;
    }
    if (arc(a, f, a.rotation.y) != 0) {
        a.anim = 0x10;
        a.enterStep(2);
        a.airborne = 0;
    }
}

// ---- states -------------------------------------------------------------------------------------------------------
void mainState(const PlayerEnv& env, AnimatedActor& a, BomberFields& f) { // exe 0x495e10
    if (damage(a, f) == 2) {
        a.enterState(uint8_t(BomberState::Down));
        shotDown(a, f);
        return;
    }
    StaticActor* const bomb = f.bomb.get();
    switch (BomberSub(a.subState)) { // [0x892ce4]
    case BomberSub::DropIn: dropIn(a, f); break;
    case BomberSub::TakeBomb: takeBomb(a, f); break;
    case BomberSub::WalkIn: walkIn(a, f); break;
    case BomberSub::Throw: throwBomb(a, f, bomb); break;
    case BomberSub::SpinOff: spinOff(a, f); break;
    case BomberSub::Fall: fall(a, f); break;
    case BomberSub::Gone: break; // exe 0x52ef00
    default: notPortedAt(kBomberPort, "class 0x495af0 sub-state", a.subState);
    }
    birdbotCollide(env, a, f, f.groundHeight); // exe 0x496580
}
void init(AnimatedActor& a, BomberFields& f) { // exe 0x495cd0
    if (!bindActorModel(a, 0)) {
        freeActor(a);
        return;
    }
    a.flags |= kActorDrawn;
    a.anim = 0;
    a.prevAnim = 0xff;
    const uint16_t hp = hasAnimatedStats(a.type) ? statsOf(a).hitPoints : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.velocity.setXY(0, 0);
    a.velocity.z = 0;
    a.spin.setXY(0, 0);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.collisionBox.pointAt(bomberBox.stateAddress());
    a.interactionBox.pointAt(bomberLiftBox.stateAddress());
    a.interaction = kInteractLift;
    a.airborne = 0;
    a.onPlatform = 0;
    a.actorHit = 0;
    a.wallHit = 0;
    a.clearClassBlock();
    f.bits |= kBomberStays;
    a.enterState(uint8_t(BomberState::Main));
    if (a.variant != 0) a.subState = uint8_t(BomberSub::TakeBomb);
    f.unused160 = 0;
    // its part set: the parts 2 and 1 shown, 6 / 7 / 8 and 9 / 10 / 11 hidden
    auto hide = [&](size_t part, bool hidden) {
        auto&& p = a.parts[part];
        p.flags = hidden ? uint8_t(p.flags | kPartHidden) : uint8_t(p.flags & ~kPartHidden);
    };
    hide(2, false);
    hide(6, true);
    hide(7, true);
    hide(8, true);
    hide(1, false);
    hide(9, true);
    hide(10, true);
    hide(11, true);
}

void bomber(const PlayerEnv& env, AnimatedActor& a) { // exe 0x495af0
    BomberFields& f = classState<BomberFields>(a);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.wasAirborne = a.airborne;
    a.tint = a.hitPoints < 0 ? kTintSpent : kTintNeutral;
    switch (BomberState(a.state)) { // [0x892cd8]
    case BomberState::Init: init(a, f); break;
    case BomberState::Main: mainState(env, a, f); break;
    case BomberState::Down: shotDown(a, f); break;
    default: notPortedAt(kBomberPort, "class 0x495af0 state", a.state);
    }
    a.wasOnPlatform = a.onPlatform;
    if (!(a.flags & kActorLive)) return;
    const uint8_t b = f.bits;
    if ((gameState().frame & 1) && f.oddTicks != 0) --f.oddTicks;
    if (!(b & kBomberStays) && (pointVisibleOrNear(a.pos, 0x200) != 0 || -0x600 < int(a.pos.y.whole))) {
        freeActor(a);
        return;
    }
    const AnimatedActorStats row = statsOf(a);
    if (a.anim == a.prevAnim) {
        animStepPose(a); // exe 0x5e1e80
    } else {
        animSetPose(a, a.anim, f.animKeyStart); // exe 0x5e1c30
        f.animKeyStart = 0;
    }
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    if (!(b & kBomberNoHitEntry)) {
        const uint32_t high = (b & kBomberNoLockOn) ? kHitNoLockOn : 0u;
        const int16_t pos[3] = {a.pos.x.whole, wrap16(a.pos.y.whole - 0x40), a.pos.z.whole};
        if (!(b & kBomberThrown))
            hitListSubmit(pos, f.hitBox, ((uint32_t(a.slot) << 15 | row.hitHeight) << 9) | high | 0x40400cu,
                          uint32_t(row.attack) | kHitAttackSolid);
        else
            hitListSubmit(pos, f.hitBox, hitOwner(a.slot) | 0x20400cu,
                          uint32_t(row.bodyAttack) | kHitAttackThrown | kHitAttackByPlayer);
    }
    // exe 0x5f20f0(a, 0x30, 0x100, 0): the shadow entry is render output; its ground probe is kept
    if (a.flags & kActorDrawn) shadowGroundProbe(env, a);
}

} // namespace

void st18BomberBirdbotUpdate(const PlayerEnv& env, uint32_t actor) { bomber(env, animatedActorAt(actor)); }

} // namespace od::port
