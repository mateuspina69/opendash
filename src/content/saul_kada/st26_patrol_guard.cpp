// The ST26 lane guard (pool 0x20 type 0x3b sub 1, exe 0x4b4b00; hypothesis names). States [0x894614]: 0 bind 0x4b4bb0,
// 1 run 0x4b4cc0 (its hits while hittable, its sub-state [0x894620], the collision, a step sound on its animation's
// events), 2 freed 0x468fa0. Sub-states: 0 wait 0x4b4d20, 1 a patrol leg 0x4b4dc0, 2 turn round 0x4b4f90, 3 chase
// 0x4b5080, 4 turn off a wall 0x4b5140, 5 give way to a neighbour 0x4b5190, 6 knocked back 0x4b5270, 7 dying 0x4b53b0.
// Its lane: the axis of its patrol heading through its home; its range 0x1400 (variant != 0: 0xb80). Its row:
// animatedStats(type, spawn byte). The shadow is render output.
// Its home points (exe 0x4b4d20 / 0x4b4dc0 / 0x4b4f90) are 16.16 points with only their high words written (the low
// words are stack garbage in the exe, 0 here; exe 0x5e3b00 only reads the high words, exe 0x5e3bc0 in 0x4b4dc0 subtracts
// whole dwords, so its angle may differ in its last bit).
#include "content/saul_kada/st26_patrol_guard.hpp"
#include "content/saul_kada/st26_patrol_guard_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/collision.hpp"
#include "game/distances.hpp"
#include "game/enemy_motion.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/explosions.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st26_patrol_guard_state;

namespace {

enum class GuardState : uint8_t { Bind = 0, Run = 1, Free = 2 };
enum class GuardSub : uint8_t {
    Wait = 0, Leg = 1, TurnRound = 2, Chase = 3, WallTurn = 4, GiveWay = 5, Knocked = 6, Dying = 7
};
enum class GuardAnim : uint8_t {
    Idle = 0, Wake = 1, Walk = 2, Run = 3, KnockedBack = 0x13, Land = 0x14, GetUp = 0x15, TurnStart = 0x17,
    Turn = 0x18, TurnEnd = 0x19
};
// where the player is against its lane (exe 0x4b56a0)
enum Lane : int { InLane = 0, BeyondRange = 1, BehindPlusX = 2, BehindMinusX = 3, BehindPlusZ = 4, BehindMinusZ = 5 };
constexpr uint8_t kGuardDead = 0x01, kGuardHittable = 0x02, kGuardAwake = 0x04;
constexpr uint8_t kModelVariant = 2;
constexpr uint16_t kRange = 0x1400, kShortRange = 0xb80;
constexpr uint16_t kHurtSound = 0x8d, kHeavyHurtSound = 0x8e, kWakeSound = 0x16e, kKnockSound = 0x16f,
                   kLandSound = 0x16d, kStepSound = 0x16b;

AnimatedActorStats guardStats(const AnimatedActor& actor) { return animatedStats(actor.type, actor.spawnByte); }
void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
void setAnim(AnimatedActor& actor, GuardAnim anim) { actor.anim = uint8_t(anim); }
void enterSub(AnimatedActor& actor, GuardSub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}
// a home point {x, 0, z} (16.16, the high words)
void homePoint(uint16_t x, uint16_t z, int32_t out[3]) {
    out[0] = int32_t(uint32_t(x) << 16);
    out[1] = 0;
    out[2] = int32_t(uint32_t(z) << 16);
}
int playerFromHome(const GuardFields& guard) {
    int32_t home[3];
    homePoint(guard.homeX, guard.homeZ, home);
    return planarDistance(player().pos, home);
}
// exe 0x4b56a0: where the player is along its patrol heading seen from its home: beyond its range ahead, behind its
// home by more than 0x200 (by the heading's side), or in its lane
int laneOf(const AnimatedActor& actor, const GuardFields& guard) {
    const int r = actor.variant != 0 ? 0xc00 : 0x1600;
    const Player& p = player();
    const int px = p.pos.x.whole, pz = p.pos.z.whole;
    const int hx = int16_t(guard.homeX), hz = int16_t(guard.homeZ);
    switch (guard.heading) {
    case 0x400:
        if (px < hx - r) return BeyondRange;
        return px <= hx + 0x200 ? InLane : BehindPlusX;
    case 0xc00:
        if (px > r + hx) return BeyondRange;
        return px >= hx - 0x200 ? InLane : BehindMinusX;
    case 0:
        if (pz < hz - r) return BeyondRange;
        return pz <= hz + 0x200 ? InLane : BehindPlusZ;
    case 0x800:
        if (pz > r + hz) return BeyondRange;
        return pz >= hz - 0x200 ? InLane : BehindMinusZ;
    default: return InLane;
    }
}
// exe 0x4b5550: its damage; dead -> dying; a heavy hit (the row's threshold or more) -> knocked back along the hit
void takeHit(AnimatedActor& actor, GuardFields& guard) {
    const uint32_t hit = guard.hitBox.result, damage = hit & kHitDamageMask;
    if (!(hit & kHitAttackByPlayer)) return;
    actor.tint = kTintFlash;
    if (hitPointsAdd(actor, -int(damage))) { // exe 0x5e52f0
        if (guard.flags & kGuardDead) return;
        enterSub(actor, GuardSub::Dying);
        guard.flags |= kGuardDead;
        return;
    }
    if (int(damage) < int(guardStats(actor).bodyAttack)) {
        sound(kHurtSound, actor);
        return;
    }
    sound(kHeavyHurtSound, actor);
    guard.knockYaw = uint16_t(guard.hitBox.contact.x);
    enterSub(actor, GuardSub::Knocked);
}
// exe 0x4b5620: the walls; the floor (airborne: only once landed)
void collide(const PlayerEnv& env, AnimatedActor& actor, GuardFields& guard) {
    uint32_t r = 0;
    guard.wallHit = 0;
    if (int32_t(actorTestMove(env, actor)) < 0) { // exe 0x40f570
        r = actorPushOut(env, actor);              // exe 0x410960
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if (r & kWallOnSides) guard.wallHit = 1;
    }
    const int g = actorGround(env, actor); // exe 0x40f190
    if (actor.airborne != 0) {
        if (!(r & kWallOnY)) return;
        actor.airborne = 0;
    }
    actor.pos.y.whole = int16_t(g);
}
// exe 0x4b5430: its hit capsule 0x40 to each side of its yaw, 0x60 up (dead: harmless)
void submitCapsule(const AnimatedActor& actor, GuardFields& guard) {
    guard.hitBox.previousX = actor.prevPos.x.whole;
    guard.hitBox.previousZ = actor.prevPos.z.whole;
    guard.hitBox.result = 0;
    uint32_t immunity, kinds, compare;
    uint16_t damage;
    if (guard.dead == 0) {
        damage = guardStats(actor).attack;
        immunity = hitImmunity(kHitGroupPlayer);
        kinds = kHitAttackThrown | kHitAttackSolid;
        compare = kHitCompareHeights;
    } else {
        damage = 0;
        immunity = kHitImmunityMask;
        kinds = kHitAttackBump;
        compare = 0;
    }
    int16_t dx, dz;
    angleVector(uint16_t(actor.rotation.y), 0x40, dx, dz); // exe 0x5e40c0
    const int16_t a[3] = {int16_t(dx + actor.pos.x.whole), int16_t(actor.pos.y.whole - 0x60),
                          int16_t(dz + actor.pos.z.whole)};
    angleVector(uint16_t(actor.rotation.y), -0x40, dx, dz);
    const int16_t b[3] = {int16_t(dx + actor.pos.x.whole), int16_t(actor.pos.y.whole - 0x60),
                          int16_t(dz + actor.pos.z.whole)};
    hitListSubmitCapsule(a, 0xc, b, 0xc, guard.hitBox,
                         hitOwner(actor.slot) | compare | hitGroup(kHitGroupBodies) | kHitContactMotion | hitCapsule(1),
                         uint32_t(int32_t(int16_t(damage))) | immunity | kinds); // exe 0x5e4190
}
// the charge's start (the run animation, its sound)
void startChase(AnimatedActor& actor) {
    actor.subState = uint8_t(GuardSub::Chase);
    setAnim(actor, GuardAnim::Run);
    sound(kWakeSound, actor);
    actor.restartStep();
}
// exe 0x4b4d20: waiting until the player is within its range of its home, waking -> the chase
void wait(AnimatedActor& actor, const GuardFields& guard) {
    switch (actor.step) {
    case 0:
        if (playerFromHome(guard) < int(guard.range.get())) {
            setAnim(actor, GuardAnim::Idle);
            ++actor.step;
        }
        return;
    case 1:
        if (!actor.animEnded()) return;
        startChase(actor);
        return;
    default: return;
    }
}
// exe 0x4b4dc0: a patrol leg: the player in its lane -> the chase; else 0x5a ticks, then walking to the far end of the
// leg (by the player's side), the leg's end moved, turned round
void leg(AnimatedActor& actor, GuardFields& guard) {
    switch (actor.step) {
    case 0:
        setAnim(actor, GuardAnim::Walk);
        actor.step = 1;
        guard.count = 0;
        return;
    case 1:
        if (laneOf(actor, guard) == InLane) {
            setAnim(actor, GuardAnim::Run);
            guard.count = 0;
            enterSub(actor, GuardSub::Chase);
            return;
        }
        guard.count += 1u;
        if (guard.countSigned <= 0x5a) return;
        setAnim(actor, GuardAnim::Run);
        ++actor.step;
        return;
    case 2: {
        if (actor.stepCounter == 0) {
            guard.leg = uint8_t(laneOf(actor, guard));
            ++actor.stepCounter;
        }
        const uint8_t side = guard.leg;
        uint16_t x = guard.homeX, z = guard.homeZ;
        const uint16_t e = guard.range;
        if (side == BehindPlusX) x = uint16_t(x - e);
        else if (side == BehindMinusX) x = uint16_t(x + e);
        else if (side == BehindPlusZ) z = uint16_t(z - e);
        else if (side == BehindMinusZ) z = uint16_t(z + e);
        int32_t target[3];
        homePoint(x, z, target);
        const int angle = int16_t(angleOf(target[0], target[2], actor.pos.x, actor.pos.z));
        actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) +
                                            uint16_t(angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angle)))));
        actor.velocity.z = int16_t(0xfc00);
        moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, -0x400); // exe 0x5e36f0
        if (planarDistance(actor.pos, target) >= 0x400) return;
        if (side == BehindPlusX) guard.homeX -= e;
        else if (side == BehindMinusX) guard.homeX += e;
        else if (side == BehindPlusZ) guard.homeZ -= e;
        else if (side == BehindMinusZ) guard.homeZ += e;
        if (side != BeyondRange) guard.heading = uint16_t((guard.heading - 0x800u) & 0xfffu);
        actor.subState = uint8_t(GuardSub::TurnRound);
        actor.rotation.y = int16_t(guard.heading);
        actor.restartStep();
        return;
    }
    default: return;
    }
}
// exe 0x4b4f90 (its jump table 0x4b5064): turning round, then waiting for the player within its range -> the chase
void turnRound(AnimatedActor& actor, const GuardFields& guard) {
    switch (actor.step) {
    case 0:
        setAnim(actor, GuardAnim::TurnStart);
        ++actor.step;
        return;
    case 1:
        if (!actor.animEnded()) return;
        setAnim(actor, GuardAnim::Turn);
        ++actor.step;
        return;
    case 2:
        if (playerFromHome(guard) >= int(guard.range.get())) return;
        setAnim(actor, GuardAnim::TurnEnd);
        ++actor.step;
        return;
    case 3:
        if (!actor.animEnded()) return;
        startChase(actor);
        return;
    default: return;
    }
}
// exe 0x4b5080: the chase in its lane (turning 0x20 a tick); a wall -> turning off it; a neighbour -> giving way
void chase(AnimatedActor& actor, GuardFields& guard) {
    if (laneOf(actor, guard) != InLane) {
        enterSub(actor, GuardSub::Leg);
        return;
    }
    const uint16_t angle = uint16_t(angleOf(player().pos, actor.pos));
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(int32_t(int16_t(angle)))); // 0x5e3d80
    if (d >= 0x20) actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + 0x20u));
    else if (d <= -0x20) actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) - 0x20u));
    else actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + uint16_t(d)));
    actor.velocity.z = int16_t(0xfc00);
    moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, -0x400); // exe 0x5e36f0
    if (guard.wallHit & 1) {
        guard.wallYaw = angle;
        enterSub(actor, GuardSub::WallTurn);
        return;
    }
    if (neighbourFind(actor, guard.neighbour, 0) != 0) enterSub(actor, GuardSub::GiveWay); // exe 0x5e5e00
}
// exe 0x4b5140: turning 0x20 a tick to the wall's yaw, then the chase
void wallTurn(AnimatedActor& actor, GuardFields& guard) {
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(int32_t(int16_t(guard.wallYaw))));
    if ((d < 0 ? -d : d) > 0x20) {
        actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + (d >= 0x20 ? 0x20u : 0xffe0u)));
        return;
    }
    guard.wallYaw = 0;
    enterSub(actor, GuardSub::Chase);
}
// exe 0x4b5190: giving way to its neighbour for 0x1e ticks (the word's bits read before exe 0x5e5f70 reclassifies
// them): turned away from it, slowed by its kind
void giveWay(AnimatedActor& actor, GuardFields& guard) {
    const Actor& other = neighbourActor(guard.neighbour); // exe 0x5e5cb0
    const uint16_t bits = guard.neighbour;
    if (actor.step == 0) {
        neighbourClassify(actor, guard.neighbour); // exe 0x5e5f70
        actor.step = 1;
        guard.count = 0x1e;
    }
    if (bits & kNeighbourOpposite)
        turnYawTo(actor.rotation.y, uint32_t(headingFromOther(actor, other, neighbourSide(guard.neighbour), 0x200)),
                  0x20); // exe 0x5e5d90
    if ((bits & kNeighbourCrossingSlow) && stepTowardLimit(actor.velocity.z, 0xfff0, 0x440) != 0)
        actor.velocity.z = int16_t(0xfbc0);
    if ((bits & kNeighbourCatching) && stepTowardLimit(actor.velocity.z, 0xfff0, 0x300) != 0)
        actor.velocity.z = int16_t(0xfd00);
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    guard.count -= 1u;
    if (guard.count != 0 && other.flags != 0) return;
    guard.count = 0;
    enterSub(actor, GuardSub::Chase);
}
// exe 0x4b5270 (its jump table 0x4b539c): knocked back along the hit, landing, getting up -> the chase
void knocked(AnimatedActor& actor, GuardFields& guard) {
    switch (actor.step) {
    case 0:
        setAnim(actor, GuardAnim::KnockedBack);
        sound(kKnockSound, actor);
        guard.flags &= uint8_t(~(kGuardHittable | kGuardAwake));
        guard.count = 2;
        ++actor.step;
        return;
    case 1:
        guard.count -= 1u;
        if (guard.count != 0) return;
        actor.velocity.y = int16_t(0xfe20);
        actor.velocity.z = 0x100;
        actor.acceleration.y = 0x40;
        actor.rotation.y = int16_t(uint16_t(guard.knockYaw + 0x800u));
        actor.airborne = 1;
        ++actor.step;
        integrateVelocity(actor, 0, -0x1e0, 0x100);
        return;
    case 2:
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
        if (actor.airborne != 0) return;
        setAnim(actor, GuardAnim::Land);
        sound(kLandSound, actor);
        ++actor.step;
        return;
    case 3:
        if (!actor.animEnded()) return;
        setAnim(actor, GuardAnim::GetUp);
        ++actor.step;
        return;
    case 4:
        if (!actor.animEnded()) return;
        guard.flags |= kGuardHittable | kGuardAwake;
        setAnim(actor, GuardAnim::Run);
        enterSub(actor, GuardSub::Chase);
        return;
    default: return;
    }
}
// exe 0x4b53b0: a defeat counted, hidden, an explosion (0x47a480), the drops; 0xf ticks later freed
void dying(AnimatedActor& actor, GuardFields& guard) {
    switch (actor.step) {
    case 0:
        countDefeat(); // exe 0x5e53e0
        actor.setDrawn(false);
        spawnDeathExplosionAt(actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole) - 0x60u), actor.pos.z.whole, 0x30,
                              0x3c);                               // exe 0x47a480
        spawnDrops(actor.pos, guardStats(actor).dropTable);         // exe 0x5f1270
        guard.dead = 1;
        guard.count = 0xf;
        ++actor.step;
        return;
    case 1:
        guard.count -= 1u;
        if (guard.count != 0) return;
        actor.state = uint8_t(GuardState::Free);
        actor.subState = 0;
        actor.restartStep();
        return;
    default: return;
    }
}
// exe 0x4b4bb0: its model (variant 2), hit points, its box, its home and heading, its range, hittable, registered
void bind(AnimatedActor& actor, GuardFields& guard) {
    if (!bindActorModel(actor, kModelVariant)) { // exe 0x5e0bc0(actor, &2)
        freeActor(actor);                        // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    setAnim(actor, GuardAnim::Wake);
    actor.prevAnim = 0xff;
    const uint16_t hp = hasAnimatedStats(actor.type) ? uint16_t(guardStats(actor).hitPoints) : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.velocity.setXY(0, 0);
    actor.velocity.setZSpare(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(TableId::St26PatrolGuardBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    guard.homeX = uint16_t(actor.pos.x.whole);
    guard.homeZ = uint16_t(actor.pos.z.whole);
    guard.heading = uint16_t(actor.rotation.y);
    guard.leg = 0;
    guard.knockYaw = 0;
    guard.dead = 0;
    guard.count = 0;
    guard.range = actor.variant == 0 ? kRange : kShortRange;
    guard.flags |= kGuardHittable | kGuardAwake;
    registerEnemy(actor.slot, 0); // exe 0x5e5c10
    actor.subState = uint8_t(GuardSub::Wait);
    actor.restartStep();
    actor.state = uint8_t(GuardState::Run);
}
// exe 0x4b4cc0
void run(const PlayerEnv& env, AnimatedActor& actor, GuardFields& guard) {
    if (actor.hitPoints >= 0 && (guard.flags & kGuardHittable)) takeHit(actor, guard);
    switch (GuardSub(actor.subState)) { // [0x894620]
    case GuardSub::Wait: wait(actor, guard); break;
    case GuardSub::Leg: leg(actor, guard); break;
    case GuardSub::TurnRound: turnRound(actor, guard); break;
    case GuardSub::Chase: chase(actor, guard); break;
    case GuardSub::WallTurn: wallTurn(actor, guard); break;
    case GuardSub::GiveWay: giveWay(actor, guard); break;
    case GuardSub::Knocked: knocked(actor, guard); break;
    case GuardSub::Dying: dying(actor, guard); break;
    default: notPortedAt(kStoryPort, "0x4b4cc0: sub-state past [0x894620]", actor.subState);
    }
    collide(env, actor, guard);
    if (actor.animKey.events != 0) sound(kStepSound, actor);
}
void patrolGuard(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x4b4b00
    GuardFields& guard = classState<GuardFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (GuardState(actor.state)) { // [0x894614]
    case GuardState::Bind: bind(actor, guard); break;
    case GuardState::Run: run(env, actor, guard); break;
    case GuardState::Free: freeActor(actor); break; // exe 0x468fa0
    default: notPortedAt(kStoryPort, "0x4b4b00: state past [0x894614]", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    else animStepPose(actor);                                            // exe 0x5e1e80
    submitCapsule(actor, guard);
    // drawn: the shadow (exe 0x5f20f0(actor, 0x80, 0xc0, 0)) -- render output
}

} // namespace

void patrolGuardUpdate(const PlayerEnv& env, uint32_t actor) { patrolGuard(env, animatedActorAt(actor)); }

} // namespace od::port
