// The flying platform boss of the Nino ruin's lowest floor (ST38 sub 11) and its attacks, one routine per exe routine;
// the names are hypotheses from the observed behaviour.
//   0x500b50  pool 0x20 ST38 type 0x79: the boss (0x500da0 init, 0x500e80 the fight: 0x500eb0 the patrol, 0x5011a0 the
//             hunt; 0x501840 dying; 0x501a80 / 0x501b00 the hit checks, 0x501b60 the collision, 0x501c10 the sparks)
//   0x5bab10  pool 0x60 ST38 type 0x5a: the sparks (0x5bab30 start, 0x5bab80 gather, 0x5bb0a0 fire, 0x5bb4a0 burst,
//             0x5bb660 the colour ramp)
//   0x423f40  pool 0xe0 ST38 type 0xf: the lightning bolt (0x423f60 start, 0x423fb0 the bolt and its OT quads, 0x4247b0
//             its colours)
//   0x5c2830  pool 0x40 ST38 type 0: the aimed shot (0x5c2870 start, 0x5c28f0 flight; 0x5cc7b0 / 0x5cc800 its ends)
//   0x5d0770  pool 0x40 ST38 type 0x2f: the homing orb (0x5d07b0 start, 0x5d0820 flight, 0x5d0bd0 burst, 0x5d0c60 its
//             colour)
// Render output left out as in the other ports: the shadows (exe 0x5f20f0).
#include "content/nino/platform_boss.hpp"
#include "content/nino/platform_boss_state.hpp"
#include "content/nino/ruin_effects.hpp"

#include <cstdlib>

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
#include "game/carry.hpp"
#include "game/collision.hpp"
#include "game/draw_commands.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/skid_sparks.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/territory.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace platform_boss_state;

namespace {

int16_t t16(int v) { return int16_t(uint16_t(uint32_t(v))); }
// the draw command 0xf0 (a disc in the camera plane) at an s16 point's xy word and z
void disc(uint32_t owner, uint32_t colour, uint32_t xy, uint16_t z, uint32_t radii) {
    dc::emitDisc(owner, colour, xy, dc::zWord(int16_t(z)) | dc::ringStep(2), radii);
}

// The boss's spark (pool 0x60 stage type 0x5a): its spawn parameters -- its two counters, its damage and the boss's
// slot (the gathering sparks follow its points)
struct BossSparkParams {
    explicit BossSparkParams(const StaticActor& actor) : BossSparkParams(const_cast<StaticActor&>(actor)) {}
    explicit BossSparkParams(StaticActor& actor) : counter(actor.params[0]), subCounter(actor.params[1]), damage(actor.params[2]), boss(actor.params[3]) {}
    uint8_t& counter;
    uint8_t& subCounter;
    uint8_t& damage;
    FieldAlias<int8_t, uint8_t> boss;
};
constexpr uint8_t kBossSparkType = 0x5a;
enum class SparkVariant : uint8_t { Volley = 0, Orb = 1, BurstA = 2, BurstB = 3, Aimed = 4 };
// The shots' spawn parameters (pool 0x40 stage types 0 / 0x23 / 0x2f): their life and their damage
struct BossShotParams {
    explicit BossShotParams(const ProjectileActor& actor) : BossShotParams(const_cast<ProjectileActor&>(actor)) {}
    explicit BossShotParams(ProjectileActor& actor) : life(actor.params, 0), damage(actor.params[2]) {}
    BytesField<int16_t, 4> life;
    FieldAlias<int8_t, uint8_t> damage;
};
constexpr uint8_t kAimedShotType = 0, kVolleyShotType = 0x23, kOrbType = 0x2f;
constexpr uint8_t kBoltType = 0xf;
enum class BoltParent : uint8_t { Static = 1, Projectile = 2, Animated = 3 };

// ==== 0x500b50: the boss =============================================================================================
enum class BossState : uint8_t { Init = 0, Fight = 1, Dying = 2 };
enum class BossSub : uint8_t { Patrol = 0, Hunt = 1 };
enum class HuntPhase : uint8_t { Start = 0, Cruise = 1, Aim = 2, Dive = 3 };
enum class DivePhase : uint8_t { Start = 0, Swoop = 1, Fall = 2, Climb = 3 };
enum class DyingPhase : uint8_t { Start = 0, Fall = 1, Down = 2 };
enum class FallPhase : uint8_t { First = 0, Second = 1, Land = 2 };
enum class DownPhase : uint8_t { Start = 0, Sparkles = 1, Still = 2 };
constexpr uint8_t kTouchedXWall = 0x01, kTouchedZWall = 0x02, kTouchedFloor = 0x04;
constexpr uint8_t kBossHurt = 0x02, kBossWallTurn = 0x04, kBossDiving = 0x20, kBossDying = 0x80;
// The boss's stats row (by type and spawn byte): its +8 is its starting hit points
struct BossStats : AnimatedActorStats {
    static constexpr uint32_t kBytes = 0xd;
    uint8_t startHitPoints = 0; // +0x8
    template <class R> static BossStats load(const R& r) {
        BossStats v;
        static_cast<AnimatedActorStats&>(v) = AnimatedActorStats::load(r);
        v.startHitPoints = r.template read<uint8_t>(0x8);
        return v;
    }
};
BossStats bossStats(const AnimatedActor& boss) { return dataRecord<BossStats>(animatedStatRow(boss.type, boss.spawnByte), BossStats::kBytes); }
// A waypoint (exe data, 8 bytes): its x / z, the mask of its next ones and the next ones
struct Waypoint : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    int16_t x = 0; // +0x0
    int16_t z = 0; // +0x2
    int8_t mask = 0; // +0x4
    std::array<uint8_t, 3> next{}; // +0x5
    template <class R> static Waypoint load(const R& r) {
        Waypoint v;
        v.x = r.template read<int16_t>(0x0);
        v.z = r.template read<int16_t>(0x2);
        v.mask = r.template read<int8_t>(0x4);
        for (size_t i = 0; i < v.next.size(); ++i) v.next[i] = r.template read<uint8_t>(0x5 + i * sizeof(uint8_t));
        return v;
    }
};
Waypoint waypointAt(int8_t i) { return dataRecord<Waypoint>(stateAddress(TableId::PlatformBossWaypoints) + uint32_t(int32_t(i)) * 8, Waypoint::kBytes); }
const DataTable<CollisionBoxRecord> bossBox{TableId::PlatformBossBox};
constexpr uint16_t kHumSound = 0x1de, kDiveSound = 0x1df, kHurtSound = 0x8d;
constexpr uint8_t kSparkleVariant = 0x1d;
constexpr uint32_t kBossHitKinds = kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong; // 0x1c0000

// the next waypoint: one RNG step picks among the next ones of the waypoint it comes from
void pickWaypoint(BossFields& fields) {
    const uint32_t r = rngStep();
    const Waypoint from = waypointAt(fields.fromWaypoint);
    fields.waypoint = int8_t(from.next[size_t(uint32_t(int32_t(from.mask)) & r)]);
}
void nextWaypoint(BossFields& fields) {
    fields.fromWaypoint = fields.waypoint;
    pickWaypoint(fields);
}
// the wall turn: stuck against a wall (moved < 4 across it) -> the next waypoint, 0x30 ticks of grace
void wallTurn(const AnimatedActor& boss, BossFields& fields) {
    const uint8_t mode = fields.mode;
    if (!(mode & kBossWallTurn) && (fields.contact & (kTouchedXWall | kTouchedZWall))) {
        const int d = (fields.contact & kTouchedXWall)
                          ? std::abs(int(boss.pos.z.whole) - int(boss.prevPos.z.whole))
                          : std::abs(int(boss.pos.x.whole) - int(boss.prevPos.x.whole));
        if (d >= 4) return;
        fields.ticks = 0x30;
        fields.mode = uint8_t(mode | kBossWallTurn);
        nextWaypoint(fields);
        return;
    }
    if (fields.ticks == 0) fields.mode = uint8_t(mode & ~kBossWallTurn);
    else fields.ticks -= 1u;
}
// the bank (the rotation's z) by the turn d: past 0x10 it leans by `step` (to 0x200), else back toward 0 (snapped
// within `snap`)
void bank(AnimatedActor& boss, int d, int step, int snap) {
    int16_t b = boss.rotation.z;
    if (d > 0x10) {
        b = t16(b - step);
        if (b < -0x200) b = -0x200;
    } else if (d < -0x10) {
        b = t16(b + step);
        if (b > 0x200) b = 0x200;
    } else if (b < -snap) {
        b = t16(b + step);
    } else if (b > snap) {
        b = t16(b - step);
    } else {
        b = 0;
    }
    boss.rotation.z = b;
}
// exe 0x501b60: the move's contact bits (blocked: pushed out, the position back when that fails; free: the territory)
void bossCollide(const PlayerEnv& env, AnimatedActor& boss, BossFields& fields) {
    fields.contact = 0;
    if (int32_t(actorTestMove(env, boss)) < 0) {
        const uint32_t r = actorPushOut(env, boss); // exe 0x410960
        if (int32_t(r) < 0) boss.pos = boss.prevPos;
        if (r & (kWallOnXPlus | kWallOnXMinus)) fields.contact |= kTouchedXWall;
        if (r & (kWallOnZPlus | kWallOnZMinus)) fields.contact |= kTouchedZWall;
        if (r & kWallOnY) fields.contact |= kTouchedFloor;
        return;
    }
    const int rect = territoryViolated(boss.pos);
    if (rect == 0) return;
    const uint32_t r = territoryPushBack(boss, rect);
    if (r & (kWallOnXPlus | kWallOnXMinus)) fields.contact |= kTouchedXWall;
    if (r & (kWallOnZPlus | kWallOnZMinus)) fields.contact |= kTouchedZWall;
}
// exe 0x501b00 (the patrol): a hit; only the 0x1c0000 attacks cost a hit point (a flash, the hurt sound); -1 = dead
int patrolHit(AnimatedActor& boss, const BossFields& fields) {
    const uint32_t h = fields.hit.result;
    if (!(h & kHitDamageMask)) return 0;
    int d = 0;
    if (h & kBossHitKinds) {
        boss.tint = kTintFlash;
        soundPlayAtPos(kHurtSound, boss.pos);
        d = 1;
    }
    if (hitPointsAdd(boss, -d) != 0) return -1;
    return 0;
}
// exe 0x501a80 (the hunt): a hit; the 0x1c0000 attacks cost their damage; dead -> dying, -1; a strong push -> 1
int huntHit(AnimatedActor& boss, const BossFields& fields) {
    const uint32_t h = fields.hit.result;
    const uint32_t dmg = h & kHitDamageMask;
    if (dmg == 0) return 0;
    uint32_t d = 0;
    if (h & kBossHitKinds) {
        boss.tint = kTintFlash;
        soundPlayAtPos(kHurtSound, boss.pos);
        d = dmg;
    }
    if (hitPointsAdd(boss, -int(d)) != 0) {
        boss.enterState(uint8_t(BossState::Dying));
        return -1;
    }
    if (h & kHitAttackStrongPush) return 1;
    return 0;
}
// a spark (static type 0x5a) of the variant at a spark point
void spawnSpark(const AnimatedActor& boss, SparkVariant variant, const ShortVec3& point, uint8_t damage) {
    const uint32_t s = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!s) return;
    StaticActor& spark = staticActorAt(s);
    BossSparkParams params(staticActorAt(s));
    spark.flags = kActorLive;
    spark.type = kBossSparkType;
    spark.subtype = 0;
    spark.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
    spark.variant = uint8_t(variant);
    spark.pos.x.whole = point.x;
    spark.pos.y.whole = point.y;
    spark.pos.z.whole = point.z;
    spark.rotation.x = 0;
    spark.rotation.y = 0;
    spark.rotation.z = 0;
    params.damage = damage;
    params.boss = int8_t(boss.slot);
}
// exe 0x501c10: the spark of the kind asked (1 / 2 at the tail's point, 3 at the head's): variants 0 / 4 / 1; the
// cooldown += 0x60 (0x80 for kind 2)
void bossSpark(const AnimatedActor& boss, BossFields& fields) {
    if (fields.sparkCooldown != 0) return;
    const BossStats row = bossStats(boss);
    switch (fields.sparkKind) {
    case 1:
        spawnSpark(boss, SparkVariant::Volley, fields.tailPoint, row.strongAttack);
        fields.sparkCooldown += 0x60u;
        break;
    case 2:
        spawnSpark(boss, SparkVariant::Aimed, fields.tailPoint, row.strongAttack);
        fields.sparkKind = 0;
        fields.sparkCooldown += 0x80u;
        return;
    case 3:
        spawnSpark(boss, SparkVariant::Orb, fields.headPoint, row.shotAttack);
        fields.sparkCooldown += 0x60u;
        break;
    default: break;
    }
    fields.sparkKind = 0;
}
// exe 0x500da0: the model of its spawn byte, the hit points of its row, the box; registered as an enemy
void bossInit(AnimatedActor& boss, BossFields& fields) {
    if (!bindActorModel(boss, boss.spawnByte)) { // exe 0x5e0bc0
        freeActor(boss);                        // exe 0x5e1490
        return;
    }
    boss.runFlags |= kActorRunMarked;
    boss.anim = 0;
    boss.prevAnim = 0xff;
    boss.rotation.x = 0;
    boss.rotation.z = 0;
    const BossStats row = bossStats(boss);
    boss.maxHitPoints = int16_t(row.hitPoints);
    boss.hitPoints = int16_t(row.startHitPoints);
    boss.collisionBox.pointAt(bossBox.stateAddress());
    boss.interactionBox = nullptr;
    boss.interaction = 0;
    boss.airborne = 0;
    boss.onPlatform = 0;
    boss.clearClassBlock(); // exe 0x40dc40
    fields.hit.previousX = boss.pos.x.whole;
    fields.hit.previousZ = boss.pos.z.whole;
    fields.hit.result = 0;
    fields.startKey = 0;
    boss.enterState(uint8_t(BossState::Fight));
    registerEnemy(boss.slot, 0);
    resetModelScale(boss); // exe 0x4e5cd0
}
// the shared start of the patrol and the hunt
void subStart(AnimatedActor& boss, BossFields& fields) {
    fields.mode = 0;
    boss.step = 1;
    boss.anim = 0;
    fields.startKey = 0;
    boss.velocity.setXY(0, 0);
    boss.velocity.z = -0x80;
}
// exe 0x500eb0: the patrol of the waypoints (backwards, -0x80), hovering between y -0x300 and -0x180 (higher while the
// player rides it); a hit that would kill it -> the hunt, its hit points back
void patrol(AnimatedActor& boss, BossFields& fields) {
    if (boss.step == 0) {
        subStart(boss, fields);
        fields.fromWaypoint = 0;
        pickWaypoint(fields);
    }
    int16_t tx = 0, tz = 0;
    const Waypoint w = waypointAt(fields.waypoint);
    tx = w.x;
    tz = w.z;
    if (boss.animKey.frame == 0) soundPlayAtPos(kHumSound, boss.pos);
    wallTurn(boss, fields);
    const int angle = angleOf(int32_t(uint32_t(uint16_t(tx)) << 16), int32_t(uint32_t(uint16_t(tz)) << 16),
                              boss.pos.x, boss.pos.z);
    const int d = turnYawTo(boss.rotation.y, uint32_t(angle), 8);
    if (playerRides(boss)) {
        int16_t y = t16(boss.pos.y.whole - 2);
        if (y < -0x300) y = -0x300;
        boss.pos.y.whole = y;
    } else {
        int16_t y = t16(boss.pos.y.whole + 2);
        if (y > -0x180) y = -0x180;
        boss.pos.y.whole = y;
    }
    moveAlongYaw(boss.pos, boss.rotation.y, 0, 0, boss.velocity.z); // exe 0x5e36f0
    bank(boss, d, 2, 4);
    if (!(fields.mode & kBossHurt) && patrolHit(boss, fields) < 0) {
        boss.subState = uint8_t(BossSub::Hunt);
        boss.hitPoints = boss.maxHitPoints;
        boss.restartStep();
        return;
    }
    if (uint16_t(uint16_t(boss.pos.x.whole) - uint16_t(tx) + 0x40u) < 0x80 &&
        uint16_t(uint16_t(boss.pos.z.whole) - uint16_t(tz) + 0x40u) < 0x80)
        nextWaypoint(fields);
}
// exe 0x5011a0: the hunt -- the cruise of the waypoints at y -0x2c0 (sparks at random; the player riding it: the kinds
// 1 / 2), now and then (the player near the room's centre, the boss high) the aim: it faces the player, then dives or
// cruises on
void hunt(AnimatedActor& boss, BossFields& fields) {
    Player& p = player();
    switch (HuntPhase(boss.step)) {
    case HuntPhase::Start:
        subStart(boss, fields);
        [[fallthrough]];
    case HuntPhase::Cruise: {
        int16_t tx = 0, tz = 0;
        const Waypoint w = waypointAt(fields.waypoint);
        tx = w.x;
        tz = w.z;
        if (boss.animKey.frame == 0) soundPlayAtPos(kHumSound, boss.pos);
        const int angle = angleOf(int32_t(uint32_t(uint16_t(tx)) << 16), int32_t(uint32_t(uint16_t(tz)) << 16),
                                  boss.pos.x, boss.pos.z);
        const int d = turnYawTo(boss.rotation.y, uint32_t(angle), 8);
        wallTurn(boss, fields);
        int16_t y = boss.pos.y.whole;
        if (y > -0x2c0) {
            y = t16(y - 4);
            if (y < -0x2c0) y = -0x2c0;
        } else {
            y = t16(y + 4);
            if (y > -0x2c0) y = -0x2c0;
        }
        boss.pos.y.whole = y;
        moveAlongYaw(boss.pos, boss.rotation.y, 0, 0, boss.velocity.z);
        bank(boss, d, 4, 4);
        if (uint16_t(uint16_t(boss.pos.x.whole) - uint16_t(tx) + 0x60u) < 0xc0 &&
            uint16_t(uint16_t(boss.pos.z.whole) - uint16_t(tz) + 0x60u) < 0xc0)
            nextWaypoint(fields);
        const bool ridden = playerRides(boss);
        if ((rngStep() & 7) == 0) {
            if (!ridden) {
                fields.sparkKind = int8_t((rngStep() >> 8) & 3u);
            } else {
                fields.sparkKind = int8_t(((rngStep() >> 8) & 1u) + 1u);
                break;
            }
        } else if (ridden) {
            break;
        }
        if ((rngStep() & 0xf) != 0) break;
        const int16_t px = p.pos.x.whole;
        if (uint32_t(int32_t(px) + 0x400) >= 0x800) break;
        if (uint32_t(int32_t(p.pos.z.whole) + 0x400) >= 0x800) break;
        if (boss.pos.y.whole >= -0x200) break;
        const bool inZ = uint32_t(int32_t(boss.pos.z.whole) + 0x500) < 0xa00;
        const bool inX = uint32_t(int32_t(boss.pos.x.whole) + 0x500) < 0xa00;
        if (inZ == inX) break;
        fields.diveX = px;
        fields.diveZ = p.pos.z.whole;
        boss.step = uint8_t(HuntPhase::Aim);
        break;
    }
    case HuntPhase::Aim: {
        const int d = turnYawTo(boss.rotation.y, uint32_t(angleOf(p.pos, boss.pos)), 0x20);
        moveAlongYaw(boss.pos, boss.rotation.y, 0, 0, boss.velocity.z);
        bank(boss, d, 0x10, 0x10);
        if (uint32_t(d + 0x10) < 0x20 && boss.rotation.z == 0) {
            const int16_t px = p.pos.x.whole;
            fields.diveX = px;
            boss.velocity.z = 0x200;
            const int16_t pz = p.pos.z.whole;
            fields.diveZ = pz;
            // the dive's pitch from the player's point at y -0x80 (integer parts)
            const int16_t from[3] = {px, -0x80, pz}, to[3] = {boss.pos.x.whole, boss.pos.y.whole, boss.pos.z.whole};
            const uint32_t pitch = uint32_t(pitchOf(from, to)) & kAngleMask;
            boss.velocity.y = t16((fixedSin(int(pitch)) * boss.velocity.z) >> 12);
            const int16_t vy = boss.velocity.y;
            boss.velocity.z = t16((fixedCos(int(pitch)) * boss.velocity.z) >> 12);
            if (std::abs(int(vy)) < (std::abs(int(boss.velocity.z)) >> 1)) {
                boss.anim = 1;
                boss.enterStep(uint8_t(HuntPhase::Dive));
            } else {
                boss.velocity.z = -0x80;
                boss.step = uint8_t(HuntPhase::Cruise);
            }
        } else if (playerRides(boss)) {
            boss.step = uint8_t(HuntPhase::Cruise);
        } else if ((rngStep() & 7) == 0) {
            fields.sparkKind = int8_t((rngStep() >> 8) & 3u);
        }
        break;
    }
    case HuntPhase::Dive: // the swoop (animation 2, sound 0x1df), the fall until a wall / the floor (animation 3), the
        // climb back
        switch (DivePhase(boss.stepCounter)) {
        case DivePhase::Start:
            fields.mode |= kBossDiving | kBossHurt;
            boss.stepCounter += 1u;
            [[fallthrough]];
        case DivePhase::Swoop:
            if (boss.animEnded()) {
                boss.anim = 2;
                soundPlayAtPos(kDiveSound, boss.pos);
                boss.stepCounter += 1u;
            }
            break;
        case DivePhase::Fall:
            if (fields.contact & (kTouchedXWall | kTouchedZWall | kTouchedFloor)) {
                boss.anim = 3;
                boss.stepCounter += 1u;
            }
            break;
        case DivePhase::Climb:
            boss.velocity.z = int16_t(uint16_t(boss.velocity.z) + 0x20u);
            if (boss.animEnded()) {
                fields.mode &= uint8_t(~(kBossDiving | kBossHurt));
                boss.velocity.z = -0x80;
                boss.anim = 0;
                boss.enterStep(uint8_t(HuntPhase::Cruise));
            }
            break;
        default: break;
        }
        integrateVelocity(boss, 0, boss.velocity.y, boss.velocity.z); // exe 0x5e3690
        break;
    default: break;
    }
    const uint8_t mode = fields.mode;
    if (!(mode & kBossHurt)) {
        if (huntHit(boss, fields) != 0) {
            fields.mode |= kBossHurt;
            boss.anim = 4;
        }
    } else if (!(mode & kBossDiving) && boss.animEnded()) {
        fields.mode = uint8_t(mode & ~kBossHurt);
        boss.anim = 0;
    }
}
// exe 0x501840: the fall with the death animations (4, 5, 6) to the floor, then 8 ticks of 5 sparkles (the global
// effect type 3 variant 0x1d) around it; it stays (animation 7)
void bossDying(const PlayerEnv& env, AnimatedActor& boss, BossFields& fields) {
    boss.tint = kTintNeutral;
    switch (DyingPhase(boss.step)) {
    case DyingPhase::Start:
        fields.mode = kBossDying | kBossHurt;
        boss.step = 1;
        boss.anim = 4;
        fields.startKey = 0;
        boss.velocity.setXY(0, 0);
        boss.velocity.y = t16((int32_t(fixedSin(0x600)) << 7) >> 12);
        boss.velocity.z = t16((int32_t(fixedCos(0x600)) << 7) >> 12);
        [[fallthrough]];
    case DyingPhase::Fall:
        switch (FallPhase(boss.stepCounter)) {
        case FallPhase::First:
            if (boss.animEnded()) {
                boss.anim = 5;
                boss.stepCounter = uint8_t(FallPhase::Second);
            }
            break;
        case FallPhase::Second:
            if (boss.animEnded()) {
                boss.anim = 6;
                boss.stepCounter = uint8_t(FallPhase::Land);
            }
            break;
        case FallPhase::Land:
            if (fields.contact & kTouchedFloor) boss.enterStep(uint8_t(DyingPhase::Down));
            break;
        default: break;
        }
        moveAlongYaw(boss.pos, boss.rotation.y, 0, boss.velocity.y, boss.velocity.z);
        if (boss.rotation.z < -0x10) boss.rotation.z = int16_t(uint16_t(boss.rotation.z) + 0x10u);
        else if (boss.rotation.z > 0x10) boss.rotation.z = int16_t(uint16_t(boss.rotation.z) - 0x10u);
        else boss.rotation.z = 0;
        bossCollide(env, boss, fields);
        return;
    case DyingPhase::Down:
        switch (DownPhase(boss.stepCounter)) {
        case DownPhase::Start:
            fields.ticks = 8;
            boss.anim = 7;
            boss.stepCounter = uint8_t(DownPhase::Sparkles);
            return;
        case DownPhase::Sparkles:
            for (int n = 5; n > 0; --n) {
                const uint32_t angle = (rngStep() & 0xfff) - 0x555u;
                const int radius = int((rngStep() & 0x3f) + 0x80);
                const uint32_t e = allocActorA0(0); // exe 0x5e13b0
                if (!e) continue;
                EffectActor& sparkle = effectActorAt(e);
                sparkle.flags = kActorLive;
                sparkle.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
                sparkle.type = typeByte(GlobalEffectType::SkidSparks);
                sparkle.variant = kSparkleVariant;
                sparkle.pos.x = t16(((fixedCos(int(angle)) * radius) >> 12) + boss.pos.x.whole);
                sparkle.pos.y = boss.pos.y.whole;
                sparkle.pos.z = t16(((fixedSin(int(angle)) * radius) >> 12) + boss.pos.z.whole);
                classState<SkidSparkFields>(effectActorAt(e)).yaw = uint16_t((rngStep() & 0xfff) + 0x40u);
            }
            fields.ticks -= 1u;
            if (fields.ticks == 0) boss.stepCounter = uint8_t(DownPhase::Still);
            return;
        default: return;
        }
    default: return;
    }
}
// exe 0x500b50
void platformBoss(const PlayerEnv& env, AnimatedActor& boss) {
    BossFields& fields = classState<BossFields>(boss);
    boss.prevAnim = boss.anim;
    boss.savePreviousPose();
    boss.savePreviousContact();
    switch (BossState(boss.state)) {
    case BossState::Init: bossInit(boss, fields); break;
    case BossState::Fight: // exe 0x500e80
        boss.tint = kTintNeutral;
        switch (BossSub(boss.subState)) {
        case BossSub::Patrol: patrol(boss, fields); break;
        case BossSub::Hunt: hunt(boss, fields); break;
        default: notPortedAt(kStoryPort, "0x500b50 sub", boss.subState);
        }
        bossCollide(env, boss, fields);
        break;
    case BossState::Dying: bossDying(env, boss, fields); break;
    default: notPortedAt(kStoryPort, "0x500b50 state", boss.state);
    }
    if (!(boss.flags & kActorLive)) return;
    if (boss.anim == boss.prevAnim) animStep(boss);    // exe 0x5e1ea0
    else animSet(boss, boss.anim, fields.startKey);    // exe 0x5e1c50
    animPose(boss);                                    // exe 0x5e2d90
    // the spark points: bone 7 at (0, 0, 0x100) -> the tail's (the exe's dword copy also writes a stale stack word
    // after it: left as is), bone 0 at (0, 0xa0, 0xc0) -> the head's
    int16_t o[3];
    static constexpr int16_t kTail[3] = {0, 0, 0x100}, kHead[3] = {0, 0xa0, 0xc0};
    bonePointOffset(boss, 7, kTail, o);
    fields.tailPoint.x = o[0];
    fields.tailPoint.y = o[1];
    fields.tailPoint.z = o[2];
    bonePointOffset(boss, 0, kHead, o);
    fields.headPoint.x = o[0];
    fields.headPoint.y = o[1];
    fields.headPoint.z = o[2];
    if (fields.sparkCooldown != 0) fields.sparkCooldown -= 1u;
    if (fields.sparkKind != 0) bossSpark(boss, fields);
    const BossStats row = bossStats(boss);
    fields.hit.result = 0;
    const uint8_t mode = fields.mode;
    fields.hit.previousX = boss.prevPos.x.whole;
    fields.hit.previousZ = boss.prevPos.z.whole;
    const uint32_t damage = row.attack;
    static constexpr int16_t kBody[3] = {0, 0x40, -0x40}, kBelly[3] = {0, 0, -0x80};
    bonePointOffset(boss, 0, kBody, o);
    const uint32_t owner = hitOwner(boss.slot) | ((mode & kBossDying) ? kHitNoLockOn : 0u);
    hitListSubmit(o, fields.hit, owner | hitGroup(kHitGroupBodies) | hitRadius(0x50), damage | kHitAttackSolid);
    if (mode & kBossDiving) { // a second, wider entry below
        bonePointOffset(boss, 0, kBelly, o);
        hitListSubmit(o, fields.hit, owner | hitGroup(kHitGroupHeavyThrown) | hitRadius(0xa0),
                      damage | kHitAttackThrown | kHitAttackSolid);
    }
    // exe 0x5f20f0(a, 0x96, 0x2a0, 0): the shadow (render output)
    actorCollisionSubmitSolid(boss, SolidType::StandOn, 0, 0); // exe 0x414050
}

// ==== 0x5bab10: the sparks ===========================================================================================
// (its speed: the velocity's z; its own values: platform_boss_state.hpp SparkFields)
enum class SparkState : uint8_t { Start = 0, Gather = 1, Fire = 2, Burst = 3 };
enum class GatherPhase : uint8_t { Start = 0, Spawn = 1, Wait = 2 };
enum class FirePhase : uint8_t { Start = 0, FadeVolley = 1, Drop = 2, FadeAimed = 3, Aimed = 4, FadeOut = 5 };
enum class BurstPhase : uint8_t { Start = 0, Fly = 1, End = 2 };
constexpr uint16_t kSparkShotSound = 0x1e0, kOrbSound = 0x1e1;

// exe 0x5bb660(out, v, variant): the 0..0xf ramp (up to 7, down after) as a colour (variant & 1: blue, else white)
uint32_t sparkColour(int32_t v, uint8_t variant) {
    uint32_t c;
    if (uint32_t(v) > 0xf) c = 0;
    else if (uint32_t(v) > 7) c = 0x1e0 - (uint32_t(v) << 5);
    else c = uint32_t(v) << 5;
    const uint32_t e = ((c << 8) - c) >> 8;
    if (variant & 1) return ((((e & 0xff) << 9) | (e & 0xfe)) << 7) | (e >> 1);
    return ((((e & 0xfe) << 7) | (e & 0xff)) << 8) | e;
}
// a shot (pool 0x40 stage type `type`, the variant) at the spark, flying along the yaw (its speed random), its life
// random, the spark's damage
uint32_t sparkShot(const StaticActor& spark, uint8_t type, uint8_t variant, uint16_t yaw) {
    const uint32_t s = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!s) return 0;
    ProjectileActor& shot = projectileActorAt(s);
    BossShotParams params(projectileActorAt(s));
    shot.flags = kActorLive;
    shot.loopFlags |= kActorSweepGroup2;
    shot.type = type;
    shot.variant = variant;
    shot.pos = spark.pos;
    shot.velocity.x = 0;
    shot.velocity.y = 0;
    const uint32_t r = rngStep();
    shot.acceleration.y = 0x20;
    shot.velocity.z = int16_t(uint16_t((0xffffffc0u - (r & 0x1f)) << 2));
    shot.rotation.x = 0;
    shot.rotation.y = int16_t(yaw);
    shot.rotation.z = 0;
    params.life = int16_t((rngStep() & 0x7f) + 0x80u);
    params.damage = int8_t(BossSparkParams(spark).damage);
    return s;
}
// the gathering spark follows the boss's point (its tail's / its head's)
void followBoss(StaticActor& spark, bool head) {
    const BossFields& boss = classState<BossFields>(animatedActorAt(animatedPoolSlot(BossSparkParams(spark).boss)));
    const ShortVec3& point = head ? boss.headPoint : boss.tailPoint;
    spark.pos.x.whole = point.x;
    spark.pos.y.whole = point.y;
    spark.pos.z.whole = point.z;
}
// exe 0x5bab80: the particles cleared, then spawned around (radius 0x80..0x9f, every 2nd / 4th tick) for 0x10 steps,
// then the wait until they die -> variants 0 / 4: the fire; 1: the orb 0x5d0770 (sound 0x1e1), freed. Drawn: the
// particles (pulled in during their last 7 ticks) and the core.
void sparkGather(StaticActor& spark, SparkFields& fields) {
    BossSparkParams params(spark);
    switch (GatherPhase(spark.step)) {
    case GatherPhase::Start:
        for (size_t i = 0; i < fields.particles.size(); ++i) fields.particles[i].life = 0;
        params.counter = 0x10;
        params.subCounter = 0;
        spark.step = uint8_t(GatherPhase::Spawn);
        break;
    case GatherPhase::Spawn: {
        const uint32_t mask = int8_t(params.counter) >= 7 ? 3u : 1u;
        if ((mask & uint32_t(int32_t(int8_t(params.subCounter)))) == 0) {
            const uint32_t angle = rngStep() & 0xfff;
            const int dy = int(rngStep() & 0xff) - 0x7f;
            for (int i = 15; i >= 0; --i) {
                SparkParticle& particle = fields.particles[size_t(i)];
                if (uint16_t(particle.life) != 0) continue;
                const int radius = int16_t((rngStep() & 0x1f) + 0x80);
                particle.point.x = t16(((fixedCos(int(angle)) * radius) >> 12) + spark.pos.x.whole);
                particle.point.y = t16(spark.pos.y.whole - dy);
                particle.point.z = t16(((fixedSin(int(angle)) * radius) >> 12) + spark.pos.z.whole);
                particle.life = 0x10;
                break;
            }
        }
        if (params.subCounter == 0) {
            params.counter -= 1u;
            params.subCounter = params.counter;
            if (params.counter == 0) spark.step = uint8_t(GatherPhase::Wait);
        } else {
            params.subCounter -= 1u;
        }
        break;
    }
    case GatherPhase::Wait: {
        bool alive = false;
        for (size_t i = 0; i < fields.particles.size(); ++i) alive = alive || uint16_t(fields.particles[i].life) != 0;
        if (alive) break;
        switch (SparkVariant(spark.variant)) {
        case SparkVariant::Volley:
        case SparkVariant::Aimed: spark.enterState(uint8_t(SparkState::Fire)); return;
        case SparkVariant::Orb: {
            const uint32_t s = allocActor40(kActorStageClass); // exe 0x5e1280
            if (s) {
                soundPlayAtPos(kOrbSound, spark.pos);
                ProjectileActor& orb = projectileActorAt(s);
                BossShotParams shot(projectileActorAt(s));
                orb.flags = kActorLive;
                orb.loopFlags |= kActorSweepGroup2;
                orb.type = kOrbType;
                orb.variant = 0;
                orb.pos = spark.pos;
                orb.velocity.x = 0;
                orb.velocity.y = 0;
                const uint32_t r = rngStep();
                orb.acceleration.y = 0x20;
                orb.rotation.x = 0;
                orb.velocity.z = int16_t(uint16_t((0xffffffc0u - (r & 0x1f)) << 2));
                orb.rotation.y = spark.rotation.y;
                orb.rotation.z = 0;
                shot.life = int16_t((rngStep() & 0x7f) + 0x80u);
                shot.damage = int8_t(params.damage);
            }
            freeActor(spark); // exe 0x5e1540
            return;
        }
        default: return;
        }
    }
    default: break;
    }
    switch (SparkVariant(spark.variant)) {
    case SparkVariant::Orb: followBoss(spark, true); break;
    case SparkVariant::Volley:
    case SparkVariant::Aimed: followBoss(spark, false); break;
    default: break;
    }
    const int16_t c[3] = {spark.pos.x.whole, spark.pos.y.whole, spark.pos.z.whole};
    spark.velocity.z = int16_t((0x10 - int(int8_t(params.counter))) * 2);
    for (int i = 15; i >= 0; --i) {
        SparkParticle& particle = fields.particles[size_t(i)];
        if (uint16_t(particle.life) == 0) continue;
        const int life = particle.life - 1;
        particle.life = int16_t(life);
        const uint32_t colour = sparkColour(int16_t(life), spark.variant);
        if (particle.life < 7) { // pulled toward the core
            const int16_t p[3] = {particle.point.x, particle.point.y, particle.point.z};
            const uint32_t pitch = uint32_t(pitchOf(c, p)) & kAngleMask;
            const int h = (fixedCos(int(pitch)) * spark.velocity.z) >> 12;
            const int dy = (fixedSin(int(pitch)) * spark.velocity.z) >> 12;
            const uint32_t yaw = uint32_t(angleOf(c, p)) & kAngleMask;
            particle.point.x = int16_t(uint16_t(particle.point.x) + uint16_t((fixedSin(int(yaw)) * h) >> 12));
            particle.point.y = int16_t(uint16_t(particle.point.y) + uint16_t(dy));
            particle.point.z = int16_t(uint16_t(particle.point.z) + uint16_t((fixedCos(int(yaw)) * h) >> 12));
        }
        disc(spark.at, dc::ringColour(colour), particle.xyWord(), uint16_t(particle.point.z), 0x200000);
        disc(spark.at, dc::ringColour(colour), particle.xyWord(), uint16_t(particle.point.z), 0x100000);
    }
    const uint32_t colour = sparkColour(int8_t(params.subCounter), spark.variant);
    const uint32_t xy = dc::packXY(c[0], c[1]);
    const int16_t r = t16((0x10 - int(int16_t(int8_t(params.counter)))) << 3);
    disc(spark.at, dc::ringColour(colour), xy, uint16_t(c[2]), uint32_t(int32_t(r)) << 16);
    disc(spark.at, dc::ringColour(colour), xy, uint16_t(c[2]), uint32_t(int32_t(int16_t(r >> 1))) << 16);
}
// exe 0x5bb0a0: variant 0 fades to 8, then 4 shots type 0x23 (sound 0x1e0, yaws 0x400 apart), drops (the burst state
// once it slows); variant 4 fades to 8, then 6 aimed shots type 0 at the player (each with a burst spark, variant 2)
// every 0x10 ticks, fades out, freed. Drawn: the core disc.
void sparkFire(StaticActor& spark, SparkFields& fields) {
    BossSparkParams params(spark);
    switch (FirePhase(spark.step)) {
    case FirePhase::Start:
        params.counter = 0x10;
        params.subCounter = 0;
        if (SparkVariant(spark.variant) == SparkVariant::Volley) spark.step = uint8_t(FirePhase::FadeVolley);
        else if (SparkVariant(spark.variant) == SparkVariant::Aimed) spark.step = uint8_t(FirePhase::FadeAimed);
        break;
    case FirePhase::FadeVolley:
        params.counter -= 1u;
        if (int8_t(params.counter) < 8) {
            soundPlayAtPos(kSparkShotSound, spark.pos);
            uint32_t yaw = (rngStep() & 0xf) << 8;
            for (int n = 4; n > 0; --n, yaw += 0x400) sparkShot(spark, kVolleyShotType, 1, uint16_t(yaw));
            spark.velocity.y = 0;
            spark.step = uint8_t(FirePhase::Drop);
        }
        followBoss(spark, false);
        break;
    case FirePhase::Drop:
        spark.velocity.y = int16_t(uint16_t(spark.velocity.y) + 0x20u);
        integrateVelocity(spark, 0, spark.velocity.y, 0); // exe 0x5e3690
        if (std::abs(int(spark.velocity.y)) <= 0x20) {
            spark.enterState(uint8_t(SparkState::Burst));
            return;
        }
        break;
    case FirePhase::FadeAimed:
        params.counter -= 1u;
        if (int8_t(params.counter) < 8) {
            fields.shotsLeft = 6;
            spark.step = uint8_t(FirePhase::Aimed);
        }
        break;
    case FirePhase::Aimed:
        if (params.subCounter == 0) {
            soundPlayAtPos(kSparkShotSound, spark.pos);
            if (const uint32_t s = allocActor40(kActorStageClass)) { // exe 0x5e1280
                ProjectileActor& shot = projectileActorAt(s);
                BossShotParams shotParams(projectileActorAt(s));
                shot.flags = kActorLive;
                shot.loopFlags |= kActorSweepGroup2;
                shot.type = kAimedShotType;
                shot.pos = spark.pos;
                shot.velocity.setXY(0, 0);
                shot.velocity.z = -0x500;
                // aimed at the player's point 0x80 up (the pitch from the integer parts, the yaw from the 16.16 x / z)
                const Player& p = player();
                const int16_t from[3] = {spark.pos.x.whole, spark.pos.y.whole, spark.pos.z.whole};
                const int16_t to[3] = {p.pos.x.whole, int16_t(uint16_t(p.pos.y.whole) - 0x80u), p.pos.z.whole};
                shot.rotation.x = int16_t(pitchOf(from, to) + 0x800);
                shot.rotation.y = int16_t(angleOf(spark.pos.x, spark.pos.z, p.pos.x, p.pos.z) + 0x800);
                shot.rotation.z = 0;
                shotParams.life = 0x96;
                shotParams.damage = int8_t(params.damage);
            }
            if (const uint32_t b = allocActor60(kActorStageClass)) { // exe 0x5e12f0
                StaticActor& burst = staticActorAt(b);
                burst.flags = kActorLive;
                burst.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
                burst.type = kBossSparkType;
                burst.subtype = 0;
                burst.variant = uint8_t(SparkVariant::BurstA);
                burst.pos = spark.pos;
            }
            params.subCounter = 0x10;
            fields.shotsLeft -= 1u;
            if (fields.shotsLeft == 0) spark.step = uint8_t(FirePhase::FadeOut);
        } else {
            params.subCounter -= 1u;
        }
        break;
    case FirePhase::FadeOut:
        params.counter -= 1u;
        if (params.counter == 0) {
            freeActor(spark); // exe 0x5e1540
            return;
        }
        break;
    default: break;
    }
    const uint32_t colour = dc::ringColour(sparkColour(int8_t(params.counter), spark.variant));
    const uint32_t xy = dc::packXY(spark.pos.x.whole, spark.pos.y.whole);
    disc(spark.at, colour, xy, uint16_t(spark.pos.z.whole), 0x600000);
    disc(spark.at, colour, xy, uint16_t(spark.pos.z.whole), 0x400000);
}
// exe 0x5bb4a0: 16 particles from the centre in random directions (0x20 a tick) for 8 ticks, freed
void sparkBurst(StaticActor& spark, SparkFields& fields) {
    BossSparkParams params(spark);
    switch (BurstPhase(spark.step)) {
    case BurstPhase::Start:
        for (int i = 15; i >= 0; --i) {
            SparkParticle& particle = fields.particles[size_t(i)];
            particle.point.x = spark.pos.x.whole;
            particle.point.y = spark.pos.y.whole;
            particle.point.z = spark.pos.z.whole;
            particle.life = int16_t(rngStep() & 0x3f3fu);
        }
        spark.velocity.z = 0x20;
        params.counter = 8;
        spark.step = uint8_t(BurstPhase::Fly);
        [[fallthrough]];
    case BurstPhase::Fly:
        params.counter -= 1u;
        if (params.counter == 0) spark.step = uint8_t(BurstPhase::End);
        break;
    case BurstPhase::End: freeActor(spark); return; // exe 0x5e1540
    default: break;
    }
    const uint32_t colour = dc::ringColour(sparkColour(int8_t(params.counter), spark.variant));
    for (int i = 15; i >= 0; --i) {
        SparkParticle& particle = fields.particles[size_t(i)];
        const int direction = particle.life;
        const uint32_t pitch = uint32_t(direction >> 2) & 0xfc0u, yaw = (uint32_t(direction) & 0x3f) << 6;
        const int speed = spark.velocity.z;
        const int v = fixedSin(int(pitch)) * speed;
        const int h = (fixedCos(int(pitch)) * speed) >> 12;
        particle.point.x = int16_t(uint16_t(particle.point.x) + uint16_t((fixedSin(int(yaw)) * h) >> 12));
        particle.point.y = int16_t(uint16_t(particle.point.y) + uint16_t(v >> 12));
        particle.point.z = int16_t(uint16_t(particle.point.z) + uint16_t((fixedCos(int(yaw)) * h) >> 12));
        disc(spark.at, colour, particle.xyWord(), uint16_t(particle.point.z), 0x200000);
        disc(spark.at, colour, particle.xyWord(), uint16_t(particle.point.z), 0x100000);
    }
}
// exe 0x5bab10
void sparkEffect(StaticActor& spark) {
    SparkFields& fields = classState<SparkFields>(spark);
    switch (SparkState(spark.state)) {
    case SparkState::Start: // exe 0x5bab30
        spark.flags &= uint8_t(~kActorDrawn);
        switch (SparkVariant(spark.variant)) {
        case SparkVariant::Volley:
        case SparkVariant::Orb:
        case SparkVariant::Aimed:
            spark.enterState(uint8_t(SparkState::Gather));
            sparkGather(spark, fields);
            return;
        case SparkVariant::BurstA:
        case SparkVariant::BurstB:
            spark.enterState(uint8_t(SparkState::Burst));
            sparkBurst(spark, fields);
            return;
        default: return;
        }
    case SparkState::Gather: sparkGather(spark, fields); return;
    case SparkState::Fire: sparkFire(spark, fields); return;
    case SparkState::Burst: sparkBurst(spark, fields); return;
    default: notPortedAt(kStoryPort, "0x5bab10 state", spark.state);
    }
}

// ==== 0x423f40: the lightning bolt ===================================================================================
enum class BoltState : uint8_t { Start = 0, Run = 1 };
enum class BoltPhase : uint8_t { Start = 0, Fade = 1, Done = 2 };
// the zig-zag's step table (exe data: s8, 32 entries)
const DataTable<ScalarTable<int8_t, 32>> boltZigZag{TableId::PlatformBossBoltZigZag};

// exe 0x4247b0(out, v): v (to 7) * 0xff * 32 >> 8 as {bluish (e / 2, e / 2, e), grey (e, e, e)}
void boltColours(uint32_t out[2], uint32_t v) {
    if (v > 7) v = 7;
    const uint32_t e = (((v << 8) - v) << 5) >> 8;
    out[1] = (e << 8 | e) << 8 | e;
    out[0] = ((((e & 0xfe) | (e << 9)) << 7)) | (e >> 1);
}
// exe 0x423f60: the width / amplitude from the parameters, the angles and the fade from the state bytes; the parent's
// index made a pool index
void boltStart(ControllerActor& bolt, BoltFields& fields) {
    fields.halfWidth = uint16_t(int32_t(fields.widthParam.get()));
    fields.amplitude = uint16_t(int32_t(fields.amplitudeParam.get()));
    fields.startAngle = bolt.subState;
    fields.angleStep = bolt.step;
    fields.fade = bolt.stepCounter;
    bolt.state = uint8_t(BoltState::Run);
    bolt.step = 0;
    switch (BoltParent(bolt.variant)) {
    case BoltParent::Static: fields.parent = int16_t(uint16_t(fields.parent) - kStaticClass); break;
    case BoltParent::Projectile: fields.parent = int16_t(uint16_t(fields.parent) - kProjectileClass); break;
    case BoltParent::Animated: fields.parent = int16_t(uint16_t(fields.parent) - kAnimatedClass); break;
    default: break;
    }
}
// exe 0x423fb0: it follows its parent's move; drawn (its base within the effects' depth range; 3 RNG steps) as a
// zig-zag of segments (their lengths the length >> 5 << the step table's, sideways steps by the 64-step angle) up to
// its length, each segment 3 POLY_G4 0x3a (abr 1) quads: two gouraud wings of the half width (black edges, a bluish
// core) across the player's view direction and a grey core of the half width >> 3; the half width halves twice a tick
// in the last 7 fade ticks. Port: the quads' corners kept as camera-input points (dc::otQuadG4, no screen offset).
void boltTick(ControllerActor& bolt, BoltFields& fields) {
    uint32_t colours[2] = {0, 0}; // the stack colours (written in the fade, the only phase that draws)
    switch (BoltPhase(bolt.step)) {
    case BoltPhase::Start:
        bolt.step = uint8_t(BoltPhase::Fade);
        [[fallthrough]];
    case BoltPhase::Fade:
        boltColours(colours, fields.fade);
        fields.fade -= 1u;
        if (fields.fade == 0) bolt.step += 1u;
        break;
    case BoltPhase::Done: freeActor(bolt); return; // exe 0x5e15c0
    default: break;
    }
    const int idx = fields.parent;
    uint32_t parent = 0;
    switch (BoltParent(bolt.variant)) {
    case BoltParent::Static: parent = kStaticPool + uint32_t(idx) * kStaticSlotSize; break;
    case BoltParent::Projectile: parent = kProjectilePool + uint32_t(idx) * 0xdc; break;
    case BoltParent::Animated: parent = kAnimatedPool + uint32_t(idx) * kAnimatedSlotSize; break;
    default: break;
    }
    if (parent) {
        const Actor& p = actorAt(parent);
        fields.pos.x = int16_t(uint16_t(fields.pos.x) + uint16_t(uint16_t(p.pos.x.whole) - uint16_t(p.prevPos.x.whole)));
        fields.pos.y = int16_t(uint16_t(fields.pos.y) + uint16_t(uint16_t(p.pos.y.whole) - uint16_t(p.prevPos.y.whole)));
        fields.pos.z = int16_t(uint16_t(fields.pos.z) + uint16_t(uint16_t(p.pos.z.whole) - uint16_t(p.prevPos.z.whole)));
    }
    const int sh = stage().coordinateShift & 0x1f;
    int16_t B[3] = {t16(fields.pos.x << sh), t16(fields.pos.y << sh), t16(fields.pos.z << sh)};
    const int16_t topY = t16(int(uint16_t(uint32_t(fields.length) << sh)) + B[1]);
    const CameraPoint c0 = cameraPoint(B);
    if (c0.view[2] > int32_t(uint32_t(stage().effectsFar) * 4) || c0.view[2] < 0x30) return;
    const uint32_t r1 = rngStep(), r2 = rngStep();
    const uint32_t k = (0x40u - ((uint16_t(player().rotation.y) >> 6) & 0x3f)) & 0x3f;
    const uint8_t step = uint8_t(fields.angleStep - (r2 & 3));
    uint8_t d = uint8_t(r1 & 0x1f);
    const int ck = fixedCos64(k), sk = fixedSin64(k);
    const int w = fields.halfWidth;
    const int16_t offX = t16((ck * w) >> 12), offZ = t16((sk * w) >> 12);
    const int16_t offX2 = t16((ck * (w >> 3)) >> 12), offZ2 = t16((sk * (w >> 3)) >> 12);
    const uint32_t r3 = rngStep();
    uint8_t ang = uint8_t((r3 & 3) + fields.startAngle + step);
    const int amp = fields.amplitude;
    const uint32_t l5 = uint32_t(fields.length) >> 5;
    uint16_t acc = 0;
    struct Pt {
        int16_t v[3];
    };
    auto pt = [](int16_t x, int16_t y, int16_t z) { return Pt{{x, y, z}}; };
    // the previous points: the wing + edge, the centre, the wing - edge, the core's two
    Pt pe = pt(B[0], B[1], B[2]), pc = pe, pm = pe, pk1 = pe, pk2 = pe;
    int16_t psx = c0.sx, psy = c0.sy; // the screen point of the previous centre
    for (bool more = true; more;) {
        const int8_t sv = boltZigZag[d & 0x1f];
        const uint32_t i = (uint32_t(ang) + uint32_t(int32_t(sv))) & 0x3f;
        B[0] = t16(B[0] + ((fixedCos64(i) * amp) >> 12));
        B[2] = t16(B[2] + ((fixedSin64(i) * amp) >> 12));
        const uint32_t e = l5 << (uint32_t(int32_t(sv)) & 31);
        B[1] = t16(B[1] + int(int16_t(e)));
        ++d;
        ang = uint8_t(ang + step);
        acc = uint16_t(acc + e);
        if (int16_t(acc) > int(fields.length)) {
            more = false;
            B[1] = topY;
        }
        const bool on = uint16_t(psx + 0x20) <= 0x160 && uint16_t(psy + 0x100) <= 0x1f0;
        const Pt np = pt(t16(B[0] + offX), B[1], t16(B[2] + offZ)), nc = pt(B[0], B[1], B[2]),
                 nm = pt(t16(B[0] - offX), B[1], t16(B[2] - offZ)), nk1 = pt(t16(B[0] + offX2), B[1], t16(B[2] + offZ2)),
                 nk2 = pt(t16(B[0] - offX2), B[1], t16(B[2] - offZ2));
        if (on) {
            static constexpr float kNoOff[4][2] = {};
            auto quad = [&](const Pt& q0, const Pt& q1, const Pt& q2, const Pt& q3, const uint32_t c[4]) {
                int16_t v[4][3];
                const Pt* qs[4] = {&q0, &q1, &q2, &q3};
                for (int j = 0; j < 4; ++j)
                    for (int x = 0; x < 3; ++x) v[j][x] = qs[j]->v[x];
                dc::otQuadG4(bolt.at, c, 1, v, kNoOff);
            };
            const uint32_t wings[4] = {0x3a000000u, colours[0], 0, colours[0]};
            quad(pe, pc, np, nc, wings);
            quad(pm, pc, nm, nc, wings);
            const uint32_t core[4] = {colours[1] | 0x3a000000u, colours[1], colours[1], colours[1]};
            quad(pk1, pk2, nk1, nk2, core);
        }
        const CameraPoint cn = cameraPoint(nc.v);
        psx = cn.sx;
        psy = cn.sy;
        pe = np;
        pc = nc;
        pm = nm;
        pk1 = nk1;
        pk2 = nk2;
    }
    if (fields.fade < 7) {
        uint16_t v = fields.halfWidth;
        v = uint16_t(v - (v >> 1));
        v = uint16_t(v - (v >> 1));
        fields.halfWidth = v;
    }
}
void lightningBolt(ControllerActor& bolt) {
    BoltFields& fields = classState<BoltFields>(bolt);
    switch (BoltState(bolt.state)) {
    case BoltState::Start: boltStart(bolt, fields); return;
    case BoltState::Run: boltTick(bolt, fields); return;
    default: notPortedAt(kStoryPort, "0x423f40 state", bolt.state);
    }
}

// ==== 0x5c2830 / 0x5d0770: the aimed shot and the homing orb =========================================================
enum class ShotState : uint8_t { Start = 0, Fly = 1, End = 2 };
enum class OrbPhase : uint8_t { Start = 0, FadeIn = 1, Home = 2 };
const DataTable<CollisionBoxRecord> aimedShotBox{TableId::PlatformBossAimedShotBox};
const DataTable<CollisionBoxRecord> orbBox{TableId::PlatformBossOrbBox};
constexpr uint16_t kAimedShotSound = 0xa9;

// exe 0x5c2870 / 0x5d07b0: the flight, the box, kept through warps, the hit box's previous place, the spin random
void shotStart(ProjectileActor& shot, ShotFields& fields, uint32_t box) {
    shot.enterState(uint8_t(ShotState::Fly));
    shot.collisionBox.pointAt(box);
    shot.runFlags |= kActorWarpKept;
    fields.hit.previousX = shot.pos.x.whole;
    fields.hit.previousZ = shot.pos.z.whole;
    fields.hit.result = 0;
    fields.spin = int16_t((rngStep() & 0x1f) << 7);
}
// exe 0x5c28f0: the flight along its rotation by its velocity; its life (its end: the flash variant 1), the shield,
// the walls (the flash 0) and the hits (the hit spark) end it; a hit entry and a spinning billboard (draw command 0x2c)
void aimedShotFly(const PlayerEnv& env, ProjectileActor& shot, ShotFields& fields) {
    BossShotParams params(shot);
    moveAlongRotation(shot.pos, shot.rotation, shot.velocity.x, shot.velocity.y, shot.velocity.z); // exe 0x5e37b0
    if (params.life > 0) {
        params.life = int16_t(uint16_t(params.life) - 1u);
        if (params.life == 0) {
            ruinShotFlash(shot, 1);
            shot.enterState(uint8_t(ShotState::End));
            return;
        }
    }
    if (playerShieldBlocks(shot, 0x20)) {
        shot.enterState(uint8_t(ShotState::End));
        return;
    }
    if (int32_t(actorTestMove(env, shot)) < 0) {
        ruinShotFlash(shot, 0);
        shot.enterState(uint8_t(ShotState::End));
        return;
    }
    const int16_t pos[3] = {shot.pos.x.whole, shot.pos.y.whole, shot.pos.z.whole};
    if (fields.hit.result & kHitAttackBump) {
        ruinShotHitSpark(shot);
        shot.enterState(uint8_t(ShotState::End));
        return;
    }
    fields.hit.previousX = shot.prevPos.x.whole;
    fields.hit.previousZ = shot.prevPos.z.whole;
    fields.hit.result = 0;
    hitListSubmit(pos, fields.hit,
                  hitOwner(shot.slot) | hitGroup(kHitGroupHeavyThrown) | kHitContactMotion | hitRadius(0x18),
                  uint32_t(int32_t(params.damage)) | kHitAttackStrong); // exe 0x5e4120
    dc::emit({0u, 0x2e808080u, uint32_t(uint16_t(shot.pos.x.whole)) | (uint32_t(int32_t(shot.pos.y.whole)) << 16),
              0x7d1060e0u, uint32_t(int32_t(shot.pos.z.whole)), 0x2f60ffu,
              (uint32_t(int32_t(fields.spin)) << 16) | 0x2du, 0x7fe0u, 0u, 0x7fffu},
             shot.at, false);
    fields.spin = int16_t(uint16_t(fields.spin) + 0x100u);
}
// exe 0x5d0bd0: the orb's end: a burst spark (static type 0x5a variant 3) and a blast (the global static type 7)
void orbBurst(const ProjectileActor& orb) {
    if (const uint32_t s = allocActor60(kActorStageClass)) { // exe 0x5e12f0
        StaticActor& spark = staticActorAt(s);
        spark.flags = kActorLive;
        spark.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
        spark.type = kBossSparkType;
        spark.subtype = 0;
        spark.variant = uint8_t(SparkVariant::BurstB);
        spark.pos = orb.pos;
    }
    const uint32_t b = allocActor60(0); // exe 0x5e1300
    if (!b) return;
    StaticActor& blast = staticActorAt(b);
    auto&& params = BlastParams(actorHeaderAt(b));
    blast.loopFlags |= kActorSweepGroup2;
    blast.flags = kActorLive;
    blast.type = typeByte(GlobalStaticType::Blast);
    blast.variant = 0;
    blast.pos = orb.pos;
    params.size = 0x200;
    params.attack = uint16_t(int32_t(BossShotParams(orb).damage));
}
// exe 0x5d0c60(out, v): the 0..0xf ramp as a colour (red = green / 2, no blue)
uint32_t orbColour(uint32_t v) {
    uint32_t c;
    if (v > 0xf) c = 0;
    else if (v > 7) c = 0x1e0 - (v << 5);
    else c = v << 5;
    const uint32_t e = (c << 8) - c;
    return ((((e & 0xfe00) | (e >> 8)) >> 1)) | ((e & 0xff00) << 8);
}
// a bolt (pool 0xe0 type 0xf variant 2) hanging from the orb, its shape random
void orbBolt(const ProjectileActor& orb) {
    const uint32_t e = allocActorE0(); // exe 0x5e1440
    if (!e) return;
    ControllerActor& bolt = controllerActorAt(e);
    BoltFields& fields = classState<BoltFields>(bolt);
    bolt.flags = kActorLive;
    bolt.loopFlags |= kActorLateLoop;
    bolt.type = kBoltType;
    bolt.variant = uint8_t(BoltParent::Projectile);
    fields.pos.x = orb.pos.x.whole;
    fields.pos.y = int16_t(uint16_t(orb.pos.y.whole) - 0x40u);
    fields.pos.z = orb.pos.z.whole;
    fields.parent = int16_t(uint16_t(orb.slot));
    fields.length = 0x80;
    fields.widthParam = int8_t(0x10u - (rngStep() & 7));
    fields.amplitudeParam = int8_t(0xau - (rngStep() & 7));
    bolt.subState = uint8_t(rngStep() & 0x3f); // its start angle
    if (rngStep() & 1) bolt.step = uint8_t((rngStep() & 3) + 8u); // its angle step
    else bolt.step = uint8_t(0xf8u - (rngStep() & 3));
    bolt.stepCounter = uint8_t(0x10u - (rngStep() & 7)); // its fade
}
// exe 0x5d0820: the fade in (to 8) facing away from the player, then it homes on the player (turn 0x20, pitch toward
// the chest, bolts at random) until its life ends -> the burst; the shield / the walls / the hits -> the burst; a hit
// entry and a disc (draw command 0xf0)
void homingOrbFly(const PlayerEnv& env, ProjectileActor& orb, ShotFields& fields) {
    orb.savePreviousPose();
    orb.wasAirborne = orb.airborne;
    Player& p = player();
    switch (OrbPhase(orb.step)) {
    case OrbPhase::Start:
        fields.fade = 0x10;
        orb.step = uint8_t(OrbPhase::FadeIn);
        fields.orbTicks = 0x100;
        orb.velocity.z = 0x100;
        break;
    case OrbPhase::FadeIn:
        fields.fade -= 1u;
        if (int8_t(fields.fade) < 8) {
            orb.rotation.y = int16_t(angleOf(p.pos, orb.pos));
            orb.step = uint8_t(OrbPhase::Home);
        }
        break;
    case OrbPhase::Home: {
        if ((rngStep() & 7) == 0) orbBolt(orb);
        turnYawTo(orb.rotation.y, uint32_t(angleOf(p.pos, orb.pos)), 0x20);
        // the pitch from the player's point 0x80 up (integer parts)
        const int16_t from[3] = {p.pos.x.whole, int16_t(uint16_t(p.pos.y.whole) - 0x80u), p.pos.z.whole};
        const int16_t to[3] = {orb.pos.x.whole, orb.pos.y.whole, orb.pos.z.whole};
        const uint32_t pitch = uint32_t(pitchOf(from, to)) & kAngleMask;
        integrateVelocity(orb, 0, 0x40, (fixedCos(int(pitch)) * orb.velocity.z) >> 12); // exe 0x5e3690
        if (fields.orbTicks > 0) {
            fields.orbTicks = int16_t(uint16_t(fields.orbTicks) - 1u);
            if (fields.orbTicks == 0) {
                orbBurst(orb);
                orb.enterState(uint8_t(ShotState::End));
                return;
            }
        }
        break;
    }
    default: break;
    }
    const int16_t pos[3] = {orb.pos.x.whole, orb.pos.y.whole, orb.pos.z.whole};
    if (playerShieldBlocks(orb, 0x20) || int32_t(actorTestMove(env, orb)) < 0 || (fields.hit.result & kHitAttackBump)) {
        orbBurst(orb);
        orb.enterState(uint8_t(ShotState::End));
        return;
    }
    fields.hit.previousX = orb.prevPos.x.whole;
    fields.hit.previousZ = orb.prevPos.z.whole;
    fields.hit.result = 0;
    hitListSubmit(pos, fields.hit,
                  hitOwner(orb.slot) | hitGroup(kHitGroupHeavyThrown) | kHitContactMotion | hitRadius(0x30),
                  uint32_t(int32_t(BossShotParams(orb).damage)) | kHitAttackStrong);
    const uint32_t colour = orbColour(uint32_t(int32_t(int8_t(fields.fade))));
    dc::emit({dc::kRingFullArc, dc::ringColour(0), dc::packXY(pos[0], pos[1]), dc::ringRim(colour),
              dc::zWord(pos[2]) | dc::ringStep(2), dc::ringRadii(0, 0x40)},
             orb.at);
}
void aimedShot(const PlayerEnv& env, ProjectileActor& shot) {
    ShotFields& fields = classState<ShotFields>(shot);
    shot.savePreviousPose();
    shot.wasAirborne = shot.airborne;
    switch (ShotState(shot.state)) {
    case ShotState::Start:
        shotStart(shot, fields, aimedShotBox.stateAddress());
        soundPlayAtPos(kAimedShotSound, shot.pos);
        return;
    case ShotState::Fly: aimedShotFly(env, shot, fields); return;
    case ShotState::End: freeActor(shot); return; // exe 0x5d0bc0 -> 0x5e1500
    default: notPortedAt(kStoryPort, "0x5c2830 state", shot.state);
    }
}
void homingOrb(const PlayerEnv& env, ProjectileActor& orb) {
    ShotFields& fields = classState<ShotFields>(orb);
    orb.savePreviousPose();
    orb.wasAirborne = orb.airborne;
    switch (ShotState(orb.state)) {
    case ShotState::Start: shotStart(orb, fields, orbBox.stateAddress()); return;
    case ShotState::Fly: homingOrbFly(env, orb, fields); return;
    case ShotState::End: freeActor(orb); return; // exe 0x5d0bc0 -> 0x5e1500
    default: notPortedAt(kStoryPort, "0x5d0770 state", orb.state);
    }
}

} // namespace

PlatformBossTarget platformBossTarget(const AnimatedActor& boss) {
    const Waypoint w = waypointAt(classState<BossFields>(boss).waypoint);
    return {w.x, w.z};
}

void platformBossUpdate(const PlayerEnv& env, uint32_t actor) { platformBoss(env, animatedActorAt(actor)); }

void bossSparkUpdate(const PlayerEnv&, uint32_t actor) { sparkEffect(staticActorAt(actor)); }
void lightningBoltUpdate(const PlayerEnv&, uint32_t actor) { lightningBolt(controllerActorAt(actor)); }
void aimedShotUpdate(const PlayerEnv& env, uint32_t actor) { aimedShot(env, projectileActorAt(actor)); }
void homingOrbUpdate(const PlayerEnv& env, uint32_t actor) { homingOrb(env, projectileActorAt(actor)); }

} // namespace od::port
