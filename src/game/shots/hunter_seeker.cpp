// The hunter seeker (weapon 16, pool 0x10 type 15, exe 0x570e50): a model thrown ahead that drifts to a stop, turns
// toward the player's lock-on target and, aimed, throws a crystal (static global type 0x18, exe 0x56b150) every 8
// ticks; it blinks in its last 0x60 ticks and ends in a blast. Its range (the ticks it lives) comes from the weapon's
// extra level. The crystal flies two steps a tick along its rotation (an additive double cone, ordering-table
// polygons), hurting what it reaches; a body sparks it, a wall or its end bursts it.
#include "engine/data_source.hpp"
#include "game/shots/hunter_seeker_state.hpp"
#include "game/actor_state.hpp"
#include "game/shots/player_shots.hpp"

#include "game/actor_movement.hpp"
#include "game/global_class_types.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/draw_commands.hpp"
#include "game/effect_polygons.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/shots/shot_common.hpp"

namespace od::port {

using namespace hunter_seeker_state;

namespace {

inline constexpr const char* kHunterArea = "hunter seeker";

// [0x8c4aa4]
enum class HunterState : uint8_t { Spawn = 0, Seek = 1 }; // exe 0x570ea0 / 0x571020
enum class HunterEnd { Seeking = 0, Done = 1, Gone = -1 };

const DataTable<CollisionBoxRecord> hunterBox{TableId::HunterSeekerBox};
const DataTable<ScalarTable<uint16_t, 4>> hunterRanges{TableId::HunterSeekerRanges}; // the ticks it lives by the weapon's extra level (exe data)
constexpr int16_t kThrowSpeed = -0x300;             // its forward speed when thrown (slowed by kDrag a tick to a stop)
constexpr int16_t kDrag = 0x20;
constexpr int kThrowStart = -0x100; // the previous position this far behind it
constexpr uint16_t kThrowSound = 0x14e, kBlinkSound = 0x14f;
constexpr int kTurnStep = 0x20;
constexpr uint16_t kCrystalTicks = 8;

HunterEnd spawn(const PlayerEnv& env, ShotActor& shot, HunterFields& fields) { // exe 0x570ea0
    const Player& p = player();
    shot.flags |= kActorDrawn | kActorMeshModel;
    shot.runFlags |= kActorShown | kActorMeshModel | kActorWarpKept;
    if (!bindActorModel(shot, 0)) {
        freeActor(shot);
        return HunterEnd::Gone;
    }
    shot.velocity.z = kThrowSpeed;
    PlayerShotFields(shot).hitBox.result = 0;
    shot.velocity.setXY(0, 0);
    placeAtBone(shot, kLeftHandBone, 0);
    aimAlongPlayer(shot);
    moveAlongRotation(shot.prevPos, shot.rotation, 0, 0, kThrowStart);
    const int weapon = player().activeSpecial;
    fields.stats.range = hunterRanges[uint32_t(int32_t(weaponLevel(p, weapon, WeaponStat::Extra)))];
    fields.stats.attack = uint16_t(weaponAttack(p, weapon));
    fields.stats.level = weaponLevel(p, weapon, WeaponStat::Attack);
    fields.crystalTicks = 0;
    paySpecialEnergy(player());
    shot.collisionBox.pointAt(hunterBox.stateAddress());
    shot.actorHit = 0;
    soundPlayAtPos(kThrowSound, shot.pos);
    if (int32_t(actorTestMove(env, shot)) < 0) actorPushOut(env, shot);
    ++shot.state;
    return HunterEnd::Seeking;
}

// its last 0x60 ticks: blinks every 4, 2 and 1 tick as they run out
bool blinks(uint32_t ticksLeft) {
    if (ticksLeft < 0x20) return (ticksLeft & 1) != 0;
    if (ticksLeft < 0x40) return (ticksLeft & 2) != 0;
    if (ticksLeft < 0x60) return (ticksLeft & 4) != 0;
    return false;
}

HunterEnd seek(const PlayerEnv& env, ShotActor& shot, HunterFields& fields) { // exe 0x571020
    const Player& p = player();
    auto&& shotFields = PlayerShotFields(shot);
    if (shotFields.hitBox.result & kHitAttackSolid) {
        stopHalfway(shot);
        return HunterEnd::Done;
    }
    shot.savePreviousPose();
    if (fields.crystalTicks != 0) --fields.crystalTicks;
    const uint32_t target = slotIdentityWord(p.lockedActor);
    if (target != 0 && p.lockSuspended == 0) {
        const Actor& t = actorLayoutAt(target);
        shotFields.aimOffset.x = wrap16(p.lockAimPoint.x - t.pos.x.whole);
        shotFields.aimOffset.y = wrap16(p.lockAimPoint.y - t.pos.y.whole);
        shotFields.aimOffset.z = wrap16(p.lockAimPoint.z - t.pos.z.whole);
        if (homeOnTarget(shot, target, kTurnStep, kHomingAlwaysTurn) && fields.crystalTicks == 0) {
            if (const uint32_t c = allocActor60(0)) {
                StaticActor& crystal = staticActorAt(c);
                crystal.flags = kActorLive;
                crystal.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
                crystal.type = typeByte(GlobalStaticType::Crystal);
                crystal.pos = shot.pos;
                crystal.rotation = shot.rotation;
                fields.crystalTicks = kCrystalTicks;
            }
        }
    }
    shot.tint = kTintNeutral;
    --fields.stats.range;
    const uint32_t left = fields.stats.range;
    if (left == 0) return HunterEnd::Done;
    if (blinks(left)) {
        shot.tint = kTintFlash;
        soundPlayAtPos(kBlinkSound, shot.pos);
    }
    if (shot.velocity.z != 0) {
        const int16_t v = wrap16(shot.velocity.z + kDrag);
        shot.velocity.z = v;
        if (v > 0)
            shot.velocity.z = 0;
        else
            moveShot(shot, 0, 0, v);
    }
    if (int32_t(actorTestMove(env, shot)) < 0) actorPushOut(env, shot);
    markFlying(shot);
    return HunterEnd::Seeking;
}

// [0x8c48d4]
enum class CrystalState : uint8_t { Start = 0, Fly = 1, Free = 2 }; // exe 0x56b1f0 / 0x56b2e0 / 0x56b7d0
// how a tick ended (the jump table 0x56b1d8)
enum class CrystalEnd { Flying = 0, TurnedBack = 1, Blocked = 2, Spent = 3 };
const DataTable<CollisionBoxRecord> crystalBox{TableId::HunterSeekerCrystalBox};
// the crystal's colour and size by its level (exe data 0x8c48b0: RGB by level >> 1; 0x8c48c0: s8 by level)
const DataTable<ScalarTable<uint32_t, 4>> crystalColours{TableId::HunterSeekerCrystalColours};
const DataTable<ScalarTable<int8_t, 8>> crystalSizes{TableId::HunterSeekerCrystalSizes};
constexpr uint16_t kCrystalLife = 0x20;
constexpr int16_t kCrystalSpeed = -0x400;
constexpr int kCrystalStartBehind = 0x800; // the previous position this far behind it
constexpr uint16_t kCrystalSound = 0x9a;
constexpr uint32_t kCrystalShape = hitGroup(kHitGroupPlayerAttacks) | kHitContactPoint;
constexpr uint8_t kCrystalSparkVariant = 0, kCrystalBurstVariant = 0, kCrystalFadeVariant = 1;

// exe 0x56b7f0: the hit entry (its box; radius (level * 8 + 0x20) / 4), the box reset to the previous position
void crystalHit(StaticActor& actor, CrystalFields& f) {
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const uint32_t shape = hitOwner(actor.slot) | hitRadius(uint32_t(uint16_t(f.stats.level)) * 8 + 0x20) | kCrystalShape;
    hitListSubmit(pos, f.hitBox, shape, uint32_t(f.stats.attack) | kHitAttackByPlayer); // exe 0x5e4120
    f.hitBox.result = 0;
    f.hitBox.previousX = actor.prevPos.x.whole;
    f.hitBox.previousZ = actor.prevPos.z.whole;
}
// exe 0x56b8e0(actor, b, mode): a glow (a ring record) of the level's colour, radius by the mode
void crystalGlow(StaticActor& actor, const CrystalFields& f, uint32_t mode) {
    const uint32_t lv = uint16_t(f.stats.level);
    const uint32_t r = mode != 0 ? (lv << ((mode + 2) & 0x1f)) + 0x40 : lv * 8 + 0x20;
    dc::Ring glow;
    glow.inner = dc::ringColour(crystalColours[lv >> 1]);
    glow.xy = dc::packXY(actor.pos.x.whole, actor.pos.y.whole);
    glow.zStep = dc::zWord(actor.pos.z.whole) | dc::ringStep(2);
    glow.radii = dc::ringRadii(0, r);
    dc::emit(glow, actor.at);
}
// exe 0x56b1f0: hidden, kept through the warps, aimed forward (its previous position behind it), the weapon's stats,
// the glow and the sound; Blocked when it starts in a wall
CrystalEnd crystalStart(const PlayerEnv& env, StaticActor& actor, CrystalFields& f) {
    actor.flags &= uint8_t(~kActorDrawn);
    actor.runFlags |= kActorWarpKept;
    f.hitBox.result = 0;
    actor.prevPos.y = actor.pos.y;
    actor.prevPos.x = actor.pos.x;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = kCrystalSpeed;
    actor.prevPos.z = actor.pos.z;
    moveAlongRotation(actor.prevPos, actor.rotation, 0, 0, kCrystalStartBehind); // exe 0x5e3810
    f.stats.range = kCrystalLife;
    const Player& p = player();
    const int w = p.specialWeapon;
    f.stats.attack = uint16_t(weaponAttack(p, w));
    f.stats.level = int16_t(weaponLevel(p, w, WeaponStat::Attack));
    actor.collisionBox.pointAt(crystalBox.stateAddress());
    actor.actorHit = 0;
    crystalGlow(actor, f, 1);
    soundPlayAtPos(kCrystalSound, actor.pos);
    actor.state += 1u;
    const uint32_t r = actorTestMove(env, actor);
    if (int32_t(r) < 0) {
        queueBreakableWallHit(r, f.stats.attack);
        actorPushOut(env, actor);
        return CrystalEnd::Blocked;
    }
    return CrystalEnd::Flying;
}
// exe 0x56b2e0: TurnedBack (a solid hit: put back halfway), Blocked (an actor / a wall), Spent (its life over); else two
// steps along its rotation and (in depth) the crystal: 8 additive POLY_G3 (black tips) between the ends (twice the
// step apart) and four equator points (+- the up / side vectors of the level's size)
CrystalEnd crystalFly(const PlayerEnv& env, StaticActor& actor, CrystalFields& f) {
    if (f.hitBox.result & kHitAttackSolid) {
        actor.pos.x = int32_t((actor.prevPos.x >> 1) + (actor.pos.x >> 1));
        actor.pos.y = int32_t((actor.prevPos.y >> 1) + (actor.pos.y >> 1));
        actor.pos.z = int32_t((actor.prevPos.z >> 1) + (actor.pos.z >> 1));
        return CrystalEnd::TurnedBack;
    }
    if (actor.actorHit != 0) return CrystalEnd::Blocked;
    actor.savePreviousPose();
    --f.stats.range;
    if (f.stats.range == 0) return CrystalEnd::Spent;
    int16_t pt[2][3];
    for (int i = 0; i < 2; ++i) {
        moveAlongRotation(actor.pos, actor.rotation, 0, 0, actor.velocity.z);
        pt[i][0] = actor.pos.x.whole;
        pt[i][1] = actor.pos.y.whole;
        pt[i][2] = actor.pos.z.whole;
        const uint32_t r = actorTestMove(env, actor);
        if (int32_t(r) < 0) {
            queueBreakableWallHit(r, f.stats.attack);
            actorPushOut(env, actor);
            return CrystalEnd::Blocked;
        }
    }
    const uint32_t level = uint16_t(f.stats.level);
    const int pitch = uint16_t(actor.rotation.x) & kAngleMask;
    const int sz = crystalSizes[level];
    const int sp = fixedSin(pitch) * sz >> 12;
    const int16_t uy = wrap16(fixedCos(pitch) * sz >> 12);
    const int yaw = int(uint32_t(int32_t(actor.rotation.y)) & kAngleMask);
    const int side = int((uint32_t(int32_t(actor.rotation.y)) + uint32_t(kQuarterTurn)) & kAngleMask);
    const int sp16 = int16_t(uint16_t(sp));
    const int16_t ux = wrap16(fixedSin(yaw) * sp16 >> 12), uz = wrap16(fixedCos(yaw) * sp16 >> 12);
    const int16_t vx = wrap16(fixedSin(side) * sz >> 12), vz = wrap16(fixedCos(side) * sz >> 12);
    int16_t mid[3], D[3];
    for (int k = 0; k < 3; ++k) {
        mid[k] = wrap16((pt[0][k] + pt[1][k]) >> 1);
        D[k] = wrap16((pt[0][k] - pt[1][k]) * 2);
    }
    const int16_t q0[3] = {wrap16(D[0] + mid[0]), wrap16(D[1] + mid[1]), wrap16(D[2] + mid[2])};
    const int16_t q1[3] = {wrap16(mid[0] - D[0]), wrap16(mid[1] - D[1]), wrap16(mid[2] - D[2])};
    const CameraPoint c0 = cameraPoint(q0);
    if (!effectDepthVisible(c0)) return CrystalEnd::Flying;
    const int16_t eq[5][3] = {{wrap16(mid[0] + ux), wrap16(mid[1] + uy), wrap16(mid[2] + uz)},
                              {wrap16(mid[0] + vx), mid[1], wrap16(mid[2] + vz)},
                              {wrap16(mid[0] - ux), wrap16(mid[1] - uy), wrap16(mid[2] - uz)},
                              {wrap16(mid[0] - vx), mid[1], wrap16(mid[2] - vz)},
                              {wrap16(mid[0] + ux), wrap16(mid[1] + uy), wrap16(mid[2] + uz)}};
    const uint32_t col = crystalColours[level >> 1];
    for (uint32_t k = 0; k < 8; ++k) {
        const int16_t* tip = k < 4 ? q0 : q1;
        const int16_t* e1 = eq[k & 3];
        const int16_t* e2 = eq[(k & 3) + 1];
        const uint32_t c[4] = {dc::packetColour(dc::kPolyG3Semi, 0), col, col, col};
        const int16_t v[4][3] = {{tip[0], tip[1], tip[2]}, {e1[0], e1[1], e1[2]}, {e2[0], e2[1], e2[2]},
                                 {e2[0], e2[1], e2[2]}};
        worldG4(actor.at, c, v); // a POLY_G3 0x32 (the 4th corner repeats the 3rd)
    }
    actor.airborne = kAirborneFalling;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    return CrystalEnd::Flying;
}

} // namespace

void crystalShotUpdate(const PlayerEnv& env, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    auto&& f = classState<CrystalFields>(actor);
    CrystalEnd end;
    switch (CrystalState(actor.state)) {
    case CrystalState::Start: end = crystalStart(env, actor, f); break;
    case CrystalState::Fly: end = crystalFly(env, actor, f); break;
    case CrystalState::Free: freeActor(actor); return;
    default: notPortedAt(kHunterArea, "0x56b150 state", actor.state);
    }
    switch (end) {
    case CrystalEnd::Flying:
        actorCollisionSubmitMover(actor); // exe 0x4140b0
        crystalHit(actor, f);
        return;
    case CrystalEnd::TurnedBack:
        spawnShotSpark(actor, f.stats.level, kCrystalSparkVariant); // exe 0x5704f0
        actor.state += 1u;
        return;
    case CrystalEnd::Blocked:
        spawnShotBurst(actor, f.stats, kCrystalBurstVariant); // exe 0x56b880
        crystalHit(actor, f);
        actor.state += 1u;
        return;
    case CrystalEnd::Spent:
        spawnShotBurst(actor, f.stats, kCrystalFadeVariant);
        actor.state += 1u;
        return;
    }
}

void hunterSeekerShotUpdate(const PlayerEnv& env, uint32_t address) {
    ShotActor& shot = shotActorAt(address);
    auto&& fields = classState<HunterFields>(shot);
    HunterEnd end;
    switch (HunterState(shot.state)) {
    case HunterState::Spawn: end = spawn(env, shot, fields); break;
    case HunterState::Seek: end = seek(env, shot, fields); break;
    default: notPortedAt(kHunterArea, "0x570e50 state", shot.state);
    }
    if (end == HunterEnd::Seeking) {
        actorCollisionSubmitMover(shot);
        // exe 0x571200: radius 0x20 / 4, the attack quadrupled (its bump bit becoming the player's attack kind)
        submitShotHit(shot, hitGroup(kHitGroupPlayerAttacks) | kHitContactPoint | hitRadius(0x20),
                      uint32_t(fields.stats.attack | kHitAttackBump) << 2);
    } else if (end == HunterEnd::Done) {
        spawnShotBlast(shot, fields.stats.level);
        freeActor(shot);
    }
}

} // namespace od::port
