// The Nino mission tank, class 0x4a1c40 (see mission_tank.hpp). States [0x893ba4]: 0 init 0x4a1e10, 1 0x4a1ed0
// (sub-states [0x893bb0]: 0 0x4a1f60 aim / fire, 1 0x4a22b0 pick a target, 2 0x4a2320 idle, 3 0x4a2370 shaken),
// 2 0x4a23d0 destroyed. The turret's yaw is the rotation's y, the barrel's pitch its spare lane; the wheels' yaw (the
// acceleration's spare lane) eases toward half the pitch; both turn bones of the pose (the decode buffer's part 1 /
// part 3 angles).
#include "content/nino/mission_tank.hpp"
#include "content/nino/mission_tank_state.hpp"

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
#include "game/fixed_point.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/rewards.hpp"
#include "game/skid_sparks.hpp"
#include "game/tank_shell.hpp"

namespace od::port {

using namespace mission_tank_state;

namespace {

constexpr const char* kTankPort = "mission tank port"; // the area of its port-gap errors

constexpr uint8_t kTankFire = 0x01;   // fire the shell this tick
constexpr uint8_t kTankSteady = 0x02; // shaken: a hurt does not restart the shake

// The type's row as the tank reads it: its hit points, the shell's attack and the balance a hit by the player costs
struct TankStats : AnimatedActorStats {
    static constexpr uint32_t kBytes = 0xd;
    uint8_t balanceLossHit = 0; // +0x8
    uint8_t balanceLossPush = 0; // +0x9: ... by a strong push
    template <class R> static TankStats load(const R& r) {
        TankStats v;
        static_cast<AnimatedActorStats&>(v) = AnimatedActorStats::load(r);
        v.balanceLossHit = r.template read<uint8_t>(0x8);
        v.balanceLossPush = r.template read<uint8_t>(0x9);
        return v;
    }
};
TankStats statsOf(const AnimatedActor& a) { return dataRecord<TankStats>(animatedStatRow(a.type), TankStats::kBytes); }

// The smoke on the tank (stage static type 0x26): its owner's slot, put out by the owner, the owner's type
struct TankSmokeParams {
    explicit TankSmokeParams(const StaticActor& actor) : TankSmokeParams(const_cast<StaticActor&>(actor)) {}
    explicit TankSmokeParams(StaticActor& actor) : ownerSlot(actor.params[0]), putOut(actor.params[1]), ownerType(actor.params[2]) {}
    FieldAlias<int8_t, uint8_t> ownerSlot;
    uint8_t& putOut;
    uint8_t& ownerType;
};
constexpr uint8_t kTankSmokeType = 0x26, kWreckType = 0x32, kTankType = 0x33;

enum class TankState : uint8_t { Init = 0, Main = 1, Destroyed = 2 };
enum class TankSub : uint8_t { Aim = 0, Pick = 1, Idle = 2, Shaken = 3 };
enum class AimStep : uint8_t { Start = 0, Wait = 1, Fire = 2, Recoil = 3 }; // the aim's step counter

const DataTable<CollisionBoxRecord> tankBox{TableId::MissionTankBox};
// by damage level: the lead shifts (8 each), the fire timers and the pauses (0x10 each); the smokes' offsets {z, yaw}
const DataTable<ScalarTable<uint8_t, 0x18>> leadShifts{TableId::MissionTankLeadShifts};
const DataTable<ScalarTable<uint8_t, 0x30>> fireTimers{TableId::MissionTankFireTimers}, shotPauses{TableId::MissionTankShotPauses};
struct SmokeOffset : DataRecord {
    uint16_t z = 0;
    uint16_t yaw = 0;
    static SmokeOffset load(const TableReader& r) {
        SmokeOffset rec;
        rec.z = r.u16(0);
        rec.yaw = r.u16(2);
        return rec;
    }
};
const DataTable<RecordTable<SmokeOffset, 6, 4>> smokeOffsets{TableId::MissionTankSmokeOffsets};
constexpr uint8_t kShellVariant = 0xd;
constexpr int16_t kShellTicks = 0x28;
constexpr uint16_t kSoundHit = 0x8d, kSoundHitStrong = 0x8e, kSoundSmoke = 0x19f, kSoundDestroyed = 0x1a0;
constexpr uint16_t kSoundEngine = 0x1a3;
constexpr int kDestroyedBalance = 0x5dc;

void toSub(AnimatedActor& a, TankSub s) {
    a.subState = uint8_t(s);
    a.enterStep(0);
}
// exe 0x4a25b0: the slots of the live actors of the target class and type, in slot order (at most 8)
int targets(const TankFields& f, uint8_t out[8]) {
    int n = 0;
    const int type = f.targetType;
    auto scan = [&](uint32_t mask, auto&& slotAt) {
        for (uint32_t i = 0, bits = mask; bits != 0; ++i, bits <<= 1) {
            const ActorHeader& o = slotAt(i);
            if (int32_t(bits) < 0 && int(o.type) == type) {
                out[n++] = o.slot;
                if (n == 8) return true;
            }
        }
        return false;
    };
    ActorSystemState& sys = actorSystem();
    if (f.targetClass == kAnimatedClass) {
        scan(sys.animatedLive, [](uint32_t i) -> const ActorHeader& { return animatedPool()[i]; });
    } else if (f.targetClass == kStaticClass) {
        if (scan(sys.staticsLive[0], [](uint32_t i) -> const ActorHeader& { return staticPool()[i]; })) return 8;
        scan(sys.staticsLive[1], [](uint32_t i) -> const ActorHeader& { return staticPool()[32 + i]; });
    }
    return n;
}
uint8_t firstTarget(const TankFields& f) { // exe 0x4a2540: the listed one, or a random one when there are fewer
    uint8_t t[8] = {};
    const int n = targets(f, t);
    if (n == 0) return 0;
    if (uint32_t(n) < uint32_t(int32_t(f.targetIndex))) return t[(rngStep() & 0xffff) % uint32_t(n)];
    return t[f.targetIndex];
}
// exe 0x4a2690(actor, range, cone): the last-listed target within range whose facing is within cone of the tank, else
// a random one
uint8_t nearTarget(const AnimatedActor& a, const TankFields& f, uint32_t range, int cone) {
    uint8_t t[8] = {};
    const int n = targets(f, t);
    if (n == 0) return 0;
    for (int k = n; k >= 1; --k) {
        const Actor& o = actorLayoutAt(actorBySlot(t[k - 1]));
        if (uint32_t(distance3(o.pos, a.pos)) < range) {
            const int ang = angleOf(o.pos.x, o.pos.z, a.pos.x, a.pos.z); // exe 0x5e3bc0
            const int d = angleDiff(uint32_t(int32_t(o.rotation.y)), uint32_t(ang));
            if (uint32_t(d + cone) < uint32_t(cone * 2)) return t[k - 1];
        }
    }
    return t[(rngStep() & 0xffff) % uint32_t(n)];
}
void turnWheels(TankFields& f, int target, int step) { // exe 0x4a2a10
    const int d = angleDiff(uint32_t(int32_t(f.wheelsYaw)), uint32_t(target));
    if (uint32_t(d + step) <= uint32_t(step * 2)) {
        f.wheelsYaw = int16_t(uint16_t(f.wheelsYaw) + uint16_t(d));
        return;
    }
    f.wheelsYaw = int16_t(uint16_t(f.wheelsYaw) + uint16_t(d < 1 ? -step : step));
}
// exe 0x4a29c0: a smoke on the tank at the offset; its slot (0: none)
uint8_t spawnSmoke(const AnimatedActor& a, const SmokeOffset& at) {
    const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!address) return 0;
    StaticActor& s = staticActorAt(address);
    TankSmokeParams p(s);
    s.flags = kActorLive;
    s.type = kTankSmokeType;
    s.loopFlags |= kActorSweepGroup2;
    s.subtype = 0;
    s.variant = 0;
    p.putOut = 0;
    s.pos.x.whole = 0;
    p.ownerSlot = int8_t(a.slot);
    p.ownerType = kTankType;
    s.pos.y.whole = -200;
    s.pos.z.whole = int16_t(at.z);
    s.rotation.y = int16_t(at.yaw);
    return s.slot;
}
// exe 0x4a2770: 1 = hurt (its damage level changed, or a strong push), 2 = destroyed; the extinguisher repairs it
int damage(AnimatedActor& a, TankFields& f) {
    const uint32_t v = f.hitBox.result;
    const TankStats row = statsOf(a);
    const int16_t hp0 = a.hitPoints;
    const int16_t half = int16_t(a.maxHitPoints >> 1);
    if (f.hurtCooldown != 0) return 0;
    const int16_t quarter = int16_t(a.maxHitPoints >> 2);
    int r = 0;
    if ((v & kHitAttackWater) && f.damageLevel != 0) { // repaired
        f.repair = uint8_t(f.repair + 4u);
        hitPointsAdd(a, int(f.repair >> 6));
        if (half < a.hitPoints) a.hitPoints = int16_t(half + 1);
        if (quarter <= hp0) f.damageLevel = 1;
        if (half <= hp0) f.damageLevel = 0;
        const uint32_t lvl = f.lastLevel;
        if (int(lvl) != f.damageLevel) {
            const uint32_t slot = f.smokeSlots[lvl];
            if (slot != 0) {
                StaticActor& s = staticPool()[slot - kStaticClass];
                TankSmokeParams p(s);
                if ((s.flags & kActorLive) && s.type == kTankSmokeType && int(p.ownerSlot) == int(a.slot)) {
                    p.putOut = 1;
                    f.smokeSlots[lvl] = 0;
                }
            }
        }
    }
    if ((v & kHitDamageMask) && (v & (kHitAttackStrong | kHitAttackByPlayer | kHitAttackSolid))) {
        a.tint = kTintFlash;
        r = (v & kHitAttackStrongPush) ? 1 : 0;
        f.hurtCooldown = 10;
        soundPlayAtPos(r ? kSoundHitStrong : kSoundHit, a.pos);
        if (hitPointsAdd(a, -int(v & kHitDamageMask)) != 0) r = 2;
        if (v & kHitAttackByPlayer)
            balanceAdd(-int((v & kHitAttackStrongPush) ? row.balanceLossPush : row.balanceLossHit)); // exe 0x5e51d0
        if (hp0 < half) f.damageLevel = 1;
        if (hp0 < quarter) f.damageLevel = 2;
        if (int(f.lastLevel) != f.damageLevel) {
            r = 1;
            const uint32_t k = (rngStep() & 1) + uint32_t(int32_t(f.damageLevel) * 2);
            f.smokeSlots[size_t(int32_t(f.damageLevel))] = spawnSmoke(a, smokeOffsets[k]);
            soundPlayAtPos(kSoundSmoke, a.pos);
        }
    }
    return r;
}
uint8_t levelTable(const DataTable<ScalarTable<uint8_t, 0x30>>& t, const TankFields& f) { // by damage level, 0x10 random entries
    return t[size_t(int32_t(f.damageLevel) * 0x10) + (rngStep() & 0xf)];
}
uint8_t leadShift(const TankFields& f) { return leadShifts[size_t(int32_t(f.damageLevel) * 8) + (rngStep() & 7)]; }

// ---- sub-states ---------------------------------------------------------------------------------------------------
void aim(AnimatedActor& a, TankFields& f) { // exe 0x4a1f60
    if (a.step == 0) {
        f.bits = 0;
        f.leadShift = leadShift(f);
        a.enterStep(1);
    }
    if (f.target == 0) {
        toSub(a, TankSub::Idle);
        return;
    }
    const Actor& t = actorLayoutAt(actorBySlot(f.target));
    const uint32_t cls = actorSlotClass(f.target);
    if (!(t.flags & kActorLive) || uint32_t(int32_t(int8_t(f.targetClass))) != cls ||
        uint32_t(t.type) != uint32_t(int32_t(f.targetType))) {
        toSub(a, TankSub::Pick);
        return;
    }
    const int d = distance3(a.pos, t.pos); // exe 0x5e3a60
    if (f.targetHold != 0) {
        --f.targetHold;
    } else if (d > 0x1680) {
        toSub(a, TankSub::Pick);
        return;
    }
    // its position led by its last move
    const uint32_t sh = f.leadShift & 0x1f;
    const int32_t lx = int32_t(((uint32_t(t.pos.x) - uint32_t(t.prevPos.x)) << sh) + uint32_t(t.pos.x));
    const int32_t lz = int32_t(((uint32_t(t.pos.z) - uint32_t(t.prevPos.z)) << sh) + uint32_t(t.pos.z));
    turnYawTo(a.rotation.y, uint32_t(angleOf(lx, lz, a.pos.x, a.pos.z)), 0x30); // exe 0x5e3bc0 / 0x5e3df0
    const int pd = angleDiff(uint32_t(int32_t(f.barrelPitch)), uint32_t(pitchOf(a.pos, t.pos) + kHalfTurn));
    if (uint32_t(pd + 0x40) < 0x80) f.barrelPitch = int16_t(uint16_t(f.barrelPitch) + uint16_t(pd));
    else f.barrelPitch = int16_t(uint16_t(f.barrelPitch) + uint16_t(pd < 1 ? -0x40 : 0x40));
    if (f.barrelPitch > 0) f.barrelPitch = 0;
    if (f.barrelPitch < -0x200) f.barrelPitch = -0x200;
    switch (AimStep(a.stepCounter)) {
    case AimStep::Start:
        a.anim = 0;
        a.stepCounter = uint8_t(AimStep::Wait);
        [[fallthrough]];
    case AimStep::Wait: // one tank of the group fires at a time
        if (f.timer == 0 && distance3(a.pos, t.pos) < 0xb40 &&
            !(enemyGroupMarks(f.group, EnemyGroupMarks::Either) & 2)) {
            a.stepCounter = uint8_t(AimStep::Fire);
            enemyGroupMark(f.group, 2); // exe 0x5e6d40
        }
        break;
    case AimStep::Fire:
        f.fireTimer = levelTable(fireTimers, f);
        a.anim = 1;
        f.bits = kTankFire;
        a.stepCounter = uint8_t(AimStep::Recoil);
        enemyGroupMark(f.group, 2);
        break;
    case AimStep::Recoil:
        enemyGroupMark(f.group, 2);
        if (a.animEnded()) f.bits = kTankFire;
        --f.fireTimer;
        if (f.fireTimer == 0) {
            f.leadShift = leadShift(f);
            f.timer = levelTable(shotPauses, f);
            a.stepCounter = uint8_t(AimStep::Start);
        }
        break;
    default: break;
    }
    if (f.timer != 0) --f.timer;
    turnWheels(f, f.barrelPitch >> 1, 0x20);
}
void pick(AnimatedActor& a, TankFields& f) { // exe 0x4a22b0: one tank of the group a tick
    f.bits = 0;
    if (enemyGroupMarks(f.group, EnemyGroupMarks::Either) & 1) return;
    enemyGroupMark(f.group, 1);
    const uint8_t t = nearTarget(a, f, 0x10e0, kQuarterTurn);
    f.target = t;
    f.targetHold = 0x96;
    toSub(a, t != 0 ? TankSub::Aim : TankSub::Idle);
}
void idle(AnimatedActor& a, TankFields& f) { // exe 0x4a2320
    if (a.step == 0) {
        f.timer = 0x5a;
        f.bits = 0;
        a.enterStep(1);
    }
    turnWheels(f, 0, 0x80);
    --f.timer;
    if (f.timer == 0) toSub(a, TankSub::Pick);
}
void shaken(AnimatedActor& a, TankFields& f) { // exe 0x4a2370: the body rocks (its pitch's bit 1 toggled)
    if (a.step == 0) {
        f.bits = kTankSteady;
        a.anim = 0;
        f.timer = 0x3c;
        a.enterStep(1);
    }
    turnWheels(f, 0x180, 0x80);
    a.rotation.x = int16_t(uint16_t(a.rotation.x) ^ 2u);
    --f.timer;
    if (f.timer == 0) {
        a.rotation.x = 0;
        toSub(a, TankSub::Aim);
    }
}
// exe 0x4a23d0: two explosions, 0x2d ticks, then the blast, the sparks, the wreck (stage animated type 0x32)
void destroyed(AnimatedActor& a, TankFields& f) {
    auto spawnAt = [&](uint8_t type) -> StaticActor* { // exe 0x5e1300
        const uint32_t address = allocActor60(0);
        if (!address) return nullptr;
        StaticActor& s = staticActorAt(address);
        s.flags = kActorLive;
        s.type = type;
        s.variant = 0;
        s.loopFlags |= kActorSweepGroup2;
        s.pos.x.whole = a.pos.x.whole;
        s.pos.y.whole = wrap16(a.pos.y.whole - 0x60);
        s.pos.z.whole = a.pos.z.whole;
        return &s;
    };
    if (a.step == 0) {
        f.timer = 0x2d;
        for (int i = 0; i < 2; ++i) {
            if (StaticActor* s = spawnAt(typeByte(GlobalStaticType::Explosion))) {
                ExplosionParams x(actorHeaderAt(s->at));
                x.spread = 0x80;
                x.flashes = 0x2d;
            }
        }
        a.enterStep(1);
    }
    turnWheels(f, 0x180, 0x80);
    --f.timer;
    if (f.timer != 0) return;
    if (StaticActor* s = spawnAt(typeByte(GlobalStaticType::Blast))) {
        BlastParams b(actorHeaderAt(s->at));
        b.size = 0xc0;
        b.attack = 0;
    }
    if (const uint32_t address = allocActorA0(0)) { // exe 0x5e13b0
        EffectActor& e = effectActorAt(address);
        e.flags = kActorLive;
        e.type = typeByte(GlobalEffectType::SkidSparks);
        e.variant = 0x11;
        e.pos.x = a.pos.x.whole;
        e.pos.y = wrap16(a.pos.y.whole - 0x30);
        e.pos.z = a.pos.z.whole;
        classState<SkidSparkFields>(e).yaw = 0;
    }
    actorDetachRiders(a); // exe 0x414e70
    if (const uint32_t address = allocActor20()) { // exe 0x5e1160: the wreck
        AnimatedActor& w = animatedActorAt(address);
        w.flags = kActorLive;
        w.type = kWreckType;
        w.subtype = 0;
        w.variant = 0;
        w.pos.x.whole = a.pos.x.whole;
        w.pos.y.whole = wrap16(a.pos.y.whole - 0x50);
        w.pos.z.whole = a.pos.z.whole;
        w.rotation.y = a.rotation.y;
    }
    soundPlayAtPos(kSoundDestroyed, a.pos);
    balanceAdd(-kDestroyedBalance); // exe 0x5e51d0
    freeActor(a);
}

// ---- states -------------------------------------------------------------------------------------------------------
void mainState(AnimatedActor& a, TankFields& f) { // exe 0x4a1ed0
    const int r = damage(a, f);
    if (r == 1) {
        if (!(f.bits & kTankSteady)) toSub(a, TankSub::Shaken);
    } else if (r == 2) {
        a.enterState(uint8_t(TankState::Destroyed));
        destroyed(a, f);
    }
    switch (TankSub(a.subState)) { // [0x893bb0]
    case TankSub::Aim: aim(a, f); break;
    case TankSub::Pick: pick(a, f); break;
    case TankSub::Idle: idle(a, f); break;
    case TankSub::Shaken: shaken(a, f); break;
    default: notPortedAt(kTankPort, "class 0x4a1c40 sub-state", a.subState);
    }
    if (f.damageLevel == 0) {
        f.engineTimer = 1;
    } else {
        --f.engineTimer;
        if (f.engineTimer == 0) {
            soundPlayAtPos(kSoundEngine, a.pos);
            f.engineTimer = 0x1b;
        }
    }
}
void init(const PlayerEnv& env, AnimatedActor& a, TankFields& f) { // exe 0x4a1e10
    if (!bindActorModel(a, 0)) {
        freeActor(a);
        return;
    }
    a.anim = 0;
    a.prevAnim = 0xff;
    const uint16_t hp = hasAnimatedStats(a.type) ? statsOf(a).hitPoints : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.collisionBox.pointAt(tankBox.stateAddress());
    a.interactionBox = nullptr;
    a.interaction = 0;
    a.airborne = 0;
    a.onPlatform = 0;
    a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190
    a.clearClassBlock();
    f.barrelPitch = 0;
    f.wheelsYaw = 0;
    f.group = int8_t(enemyGroupFindFor(a, 0)); // exe 0x5e6cd0
    f.target = firstTarget(f);
    a.enterState(uint8_t(TankState::Main));
}

void tank(const PlayerEnv& env, AnimatedActor& a) { // exe 0x4a1c40
    TankFields& f = classState<TankFields>(a);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    f.lastLevel = uint8_t(f.damageLevel);
    a.tint = kTintNeutral;
    switch (TankState(a.state)) { // [0x893ba4]
    case TankState::Init: init(env, a, f); break;
    case TankState::Main: mainState(a, f); break;
    case TankState::Destroyed: destroyed(a, f); break;
    default: notPortedAt(kTankPort, "class 0x4a1c40 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim) animStep(a);
    else animSet(a, a.anim, 0);
    DecodedFrame& frame = decodedFrame(); // the wheels and the barrel turn their parts
    frame.entries[2].x = int16_t(uint16_t(frame.entries[2].x) + uint16_t(f.wheelsYaw));
    frame.entries[4].x = int16_t(uint16_t(frame.entries[4].x) + uint16_t(f.barrelPitch));
    animPose(a);
    const TankStats row = statsOf(a);
    if (f.hurtCooldown != 0) --f.hurtCooldown;
    if (f.bits & kTankFire) { // the shell from bone 3, 0xa8 ahead
        int16_t p[3];
        bonePointAhead(a, 3, 0xa8, p); // exe 0x5e18d0
        if (const uint32_t address = allocActor40(kActorStageClass)) { // exe 0x5e1280
            ProjectileActor& s = projectileActorAt(address);
            TankShellParams shell(s);
            s.flags = kActorLive;
            s.type = kTankShellStageType;
            s.variant = kShellVariant;
            s.loopFlags |= kActorSweepGroup2;
            s.pos.x.whole = p[0];
            s.pos.y.whole = p[1];
            s.pos.z.whole = p[2];
            s.rotation.x = f.barrelPitch;
            s.rotation.y = a.rotation.y;
            s.rotation.z = 0;
            shell.ticks = kShellTicks;
            shell.attack = row.attack;
        }
        f.bits &= uint8_t(~kTankFire);
    }
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    const ShortVec4& bp = bonePoint(a, 0, kTankPort); // exe 0x5e1810
    const int16_t pos[3] = {bp.x, wrap16(bp.y - 0x50), bp.z};
    hitListSubmit(pos, f.hitBox, hitOwner(a.slot) | 0xa88028u, kHitAttackHarmless | kHitAttackSolid);
    actorCollisionSubmitSolid(a, SolidType::StandOn, kNoBone, 0); // exe 0x414050
}

} // namespace

void missionTankUpdate(const PlayerEnv& env, uint32_t actor) { tank(env, animatedActorAt(actor)); }

} // namespace od::port
