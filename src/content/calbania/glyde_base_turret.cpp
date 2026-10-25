// The wall turrets of Glyde's base (ST30 sub 2, pool 0x20 type 0x57; see glyde_base_turret.hpp), transcribed from
// dash2.exe one function per exe function. States [0x895a64]: 0 init 0x4e1080, 1 0x4e1170 (sub-states [0x895a74]: 0
// 0x4e11b0 aim, 1 0x4e13b0 fire; then the hurt test 0x4e1690), 2 destroyed 0x4e1510, 3 0x468fa0 (freed). The aim is
// a yaw / pitch offset (eased 0x10 a tick toward its target) added to the barrel part's decoded angles before the
// pose; the timers / the aim's jitter come from the CRT rand() (exe 0x611443).
#include "content/calbania/glyde_base_turret.hpp"
#include "content/calbania/glyde_base_turret_state.hpp"

#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/enemy_shot.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/stage_smoke.hpp"

namespace od::port {

using namespace glyde_base_turret_state;

namespace {

constexpr const char* kTurretPort = "glyde base turret port"; // the area of its port-gap errors

// +0x160
constexpr uint8_t kTurretNoHitEntry = 0x01;
constexpr uint8_t kTurretNoDamage = 0x02;
constexpr uint8_t kTurretAimClamped = 0x20; // the aim's jitter hit its limit: no shots
constexpr uint8_t kTurretHitFlag = 0x80;    // an owner bit of its hit entry

enum class TurretState : uint8_t { Init = 0, Active = 1, Destroyed = 2, Free = 3 };
enum class TurretSub : uint8_t { Aim = 0, Fire = 1 };
enum class AimStep : uint8_t { Enter = 0, Track = 1, Hold = 2 };
enum class FireStep : uint8_t { Enter = 0, Burst = 1, Pause = 2 };
enum class DestroyedStep : uint8_t { Explode = 0, Flash = 1 };

// its row of the animated stats (the turret's type reads its own bytes 8..0xa)
struct TurretStats : DataRecord {
    static constexpr uint32_t kBytes = 0xb;
    uint8_t hitPoints = 0; // +0x0
    uint8_t hitHeight = 0; // +0x4
    uint8_t hitRadius = 0; // +0x8: the hit entry's radius (>> 2)
    uint8_t shotTicks = 0; // +0x9: its shots' ticks of flight
    uint8_t shotAttack = 0; // +0xa: and their damage
    template <class R> static TurretStats load(const R& r) {
        TurretStats v;
        v.hitPoints = r.template read<uint8_t>(0x0);
        v.hitHeight = r.template read<uint8_t>(0x4);
        v.hitRadius = r.template read<uint8_t>(0x8);
        v.shotTicks = r.template read<uint8_t>(0x9);
        v.shotAttack = r.template read<uint8_t>(0xa);
        return v;
    }
};
TurretStats statsOf(const AnimatedActor& actor) { return dataRecord<TurretStats>(animatedStatRow(actor.type), TurretStats::kBytes); }

// the wreck's smoke (the stage's smoke static) keeps the turret it rises from
// the wreck's smoke: the stage smoke's parameters and its source (the identity word of the turret, in the hit box's
// first word)
struct WreckSmokeParams : SmokeParams {
    explicit WreckSmokeParams(const StaticActor& actor) : WreckSmokeParams(const_cast<StaticActor&>(actor)) {}
    explicit WreckSmokeParams(StaticActor& actor) : SmokeParams(actor), source(actor.hitBox.result) {}
    uint32_t& source;
};

constexpr uint16_t kTurretScale = 0x600;
constexpr int kAimRange = 0xa00;
constexpr int kAimJitterLimit = 0x180;
constexpr int16_t kAimEaseStep = 0x10;
constexpr uint8_t kShotVariant = 0x84;
constexpr uint32_t kBarrelBone = 1, kBodyBone = 0;
constexpr size_t kBarrelPart = 1;      // the part hidden when destroyed
constexpr size_t kBarrelAngles = 2;    // the decoded frame's entry of the barrel's angles (part 1)
constexpr uint8_t kSmokeVariant = 0;
constexpr uint16_t kHurtSound = 0x8d;
constexpr uint32_t kHurtAttacks = 0x71c0000; // the hit kinds that hurt it (the player's attacks and the compared heights)
constexpr uint16_t kFlashTint = 0x4214;

void enterSub(AnimatedActor& actor, TurretSub s) {
    actor.subState = uint8_t(s);
    actor.restartStep();
}

// exe 0x4e12f0: d + (rand & 0x3f) - 0x20, clamped to +-0x180 (kTurretAimClamped)
int aimJitter(TurretFields& f, int d) {
    int v = d - 0x20 + int(crtRand() & 0x3f);
    if (v < -kAimJitterLimit || v > kAimJitterLimit) {
        f.bits |= kTurretAimClamped;
        v = v >= 0 ? kAimJitterLimit : -kAimJitterLimit;
    }
    return v;
}
// exe 0x4e1340: the pitch offset toward its target, then the yaw offset, kAimEaseStep a tick
void easeAim(TurretFields& f) {
    const int p = f.pitchOffset, pt = f.pitchTarget;
    int np;
    if (p < pt) np = p + kAimEaseStep <= pt ? p + kAimEaseStep : pt;
    else np = pt <= p - kAimEaseStep ? p - kAimEaseStep : pt;
    f.pitchOffset = int16_t(np);
    const int y = f.yawOffset, yt = f.yawTarget;
    if (y < yt) {
        if (yt < y + kAimEaseStep) {
            f.yawOffset = int16_t(yt);
            return;
        }
        f.yawOffset = int16_t(y + kAimEaseStep);
    } else {
        if (y - kAimEaseStep < yt) {
            f.yawOffset = int16_t(yt);
            return;
        }
        f.yawOffset = int16_t(y - kAimEaseStep);
    }
}
// exe 0x4e11b0: every 4..7 ticks the player within reach -> a new (jittered) aim, held 0xf ticks -> fire
void aim(AnimatedActor& actor, TurretFields& f) {
    const AimStep s = AimStep(actor.step);
    if (s == AimStep::Enter) { // then on into the tracking
        f.bits = 0;
        actor.enterStep(uint8_t(AimStep::Track));
        actor.tint = kTintNeutral;
    } else if (s == AimStep::Hold) {
        --f.timer;
        if (f.timer == 0) {
            enterSub(actor, TurretSub::Fire);
            f.timer = 0;
        }
        easeAim(f);
        return;
    } else if (s != AimStep::Track) {
        return;
    }
    const uint16_t t = f.timer;
    f.timer = uint16_t(t - 1u);
    if (t != 0) return;
    f.timer = uint16_t((crtRand() & 3) + 4);
    const Player& p = player();
    if (planarDistance(p.pos, actor.pos) > kAimRange) return;
    actor.enterStep(uint8_t(AimStep::Hold));
    f.timer = 0xf;
    const int ang = angleOf(actor.pos, p.pos); // exe 0x5e3bc0(position, player)
    f.yawTarget = int16_t(aimJitter(f, angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(ang))));
    const int16_t dist = int16_t(planarDistance(actor.pos, p.pos));
    const int16_t dy = int16_t(actor.pos.y.whole - p.pos.y.whole + 100);
    const int pitch = atanAngle(float(dist), float(dy)) & kAngleMask; // exe 0x5e3c50({0, 0}, {dist, dy})
    f.pitchTarget = int16_t(aimJitter(f, angleDiff(uint32_t(pitch), uint32_t(kQuarterTurn))));
}
// exe 0x4e13b0: 0x1e..0x2d ticks of shots every 4th tick (none while the aim is clamped), then a 0x3c..0x7b pause
void fire(AnimatedActor& actor, TurretFields& f) {
    const FireStep s = FireStep(actor.step);
    if (s == FireStep::Enter) {
        actor.enterStep(uint8_t(FireStep::Burst));
        actor.tint = kTintNeutral;
        const uint32_t r = crtRand();
        f.shotTicks = 0;
        f.timer = uint16_t((r & 0xf) + 0x1e);
    } else if (s != FireStep::Burst) {
        if (s == FireStep::Pause) {
            --f.timer;
            if (f.timer == 0) {
                actor.enterState(uint8_t(TurretState::Active), uint8_t(TurretSub::Aim));
                easeAim(f);
                return;
            }
        }
        easeAim(f);
        return;
    }
    --f.timer;
    if (f.timer == 0) {
        actor.enterStep(uint8_t(FireStep::Pause));
        f.timer = uint16_t((crtRand() & 0x3f) + 0x3c);
    }
    if (!(f.bits & kTurretAimClamped)) {
        const uint16_t c = f.shotTicks;
        f.shotTicks = uint16_t(c - 1u);
        if (c == 0) {
            f.shotTicks = 3;
            const ShortVec4& muzzle = bonePoint(actor, kBarrelBone, kTurretPort); // exe 0x5e1810
            const uint32_t address = allocActor40(kActorStageClass);           // exe 0x5e1280
            const TurretStats stats = statsOf(actor);
            if (address) {
                ProjectileActor& shot = projectileActorAt(address);
                shot.flags = kActorLive;
                shot.type = kEnemyShotType;
                shot.variant = kShotVariant;
                shot.loopFlags |= kActorSweepGroup2;
                shot.pos.x.whole = muzzle.x;
                shot.pos.y.whole = muzzle.y;
                shot.pos.z.whole = muzzle.z;
                shot.rotation.x = int16_t(-int(f.pitchOffset));
                shot.rotation.z = 0;
                shot.rotation.y = int16_t((uint16_t(actor.rotation.y) ^ uint16_t(kHalfTurn)) + uint16_t(f.yawOffset));
                EnemyShotFields params(projectileActorAt(address));
                params.ticks = int16_t(stats.shotTicks);
                params.attack = stats.shotAttack;
            }
        }
    }
    easeAim(f);
}
// exe 0x4e1080
void init(AnimatedActor& actor, TurretFields& f) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.rotation.y = int16_t(uint16_t(actor.rotation.y) ^ uint16_t(kHalfTurn));
    actor.anim = 0;
    actor.prevAnim = 0xff;
    actor.scale.x = int16_t(kTurretScale);
    actor.scale.y = int16_t(kTurretScale);
    actor.scale.z = int16_t(kTurretScale);
    integrateVelocity(actor, 0, 0, 0x100); // exe 0x5e3690
    actor.runFlags |= kActorRunMarked;
    const uint16_t hp = hasAnimatedStats(actor.type) ? statsOf(actor).hitPoints : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::GlydeBaseTurretBox);
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock();
    f.hitBox.previousX = actor.pos.x.whole;
    f.hitBox.previousZ = actor.pos.z.whole;
    f.hitBox.result = 0;
    f.timer = 0x1e;
    actor.enterState(uint8_t(TurretState::Active));
    registerEnemy(actor.slot, 0); // exe 0x5e5c10
}
// exe 0x4e1510: an explosion (static global type 8) at the barrel, 10 flashing ticks, then the barrel hidden and the
// wreck's smoke (the stage's smoke static) at it
void destroyed(AnimatedActor& actor, TurretFields& f) {
    if (DestroyedStep(actor.subState) == DestroyedStep::Explode) {
        f.bits = kTurretNoDamage;
        actor.subState = uint8_t(DestroyedStep::Flash);
        f.timer = 10;
        if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
            const ShortVec4& barrel = bonePoint(actor, kBarrelBone, kTurretPort);
            StaticActor& e = staticActorAt(address);
            e.flags = kActorLive;
            e.type = typeByte(GlobalStaticType::Explosion);
            e.variant = 0;
            e.loopFlags |= kActorSweepGroup2;
            e.pos.x.whole = barrel.x;
            e.pos.y.whole = barrel.y;
            e.pos.z.whole = barrel.z;
            ExplosionParams explosion(actorHeaderAt(address));
            explosion.spread = 0x1e;
            explosion.flashes = f.timer;
        }
        return;
    }
    if (DestroyedStep(actor.subState) != DestroyedStep::Flash) return;
    actor.tint = (uint8_t(f.timer) & 1) ? kTintFlash : kFlashTint;
    --f.timer;
    if (f.timer != 0) return;
    actor.tint = kTintNeutral;
    ++actor.subState;
    f.timer = 0x28;
    f.bits |= kTurretNoHitEntry;
    actor.parts[kBarrelPart].flags |= kPartHidden;
    countDefeat(); // exe 0x5e53e0
    if (const uint32_t address = allocActor60(kActorStageClass)) { // exe 0x5e12f0
        const int16_t in[3] = {0, -8, 0xc};
        int16_t out[3];
        bonePointOffset(actor, kBarrelBone, in, out); // exe 0x5e1970
        StaticActor& e = staticActorAt(address);
        e.flags = kActorLive;
        e.type = kSmokeType;
        e.variant = kSmokeVariant;
        e.loopFlags |= kActorSweepGroup2;
        e.pos.x.whole = out[0];
        e.pos.y.whole = out[1];
        e.pos.z.whole = out[2];
        WreckSmokeParams smoke(staticActorAt(address));
        smoke.driftX = 0;
        smoke.driftZ = 0xff;
        smoke.life = 0x3c;
        smoke.source = slotIdentityWord(actor);
    }
}
// exe 0x4e1690: a hit by the player's attacks -> the flash, sound 0x8d, the hit points; out -> destroyed
void hurt(AnimatedActor& actor, TurretFields& f) {
    const uint32_t v = f.hitBox.result, d = v & kHitDamageMask;
    if (d == 0 || !(v & kHurtAttacks)) return;
    f.lastHitContact = f.hitBox.contact.x;
    actor.tint = kTintFlash;
    soundPlayAtPos(kHurtSound, actor.pos);
    if (hitPointsAdd(actor, -int(d)) != 0) actor.enterState(uint8_t(TurretState::Destroyed));
}

// exe 0x4e0f50's body
void turret(AnimatedActor& actor) {
    TurretFields& f = classState<TurretFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    switch (TurretState(actor.state)) {
    case TurretState::Init: init(actor, f); break;
    case TurretState::Active: // exe 0x4e1170
        actor.tint = kTintNeutral;
        if (TurretSub(actor.subState) == TurretSub::Aim) aim(actor, f);
        else if (TurretSub(actor.subState) == TurretSub::Fire) fire(actor, f);
        else notPortedAt(kTurretPort, "class 0x4e0f50 sub-state", actor.subState);
        if (!(f.bits & kTurretNoDamage)) hurt(actor, f);
        break;
    case TurretState::Destroyed: destroyed(actor, f); break;
    case TurretState::Free: freeActor(actor); break; // exe 0x468fa0
    default: notPortedAt(kTurretPort, "class 0x4e0f50 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStep(actor); // exe 0x5e1e80
    else animSet(actor, actor.anim, 0);                // exe 0x5e1c30
    animPose(actor);
    const uint8_t b = f.bits;
    const TurretStats stats = statsOf(actor);
    f.hitBox.result = 0;
    f.hitBox.previousX = actor.prevPos.x.whole;
    f.hitBox.previousZ = actor.prevPos.z.whole;
    if (!(b & kTurretNoHitEntry)) {
        const ShortVec4& body = bonePoint(actor, kBodyBone, kTurretPort); // exe 0x5e1810
        const int16_t pos[3] = {body.x, body.y, body.z};
        hitListSubmit(pos, f.hitBox,
                      ((((uint32_t(actor.slot) << 12) | (b & kTurretHitFlag) | 0x508u) << 3 | stats.hitHeight) << 9) |
                          uint32_t(stats.hitRadius >> 2),
                      kHitAttackSolid);
    }
    LaneVec4 barrel = decodedFrame().entries[kBarrelAngles];
    barrel.y = int16_t(uint16_t(f.yawOffset) + uint16_t(barrel.y));
    barrel.x = int16_t(uint16_t(f.pitchOffset) + uint16_t(barrel.x));
    animPose(actor); // exe 0x5e2d90
}

} // namespace

void glydeBaseTurretUpdate(const PlayerEnv&, uint32_t actor) { turret(animatedActorAt(actor)); }

} // namespace od::port
