// The buster's shots (pool 0x10): the buster shot (type 1, exe 0x56bfa0) fired from the right hand along the player's
// aim, homing on the locked target, bursting on walls and bodies; and the town's kick (type 0, exe 0x56bde0: the
// buster of weapon mode 0), a hit entry that follows the player's foot for 8 ticks.
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/shots/buster.hpp"
#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/shots/player_shots.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/shots/shot_common.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {

inline constexpr const char* kBusterArea = "buster shot";

// ---- the buster shot ------------------------------------------------------------------------------------------------
// how a tick ended
enum class BusterEnd { Flying = 0, HitBody = 1, HitWall = 2, OutOfRange = 3, Gone = -1 };

const DataTable<CollisionBoxRecord> busterBox{TableId::BusterBox};
constexpr int16_t kBusterSpeed = -0x400;
constexpr uint16_t kOrbitRange = 2; // the shot's range under the orbit status
constexpr uint16_t kShotSound = 0x9a, kOrbitShotSound = 0xf4, kDeflectSound = 0x8f;
constexpr uint16_t kDeflectTicks = 2;
constexpr uint16_t kArmBusterLimitMask = 0x0bff; // the shots in flight reached the limit: kArmBusterFree cleared (the
                                                 // exe's mask also clears the queue / refire bits)

// exe 0x56c580: the hit entry (group 1, the contact point; radius (level * 8 + 0x20) / 4, attack | the player's kind)
void submitBusterHit(ShotActor& shot, const ShotStats& stats) {
    submitShotHit(shot,
                  hitGroup(kHitGroupPlayerAttacks) | kHitContactPoint |
                      hitRadius(uint32_t(uint16_t(stats.level)) * 8 + 0x20),
                  stats.attack | kHitAttackByPlayer);
}

BusterEnd spawnBusterShot(const PlayerEnv& env, ShotActor& shot, BusterShotFields& fields) { // exe 0x56c060
    Player& p = player();
    shot.flags &= uint8_t(~kActorDrawn);
    shot.runFlags |= kActorWarpKept;
    aimRelativeToTarget(shot);
    PlayerShotFields(shot).hitBox.result = 0;
    if (p.onPlatform == 0) {
        shot.velocity.x = 0;
        shot.velocity.z = kBusterSpeed;
    } else { // standing on a moving actor: its last move, in the player's facing frame, halved
        int x, z;
        platformMove(x, z);
        shot.velocity.x = wrap16(x / 2);
        shot.velocity.z = wrap16(z / 2 + kBusterSpeed);
    }
    shot.velocity.y = 0;
    placeAtBone(shot, kRightHandBone, 0);
    aimAlongPlayer(shot);
    moveAlongRotation(shot.prevPos, shot.rotation, 0, 0, -int(shot.velocity.z) * 2); // one step behind
    const int w = p.activeBuster;
    const bool orbit = (p.status & (kStatusOrbit | kStatusOrbitFloor)) != 0;
    fields.stats.range = orbit ? kOrbitRange : uint16_t(weaponStat(p, w, WeaponStat::Range)); // exe 0x573510
    fields.stats.attack = uint16_t(weaponAttack(p, w));                                       // exe 0x5734b0
    fields.stats.level = int16_t(weaponLevel(p, w, WeaponStat::Attack));
    fields.deflectTicks = 0;
    ++p.busterShotsInFlight;
    if (p.busterShotsInFlight >= weaponStat(p, w, WeaponStat::Energy))
        p.armFlags &= kArmBusterLimitMask; // exe 0x5734e0
    shot.collisionBox.pointAt(busterBox.stateAddress());
    shot.actorHit = 0;
    // exe 0x56c610 (draw command): output only
    soundPlayAtPos(orbit ? kOrbitShotSound : kShotSound, shot.pos);
    fields.lockSlot = p.lockTarget;
    ++shot.state;
    if (shotHitWall(env, shot, fields.stats.attack)) return BusterEnd::HitWall;
    return BusterEnd::Flying;
}

BusterEnd flyBusterShot(const PlayerEnv& env, ShotActor& shot, BusterShotFields& fields) { // exe 0x56c310
    auto&& shotFields = PlayerShotFields(shot);
    if (shotFields.hitBox.result & kHitAttackSolid) { // a solid body (an enemy): stop between the last two positions
        stopHalfway(shot);
        return BusterEnd::HitBody;
    }
    if (shot.actorHit != 0) return BusterEnd::HitWall; // a solid actor (the actor collision pass)
    shot.savePreviousPose();
    if (fields.deflectTicks == 0) {
        if (shotFields.hitBox.result & kHitAttackDeflects) { // deflected: turn away from the contact point
            const int dx = int(shot.pos.x.whole) - shotFields.hitBox.contact.x;
            const int dz = -(int(shot.pos.z.whole) - shotFields.hitBox.contact.z);
            shot.rotation.y = int16_t(uint32_t(-atanAngle(float(dx), float(dz))) & kAngleMask);
            fields.deflectTicks = kDeflectTicks;
            soundPlayAtPos(kDeflectSound, shot.pos);
        }
    } else {
        --fields.deflectTicks;
    }
    if (fields.lockSlot != 0 && shotFields.target != nullptr) homeOnTarget(shot, slotIdentityWord(shotFields.target), 2, 0);
    --fields.stats.range;
    if (fields.stats.range == 0) return BusterEnd::OutOfRange;
    for (int k = 0; k < 2; ++k) {
        moveShot(shot, shot.velocity.x, 0, shot.velocity.z);
        if (shotHitWall(env, shot, fields.stats.attack)) return BusterEnd::HitWall;
    }
    // draw commands (the shot's sprite + the exe 0x56c610 glow): render output
    markFlying(shot);
    return BusterEnd::Flying;
}

// ---- the town's kick -----------------------------------------------------------------------------------------------
struct KickFields {
    explicit KickFields(const ShotActor& actor) : KickFields(const_cast<ShotActor&>(actor)) {}
    explicit KickFields(ShotActor& actor) : ticks(actor.params[0]), attack(actor.params[1]) {}
    uint8_t& ticks;
    FieldAlias<int8_t, uint8_t> attack;
};
enum class KickState : uint8_t { Spawn = 0, Follow = 1 }; // [0x8c48fc]: exe 0x56be10 / 0x56beb0
const DataTable<CollisionBoxRecord> kickBox{TableId::BusterKickBox};
constexpr uint8_t kKickTicks = 8;
constexpr uint16_t kKickSound = 0x95;

// the kick's position on a bone point of the player (<< 16: not by the stage shift; the previous position kept)
void kickAtBone(ShotActor& shot, uint32_t bone) {
    const auto& b = player().bonePoints[bone];
    shot.pos.x = int32_t(uint32_t(int32_t(b.x)) << 16);
    shot.pos.y = int32_t(uint32_t(int32_t(b.y)) << 16);
    shot.pos.z = int32_t(uint32_t(int32_t(b.z)) << 16);
}

} // namespace

void busterShotUpdate(const PlayerEnv& env, uint32_t address) {
    ShotActor& shot = shotActorAt(address);
    auto&& fields = classState<BusterShotFields>(shot);
    dropDeadTarget(shot);
    BusterEnd end;
    switch (BusterState(shot.state)) {
    case BusterState::Spawn: end = spawnBusterShot(env, shot, fields); break;
    case BusterState::Fly: end = flyBusterShot(env, shot, fields); break;
    case BusterState::Free: { // exe 0x56c560: the buster is usable again
        Player& p = player();
        --p.busterShotsInFlight;
        p.setArmBits(kArmBusterFree);
        freeActor(shot); // exe 0x5e14d0
        end = BusterEnd::Gone;
        break;
    }
    default: notPortedAt(kBusterArea, "buster shot state", shot.state);
    }
    switch (end) {
    case BusterEnd::Flying:
        actorCollisionSubmitMover(shot); // exe 0x4140b0
        submitBusterHit(shot, fields.stats);
        return;
    case BusterEnd::HitBody:
        spawnShotSpark(shot, fields.stats.level, 0); // exe 0x5704f0
        ++shot.state;
        return;
    case BusterEnd::HitWall:
        spawnShotBurst(shot, fields.stats, 0); // exe 0x56b880
        submitBusterHit(shot, fields.stats);
        ++shot.state;
        return;
    case BusterEnd::OutOfRange:
        spawnShotBurst(shot, fields.stats, 1);
        ++shot.state;
        return;
    default: return;
    }
}

void townKickUpdate(const PlayerEnv&, uint32_t address) {
    ShotActor& shot = shotActorAt(address);
    auto&& kick = KickFields(shot);
    dropDeadTarget(shot);
    switch (KickState(shot.state)) {
    case KickState::Spawn: { // exe 0x56be10: 8 ticks at the player's body, its attack at its level
        PlayerShotFields(shot).hitBox.result = 0;
        shot.flags &= uint8_t(~kActorDrawn);
        kick.ticks = kKickTicks;
        kick.attack = int8_t(weaponAttack(player(), player().activeBuster)); // exe 0x5734b0
        kickAtBone(shot, kBodyBone);
        shot.rotation.z = 0;
        shot.rotation.y = 0;
        shot.prevPos = shot.pos;
        shot.rotation.x = 0;
        shot.collisionBox.pointAt(kickBox.stateAddress());
        shot.actorHit = 0;
        soundPlayAtPos(kKickSound, shot.pos); // exe 0x5d5380: the kick
        ++shot.state;
        return;
    }
    case KickState::Follow: { // exe 0x56beb0: the foot followed, a hit entry
        shot.savePreviousPose();
        const uint8_t t = uint8_t(kick.ticks - 1);
        kick.ticks = t;
        if (t == 0) {
            freeActor(shot); // exe 0x5e14d0
            return;
        }
        kickAtBone(shot, kRightFootBone);
        submitShotHit(shot, hitGroup(kHitGroupPlayerAttacks) | kHitContactMotion | hitRadius(0x30),
                      uint32_t(int32_t(kick.attack)) | kHitAttackThrown);
        actorCollisionSubmitMover(shot); // exe 0x4140b0
        return;
    }
    default: notPortedAt(kBusterArea, "town shot state", shot.state);
    }
}

} // namespace od::port
