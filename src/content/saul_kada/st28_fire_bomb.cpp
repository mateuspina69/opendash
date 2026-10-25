// The lava boss's fire bombs (ST28 pool 0x40 type 0x17, exe 0x5c86e0; hypothesis names), by variant [0x8d13dc]:
//   0 homing (0x5c8700, states [0x8d13e8]: bind 0x5c8720, live 0x5c87a0, freed): it homes on the player's chest for
//     200 ticks (0x5c8ef0), liftable and throwable, 80 hit points; it dies in a blast and lets the boss throw again;
//   1 the invisible centre (0x5c8b30, [0x8d1408]): homes as variant 0, spins, carries its two orbiting bombs 0x400 above
//     / below it turned by its angles, a hum every 10 ticks, the ribbon between them;
//   2 an orbiting bomb (0x5c8eb0): bound, moved and freed by its centre.
#include "content/saul_kada/st28_fire_bomb.hpp"
#include "content/saul_kada/st28_fire_bomb_state.hpp"

#include "content/saul_kada/st28_bomb_ribbon.hpp"
#include "content/saul_kada/st28_lava_boss.hpp"

#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/collision.hpp"
#include "game/distances.hpp"
#include "game/enemy_motion.hpp"
#include "game/explosions.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st28_fire_bomb_state;

namespace {

constexpr uint8_t kFireBombType = 0x17;
enum class BombState : uint8_t { Bind = 0, Live = 1, Free = 2 };
enum class HomingSub : uint8_t { Home = 0, Lifted = 1, Die = 2 };
enum class LiftedStep : uint8_t { Carried = 0, Thrown = 1 };
enum class CentreSub : uint8_t { Move = 0, Die = 1 };
constexpr uint8_t kBombLifted = 0x01, kBombAirborne = 0x02;
constexpr uint8_t kFellOnWall = 0x01, kFellOffLedge = 0x02, kFellLanded = 0x04;
constexpr int16_t kBombHitPoints = 0x50, kHomingTicks = 200;
constexpr uint16_t kBombSound = 0x224, kPairSound = 0x225, kHumSound = 0x22b;
constexpr uint32_t kCentreAttackKind = 0x4000; // an attack kind bit of the centre's entry (its reader is not traced)

void savePose(ProjectileActor& actor) {
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
}
// exe 0x5e1280 + the spawn of 0x5c9160: a bomb at the point with the boss's angles
uint32_t placeBomb(const AnimatedActor& boss, const int16_t point[3], FireBombVariant variant) {
    const uint32_t slot = allocActor40(kActorStageClass);
    if (!slot) return 0;
    ProjectileActor& bomb = projectileActorAt(slot);
    bomb.flags = kActorLive;
    bomb.type = kFireBombType;
    bomb.loopFlags |= kActorSweepGroup2;
    bomb.pos.x = int32_t(uint32_t(int32_t(point[0])) << 16);
    bomb.pos.y = int32_t(uint32_t(int32_t(point[1])) << 16);
    bomb.pos.z = int32_t(uint32_t(int32_t(point[2])) << 16);
    bomb.rotation.x = boss.rotation.x;
    bomb.rotation.y = boss.rotation.y;
    bomb.rotation.z = boss.rotation.z;
    classState<BombFields>(bomb).damage = int16_t(animatedStats(boss.type, boss.variant).strongAttack);
    bomb.variant = uint8_t(variant);
    return slot;
}
// its boss may throw again (when it is still the live lava boss)
void letBossThrowAgain(const BombFields& bomb) {
    const uint32_t boss = slotIdentityWord(bomb.boss);
    if (!boss) notPorted(kStoryPort, "0x5c8ac0 / 0x5c87a0: a bomb without a boss (the exe reads [0 + 4])");
    const AnimatedActor& b = animatedActorAt(boss);
    if (b.type == kLavaBossType && b.flags != 0) LavaBossBombOut(animatedActorAt(boss)).bombOut = 0;
}
void chestTarget(int32_t out[3]) { // the player's position 0x80 up (a stack copy in the exe)
    const Player& p = player();
    out[0] = p.pos.x;
    out[1] = int32_t(uint32_t(p.pos.y) - 0x800000u);
    out[2] = p.pos.z;
}
void swapTravelYaw(ProjectileActor& actor, BombFields& bomb) {
    const int16_t yaw = actor.rotation.y;
    actor.rotation.y = bomb.travelYaw;
    bomb.travelYaw = yaw;
}
// exe 0x5c8ef0(bomb, target): homing -- facing the player: its yaw / pitch to the target, faster (to -0x400); else
// the swerve (its travel yaw turning at a growing rate); then forward along its angles
void home(ProjectileActor& actor, BombFields& bomb, const int32_t target[3]) {
    const auto pitchStep = [&]() {
        const int32_t pos[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
        const int16_t d = int16_t(uint16_t(0x800 - pitchOf(target, pos) - int(uint16_t(actor.rotation.x))));
        actor.rotation.x = int16_t(uint16_t(uint16_t(actor.rotation.x) + (d > 0 ? 0x10u : 0xfff0u)));
    };
    const uint32_t bearing = uint32_t(angleOf(target[0], target[2], actor.pos.x, actor.pos.z));
    if (facingWithin(actor, player().pos, 0x20)) {
        if (planarDistance(player().pos, actor.pos) > 0x200) {
            turnYawTo(actor.rotation.y, bearing, 0x20);
            bomb.travelYaw = actor.rotation.y;
            pitchStep();
            actor.stepCounter = 0;
            bomb.turnRate = 0;
            if (actor.velocity.z < -0x400) actor.velocity.z = int16_t(0xfc00);
            else actor.velocity.z = int16_t(uint16_t(uint16_t(actor.velocity.z) - 0x80u));
        }
    } else {
        if (actor.stepCounter == 0) {
            actor.acceleration.z = 0x10;
            bomb.travelYaw = actor.rotation.y;
            actor.stepCounter = 1;
        }
        if (actor.velocity.z > -0x200) actor.stepCounter = 0;
        else actor.velocity.z = int16_t(uint16_t(uint16_t(actor.acceleration.z) + uint16_t(actor.velocity.z)));
        swapTravelYaw(actor, bomb);
        turnYawTo(actor.rotation.y, uint32_t(angleOf(target[0], target[2], actor.pos.x, actor.pos.z)), 0x20);
        const int16_t kept = actor.rotation.y;
        actor.rotation.y = bomb.travelYaw;
        turnYawTo(actor.rotation.y, uint32_t(angleOf(target[0], target[2], actor.pos.x, actor.pos.z)),
                  int((bomb.turnRate >> 4) + 0x20));
        bomb.turnRate += 8u;
        if (bomb.turnRate > 0x200) bomb.turnRate = 0x200;
        bomb.travelYaw = actor.rotation.y;
        actor.rotation.y = kept;
        pitchStep();
    }
    moveAlongRotation(actor.pos, actor.rotation, 0, 0, actor.velocity.z); // exe 0x5e3810
    swapTravelYaw(actor, bomb);
}
// exe 0x5c90b0: the move resolved; its fall's result
int32_t resolveFall(const PlayerEnv& env, ProjectileActor& actor, BombFields& bomb) {
    int32_t r = 0;
    if (int32_t(actorTestMove(env, actor)) < 0) { // exe 0x40f570
        r = int32_t(actorPushOut(env, actor));     // exe 0x410960
        if (r < 0) actor.pos = actor.prevPos;
        if (r & kWallOnSides) bomb.fallResult |= kFellOnWall;
    }
    const int g = actorGround(env, actor); // exe 0x40f190
    if (!(bomb.status & kBombAirborne)) {
        if (g - actor.pos.y.whole > 0x20) {
            bomb.fallResult |= kFellOffLedge;
            return r;
        }
        actor.pos.y.whole = int16_t(g);
        return r;
    }
    if (r & kWallOnY) {
        actor.airborne = 0;
        actor.pos.y.whole = int16_t(g);
        bomb.fallResult |= kFellLanded;
    }
    return r;
}
// the homing of 0x5c88f0 / 0x5c8c90: spinning, a wall -> true, kept 0x40 over the ground; its time out -> true
bool homeTick(const PlayerEnv& env, ProjectileActor& actor, BombFields& bomb) {
    int32_t target[3];
    chestTarget(target);
    savePose(actor);
    home(actor, bomb, target);
    actor.rotation.z = int16_t((uint16_t(actor.rotation.z) + 0x100u) & 0xfffu);
    bool dead = false;
    if (actorPushOut(env, actor) & kWallOnSides) {
        dead = true;
    } else {
        const int g = actorGround(env, actor);
        if (g - 0x40 < actor.pos.y.whole) actor.pos.y.whole = int16_t(g - 0x40);
    }
    if (bomb.homingTime != 0) {
        bomb.homingTime -= 1u;
        return dead;
    }
    return true;
}
void blastHere(const ProjectileActor& actor) { spawnBlastAt(actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole, 0x80, 0); }
// exe 0x5c8ac0: a blast, freed, the boss may throw again
void die(ProjectileActor& actor, BombFields& bomb) {
    blastHere(actor);
    actor.state = uint8_t(BombState::Free);
    letBossThrowAgain(bomb);
}
// exe 0x5c8a80: thrown
void fly(const PlayerEnv& env, ProjectileActor& actor, BombFields& bomb) {
    actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    resolveFall(env, actor, bomb);
    if (bomb.fallResult & (kFellOnWall | kFellLanded)) actor.subState = uint8_t(HomingSub::Die);
}
// exe 0x5c89d0: in the player's hands until thrown (along his aim) or dropped
void carried(const PlayerEnv& env, ProjectileActor& actor, BombFields& bomb) {
    carryAtHands(actor, 0, 0, 0);                 // exe 0x573ab0
    const CarryRelease release = carryReleased(); // exe 0x573950
    if (release == CarryRelease::Held) return;
    if (release != CarryRelease::Thrown) {
        actor.velocity.x = 0;
        actor.velocity.y = 0;
        actor.velocity.z = 0;
    } else {
        int32_t aim[3];
        playerAimVector(0, -0x200, -0x400, aim); // exe 0x5e3e50
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(aim[1]);
        actor.velocity.z = int16_t(aim[2]);
    }
    actor.acceleration.y = 0x40;
    actor.rotation.y = player().rotation.y;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    carryDropFromPlayer(actor); // exe 0x573990
    ++actor.step;
    fly(env, actor, bomb);
}
// its hit entry at its position
void submitHit(const ProjectileActor& actor, BombFields& bomb, uint32_t shape, uint32_t attack) {
    const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    hitListSubmit(p, bomb.hitBox, hitOwner(actor.slot) | shape, attack); // exe 0x5e4120
}
void clearHitBox(const ProjectileActor& actor, BombFields& bomb) {
    bomb.hitBox.previousX = actor.prevPos.x.whole;
    bomb.hitBox.previousZ = actor.prevPos.z.whole;
    bomb.hitBox.result = 0;
}
// exe 0x5c87a0
void live(const PlayerEnv& env, ProjectileActor& actor, BombFields& bomb) {
    savePose(actor);
    bomb.fallResult = 0;
    actor.tint = kTintNeutral;
    const uint32_t hit = bomb.hitBox.result;
    if (hit & kHitAttackByPlayer) {
        actor.tint = kTintFlash;
        if (hitPointsAdd(actor, -int(hit & kHitDamageMask))) { // exe 0x5e52f0
            actor.subState = uint8_t(HomingSub::Die);
            letBossThrowAgain(bomb);
        }
    } else if (!(bomb.status & kBombLifted) && playerLiftTargetCheck(actor, 0)) { // exe 0x573a60
        bomb.status |= kBombLifted;
        actor.subState = uint8_t(HomingSub::Lifted);
    }
    switch (HomingSub(actor.subState)) { // [0x8d13f4]
    case HomingSub::Home:                       // exe 0x5c88f0
        if (homeTick(env, actor, bomb)) actor.subState = uint8_t(HomingSub::Die);
        break;
    case HomingSub::Lifted: // exe 0x5c89b0: [0x8d1400]
        if (LiftedStep(actor.step) == LiftedStep::Carried) carried(env, actor, bomb);
        else if (LiftedStep(actor.step) == LiftedStep::Thrown) fly(env, actor, bomb);
        else notPortedAt(kStoryPort, "0x5c89b0 step past [0x8d1400]", actor.step);
        break;
    case HomingSub::Die: die(actor, bomb); break;
    default: notPortedAt(kStoryPort, "0x5c87a0 sub-state past [0x8d13f4]", actor.subState);
    }
    clearHitBox(actor, bomb);
    if (!(bomb.status & kBombLifted))
        submitHit(actor, bomb, hitGroup(kHitGroupBodies) | hitRadius(0x1c),
                  uint32_t(int32_t(bomb.damage)) | kHitAttackSolid);
    else
        submitHit(actor, bomb, hitGroup(kHitGroupPlayerAttacks) | kHitNoLockOn | hitRadius(0x50), kHitAttackByPlayer | 8);
}
// the binds' common part (exe 0x5c8720 / 0x5c8b50)
void bindCommon(ProjectileActor& actor, BombFields& bomb) {
    actor.maxHitPoints = kBombHitPoints;
    actor.hitPoints = kBombHitPoints;
    actor.airborne = 1;
    actor.collisionBox.pointAt(TableId::St28FireBombBox);
    actor.interactionBox.pointAt(TableId::St28FireBombLiftBox);
    actor.interaction = kInteractLift;
    actor.velocity.z = int16_t(0xfc00);
}
void bindTail(ProjectileActor& actor, BombFields& bomb) {
    bomb.spare = 0;
    bomb.status = kBombAirborne;
    bomb.homingTime = kHomingTicks;
    bomb.hitBox.result = 0;
}
// exe 0x5c8720
void bindHoming(const PlayerEnv& env, ProjectileActor& actor, BombFields& bomb) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    const uint8_t state = actor.state;
    actor.flags |= kActorDrawn | kActorMeshModel;
    bindCommon(actor, bomb);
    actor.state = uint8_t(state + 1u);
    bindTail(actor, bomb);
    live(env, actor, bomb);
}

// ---- the centre ----------------------------------------------------------------------------------------------------
ProjectileActor& orbiter(const BombFields& bomb, int k) {
    const uint32_t slot = k == 0 ? slotIdentityWord(bomb.firstOrbiter) : slotIdentityWord(bomb.secondOrbiter);
    if (!slot) notPorted(kStoryPort, "0x5c8c90: an orbiting bomb not allocated (the exe writes through a null pointer)");
    return projectileActorAt(slot);
}
// exe 0x5c8e30
void centreDie(ProjectileActor& actor, BombFields& bomb) {
    blastHere(actor);
    actor.state = uint8_t(BombState::Free);
    orbiter(bomb, 0).state = uint8_t(BombState::Free);
    orbiter(bomb, 1).state = uint8_t(BombState::Free);
    letBossThrowAgain(bomb);
}
// exe 0x5c8c90: the hum, the homing, its two bombs carried 0x400 above / below it
void centreMove(const PlayerEnv& env, ProjectileActor& actor, BombFields& bomb) {
    if (bomb.humTimer != 0) {
        bomb.humTimer -= 1u;
    } else {
        soundPlayAtPos(kHumSound, actor.pos);
        bomb.humTimer = 9;
    }
    int32_t target[3];
    chestTarget(target);
    savePose(actor);
    home(actor, bomb, target);
    actor.rotation.z = int16_t((uint16_t(actor.rotation.z) + 0x100u) & 0xfffu);
    if (actorPushOut(env, actor) & kWallOnSides) {
        actor.subState = uint8_t(CentreSub::Die);
    } else {
        const int g = actorGround(env, actor);
        if (g - 0x40 < actor.pos.y.whole) actor.pos.y.whole = int16_t(g - 0x40);
    }
    if (bomb.homingTime != 0) {
        bomb.homingTime -= 1u;
    } else {
        actor.subState = uint8_t(CentreSub::Die);
        orbiter(bomb, 0).state = uint8_t(BombState::Free);
        orbiter(bomb, 1).state = uint8_t(BombState::Free);
    }
    for (int k = 0; k < 2; ++k) {
        ProjectileActor& o = orbiter(bomb, k);
        o.rotation.y = actor.rotation.y;
        o.rotation.x = actor.rotation.x;
        const int16_t roll = o.rotation.z;
        o.rotation.z = actor.rotation.z;
        o.pos = actor.pos;
        moveAlongRotation(o.pos, o.rotation, 0, k ? 0x400 : -0x400, 0); // exe 0x5e3810
        o.rotation.z = roll;
    }
}
// exe 0x5c8bc0
void centreLive(const PlayerEnv& env, ProjectileActor& actor, BombFields& bomb) {
    savePose(actor);
    bomb.fallResult = 0;
    const uint32_t hit = bomb.hitBox.result;
    if (hit & kHitAttackByPlayer) {
        actor.tint = kTintFlash;
        if (hitPointsAdd(actor, -int(hit & kHitDamageMask))) actor.subState = uint8_t(CentreSub::Die);
    }
    switch (CentreSub(actor.subState)) { // [0x8d1414]
    case CentreSub::Move: centreMove(env, actor, bomb); break;
    case CentreSub::Die: centreDie(actor, bomb); break;
    default: notPortedAt(kStoryPort, "0x5c8bc0 sub-state past [0x8d1414]", actor.subState);
    }
    clearHitBox(actor, bomb);
    submitHit(actor, bomb, hitGroup(kHitGroupBodies) | hitRadius(0x1c),
              uint32_t(int32_t(bomb.damage)) | kHitAttackSolid | kCentreAttackKind);
}
// exe 0x5c8b50 (no model of its own)
void bindCentre(const PlayerEnv& env, ProjectileActor& actor, BombFields& bomb) {
    bindCommon(actor, bomb);
    actor.state += 1u;
    bindTail(actor, bomb);
    centreLive(env, actor, bomb);
    spawnBombRibbon(slotIdentityWord(bomb.firstOrbiter), slotIdentityWord(bomb.secondOrbiter)); // exe 0x438720
}

void fireBomb(const PlayerEnv& env, ProjectileActor& actor) { // exe 0x5c86e0: [0x8d13dc]
    BombFields& bomb = classState<BombFields>(actor);
    switch (FireBombVariant(actor.variant)) {
    case FireBombVariant::Homing: // exe 0x5c8700: [0x8d13e8]
        switch (BombState(actor.state)) {
        case BombState::Bind: bindHoming(env, actor, bomb); break;
        case BombState::Live: live(env, actor, bomb); break;
        case BombState::Free: freeActor(actor); break; // exe 0x5d0bc0
        default: notPortedAt(kStoryPort, "0x5c8700 state past [0x8d13e8]", actor.state);
        }
        break;
    case FireBombVariant::Centre: // exe 0x5c8b30: [0x8d1408]
        switch (BombState(actor.state)) {
        case BombState::Bind: bindCentre(env, actor, bomb); break;
        case BombState::Live: centreLive(env, actor, bomb); break;
        case BombState::Free: freeActor(actor); break;
        default: notPortedAt(kStoryPort, "0x5c8b30 state past [0x8d1408]", actor.state);
        }
        break;
    case FireBombVariant::Orbiting: // exe 0x5c8eb0
        if (BombState(actor.state) == BombState::Bind) {
            if (bindActorModel(actor, 0)) {
                actor.flags |= kActorDrawn | kActorMeshModel;
                actor.state = uint8_t(BombState::Live);
                return;
            }
            freeActor(actor);
        } else if (BombState(actor.state) == BombState::Free) {
            freeActor(actor);
        }
        break;
    default: notPortedAt(kStoryPort, "0x5c86e0 variant past [0x8d13dc]", actor.variant);
    }
}

} // namespace

void spawnFireBomb(const AnimatedActor& boss, const int16_t point[3], FireBombVariant variant) { // exe 0x5c9160
    const uint32_t slot = placeBomb(boss, point, variant);
    if (!slot) return;
    ProjectileActor& bomb = projectileActorAt(slot);
    BombFields& fields = classState<BombFields>(bomb);
    if (variant != FireBombVariant::Homing) {
        for (int k = 0; k < 2; ++k) {
            const uint32_t o = placeBomb(boss, point, FireBombVariant::Orbiting);
            if (!o) continue;
            if (k == 0) fields.firstOrbiter = &projectileActorAt(o);
            else fields.secondOrbiter = &projectileActorAt(o);
        }
        soundPlayAtPos(kPairSound, bomb.pos);
    } else {
        soundPlayAtPos(kBombSound, bomb.pos);
    }
    fields.boss = &animatedActorAt(boss.at);
    fields.humTimer = 9;
}

void fireBombUpdate(const PlayerEnv& env, uint32_t actor) { fireBomb(env, projectileActorAt(actor)); }

} // namespace od::port
