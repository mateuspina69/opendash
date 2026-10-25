// The homing missile (weapon 6, pool 0x10 type 5, exe 0x56da70): a missile with a trail (static global type 6) homing
// on its target with a turn rate that grows each tick, accelerating to 0x600 (2 steps a tick); a blast where it ends.
#include "engine/data_source.hpp"
#include "game/shots/homing_missile_state.hpp"
#include "game/actor_state.hpp"
#include "game/shots/player_shots.hpp"

#include "game/actor_movement.hpp"
#include "game/global_class_types.hpp"
#include "game/missile_trail.hpp"
#include "game/player/player_weapons.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/audio_logic.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/shots/shot_common.hpp"

namespace od::port {

using namespace homing_missile_state;

namespace {

inline constexpr const char* kMissileArea = "homing missile";

// [0x8c49bc]
enum class MissileState : uint8_t { Spawn = 0, Fly = 1, Free = 2 }; // exe 0x56db10 / 0x56dd40 / 0x56dea0
enum class MissileEnd { Flying = 0, HitBody = 1, HitWall = 2, OutOfRange = 3, Gone = -1 };

const DataTable<CollisionBoxRecord> missileBox{TableId::HomingMissileBox};
constexpr int16_t kMissileSpeed = -0x100, kMissileAcceleration = 0x40, kMissileTopSpeed = -0x600;
constexpr uint16_t kMissileSound = 0xbf;
constexpr uint16_t kMaxTurnRate = 0x40;
constexpr int kWobbleLevels = 4; // the wobble starts after 4 - the weapon's level ticks
constexpr uint8_t kWobbling = 1; // the variant once it wobbles (the homing's shift)
constexpr int kSteps = 2;

MissileEnd spawn(const PlayerEnv& env, ShotActor& shot, MissileFields& fields) { // exe 0x56db10
    const Player& p = player();
    shot.flags |= kActorDrawn | kActorMeshModel;
    shot.runFlags |= kActorShown | kActorMeshModel | kActorWarpKept;
    const uint32_t tr = bindActorModel(shot, 0) ? allocActor60(0) : 0;
    if (tr == 0) {
        freeActor(shot);
        return MissileEnd::Gone;
    }
    StaticActor& trail = staticActorAt(tr);
    trail.flags = kActorLive;
    trail.type = typeByte(GlobalStaticType::MissileTrail);
    trail.variant = 0;
    trail.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    classState<MissileTrailFields>(trail).parent = shot.at;
    aimRelativeToTarget(shot);
    PlayerShotFields(shot).hitBox.result = 0;
    shot.velocity.setXY(0, 0);
    shot.velocity.z = kMissileSpeed;
    placeAtBone(shot, kLeftHandBone, 0);
    aimAlongPlayer(shot);
    moveShot(shot, 0, 0, -0x100);
    moveAlongRotation(shot.prevPos, shot.rotation, 0, 0, -int(shot.velocity.z) * 2);
    shot.collisionBox.pointAt(missileBox.stateAddress());
    shot.actorHit = 0;
    soundPlayAtPos(kMissileSound, shot.pos);
    fillShotStats(fields.stats);
    paySpecialEnergy(player());
    const int level = weaponLevel(p, p.activeSpecial, WeaponStat::Extra);
    fields.wobbleDelay = uint16_t(kWobbleLevels - level);
    fields.turnRate = 0;
    fields.turnStep = uint16_t(level + 1);
    ++shot.state;
    return shotHitWall(env, shot, fields.stats.attack) ? MissileEnd::HitWall : MissileEnd::Flying;
}

MissileEnd fly(const PlayerEnv& env, ShotActor& shot, MissileFields& fields) { // exe 0x56dd40
    auto&& shotFields = PlayerShotFields(shot);
    if (shot.actorHit != 0 || (shotFields.hitBox.result & (kHitAttackSolid | kHitAttackDeflects))) {
        stopHalfway(shot);
        return MissileEnd::HitBody;
    }
    shot.savePreviousPose();
    --fields.wobbleDelay;
    if (fields.wobbleDelay == 0) shot.variant = kWobbling;
    fields.turnRate = uint16_t(fields.turnRate + fields.turnStep);
    if (fields.turnRate > kMaxTurnRate) fields.turnRate = kMaxTurnRate;
    if (shotFields.target != nullptr)
        homeOnTarget(shot, slotIdentityWord(shotFields.target), fields.turnRate >> 2, kHomingKeepTarget | kHomingAlwaysTurn);
    --fields.stats.range;
    if (fields.stats.range == 0) return MissileEnd::OutOfRange;
    for (int k = 0; k < kSteps; ++k) {
        moveShot(shot, 0, 0, shot.velocity.z);
        if (shotHitWall(env, shot, fields.stats.attack, false)) return MissileEnd::HitWall;
    }
    shot.velocity.z = wrap16(shot.velocity.z - kMissileAcceleration);
    if (shot.velocity.z < kMissileTopSpeed) shot.velocity.z = kMissileTopSpeed;
    markFlying(shot);
    return MissileEnd::Flying;
}

// exe 0x56dec0: the capsule from the previous position (radius 0xc)
void submitMissileHit(ShotActor& shot, const ShotStats& stats) {
    submitShotCapsule(shot, 0xc, hitGroup(kHitGroupPlayerAttacks) | kHitContactMotion | hitCapsule(1),
                      stats.attack | kHitAttackByPlayer | kHitAttackStrongPush);
}

} // namespace

void homingMissileShotUpdate(const PlayerEnv& env, uint32_t address) {
    ShotActor& shot = shotActorAt(address);
    auto&& fields = classState<MissileFields>(shot);
    dropDeadTarget(shot);
    MissileEnd end;
    switch (MissileState(shot.state)) {
    case MissileState::Spawn: end = spawn(env, shot, fields); break;
    case MissileState::Fly: end = fly(env, shot, fields); break;
    case MissileState::Free: freeActor(shot); return; // exe 0x56dea0
    default: notPortedAt(kMissileArea, "0x56da70 state", shot.state);
    }
    switch (end) {
    case MissileEnd::Flying:
        actorCollisionSubmitMover(shot);
        submitMissileHit(shot, fields.stats);
        return;
    case MissileEnd::HitBody:
    case MissileEnd::OutOfRange:
        spawnShotBlast(shot, fields.stats.level); // exe 0x56e5d0
        ++shot.state;
        return;
    case MissileEnd::HitWall:
        spawnShotBlast(shot, fields.stats.level);
        submitMissileHit(shot, fields.stats);
        ++shot.state;
        return;
    default: return;
    }
}

} // namespace od::port
