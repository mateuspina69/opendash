// ST18 (Nino island) at stories 9 / 0xa: Glyde's last attack. Glyde's ship hovers far away and launches missiles in
// bursts; the player catches them and throws them back while Roll repairs the machine (content/nino/repair_machine).
// One routine per exe routine; the names are hypotheses from the observed behaviour.
//   0x5af3d0  static stage type 0x38: Glyde's ship -- bobs, launches the missiles (its launch script 0x8cf97c)
//   0x5a7b30  static stage type 0x24: the missile -- homes on its target (variant 0 the player / 1 the centre / 2
//             around the machine), lifted / carried / thrown back (homing on the lock-on target)
//   0x4370b0  pool 0xa0 stage type 0x10: a glow sprite following an actor (the missile's flare; draw command 0x2e)
// Render output left out as in the other ports: the missile's shadow (exe 0x5f20f0).
#include "content/nino/glyde_missiles.hpp"
#include "content/nino/glyde_missiles_state.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
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
#include "game/draw_commands.hpp"
#include "game/encounter.hpp"
#include "game/explosions.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/matrix.hpp"
#include "game/missile_trail.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/table_records.hpp"

namespace od::port {

using namespace glyde_missiles_state;

namespace {

enum class GlowMode : uint8_t { Bone = 0, BoneOffset = 1, Position = 2, Rotated = 3 };
constexpr uint8_t kGlowNotAnimated = 2;

// ---- 0x5af3d0: Glyde's ship. States [0x8cf9bc]. It bobs (the vertical speed within -0x40..0x40, its step -2 / 2) and
// runs its launch script: steps of a burst's missiles, the ticks between them and the pause after the burst (negative:
// the script loops back to 0x8cf9a0). Its missiles are tracked as the enemy ids 0x18..0x1f.
enum class ShipState : uint8_t { Init = 0, Run = 1 };
// the launch (exe 0x5af4f0 by the sub-state)
enum class LaunchPhase : uint8_t { Burst = 0, Fire = 1, Gap = 2, Pause = 3, Next = 4 };
// a step of the launch script (exe data, 4 bytes)
struct LaunchStep : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    uint8_t missiles = 0; // +0x0
    uint8_t gap = 0; // +0x1: the ticks between two missiles
    int16_t pause = 0; // +0x2: & 0x7fff: the ticks after the burst; negative: the last step
    template <class R> static LaunchStep load(const R& r) {
        LaunchStep v;
        v.missiles = r.template read<uint8_t>(0x0);
        v.gap = r.template read<uint8_t>(0x1);
        v.pause = r.template read<int16_t>(0x2);
        return v;
    }
};
const DataTable<ScalarTable<uint8_t, 16>> missileVariantCycle{TableId::GlydeMissilesMissileVariantCycle};
const DataTable<CollisionBoxRecord> missileShipBox{TableId::GlydeMissilesMissileShipBox};
constexpr uint8_t kMissileType = 0x24;
constexpr uint32_t kFirstMissileId = 0x18, kMissileIds = 8;
constexpr int16_t kBobLimit = 0x40, kBobStep = 2;

// exe 0x5af430
void shipInit(StaticActor& actor, MissileShipFields& fields) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);           // exe 0x5e1540
        return;
    }
    actor.flags |= kActorScenery;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.velocity.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.spin.setXY(0, 0);
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.acceleration.y = -kBobStep;
    actor.collisionBox.pointAt(missileShipBox.stateAddress());
    actor.clearClassBlock(); // exe 0x40dc40(+0xbc, +0xc8): +0xbc..+0xcb
    fields.launch = stateAddress(TableId::GlydeMissilesLaunchScript);
    fields.nextVariant = uint8_t(rngStep() & 7);
    actor.enterState(uint8_t(ShipState::Run));
    for (uint32_t i = kMissileIds; i-- > 0;) enemyMarkDefeated(i + kFirstMissileId); // exe 0x5e63c0
}
// a missile launched from 0x100 below the ship (its variant the cycle's next, a random pitch up and yaw), tracked
void launchMissile(const StaticActor& actor, MissileShipFields& fields) {
    const uint32_t e = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (e == 0) return;
    StaticActor& missile = staticActorAt(e);
    missile.flags = kActorLive;
    missile.type = kMissileType;
    missile.subtype = 0;
    missile.loopFlags |= kActorSweepGroup2;
    missile.variant = missileVariantCycle[fields.nextVariant];
    missile.pos.x = actor.pos.x;
    missile.pos.y = int32_t(uint32_t(actor.pos.y) - 0x1000000u);
    missile.pos.z = actor.pos.z;
    missile.rotation.x = int16_t(-int32_t(rngStep() & 0xff));
    missile.rotation.y = int16_t((rngStep() & 0x1ff) + 0x301);
    const uint32_t id = uint32_t(fields.missilesLeft) + kFirstMissileId - 1;
    enemyClearDefeated(id);                             // exe 0x5e63f0
    encounterTrackInScriptSlot(missile, uint16_t(id));  // exe 0x5e6500
    fields.nextVariant = uint8_t((fields.nextVariant + 1u) & 0xfu);
}
// exe 0x5af4f0: the launch script, then the bob
void shipRun(StaticActor& actor, MissileShipFields& fields) {
    const LaunchStep step = dataRecord<LaunchStep>(fields.launch, LaunchStep::kBytes);
    switch (LaunchPhase(actor.subState)) {
    case LaunchPhase::Burst:
        fields.missilesLeft = step.missiles;
        actor.subState = uint8_t(LaunchPhase::Fire);
        [[fallthrough]];
    case LaunchPhase::Fire:
        launchMissile(actor, fields);
        fields.missilesLeft -= 1u;
        if (fields.missilesLeft == 0) {
            fields.countdown = uint16_t(uint16_t(step.pause) & 0x7fffu);
            actor.subState = uint8_t(LaunchPhase::Pause);
        } else {
            fields.countdown = step.gap;
            actor.subState = uint8_t(LaunchPhase::Gap);
        }
        break;
    case LaunchPhase::Gap:
        fields.countdown -= 1u;
        if (fields.countdown == 0) actor.subState = uint8_t(LaunchPhase::Fire);
        break;
    case LaunchPhase::Pause:
        fields.countdown -= 1u;
        if (fields.countdown == 0) actor.subState = uint8_t(LaunchPhase::Next);
        break;
    case LaunchPhase::Next:
        fields.launch = step.pause < 0 ? stateAddress(TableId::GlydeMissilesLaunchLoop) : fields.launch + LaunchStep::kBytes;
        actor.subState = uint8_t(LaunchPhase::Burst);
        break;
    default: break;
    }
    actor.velocity.y = int16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y));
    if (actor.velocity.y > kBobLimit) {
        actor.velocity.y = kBobLimit;
        actor.acceleration.y = -kBobStep;
    }
    if (actor.velocity.y < -kBobLimit) {
        actor.velocity.y = -kBobLimit;
        actor.acceleration.y = kBobStep;
    }
    actor.pos.y = int32_t(uint32_t(actor.pos.y) + (uint32_t(int32_t(actor.velocity.y)) << 12));
}
// exe 0x5af3d0
void missileShip(StaticActor& actor) {
    MissileShipFields& fields = classState<MissileShipFields>(actor);
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    switch (ShipState(actor.state)) { // [0x8cf9bc]
    case ShipState::Init: shipInit(actor, fields); break;
    case ShipState::Run: shipRun(actor, fields); break;
    default: notPortedAt(kStoryPort, "0x5af3d0 state", actor.state);
    }
    if (actor.flags & kActorLive) actor.wasOnPlatform = actor.onPlatform;
}

// ---- 0x5a7b30: the missile. States [0x8ceec4]: 0 init 0x5a7c40, 1 flying 0x5a7d60, 2 lifted (0x5a7f10: carried
// 0x5a7f30 / thrown 0x5a7fe0), 3 the blast 0x5a8060. Its stats row: the hit points, the damage, the blast's attack and
// the thrown one's damage.
enum class MissileState : uint8_t { Init = 0, Fly = 1, Lifted = 2, Blast = 3 };
enum class MissileLift : uint8_t { Carried = 0, Thrown = 1 };
constexpr uint8_t kMissileThrown = 0x01; // the player's attack
constexpr uint8_t kMissileHeld = 0x02;   // no hit entry
// the points around the machine variant 2 homes on (exe data, 8 bytes: s16 x, y, z)
const DataTable<RecordTable<ShortPoint, 4, 8>> machinePoints{TableId::GlydeMissilesMachinePoints};
const DataTable<CollisionBoxRecord> missileBox{TableId::GlydeMissilesMissileBox};
const DataTable<InteractionBoxRecord> missileLiftBox{TableId::GlydeMissilesMissileLiftBox};
constexpr uint8_t kFlareType = 0x10, kFlareVariant = 1;
constexpr uint8_t kTrailVariant = 1;
constexpr uint16_t kLaunchSound = 0x1a8;
constexpr int16_t kFlySpeed = -0x300, kThrownSpeed = -0x500;
constexpr int32_t kAboveFeet = int32_t(0xff740000u); // -0x8c << 16
constexpr int32_t kCentreHeight = int32_t(0xf2000000u);

StaticActorStats missileStats(const StaticActor& actor) { return staticStats(actor.type, 0); }

// exe 0x5a7e70: the target point (16.16) by the variant
void missileTarget(const StaticActor& actor, int32_t out[3]) {
    switch (actor.variant) {
    case 0: { // the player, 0x8c above his feet
        const Player& p = player();
        out[0] = p.pos.x;
        out[1] = int32_t(uint32_t(p.pos.y) + uint32_t(kAboveFeet));
        out[2] = p.pos.z;
        return;
    }
    case 1:
        out[0] = 0;
        out[1] = kCentreHeight;
        out[2] = 0;
        return;
    case 2: { // one of 4 points around the machine
        const ShortPoint point = machinePoints[rngStep() & 3];
        out[0] = int32_t(uint32_t(int32_t(point.x)) << 16);
        out[1] = int32_t(uint32_t(int32_t(point.y)) << 16);
        out[2] = int32_t(uint32_t(int32_t(point.z)) << 16);
        return;
    }
    default: notPortedAt(kStoryPort, "0x5a7e70 variant (the cycle 0x8cf9ac holds 0..2)", actor.variant);
    }
}
// exe 0x5a86c0: the yaw turned toward the target by the yaw step, the pitch by the pitch step
void missileSteer(StaticActor& actor, const int32_t target[3]) {
    const int32_t pos[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
    turnYawTo(actor.rotation.y, uint32_t(angleOf(target[0], target[2], pos[0], pos[2])),
              actor.spin.y); // exe 0x5e3bc0 / 0x5e3df0
    const int pitch = pitchOf(pos, target) + 0x800;                                  // exe 0x5e3ca0
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.x)), uint32_t(pitch)); // exe 0x5e3d80
    int step = actor.spin.x;
    if (uint32_t(step + d) < uint32_t(step * 2)) {
        actor.rotation.x = int16_t(pitch);
        return;
    }
    if (d <= 0) step = -step;
    actor.rotation.x = int16_t(actor.rotation.x + step);
}
// exe 0x5a80d0: the player's damage costs hit points -> true when destroyed; else any bump / solid hit
bool missileHit(StaticActor& actor, const MissileFields& fields) {
    const uint32_t v = fields.hit.result, d = v & kHitDamageMask;
    if (d != 0 && (v & kHitAttackByPlayer)) {
        actor.tint = kTintFlash;
        if (hitPointsAdd(actor, -int(d)) != 0) return true; // exe 0x5e52f0
    }
    return (v & (kHitAttackBump | kHitAttackSolid)) != 0;
}
// exe 0x5a8120: its trail (the global static type 6 variant 1) following it
void missileTrail(const StaticActor& actor, MissileFields& fields) {
    const uint32_t e = allocActor60(0); // exe 0x5e1300
    if (e == 0) return;
    StaticActor& trail = staticActorAt(e);
    trail.flags = kActorLive;
    trail.loopFlags |= kActorLateLoop;
    trail.type = typeByte(GlobalStaticType::MissileTrail);
    trail.variant = kTrailVariant;
    classState<MissileTrailFields>(staticActorAt(e)).parent = actor.at;
    fields.trail = trail.slot;
}
// exe 0x5a8060: the kill counted, a blast (the global static type 7, the row's attack), freed
void missileBlast(StaticActor& actor) {
    const StaticActorStats row = missileStats(actor);
    countDefeat();
    if (const uint32_t e = allocActor60(0)) { // exe 0x5e1300
        StaticActor& blast = staticActorAt(e);
        auto&& params = BlastParams(actorHeaderAt(e));
        blast.loopFlags |= kActorSweepGroup2;
        blast.flags = kActorLive;
        blast.type = typeByte(GlobalStaticType::Blast);
        blast.variant = 0;
        blast.pos = actor.pos;
        params.size = 0xa0;
        params.attack = row.power;
    }
    freeActor(actor); // exe 0x5e1540
}
// exe 0x5a7c40: the row's hit points, airborne, liftable; its homing delay and life by the distance to its target;
// its flare (pool 0xa0 stage type 0x10 variant 1) and its trail
void missileInit(StaticActor& actor, MissileFields& fields) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    const int16_t hp = hasStaticStats(actor.type) ? int16_t(missileStats(actor).hitPoints) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.velocity.setXY(0, 0);
    actor.rotation.z = 0;
    actor.airborne = 1;
    actor.onPlatform = 0;
    actor.collisionBox.pointAt(missileBox.stateAddress());
    actor.interactionBox.pointAt(missileLiftBox.stateAddress());
    actor.interaction = kInteractLift;
    actor.clearClassBlock(); // exe 0x40dc40(+0xbc, +0xc8): +0xbc..+0xcb
    int32_t target[3];
    missileTarget(actor, target);
    const uint32_t d16 = uint32_t(distance3(actor.pos, target)) << 4;
    const uint32_t delay = uint32_t((uint64_t(d16) * 0xaaaaaaabull) >> 32) >> 11; // (d * 16 / 1.5) >> 11
    fields.homing = uint16_t(delay);
    fields.life = uint16_t(delay + 0x12c);
    actor.enterState(uint8_t(MissileState::Fly));
    if (const uint32_t g = allocActorA0(kActorStageClass)) { // exe 0x5e13a0: the flare
        EffectActor& flare = effectActorAt(g);
        flare.flags = kActorLive;
        flare.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
        flare.type = kFlareType;
        flare.variant = kFlareVariant;
        flare.params[0] = actor.slot;
        classState<GlowFields>(flare).size = 0x20;
        flare.pos.x = 0;
        flare.pos.y = 0;
        flare.pos.z = 0x80;
    }
    missileTrail(actor, fields);
}
// exe 0x5a7d60: flies along its rotation (homing after its delay) until it hits, meets a wall or its life ends; lifted
// by the player -> state 2 (its trail freed)
void missileFly(const PlayerEnv& env, StaticActor& actor, MissileFields& fields) {
    if (actor.subState == 0) {
        fields.hitKind = 0;
        actor.spin.x = 8;
        actor.spin.y = 8;
        actor.subState = 1;
        soundPlayFlat(kLaunchSound); // exe 0x5d51f0
    }
    if (missileHit(actor, fields)) {
        missileBlast(actor);
        return;
    }
    if (fields.homing == 0) {
        int32_t target[3];
        missileTarget(actor, target);
        missileSteer(actor, target);
    } else {
        fields.homing -= 1u;
    }
    moveAlongRotation(actor.pos, actor.rotation, 0, 0, kFlySpeed); // exe 0x5e37b0
    if (int32_t(actorTestMove(env, actor)) < 0) {                  // exe 0x40f570
        missileBlast(actor);
        return;
    }
    fields.life -= 1u;
    if (fields.life == 0) {
        missileBlast(actor);
        return;
    }
    if (!playerLiftTargetCheck(actor, 0)) return; // exe 0x573a60
    actor.enterState(uint8_t(MissileState::Lifted));
    fields.hitKind |= kMissileHeld;
    if (fields.trail != 0) {
        const uint32_t trailAt = staticPoolSlot(fields.trail);
        if (staticActorAt(trailAt).flags & kActorLive) {
            freeActor(staticActorAt(trailAt));
            fields.trail = 0;
        }
    }
}
// exe 0x5a7fe0: homes on its target (while it lives) at 0x500 a tick; blasts on a hit, a wall or the end of its life
void missileThrown(const PlayerEnv& env, StaticActor& actor, MissileFields& fields) {
    if (!missileHit(actor, fields)) {
        if (fields.target != 0) {
            const uint32_t t = actorBySlot(fields.target); // exe 0x5e0fa0
            if (actorHeaderAt(t).flags & kActorLive) {
                const Actor& target = actorLayoutAt(t);
                const int32_t point[3] = {target.pos.x, target.pos.y, target.pos.z};
                missileSteer(actor, point);
            } else {
                fields.target = 0;
            }
        }
        moveAlongRotation(actor.pos, actor.rotation, 0, 0, kThrownSpeed);
        if (int32_t(actorTestMove(env, actor)) >= 0) {
            fields.homing -= 1u;
            if (fields.homing != 0) return;
        }
    }
    missileBlast(actor);
}
// exe 0x5a7f30: at the player's hands; thrown -> the player's attack aimed at his lock-on target; dropped -> its own
void missileCarried(const PlayerEnv& env, StaticActor& actor, MissileFields& fields) {
    if (actor.step == 0) {
        fields.hitKind = kMissileHeld;
        fields.target = 0;
        actor.rotation.x = 0;
        actor.enterStep(1);
    }
    carryAtHands(actor, 0, -0x10, 0);             // exe 0x573ab0
    const CarryRelease release = carryReleased(); // exe 0x573950
    if (release == CarryRelease::Held) return;
    Player& p = player();
    if (release == CarryRelease::Thrown) {
        fields.hitKind = kMissileThrown;
        if (p.lockedActor) fields.target = p.lockedActor->slot;
        actor.spin.x = 0x40;
        actor.spin.y = 0x40;
        actor.rotation.x = p.rotation.x;
    } else {
        fields.hitKind = 0;
        actor.rotation.x = -0x400;
    }
    actor.airborne = 1;
    actor.rotation.y = p.rotation.y;
    fields.homing = 0x3c;
    carryDropFromPlayer(actor); // exe 0x573990
    missileTrail(actor, fields);
    actor.subState = uint8_t(MissileLift::Thrown);
    actor.restartStep();
    missileThrown(env, actor, fields);
}
// exe 0x5a7b30
void glydeMissile(const PlayerEnv& env, StaticActor& actor) {
    MissileFields& fields = classState<MissileFields>(actor);
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (MissileState(actor.state)) { // [0x8ceec4]
    case MissileState::Init: missileInit(actor, fields); break;
    case MissileState::Fly: missileFly(env, actor, fields); break;
    case MissileState::Lifted: // exe 0x5a7f10
        if (MissileLift(actor.subState) == MissileLift::Carried) missileCarried(env, actor, fields);
        else missileThrown(env, actor, fields);
        break;
    case MissileState::Blast: missileBlast(actor); break;
    default: notPortedAt(kStoryPort, "0x5a7b30 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    const StaticActorStats row = missileStats(actor);
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    fields.hit.result = 0;
    fields.hit.previousX = actor.prevPos.x.whole;
    fields.hit.previousZ = actor.prevPos.z.whole;
    const uint8_t kind = fields.hitKind;
    if (!(kind & kMissileHeld)) {
        const uint32_t owner = hitOwner(actor.slot);
        if (!(kind & kMissileThrown))
            hitListSubmit(pos, fields.hit, owner | hitGroup(kHitGroupBodies) | hitRadius(0x40),
                          uint32_t(row.damage) | kHitAttackThrown | kHitAttackSolid); // exe 0x5e4120
        else
            hitListSubmit(pos, fields.hit, owner | hitGroup(kHitGroupPlayerAttacks) | kHitNoLockOn | hitRadius(0x40),
                          uint32_t(row.deathAttack) | kHitAttackByPlayer);
    }
    // the shadow while not lifted (exe 0x5f20f0(a, 0x20, 0x100, 0)): render output
}

// ---- 0x4370b0: a glow sprite on an actor (its slot in the spawn's first parameter; the missile's flare). The first
// state keeps its start offset and finds the actor; the mode (the variant, | 2 when the actor is not in pool 0x20): 0
// at the actor's bone, 1 the bone with the offset (exe 0x5e1970), 2 the actor's position, 3 the offset rotated by the
// actor's rotation (exe 0x5e3970). Freed with its actor. Draw command 0x2e (10 words), its angle turning 0x20 a tick.

// exe 0x4370b0
void actorGlow(EffectActor& actor) {
    GlowFields& glow = classState<GlowFields>(actor);
    if (actor.state == 0) {
        actor.runFlags |= kActorWarpKept;
        glow.clearOwn(); // exe 0x40dc40(+0x18, +0x24)
        glow.followed = slotByIdentityWord<ActorHeader>(actorBySlot(uint32_t(int32_t(glow.slot))));
        const uint32_t cls = actorSlotClass(uint8_t(glow.slot)); // exe 0x5e0f70
        glow.mode = uint8_t((cls != kAnimatedClass ? kGlowNotAnimated : 0u) | actor.variant);
        actor.state = 1;
        glow.offset = glow.start();
    }
    ActorHeader& t = *glow.followed;
    if (!(t.flags & kActorLive) || glow.slot == 0) {
        freeActor(actor); // exe 0x5e1580
        return;
    }
    int16_t p[3] = {0, 0, 0};
    switch (GlowMode(glow.mode)) {
    case GlowMode::Bone: {
        const ShortVec4& point = bonePoint(animatedPool().of(t), uint32_t(int32_t(glow.bone)), kStoryPort);
        p[0] = point.x;
        p[1] = point.y;
        p[2] = point.z;
        break;
    }
    case GlowMode::BoneOffset: { // exe 0x43715e: the offset copied to the stack (exe 0x5e1970 shifts the copy)
        const int16_t v[3] = {glow.offset.x, glow.offset.y, glow.offset.z};
        bonePointOffset(animatedPool().of(t), uint32_t(int32_t(glow.bone)), v, p);
        break;
    }
    case GlowMode::Position: {
        const Actor& followed = actorLayoutOf(t);
        p[0] = followed.pos.x.whole;
        p[1] = followed.pos.y.whole;
        p[2] = followed.pos.z.whole;
        break;
    }
    case GlowMode::Rotated: {
        const Actor& followed = actorLayoutOf(t);
        int32_t o[3];
        rotateVector(followed.rotation, int(glow.offset.x) << 4, int(glow.offset.y) << 4, int(glow.offset.z) << 4,
                     o); // exe 0x5e3970
        p[0] = int16_t(int16_t(uint16_t(uint32_t(o[0]) >> 16)) + followed.pos.x.whole);
        p[1] = int16_t(int16_t(uint16_t(uint32_t(o[1]) >> 16)) + followed.pos.y.whole);
        p[2] = int16_t(int16_t(uint16_t(uint32_t(o[2]) >> 16)) + followed.pos.z.whole);
        break;
    }
    default: break;
    }
    const uint32_t ang = uint32_t(int32_t(glow.angle));
    const uint32_t ang2 = ang + 0x1f;
    dc::emit({0, 0x2e808080u, (uint32_t(int32_t(p[1])) << 16) | uint16_t(p[0]), ang | 0x7cd24000u,
              uint32_t(uint16_t(p[2])), ang2 | 0x2f4000u, uint32_t(int32_t(glow.size)), ang | 0x5f00u, 0u,
              ang2 | 0x5f00u},
             actor.at, false);
    glow.angle = int16_t((uint8_t(glow.angle) + 0x20u) & 0xffu);
}

} // namespace

void missileShipUpdate(const PlayerEnv&, uint32_t actor) { missileShip(staticActorAt(actor)); }
void glydeMissileUpdate(const PlayerEnv& env, uint32_t actor) { glydeMissile(env, staticActorAt(actor)); }
void actorGlowUpdate(const PlayerEnv&, uint32_t actor) { actorGlow(effectActorAt(actor)); }

} // namespace od::port
