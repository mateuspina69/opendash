// The small reaverbot of the Nino ruin (pool 0x20 type 0x42, ST36 / ST38; exe 0x4c1180): it wanders with random turns,
// keeps apart from the other enemies, flees the player when it moves near and the player's shots; hit, it pops up
// (the player's balance -500), floats and sinks back to its home height. One routine per exe routine; the names are
// hypotheses from the observed behaviour. Render output left out: the shadow (exe 0x5f20f0).
#include "content/nino/fleeing_reaverbot.hpp"
#include "content/nino/fleeing_reaverbot_state.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/collision.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/hit_list.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/rewards.hpp"
#include "game/story.hpp"
#include "game/territory.hpp"

namespace od::port {

using namespace fleeing_reaverbot_state;

namespace {

enum class ReaverbotState : uint8_t { Init = 0, Run = 1, Dying = 2 };
enum class ReaverbotSub : uint8_t { Wander = 0, PopUp = 1 };
// the pop-up (exe 0x4c15f0 by the step)
enum class PopUpPhase : uint8_t { Start = 0, Float = 1, Sink = 2 };

constexpr uint8_t kTouchedXWall = 0x01, kTouchedZWall = 0x02, kTouchedFloor = 0x04;
constexpr uint8_t kReaverbotNoHitEntry = 0x01, kReaverbotNoDamage = 0x02, kReaverbotTurned = 0x04,
                  kReaverbotFleeing = 0x20, kReaverbotReflects = 0x40;
// the spawn parameter: its size row
struct ReaverbotParams {
    explicit ReaverbotParams(const AnimatedActor& actor) : ReaverbotParams(const_cast<AnimatedActor&>(actor)) {}
    explicit ReaverbotParams(AnimatedActor& actor) : size(actor.params[0]) {}
    FieldAlias<int8_t, uint8_t> size;
};
// a size row (exe data, 4 bytes): the scale's offsets from 0x200
struct ReaverbotSize : DataRecord {
    static constexpr uint32_t kBytes = 0x3;
    int8_t x = 0; // +0x0
    int8_t y = 0; // +0x1
    int8_t z = 0; // +0x2
    template <class R> static ReaverbotSize load(const R& r) {
        ReaverbotSize v;
        v.x = r.template read<int8_t>(0x0);
        v.y = r.template read<int8_t>(0x1);
        v.z = r.template read<int8_t>(0x2);
        return v;
    }
};
const DataTable<CollisionBoxRecord> reaverbotBox{TableId::FleeingReaverbotBox};
const DataTable<InteractionBoxRecord> reaverbotLiftBox{TableId::FleeingReaverbotLiftBox};
constexpr int kShotSlots = 16;
constexpr uint16_t kHurtSound = 0x8d;
constexpr int kBalanceLoss = -500;

// the actor collision's result -> the contact bits (exe 0x4c1af0)
void contactFromHits(const AnimatedActor& actor, ReaverbotFields& fields) {
    const uint32_t v = actor.actorHit;
    if (v == 0) return;
    const uint32_t sides = v & 0xff00u;
    if (sides & (kWallOnXPlus | kWallOnXMinus)) fields.contact |= kTouchedXWall;
    if (sides & (kWallOnZPlus | kWallOnZMinus)) fields.contact |= kTouchedZWall;
    if ((fields.mode & kReaverbotReflects) || (sides & kWallOnY)) fields.contact |= kTouchedFloor;
}
// exe 0x4c1990: the model's scale from its size row
void applySize(AnimatedActor& actor) {
    const ReaverbotSize row = dataRecord<ReaverbotSize>(stateAddress(TableId::FleeingReaverbotSizeRows) + uint32_t(int32_t(ReaverbotParams(actor).size)) * 4, ReaverbotSize::kBytes);
    actor.scale.x = int16_t(int32_t(row.x) + 0x200);
    actor.scale.y = int16_t(int32_t(row.y) + 0x200);
    actor.scale.z = int16_t(int32_t(row.z) + 0x200);
}
// exe 0x4c19e0: damage in the box -> a flash, the hurt sound
bool hurt(AnimatedActor& actor, const ReaverbotFields& fields) {
    if (!(fields.hit.result & kHitDamageMask)) return false;
    actor.tint = kTintFlash;
    soundPlayAtPos(kHurtSound, actor.pos);
    return true;
}
// exe 0x4c1a20: the walls (the yaw reflected off them in the reflecting mode) or the territory; queued as a mover
void collide(const PlayerEnv& env, AnimatedActor& actor, ReaverbotFields& fields) {
    fields.contact = 0;
    if (int32_t(actorTestMove(env, actor)) < 0) {      // exe 0x40f570
        const uint32_t r = actorPushOut(env, actor);    // exe 0x410960
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if (r & (kWallOnXPlus | kWallOnXMinus)) fields.contact |= kTouchedXWall;
        if (r & (kWallOnZPlus | kWallOnZMinus)) fields.contact |= kTouchedZWall;
        if ((fields.mode & kReaverbotReflects) && reflectRotationOffWalls(actor.rotation, r) != 0) // exe 0x410740
            fields.contact |= kTouchedFloor;
    } else {
        const int rect = territoryViolated(actor.pos); // exe 0x5e6ab0
        if (rect != 0) {
            const uint32_t r = territoryPushBack(actor, rect); // exe 0x5e6b40
            if (r & (kWallOnXPlus | kWallOnXMinus)) fields.contact |= kTouchedXWall;
            if (r & (kWallOnZPlus | kWallOnZMinus)) fields.contact |= kTouchedZWall;
        }
    }
    actorCollisionSubmitMover(actor); // exe 0x4140b0
}
// the random turn: the countdown out -> a new one 0x10 + (RNG & 0xff), the yaw +- that value, the mode & keep
void randomTurn(AnimatedActor& actor, ReaverbotFields& fields, uint8_t keep) {
    fields.ticks -= 1u;
    if (fields.ticks != 0) return;
    const uint32_t r = rngStep() & 0xff;
    fields.ticks = uint16_t(r + 0x10);
    if (r & 1) actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + r);
    else actor.rotation.y = int16_t(uint16_t(actor.rotation.y) - r);
    fields.mode &= keep;
}
// another enemy close (exe 0x5e5e00 / 0x5e5cb0): turned back, a hop when it is slower; the countdown 0x10, turned
void neighbourTurn(AnimatedActor& actor, ReaverbotFields& fields) {
    if (neighbourFind(actor, fields.neighbour, 0) == 0) return;
    const Actor& other = neighbourActor(fields.neighbour);
    actor.rotation.y = int16_t(0u - uint16_t(actor.rotation.y));
    if (other.velocity.z < actor.velocity.z) {
        actor.velocity.z = -0xe0;
        actor.acceleration.z = 0x60;
    }
    fields.ticks = 0x10;
    fields.mode |= kReaverbotTurned;
}
// a live player shot nearer than `reach` (the shot slots 15 .. 0): a dash to its side (the shot's yaw +- a quarter turn)
bool dodgeShot(AnimatedActor& actor, ReaverbotFields& fields, int reach) {
    for (int i = kShotSlots - 1; i >= 0; --i) {
        const ShotActor& shot = shotPool()[size_t(i)];
        if (shot.flags == 0) continue;
        if (planarDistance(shot.pos, actor.pos) >= reach) continue; // exe 0x5e3b00
        const int angle = angleOf(shot.pos, actor.pos) & kAngleMask; // exe 0x5e3bc0
        uint32_t yaw = uint16_t(shot.rotation.y);
        const int side = angleDiff(uint32_t(angle), yaw);
        yaw = side < 0 ? yaw - 0x400u : yaw + 0x400u;
        actor.rotation.y = int16_t(uint16_t(yaw));
        actor.velocity.z = -0x380;
        actor.acceleration.z = 0x300;
        fields.ticks = 0x10;
        return true;
    }
    return false;
}
// the dash's brake: the speed back toward 0 by 8 a tick while the brake lasts
void brake(AnimatedActor& actor) {
    if (actor.acceleration.z > 0) {
        actor.velocity.z = int16_t(uint16_t(actor.velocity.z) + 8u);
        actor.acceleration.z = int16_t(uint16_t(actor.acceleration.z) - 8u);
    }
}
// exe 0x4c13c0: random turns; the player moving near -> it flees; a shot near -> a dodge
void wander(AnimatedActor& actor, ReaverbotFields& fields) {
    if (actor.step == 0) {
        fields.mode = kReaverbotReflects;
        actor.step = 1;
        actor.anim = 0;
        fields.startKey = 0;
        actor.velocity.setXY(0, 0);
        actor.velocity.z = -0x80;
        actor.acceleration.z = 0;
        fields.ticks = 0x10;
    }
    randomTurn(actor, fields, 0xdb);
    if (!(fields.mode & kReaverbotTurned)) {
        neighbourTurn(actor, fields);
        Player& p = player();
        if (planarDistance(p.pos, actor.pos) < 0x100) {
            if (p.body.anim != 0) { // the player moves
                const int angle = angleOf(p.pos, actor.pos);
                actor.velocity.z = -0x200;
                actor.rotation.y = int16_t(-angle);
                actor.acceleration.z = 0x180;
                fields.ticks = 0x10;
                fields.mode |= kReaverbotFleeing | kReaverbotTurned;
            }
        } else if (dodgeShot(actor, fields, 0x100)) {
            fields.mode |= kReaverbotFleeing | kReaverbotTurned;
        }
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    brake(actor);
}
// exe 0x4c15f0: hit -> it pops up (the player's balance -500), floats 0x200 ticks rolling over, then sinks back to its
// home height (its hit points back)
void popUp(AnimatedActor& actor, ReaverbotFields& fields) {
    switch (PopUpPhase(actor.step)) {
    case PopUpPhase::Start:
        fields.mode = kReaverbotReflects | kReaverbotNoDamage;
        actor.step = 1;
        actor.anim = 0;
        fields.startKey = 4;
        actor.rotation.z = 0;
        actor.velocity.y = -0x80;
        fields.ticks = 0x200;
        balanceAdd(kBalanceLoss); // exe 0x5e51d0
        fields.ticks = 0x200;
        fields.leave = 0;
        [[fallthrough]];
    case PopUpPhase::Float:
        fields.ticks -= 1u;
        if (fields.ticks == 0) {
            if (fields.leave != 0) {
                actor.enterState(uint8_t(ReaverbotState::Dying));
                return;
            }
            actor.velocity.y = 0x20;
            actor.velocity.z = -0x80;
            actor.step += 1u;
            actor.acceleration.z = 0;
            fields.ticks = 0x10;
            fields.mode &= uint8_t(~kReaverbotTurned);
            return;
        }
        actor.rotation.z = int16_t(uint16_t(actor.rotation.z) + 0x10u);
        if (actor.rotation.z > 0x400) actor.rotation.z = 0x400;
        actor.prevAnim = kNoAnimShown;
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        return;
    case PopUpPhase::Sink: {
        randomTurn(actor, fields, 0xfb);
        actor.rotation.z = int16_t(uint16_t(actor.rotation.z) - 0x10u);
        if (actor.rotation.z < 0) actor.rotation.z = 0;
        if (!(fields.mode & kReaverbotTurned)) {
            neighbourTurn(actor, fields);
            Player& p = player();
            if (planarDistance(p.pos, actor.pos) < 0x100) {
                const int angle = angleOf(p.pos, actor.pos) - 0x800;
                actor.velocity.z = -0x200;
                actor.rotation.y = int16_t(angle);
                actor.acceleration.z = 0x180;
                fields.ticks = 0x10;
                fields.mode |= kReaverbotTurned;
            } else if (dodgeShot(actor, fields, 0x200)) {
                fields.mode |= kReaverbotTurned;
            }
        }
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        brake(actor);
        if (actor.pos.y.whole > fields.homeY) {
            actor.pos.y.whole = fields.homeY;
            actor.hitPoints = actor.maxHitPoints;
            actor.subState = uint8_t(ReaverbotSub::Wander);
            actor.restartStep();
        }
        return;
    }
    default: return;
    }
}
// exe 0x4c12a0: the model, its row's hit points, liftable box; registered as an enemy; its size
void reaverbotInit(AnimatedActor& actor, ReaverbotFields& fields) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);           // exe 0x5e1490
        return;
    }
    actor.anim = 0;
    actor.prevAnim = kNoAnimShown;
    actor.runFlags |= kActorRunMarked;
    actor.rotation.x = 0;
    actor.rotation.z = 0;
    const int16_t hp = hasAnimatedStats(actor.type) ? int16_t(animatedStats(actor.type).hitPoints) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.collisionBox.pointAt(reaverbotBox.stateAddress());
    actor.interactionBox.pointAt(reaverbotLiftBox.stateAddress());
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    fields.hit.previousX = actor.pos.x.whole;
    fields.hit.previousZ = actor.pos.z.whole;
    fields.hit.result = 0;
    fields.homeY = actor.pos.y.whole;
    fields.startKey = 0;
    actor.enterState(uint8_t(ReaverbotState::Run));
    registerEnemy(actor.slot, 0); // exe 0x5e5c10
    applySize(actor);
}
// exe 0x4c1370: hurt -> the pop-up
void reaverbotRun(const PlayerEnv& env, AnimatedActor& actor, ReaverbotFields& fields) {
    actor.tint = kTintNeutral;
    if (!(fields.mode & kReaverbotNoDamage) && hurt(actor, fields)) {
        actor.subState = uint8_t(ReaverbotSub::PopUp);
        actor.restartStep();
    }
    switch (ReaverbotSub(actor.subState)) { // [0x894ba0]
    case ReaverbotSub::Wander: wander(actor, fields); break;
    case ReaverbotSub::PopUp: popUp(actor, fields); break;
    default: notPortedAt(kStoryPort, "0x4c1180 sub (past the table)", actor.subState);
    }
    collide(env, actor, fields);
}
// exe 0x4c1910: one tick, then its drops and freed
void reaverbotDying(const PlayerEnv& env, AnimatedActor& actor, ReaverbotFields& fields) {
    actor.tint = kTintFlash;
    if (actor.step == 0) {
        actor.step = 1;
        fields.ticks = 1;
    }
    fields.ticks -= 1u;
    if (fields.ticks == 0) {
        spawnDrops(actor.pos, animatedStats(actor.type).dropTable); // exe 0x5f1270
        freeActor(actor);                                          // exe 0x5e1490
        return;
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    collide(env, actor, fields);
}
// exe 0x4c1180
void fleeingReaverbot(const PlayerEnv& env, AnimatedActor& actor) {
    ReaverbotFields& fields = classState<ReaverbotFields>(actor);
    contactFromHits(actor, fields);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (ReaverbotState(actor.state)) { // [0x894b94]
    case ReaverbotState::Init: reaverbotInit(actor, fields); break;
    case ReaverbotState::Run: reaverbotRun(env, actor, fields); break;
    case ReaverbotState::Dying: reaverbotDying(env, actor, fields); break;
    default: notPortedAt(kStoryPort, "0x4c1180 state (past the table)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStepPose(actor);  // exe 0x5e1e80
    else animSetPose(actor, actor.anim, fields.startKey);   // exe 0x5e1c30
    const uint8_t mode = fields.mode;
    const AnimatedActorStats row = animatedStats(actor.type);
    const ShortVec4& body = bonePoint(actor, 0, kStoryPort); // exe 0x5e1810
    fields.hit.result = 0;
    fields.hit.previousX = actor.prevPos.x.whole;
    fields.hit.previousZ = actor.prevPos.z.whole;
    if (!(mode & kReaverbotNoHitEntry)) {
        const int16_t pos[3] = {body.x, body.y, body.z};
        hitListSubmit(pos, fields.hit, hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x20),
                      uint32_t(row.attack) | kHitAttackHarmless | kHitAttackSolid |
                          ((mode & kReaverbotFleeing) ? kHitAttackThrown : 0u)); // exe 0x5e4120
    }
    // exe 0x5f20f0(a, 0x30, 0x2a0, 0): the shadow (render output)
}

} // namespace

void fleeingReaverbotUpdate(const PlayerEnv& env, uint32_t actor) { fleeingReaverbot(env, animatedActorAt(actor)); }

} // namespace od::port
