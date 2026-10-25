// ST0E: the carrier crate (see st0e_carrier_crate.hpp).
//
// The crate (exe 0x5a8150; ST0E type 0x24, spawned by the boss vehicle with variant 0 and spawn byte 1 -- its stat row
// index). States (the exe's table 0x8cef14): 0 bind 0x5a83d0, 1 falling 0x5a8510, 2 lifted / thrown 0x5a8730, 3 broken
// 0x5a88f0. Every tick the Servbot inside rides it (its position + the crate's rotation of a zero offset, its pitch /
// yaw, its animation stepped), or -- the crate gone -- that Servbot is freed and one that jumps out is spawned (type 0xb
// variant 1); its hit entry by its kind (Falling hurts the player with the row's damage, Thrown hurts with the boss
// vehicle's row hit points << 6 / 3 >> 2, Lifted none). The shadow (exe 0x5f20f0) is render output and left out.
//   Bind: the model, the HP of its row, its boxes, the class block cleared, the fall time (the 3D distance to the
//   target * 16 / 0xc00) and its life (the fall time + 300), the Servbot inside (type 0xb variant 0) and a trail ring
//   (static global 6 variant 1, exe 0x5a8970).
//   The target (exe 0x5a8620) by the variant: 0 the player (y - 0x8c), 1 (0, -0xe00, 0), 2 one of 4 points (rng).
//   The aim (exe 0x5a86c0): it turns to face away from the target and pitches toward it (the crate moves along -z).
//   Falling: hit / below y -0x1000 -> broken; after the fall time it aims at the target each tick, moves -0x300; walls /
//   its life out -> broken; lifted (exe 0x573a60) -> Lifted, the trail ring freed.
//   Lifted (exe 0x5a8750): at the hands until released: thrown (aimed at the lock-on target, the steps 0x40, the
//   player's pitch) or dropped (the pitch -0x400); the player's yaw, 300 ticks, a new trail ring -> Thrown.
//   Thrown (exe 0x5a8800): homes on its target (the boss vehicle 0x7a: its bone 0 point (0x51, -0x262, 0x400), the
//   front weak point of content/yosyonke/st0e_boss_vehicle) at -0x500 per tick; walls / a hit / the ticks out -> broken
//   (exe 0x5a88f0: the kill counters, a blast (static global 7) of size 0xa0 and the row's power, freed).
// The exe reads an uninitialised local as the target of a variant above 2: a port gap there.
#include "content/yosyonke/st0e_carrier_crate.hpp"
#include "content/yosyonke/st0e_carrier_crate_state.hpp"

#include "content/yosyonke/st0e_vehicles.hpp"
#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/matrix.hpp"
#include "game/missile_trail.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st0e_carrier_crate_state;

namespace {

constexpr uint32_t kNoServbot = 0xffffffffu;
constexpr uint8_t kCrateThrown = 0x01, kCrateLifted = 0x02; // the kind (0: falling)
enum class CrateState : uint8_t { Bind = 0, Falling = 1, Carried = 2, Broken = 3 };
enum class FallStep : uint8_t { Start = 0, Fall = 1 };
enum class CarriedSub : uint8_t { Lifted = 0, Thrown = 1 };
constexpr uint8_t kBossVehicleType = 0x7a;
constexpr uint8_t kServbotType = 0xb;
constexpr uint8_t kServbotInside = 0, kServbotJumpsOut = 1;
const DataTable<CollisionBoxRecord> crateBox{TableId::St0eCarrierCrateBox};
const DataTable<InteractionBoxRecord> crateLiftBox{TableId::St0eCarrierCrateLiftBox};
// the four points of the variant 2's target (s16 x / y / z, 8 bytes apart)
struct CratePoint : DataRecord {
    int16_t x = 0, y = 0, z = 0;
    static CratePoint load(const TableReader& r) {
        CratePoint rec;
        rec.x = r.s16(0);
        rec.y = r.s16(2);
        rec.z = r.s16(4);
        return rec;
    }
};
const DataTable<RecordTable<CratePoint, 4, 8>> cratePoints{TableId::St0eCarrierCratePoints};
// the spawn bytes of the Servbot it carries (the Servbot class does not read them)
struct CarriedServbotSpawn {
    explicit CarriedServbotSpawn(const AnimatedActor& actor) : CarriedServbotSpawn(const_cast<AnimatedActor&>(actor)) {}
    explicit CarriedServbotSpawn(AnimatedActor& actor) : crateSlot(actor.params[0]), tag(actor.params, 2) {}
    uint8_t& crateSlot;
    BytesField<uint16_t, 4> tag; // 0x20
};

StaticActorStats rowOf(const StaticActor& a) { return staticStats(a.type, a.spawnByte); }
void positionOf(const Actor& a, int32_t out[3]) {
    out[0] = a.pos.x;
    out[1] = a.pos.y;
    out[2] = a.pos.z;
}
// exe 0x5a80d0(a, box): a hit with damage by the player flashes and hurts; true = dead, or a touch / a solid hit
bool crateHit(StaticActor& a, const HitBoxHead& box) {
    const uint32_t v = box.result;
    if ((v & kHitDamageMask) && (v & kHitAttackByPlayer)) {
        a.tint = kTintFlash;
        if (hitPointsAdd(a, -int(v & kHitDamageMask)) != 0) return true; // exe 0x5e52f0
    }
    return (v & (kHitAttackBump | kHitAttackSolid)) != 0;
}
// exe 0x5a8620(a, out): the target point by the variant
void crateTarget(const StaticActor& a, int32_t out[3]) {
    switch (a.variant) {
    case 0: {
        const Player& p = player();
        out[0] = p.pos.x;
        out[1] = int32_t(uint32_t(p.pos.y) - 0x8c0000u);
        out[2] = p.pos.z;
        return;
    }
    case 1:
        out[0] = 0;
        out[1] = int32_t(0xf2000000u);
        out[2] = 0;
        return;
    case 2: {
        const CratePoint q = cratePoints[rngStep() & 3];
        out[0] = int32_t(uint32_t(int32_t(q.x)) << 16);
        out[1] = int32_t(uint32_t(int32_t(q.y)) << 16);
        out[2] = int32_t(uint32_t(int32_t(q.z)) << 16);
        return;
    }
    default: notPortedAt(kSt0ePort, "0x5a8620: crate target variant (an uninitialised local in the exe)", a.variant);
    }
}
// exe 0x5a86c0(a, target): face away from the target (the yaw step spin.y), pitch toward it (the step spin.x)
void crateAim(StaticActor& a, const int32_t t[3]) {
    turnYawTo(a.rotation.y, uint32_t(angleOf(t[0], t[2], a.pos.x, a.pos.z)), a.spin.y); // exe 0x5e3bc0 / 0x5e3df0
    int32_t from[3];
    positionOf(a, from);
    const int p = pitchOf(from, t) + kHalfTurn;                          // exe 0x5e3ca0
    const int d = angleDiff(uint32_t(int32_t(a.rotation.x)), uint32_t(p)); // exe 0x5e3d80
    int v = a.spin.x;
    if (uint32_t(v + d) < uint32_t(v * 2)) {
        a.rotation.x = int16_t(p);
        return;
    }
    if (d <= 0) v = -v;
    a.rotation.x = int16_t(uint16_t(a.rotation.x) + uint16_t(v));
}
// exe 0x5a8970(a, block): the trail ring (static global 6 variant 1, late, following the crate); its slot kept
void spawnTrailRing(StaticActor& a, CrateFields& f) {
    const uint32_t at = allocActor60(0); // exe 0x5e1300
    if (!at) return;
    StaticActor& e = staticActorAt(at);
    e.loopFlags |= kActorLateLoop;
    e.runFlags |= kActorWarpKept;
    e.flags = kActorLive;
    e.type = typeByte(GlobalStaticType::MissileTrail);
    e.variant = 1;
    classState<MissileTrailFields>(e).parent = a.at;
    f.ringSlot = e.slot;
}
// exe 0x5a88f0: broken -- the kill counters, a blast (static global 7, size 0xa0, the row's power), freed
void crateBreak(StaticActor& a) {
    const StaticActorStats row = rowOf(a);
    countDefeat();                       // exe 0x5e53e0
    if (const uint32_t at = allocActor60(0)) { // exe 0x5e1300
        StaticActor& e = staticActorAt(at);
        e.loopFlags |= kActorSweepGroup2;
        e.runFlags |= kActorWarpKept;
        e.flags = kActorLive;
        e.type = typeByte(GlobalStaticType::Blast);
        e.variant = 0;
        e.pos.x = a.pos.x;
        e.pos.y = a.pos.y;
        BlastParams b(e);
        b.size = 0xa0;
        e.pos.z = a.pos.z;
        b.attack = row.power;
    }
    freeActor(a); // exe 0x5e1540
}
void crateBind(StaticActor& a, CrateFields& f) { // exe 0x5a83d0
    if (!bindActorModel(a, 0)) {                 // exe 0x5e0ba0
        freeActor(a);                            // exe 0x5e1540
        return;
    }
    a.flags |= kActorDrawn;
    const uint16_t hp = hasStaticStats(a.type) ? uint16_t(rowOf(a).hitPoints) : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.velocity.setXY(0, 0);
    a.rotation.z = 0;
    a.airborne = 1;
    a.onPlatform = 0;
    a.collisionBox.pointAt(crateBox.stateAddress());
    a.interactionBox.pointAt(crateLiftBox.stateAddress());
    a.interaction = kInteractLift;
    a.clearClassBlock(); // exe 0x40dc40
    int32_t t[3];
    crateTarget(a, t);
    const Fixed16Vec3& pos = a.pos;
    const uint32_t d = (uint32_t(distance3(pos, t)) << 4) / 0xc00; // exe 0x5e3a60
    f.fallTicks = uint16_t(d);
    f.life = uint16_t(d + 0x12c);
    a.enterState(uint8_t(CrateState::Falling));
    f.servbot = kNoServbot;
    if (const uint32_t at = allocActor20()) { // exe 0x5e1160: the Servbot inside
        AnimatedActor& e = animatedActorAt(at);
        e.loopFlags |= kActorSweepGroup2;
        e.runFlags |= kActorWarpKept;
        e.flags = kActorLive;
        e.type = kServbotType;
        e.variant = kServbotInside;
        CarriedServbotSpawn s(e);
        s.crateSlot = a.slot;
        s.tag = 0x20;
        e.pos.x.whole = 0;
        e.pos.y.whole = 0;
        e.pos.z.whole = 0;
        e.anim = 0;
        e.prevAnim = 0xff;
        f.servbot = e.at;
    }
    spawnTrailRing(a, f);
}
void crateFalling(const PlayerEnv& env, StaticActor& a, CrateFields& f) { // exe 0x5a8510
    if (FallStep(a.subState) == FallStep::Start) {
        f.kind = 0;
        a.spin.x = 0x18;
        a.spin.y = 0x18;
        a.subState = uint8_t(FallStep::Fall);
    }
    if (crateHit(a, f.box) || a.pos.y.whole < -0x1000) {
        crateBreak(a);
        return;
    }
    if (f.fallTicks == 0) {
        int32_t t[3];
        crateTarget(a, t);
        crateAim(a, t);
    } else {
        --f.fallTicks;
    }
    moveAlongRotation(a.pos, a.rotation, 0, 0, -0x300); // exe 0x5e37b0
    if (int32_t(actorTestMove(env, a)) < 0) {           // exe 0x40f570
        crateBreak(a);
        return;
    }
    --f.life;
    if (f.life == 0) {
        crateBreak(a);
        return;
    }
    if (!playerLiftTargetCheck(a, 0)) return; // exe 0x573a60
    a.enterState(uint8_t(CrateState::Carried));
    f.kind |= kCrateLifted;
    const uint8_t ring = f.ringSlot;
    if (ring == 0) return;
    StaticActor& r = staticPool()[size_t(ring) - kStaticClass];
    if (!(r.flags & kActorLive)) return;
    freeActor(r); // exe 0x5e1540
    f.ringSlot = 0;
}
void crateThrown(const PlayerEnv& env, StaticActor& a, CrateFields& f) { // exe 0x5a8800
    if (!crateHit(a, f.box)) {
        if (f.throwTarget != 0) {
            const uint32_t t = actorBySlot(f.throwTarget); // exe 0x5e0fa0
            ActorHeader& target = actorHeaderAt(t);
            if (target.flags & kActorLive) {
                int32_t q[3];
                if (target.type == kBossVehicleType) { // the boss vehicle: its front weak point
                    fixedOfPoint(bonePointAt(animatedActorAt(t), 0, 0x51, -0x262, 0x400), q);
                } else {
                    positionOf(actorAt(t), q);
                }
                crateAim(a, q);
            } else {
                f.throwTarget = 0;
            }
        }
        moveAlongRotation(a.pos, a.rotation, 0, 0, -0x500); // exe 0x5e37b0
        if (int32_t(actorTestMove(env, a)) >= 0) {           // exe 0x40f570
            --f.fallTicks;
            if (f.fallTicks != 0) return;
        }
    }
    crateBreak(a);
}
void crateLifted(const PlayerEnv& env, StaticActor& a, CrateFields& f) { // exe 0x5a8750
    if (a.step == 0) {
        f.kind = kCrateLifted;
        f.throwTarget = 0;
        a.rotation.x = 0;
        a.enterStep(1);
    }
    carryAtHands(a, 0, -0x10, 0); // exe 0x573ab0
    const CarryRelease r = carryReleased(); // exe 0x573950
    if (r == CarryRelease::Held) return;
    const Player& p = player();
    if (r != CarryRelease::Thrown) {
        f.kind = 0;
        a.rotation.x = -0x400;
    } else {
        f.kind = kCrateThrown;
        if (p.lockedActor != nullptr) f.throwTarget = actorHeaderAt(slotIdentityWord(p.lockedActor)).slot;
        a.spin.x = 0x40;
        a.spin.y = 0x40;
        a.rotation.x = p.rotation.x;
    }
    a.airborne = 1;
    a.rotation.y = p.rotation.y;
    f.fallTicks = 0x12c;
    carryDropFromPlayer(a); // exe 0x573990
    spawnTrailRing(a, f);
    a.subState = uint8_t(CarriedSub::Thrown);
    a.restartStep();
    crateThrown(env, a, f);
}
// the Servbot at the crate: its position the crate's plus the crate's rotation of a zero offset, its pitch / yaw
void placeServbot(const StaticActor& a, AnimatedActor& e) {
    int32_t o[3];
    rotateVector(a.rotation, 0, 0, 0, o); // exe 0x5e3970
    e.pos.x = int32_t(uint32_t(o[0]) + uint32_t(a.pos.x));
    e.pos.y = int32_t(uint32_t(a.pos.y) + uint32_t(o[1]));
    e.pos.z = int32_t(uint32_t(a.pos.z) + uint32_t(o[2]));
    e.rotation.y = a.rotation.y;
    e.rotation.x = a.rotation.x;
}
void crate(const PlayerEnv& env, StaticActor& a) { // exe 0x5a8150
    CrateFields& f = classState<CrateFields>(a);
    const uint32_t thrownDamage = ((uint32_t(animatedStats(kBossVehicleType).hitPoints) << 6) / 3) >> 2;
    const StaticActorStats row = rowOf(a);
    a.tint = kTintNeutral;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (CrateState(a.state)) { // the exe's table 0x8cef14
    case CrateState::Bind: crateBind(a, f); break;
    case CrateState::Falling: crateFalling(env, a, f); break;
    case CrateState::Carried: // exe 0x5a8730
        if (CarriedSub(a.subState) == CarriedSub::Lifted) crateLifted(env, a, f);
        else crateThrown(env, a, f);
        break;
    case CrateState::Broken: crateBreak(a); break;
    default: notPortedAt(kSt0ePort, "0x5a8150 state", a.state);
    }
    if (a.flags & kActorLive) {
        if (f.servbot != kNoServbot) { // the Servbot rides the crate
            AnimatedActor& e = animatedActorAt(f.servbot);
            placeServbot(a, e);
            if (e.anim == e.prevAnim) animStepPose(e); // exe 0x5e1e80
        }
    } else {
        if (f.servbot != kNoServbot) freeActor(animatedActorAt(f.servbot)); // exe 0x5e1490
        if (const uint32_t at = allocActor20()) {                             // exe 0x5e1160: the Servbot jumps out
            AnimatedActor& e = animatedActorAt(at);
            e.runFlags |= kActorWarpKept;
            e.loopFlags |= kActorSweepGroup2;
            e.flags = kActorLive;
            e.type = kServbotType;
            e.variant = kServbotJumpsOut;
            CarriedServbotSpawn s(e);
            s.crateSlot = a.slot;
            s.tag = 0x20;
            placeServbot(a, e);
        }
    }
    if (!(a.flags & kActorLive)) return;
    const int16_t pos[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    f.box.result = 0;
    f.box.previousX = a.prevPos.x.whole;
    f.box.previousZ = a.prevPos.z.whole;
    const uint8_t kind = f.kind;
    if (kind & kCrateLifted) return;
    if (kind & kCrateThrown)
        hitListSubmit(pos, f.box, hitOwner(a.slot) | hitGroup(kHitGroupPlayerAttacks) | kHitNoLockOn | hitRadius(0x40),
                      thrownDamage | kHitAttackByPlayer);
    else
        hitListSubmit(pos, f.box, hitOwner(a.slot) | hitGroup(kHitGroupBodies) | hitRadius(0x40),
                      uint32_t(row.damage) | kHitAttackThrown | kHitAttackSolid);
}

} // namespace

void carrierCrateUpdate(const PlayerEnv& env, uint32_t actor) { crate(env, staticActorAt(actor)); }

} // namespace od::port
