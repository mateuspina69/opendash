// What the player's shot classes share (see shot_common.hpp).
#include "game/actor_state.hpp"
#include "game/shots/shot_common.hpp"

#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/breakable_walls.hpp"
#include "game/draw_commands.hpp"
#include "game/explosions.hpp"
#include "game/fixed_point.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {

constexpr int kHomingTolerance = 0x200; // an angle off by this much or more drops the target

} // namespace

void fillShotStats(ShotStats& stats) {
    const Player& p = player();
    const int w = p.activeSpecial;
    stats.range = uint16_t(weaponStat(p, w, WeaponStat::Range)); // exe 0x573510
    stats.attack = uint16_t(weaponAttack(p, w));                 // exe 0x5734b0
    stats.level = int16_t(weaponLevel(p, w, WeaponStat::Attack));
}

void placeAtBone(ShotActor& shot, uint32_t bone, int dy) {
    const uint32_t sh = (0x10u - stage().coordinateShift) & 0x1f;
    const auto& b = player().bonePoints[bone];
    const int16_t point[3] = {b.x, b.y, b.z};
    for (int k = 0; k < 3; ++k) {
        const int32_t v = int32_t(uint32_t(int32_t(point[k]) + (k == 1 ? dy : 0)) << sh);
        Fixed16& prev = k == 0 ? shot.prevPos.x : k == 1 ? shot.prevPos.y : shot.prevPos.z;
        Fixed16& pos = k == 0 ? shot.pos.x : k == 1 ? shot.pos.y : shot.pos.z;
        prev = v;
        pos = v;
    }
}

void aimAlongPlayer(ShotActor& shot) {
    shot.rotation.x = player().rotation.x;
    shot.rotation.y = player().rotation.y;
    shot.rotation.z = 0;
}

void stopHalfway(ShotActor& shot) {
    shot.pos.x = (shot.prevPos.x >> 1) + (shot.pos.x >> 1);
    shot.pos.y = (shot.prevPos.y >> 1) + (shot.pos.y >> 1);
    shot.pos.z = (shot.prevPos.z >> 1) + (shot.pos.z >> 1);
}

void aimRelativeToTarget(ShotActor& shot) {
    auto&& fields = PlayerShotFields(shot);
    const uint32_t t = slotIdentityWord(fields.target);
    if (t == 0) return;
    const Actor& target = actorLayoutAt(t);
    fields.aimOffset.x = wrap16(fields.aimPoint.x - target.pos.x.whole);
    fields.aimOffset.y = wrap16(fields.aimPoint.y - target.pos.y.whole);
    fields.aimOffset.z = wrap16(fields.aimPoint.z - target.pos.z.whole);
}

void dropDeadTarget(ShotActor& shot) {
    auto&& fields = PlayerShotFields(shot);
    const uint32_t t = slotIdentityWord(fields.target);
    if (t != 0 && actorHeaderAt(t).flags == 0) fields.target = 0;
}

void platformMove(int& x, int& z) {
    const Player& p = player();
    const Actor& platform = *p.platform;
    const int yaw = uint16_t(p.rotation.y) & kAngleMask;
    const int dx = int32_t(uint32_t(platform.pos.x) - uint32_t(platform.prevPos.x)) >> 12;
    const int dz = int32_t(uint32_t(platform.pos.z) - uint32_t(platform.prevPos.z)) >> 12;
    x = ((fixedCos(yaw) * dx) >> 12) - ((fixedSin(yaw) * dz) >> 12);
    z = ((fixedSin(yaw) * dx) >> 12) - ((fixedCos(yaw) * dz) >> 12);
}

void targetAimPoint(ShotActor& shot, uint32_t target, int32_t out[3]) {
    const PlayerShotFields fields(shot);
    const Actor& t = actorLayoutAt(target);
    out[0] = int32_t((uint32_t(int32_t(fields.aimOffset.x)) << 16) + uint32_t(t.pos.x));
    out[1] = int32_t((uint32_t(int32_t(fields.aimOffset.y)) << 16) + uint32_t(t.pos.y));
    out[2] = int32_t((uint32_t(int32_t(fields.aimOffset.z)) << 16) + uint32_t(t.pos.z));
}

bool homeOnTarget(ShotActor& shot, uint32_t target, int step, uint8_t flags) {
    int32_t aim[3];
    targetAimPoint(shot, target, aim);
    const int32_t from[3] = {shot.pos.x, shot.pos.y, shot.pos.z};
    const int limit = step << (shot.variant & 0x1f);
    const int pitch = pitchOf(from, aim) - kHalfTurn; // exe 0x5e3ca0 (the integer parts)
    int cur = shot.rotation.x;
    int diff = signedAngle(pitch - cur);
    if (diff != 0) {
        const int ad = diff < 0 ? -diff : diff;
        if (ad < kHomingTolerance || (flags & kHomingAlwaysTurn)) {
            int v = pitch;
            if (diff > 0) {
                if (diff > limit) v = cur + limit;
            } else if (diff < -limit) {
                v = cur - limit;
            }
            cur = v;
            shot.rotation.x = wrap16(v);
        } else if (!(flags & kHomingKeepTarget)) {
            PlayerShotFields(shot).target = 0;
            return false;
        }
    }
    int reached = pitch == cur ? 1 : 0;
    const int yaw = angleOf(from[0], from[2], aim[0], aim[2]) - kHalfTurn; // exe 0x5e3bc0
    cur = shot.rotation.y;
    diff = signedAngle(yaw - cur);
    if (diff != 0) {
        const int ad = diff < 0 ? -diff : diff;
        if (ad < kHomingTolerance || (flags & kHomingAlwaysTurn)) {
            int v = yaw;
            if (diff > 0) {
                if (diff > limit * 2) v = cur + limit * 2;
            } else if (diff < -limit * 2) {
                v = cur - limit * 2;
            }
            cur = v;
            shot.rotation.y = wrap16(v);
        } else if (!(flags & kHomingKeepTarget)) {
            PlayerShotFields(shot).target = 0;
        }
    }
    if (yaw == cur) reached |= 2;
    return reached == 3;
}

bool shotHitWall(const PlayerEnv& env, ShotActor& shot, uint16_t attack, bool pushOut) {
    const uint32_t r = actorTestMove(env, shot);
    if (int32_t(r) >= 0) return false;
    queueBreakableWallHit(r, attack);
    if (pushOut) actorPushOut(env, shot);
    return true;
}

void moveShot(ShotActor& shot, int x, int y, int z) { moveAlongRotation(shot.pos, shot.rotation, x, y, z); }

void markFlying(ShotActor& shot) {
    shot.airborne = kAirborneFalling;
    shot.onPlatform = 0;
    shot.platform = nullptr;
}

void clearHitBox(ShotActor& shot) {
    auto&& fields = PlayerShotFields(shot);
    fields.hitBox.result = 0;
    fields.hitBox.previousX = shot.prevPos.x.whole;
    fields.hitBox.previousZ = shot.prevPos.z.whole;
}

void submitShotHit(ShotActor& shot, uint32_t shape, uint32_t attack) {
    const int16_t p[3] = {shot.pos.x.whole, shot.pos.y.whole, shot.pos.z.whole};
    hitListSubmit(p, PlayerShotFields(shot).hitBox, hitOwner(shot.slot) | shape, attack);
    clearHitBox(shot);
}

void submitShotCapsule(ShotActor& shot, uint16_t r, uint32_t shape, uint32_t attack, const int16_t* offset) {
    const int16_t o[3] = {offset ? offset[0] : int16_t(0), offset ? offset[1] : int16_t(0),
                          offset ? offset[2] : int16_t(0)};
    const int16_t p[3] = {wrap16(shot.pos.x.whole + o[0]), wrap16(shot.pos.y.whole + o[1]),
                          wrap16(shot.pos.z.whole + o[2])};
    const int16_t q[3] = {wrap16(shot.prevPos.x.whole + o[0]), wrap16(shot.prevPos.y.whole + o[1]),
                          wrap16(shot.prevPos.z.whole + o[2])};
    hitListSubmitCapsule(p, r, q, r, PlayerShotFields(shot).hitBox, hitOwner(shot.slot) | shape, attack);
    clearHitBox(shot);
}

uint32_t drawXY(const ShotActor& shot) {
    return uint32_t(uint16_t(shot.pos.y.whole)) << 16 | uint16_t(shot.pos.x.whole);
}

void spawnShotSpark(const Actor& shot, int16_t level, uint8_t variant, const int16_t* offset) {
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (address == 0) return;
    EffectActor& e = effectActorAt(address);
    e.flags = kActorLive | kActorDrawn;
    e.type = typeByte(GlobalEffectType::HitSpark);
    e.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    e.variant = variant;
    e.pos.x = int16_t(uint16_t(shot.pos.x.whole) + (offset ? offset[0] : 0));
    e.pos.y = int16_t(uint16_t(shot.pos.y.whole) + (offset ? offset[1] : 0));
    e.pos.z = int16_t(uint16_t(shot.pos.z.whole) + (offset ? offset[2] : 0));
    auto&& spark = classState<HitSparkFields>(e);
    spark.frameTicks = 1;
    spark.size = uint16_t((uint32_t(uint16_t(level)) + 0x10) * 8);
}

void spawnShotBurst(const Actor& shot, const ShotStats& stats, uint8_t variant) {
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (address == 0) return;
    EffectActor& e = effectActorAt(address);
    e.flags = kActorLive | kActorDrawn;
    e.type = typeByte(GlobalEffectType::Burst);
    e.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    e.variant = variant;
    e.pos.x = shot.pos.x.whole;
    e.pos.y = shot.pos.y.whole;
    e.pos.z = shot.pos.z.whole;
    auto&& burst = classState<BurstFields>(e);
    burst.size = uint16_t((stats.level + 8) * 0x10);
    burst.sizeClass = uint8_t(stats.level);
}

void spawnShotBlast(const ShotActor& shot, int16_t level) {
    const uint32_t address = allocActor60(0);
    if (address == 0) return;
    StaticActor& s = staticActorAt(address);
    s.flags = kActorLive;
    s.variant = 1;
    s.loopFlags |= kActorSweepGroup1;
    s.type = typeByte(GlobalStaticType::Blast);
    s.pos = shot.pos;
    auto&& blast = BlastParams(s);
    blast.size = uint16_t((uint32_t(uint16_t(level)) + 8) << 4);
    blast.attack = uint16_t((uint32_t(uint16_t(level)) + 2) * 2);
}

} // namespace od::port
