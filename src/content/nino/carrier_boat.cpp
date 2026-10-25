// The ST17 carrier boat (static type 0x21, exe 0x5a5ea0; hypothesis names), one routine per exe routine.
//
// Variant 0 is the mission's boat (hit points its row's << 4, the two walker spawners (e0 type 0xa variants 1 / 0) and
// the boss gauge at its init); variant 1 the boat that brings walkers during script 0x3d. States [0x8ceddc]: 0 init
// 0x5a6130, 1 0x5a6220 (variant 0: 0x5a6240, sub-states [0x8cede8]: 0 0x5a62e0 wait, 1 0x5a63d0 come in, 2 0x5a6560
// leave, 3 0x5a6670 rise / settle, 4 0x5a67c0 rocked; other variants 0x5a6940), 2 sunk 0x5a6b20 (debris, the blast, id 9
// defeated -> the ST17 spawner variant 3 sets flag 0x29). Its trip: 0 / 1 / 2 (flags 0x680 / 0x681); its damage stage
// (the smoke static, the two type-0x26 fires); its gun cycle.
#include "content/nino/carrier_boat.hpp"
#include "content/nino/stage_timer.hpp"
#include "content/nino/carrier_boat_state.hpp"
#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/stage_smoke.hpp"

#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/enemy_shot.hpp"
#include "game/explosions.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/actor_collision.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace carrier_boat_state;

namespace {

constexpr uint8_t kRamming = 0x01;
enum class BoatTrip : uint8_t { Waiting = 0, In = 1, Out = 2 };
enum class BoatDamage : uint8_t { Whole = 0, Smoking = 1, Burning = 2 };

// [0x8ceddc]
enum class BoatState : uint8_t { Init = 0, Main = 1, Sunk = 2 };
// [0x8cede8] (variant 0)
enum class BoatSub : uint8_t { Wait = 0, ComeIn = 1, Leave = 2, Rise = 3, Rocked = 4 };
// the ferry's sub-states (variant 1)
enum class FerrySub : uint8_t { Start = 0, Sail = 1, Stop = 2, WaitScript = 3, Walker1 = 4, Walker2 = 5, Walker3 = 6 };
enum class SunkSub : uint8_t { Start = 0, Sinking = 1 };
enum class ComeInStep : uint8_t { Turn = 0, Sail = 1, Steer = 2, Arrive = 3 };
enum class LeaveStep : uint8_t { Steer = 0, Stop = 1, Sail = 2, Arrive = 3 };
enum class RiseStep : uint8_t { Init = 0, Back = 1, Brake = 2, Forward = 3, Ram = 4, Settle = 5, Wait = 6 };

// A fire on the boat (stage static type 0x26): its owner's slot, put out (set by the owner), its size; its place
// relative to the owner
struct BoatFireParams {
    explicit BoatFireParams(const StaticActor& actor) : BoatFireParams(const_cast<StaticActor&>(actor)) {}
    explicit BoatFireParams(StaticActor& actor) : owner(actor.params[0]), putOut(actor.params[1]), size(actor.params[2]) {}
    uint8_t& owner;
    uint8_t& putOut;
    uint8_t& size;
};
// A fire's place on the boat (exe data, 8 bytes: z, yaw, y)
struct FirePlace : DataRecord {
    static constexpr uint32_t kBytes = 0x6;
    int16_t z = 0; // +0x0
    int16_t yaw = 0; // +0x2
    int16_t y = 0; // +0x4
    template <class R> static FirePlace load(const R& r) {
        FirePlace v;
        v.z = r.template read<int16_t>(0x0);
        v.yaw = r.template read<int16_t>(0x2);
        v.y = r.template read<int16_t>(0x4);
        return v;
    }
};
// A gun of the boat (exe data, 8 bytes): its offset x / z (>> 4) and its yaw offset
struct BoatGun : DataRecord {
    int16_t x = 0;
    int16_t z = 0;
    int16_t yaw = 0;
    static BoatGun load(const TableReader& r) {
        BoatGun rec;
        rec.x = r.s16(0x0);
        rec.z = r.s16(0x4);
        rec.yaw = r.s16(0x6);
        return rec;
    }
};
// The smoke that follows the boat (stage static type 3 variant 9): the boat
// the smoke's actor followed (its identity word, the hit box's first word)
struct FollowingSmoke {
    explicit FollowingSmoke(const StaticActor& actor) : FollowingSmoke(const_cast<StaticActor&>(actor)) {}
    explicit FollowingSmoke(StaticActor& actor) : followed(actor.hitBox.result) {}
    uint32_t& followed;
};
// The walker the ferry brings (pool 0x20 type 0x2d sub 1 variant 4): its mode, count, the ferry's slot, its tracked id,
// its point
struct FerryWalkerParams {
    explicit FerryWalkerParams(const AnimatedActor& actor) : FerryWalkerParams(const_cast<AnimatedActor&>(actor)) {}
    explicit FerryWalkerParams(AnimatedActor& actor)
        : mode(actor.params[0]), count(actor.params[1]), ferry(actor.params[2]), trackId(actor.params[3]),
          pointX(actor.velocity.spare), pointZ(actor.acceleration.spare) {}
    uint8_t& mode;
    uint8_t& count;
    uint8_t& ferry;
    uint8_t& trackId;
    int16_t& pointX;
    int16_t& pointZ;
};

const DataTable<CollisionBoxRecord> boatBox{TableId::CarrierBoatBox};
const DataTable<RecordTable<BoatGun, 6, 8>> boatGuns{TableId::CarrierBoatGuns};
const DataTable<ScalarTable<int16_t, 16>> sinkingSpread{TableId::CarrierBoatSinkingSpread}; // the sinking's blasts around the boat
const DataTable<ScalarTable<uint8_t, 8>> walkerTicks{TableId::CarrierBoatWalkerTicks}; // the ticks to the next walker by the sub-state
constexpr uint8_t kFollowingSmokeVariant = 9, kFireType = 0x26, kFireSize = 0x21;
constexpr uint8_t kTimerType = 0x1b, kSinkTimerType = 0x18, kWalkerSpawnerType = 10;
constexpr uint8_t kWalkerType = 0x2d, kWalkerSub = 1, kWalkerVariant = 4;
constexpr uint16_t kEngineSound = 0x18d, kHurtSound = 0x8d, kStrongHurtSound = 0x8e;
constexpr int16_t kEngineTicks = 0x14;
constexpr uint32_t kBossGaugeColour = 0xffa020;
constexpr uint32_t kFlagBoatIn = 0x680, kFlagBoatOut = 0x681, kFlagBoatAlarm = 0x682, kFlagBoatRise = 0x683,
                   kFlagWalkersRetreat = 0x684;
constexpr uint32_t kFlagMissionA = 0x32, kFlagMissionB = 0x33;
constexpr uint32_t kIdBoatIn = 7, kIdBoatRise = 8, kIdBoatSunk = 9;
// the trips' points (16.16)
constexpr int32_t kDockX = -0x4500000, kDockZ = int32_t(0xf8800000u), kOutZ = 0x2800000, kAwayX = -0x5c00000;
constexpr int32_t kFerryX = 0x1000000, kFerryZ = int32_t(0xf2000000u);

// the engine sound every 0x14 ticks (exe 0x5a7300)
void engineTick(StaticActor& actor, BoatFields& boat) {
    boat.engineTicks = int16_t(boat.engineTicks - 1);
    if (boat.engineTicks == 0) {
        soundPlayAtPos(kEngineSound, actor.pos);
        boat.engineTicks = kEngineTicks;
    }
}

// exe 0x5a6db0 (lim 0x100) / 0x5a6e30 (lim 0x10): the turn rate toward the angle; 0 once turning the right way
int turnToward(StaticActor& actor, uint32_t angle, int lim) {
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), angle);
    const int dir = d == 0 ? 0 : (d < 1 ? -1 : 1);
    actor.spin.y = int16_t(actor.spin.y + dir);
    if (actor.spin.y > lim) actor.spin.y = int16_t(lim);
    if (actor.spin.y < -lim) actor.spin.y = int16_t(-lim);
    actor.rotation.y = int16_t(actor.rotation.y + (actor.spin.y >> 4));
    return ((int32_t(actor.spin.y) ^ dir) >= 0 && dir != 0) ? 0 : 1;
}
// the bearing from a 16.16 point to the boat
int bearingFrom(const StaticActor& actor, const int32_t point[3]) {
    return angleOf(point[0], point[2], actor.pos.x, actor.pos.z);
}

// exe 0x5a6e90(actor, point, near, max): the speed up to -max along the bearing from the point; -1 within near, 1
// nearer than max
int sailFrom(StaticActor& actor, const int32_t point[3], int nearD, int maxV) {
    actor.velocity.z = int16_t(actor.velocity.z - 2);
    if (actor.velocity.z < -maxV) actor.velocity.z = int16_t(-maxV);
    moveAlongYaw(actor.pos, bearingFrom(actor, point), 0, 0, actor.velocity.z);
    const int d = planarDistance(actor.pos, point);
    if (d <= nearD) return -1;
    return uint32_t(d + maxV) < uint32_t(maxV * 2) ? 1 : 0;
}
// exe 0x5a6f00(actor, point, turn, near): the speed up to -0x80, the yaw turned toward the bearing (turn >> 4), along
// the yaw; -1 within near, 1 nearer than 0x80
int steerFrom(StaticActor& actor, const int32_t point[3], int turn, int nearD) {
    actor.velocity.z = int16_t(actor.velocity.z - 2);
    if (actor.velocity.z < -0x80) actor.velocity.z = -0x80;
    turnYawTo(actor.rotation.y, uint32_t(bearingFrom(actor, point)), turn >> 4);
    moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, actor.velocity.z);
    const int d = planarDistance(actor.pos, point);
    if (d <= nearD) return -1;
    return uint32_t(d + 0x80) < 0x100 ? 1 : 0;
}
// exe 0x5a6f80: decelerate; 1 once stopped
int stopBoat(StaticActor& actor) {
    actor.velocity.z = int16_t(actor.velocity.z + 2);
    if (actor.velocity.z >= 0) {
        actor.velocity.z = 0;
        return 1;
    }
    moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, actor.velocity.z);
    return 0;
}

// exe 0x5a7140(actor, place): a fire (stage static type 0x26) on the boat at the place; its slot
uint8_t lightFire(const StaticActor& actor, const FirePlace& place) {
    const uint32_t s = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!s) return 0;
    StaticActor& fire = staticActorAt(s);
    auto&& params = BoatFireParams(staticActorAt(s));
    fire.flags = kActorLive;
    fire.type = kFireType;
    fire.loopFlags |= kActorSweepGroup2;
    fire.subtype = 0;
    fire.variant = 0;
    params.putOut = 0;
    fire.pos.x.whole = 0;
    params.owner = actor.slot;
    params.size = kFireSize;
    fire.pos.y.whole = place.y;
    fire.pos.z.whole = place.z;
    fire.rotation.y = place.yaw;
    return fire.slot;
}

// exe 0x5a6fc0: 0 nothing, 1 sunk (hit points out), 2 a damage stage reached (the smoke at a quarter, the two fires at
// an eighth); below its row's threshold * 0x10 hit points flag 0x684 (the walkers retreat)
int boatDamage(StaticActor& actor, BoatFields& boat) {
    const int hpMax = actor.maxHitPoints;
    const uint32_t h = boat.hitBox.result, n = h & kHitDamageMask;
    if (boat.hurtTicks != 0 || n == 0 || !(h & kHitAttackByPlayer)) return 0;
    int r = 0;
    actor.tint = kTintFlash;
    if (h & kHitAttackStrongPush) {
        soundPlayAtPos(kStrongHurtSound, actor.pos);
        boat.hurtTicks = 0xf;
    } else {
        soundPlayAtPos(kHurtSound, actor.pos);
    }
    if (hitPointsAdd(actor, -int(n)) != 0) r = 1;
    if (BoatDamage(boat.damage) == BoatDamage::Whole) {
        if (actor.hitPoints < (hpMax >> 2)) {
            if (const uint32_t s = allocActor60(kActorStageClass)) { // exe 0x5e12f0: the smoke on the boat
                StaticActor& smoke = staticActorAt(s);
                smoke.flags = kActorLive;
                smoke.type = kSmokeType;
                smoke.subtype = 0;
                smoke.variant = kFollowingSmokeVariant;
                smoke.loopFlags |= kActorSweepGroup2;
                smoke.pos.x = actor.pos.x;
                smoke.pos.y = actor.pos.y;
                smoke.setParamsWord(0);
                FollowingSmoke(staticActorAt(s)).followed = slotIdentityWord(actor);
                smoke.pos.z = actor.pos.z;
                boat.damage = uint8_t(BoatDamage::Smoking);
                if (r == 0) r = 2;
            }
        }
    } else if (BoatDamage(boat.damage) == BoatDamage::Smoking && actor.hitPoints < (hpMax >> 3)) {
        boat.fires[0] = lightFire(actor, dataRecord<FirePlace>(stateAddress(TableId::CarrierBoatFirePlaceA), FirePlace::kBytes));
        boat.fires[1] = lightFire(actor, dataRecord<FirePlace>(stateAddress(TableId::CarrierBoatFirePlaceB), FirePlace::kBytes));
        boat.damage = uint8_t(BoatDamage::Burning);
        if (r == 0) r = 2;
    }
    // the row's byte +8: the hit points (/ 0x10) below which the walkers retreat
    if (actor.hitPoints < int(staticStats(actor.type, 0).pushThreshold) * 0x10) setStoryFlag(kFlagWalkersRetreat);
    return r;
}

// exe 0x5a71a0(actor, gun, low): an enemy shot from the gun, aimed at the player within +-0x200 of the gun's yaw (0x5a
// lower when the boat is not in view or `low`)
void fireGun(const StaticActor& actor, int gun, int low) {
    const uint32_t s = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!s) return;
    ProjectileActor& shot = projectileActorAt(s);
    auto&& params = EnemyShotFields(projectileActorAt(s));
    shot.flags = kActorLive;
    shot.type = kEnemyShotType;
    shot.variant = 0;
    const BoatGun g = boatGuns[size_t(gun)];
    int32_t o[3];
    rotateByYaw(actor.rotation.y, int(g.x) << 4, 0, int(g.z) << 4, o); // exe 0x5e38d0
    shot.pos.x = int32_t(uint32_t(actor.pos.x) + uint32_t(o[0]));
    shot.pos.y = int32_t(uint32_t(actor.pos.y) - 0xaa0000u);
    shot.pos.z = int32_t(uint32_t(actor.pos.z) + uint32_t(o[2]));
    const Player& p = player();
    int32_t py = p.pos.y;
    const uint32_t yaw = uint32_t(int(g.yaw) + int(actor.rotation.y));
    int d = angleDiff(yaw, uint32_t(angleOf(p.pos, shot.pos)));
    if (d > 0x200) d = 0x200;
    if (d < -0x200) d = -0x200;
    shot.rotation.y = int16_t(d + int(yaw));
    if (pointVisibleOrNear(actor.pos, 0) == 0 || low != 0) py -= 0x5a0000;
    const int16_t from[3] = {shot.pos.x.whole, shot.pos.y.whole, shot.pos.z.whole};
    const int16_t to[3] = {p.pos.x.whole, int16_t(uint32_t(py) >> 16), p.pos.z.whole};
    shot.rotation.x = int16_t(pitchOf(from, to) + 0x800); // exe 0x5e3ca0
    shot.rotation.z = 0;
    params.ticks = 0x2d;
    params.attack = staticStats(actor.type, 0).power;
}

// exe 0x5a6880: the player within 2000 and off to a side -> the side's guns, one on the frames the gun cycle allows
void boatGunsTick(StaticActor& actor, BoatFields& boat) {
    if (planarDistance(player().pos, actor.pos) >= 2000) {
        boat.gunCycle = 0;
        return;
    }
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(player().pos, actor.pos)));
    if ((gameState().frame & 0x23) != 0) return;
    uint8_t c = uint8_t(boat.gunCycle + 1);
    if (c > 0xc) c = 0;
    boat.gunCycle = c;
    const int u = c >> 2;
    if (d > 0x200) fireGun(actor, u + 3, u & 2);
    else if (d < -0x200) fireGun(actor, u, u & 2);
}

// the sub-state s entered (the step cleared)
void enterSub(StaticActor& actor, BoatSub s) {
    actor.subState = uint8_t(s);
    actor.enterStep(0);
}

// exe 0x5a62e0: waiting -- at the start, until a mission flag or a quarter of the hit points lost (the timer
// controller, then the come-in); docked, until flag 0x683 or half the hit points (the rise, but once id 8 is down) or
// id 7 down (the leave)
void boatWait(StaticActor& actor, BoatFields& boat) {
    const int hpMax = actor.maxHitPoints;
    boat.busy = 0;
    if (BoatTrip(boat.trip) == BoatTrip::Waiting) {
        if (!storyFlag(kFlagMissionA) && !storyFlag(kFlagMissionB) && !storyFlag(kFlagBoatAlarm) &&
            hpMax - (hpMax >> 2) <= actor.hitPoints)
            return;
        if (const uint32_t e = allocActorE0()) { // exe 0x5e1440: the e0 timer 0x22
            ControllerActor& timer = controllerActorAt(e);
            auto&& params = StageTimerParams(controllerActorAt(e));
            timer.flags = kActorLive;
            timer.type = kTimerType;
            timer.variant = 0;
            params.value = 0x22;
            params.limit = 0x7f;
            params.flag = -1;
        }
        boat.engineTicks = 1;
        enterSub(actor, BoatSub::ComeIn);
    } else if (BoatTrip(boat.trip) == BoatTrip::In) {
        if ((storyFlag(kFlagBoatRise) || actor.hitPoints < (hpMax >> 1)) && !enemyDefeated(kIdBoatRise))
            enterSub(actor, BoatSub::Rise);
        if (enemyDefeated(kIdBoatIn)) {
            boat.engineTicks = 1;
            enterSub(actor, BoatSub::Leave);
        }
    }
}

// exe 0x5a63d0: to (-0x450, -0x780), then (id 7 down -> the leave) or stopped (flag 0x680)
void boatComeIn(StaticActor& actor, BoatFields& boat) {
    boat.busy = 1;
    const int32_t dock[3] = {kDockX, 0, kDockZ};
    switch (ComeInStep(actor.step)) {
    case ComeInStep::Turn:
        if (turnToward(actor, uint32_t(bearingFrom(actor, dock)), 0x100) != 0) {
            actor.enterStep(uint8_t(ComeInStep::Sail));
            engineTick(actor, boat);
            return;
        }
        break;
    case ComeInStep::Sail: {
        turnToward(actor, uint32_t(bearingFrom(actor, dock)), 0x10);
        const int32_t abeam[3] = {kDockX, 0, actor.pos.z};
        if (sailFrom(actor, abeam, 0, 0x80) > 0) {
            actor.enterStep(uint8_t(ComeInStep::Steer));
            engineTick(actor, boat);
            return;
        }
        break;
    }
    case ComeInStep::Steer:
        if (steerFrom(actor, dock, 0x40, 0x100) < 0) {
            actor.enterStep(uint8_t(ComeInStep::Arrive));
            engineTick(actor, boat);
            return;
        }
        break;
    case ComeInStep::Arrive:
        if (enemyDefeated(kIdBoatIn)) {
            boat.trip = uint8_t(BoatTrip::In);
            setStoryFlag(kFlagBoatIn);
            enterSub(actor, BoatSub::Leave);
            engineTick(actor, boat);
            return;
        }
        if (stopBoat(actor) != 0) {
            boat.trip = uint8_t(BoatTrip::In);
            setStoryFlag(kFlagBoatIn);
            enterSub(actor, BoatSub::Wait);
        }
        break;
    default: break;
    }
    engineTick(actor, boat);
}

// exe 0x5a6560: to (-0x450, 0x280), stopped, on to (-0x5c0, 0x280), stopped -> flag 0x681
void boatLeave(StaticActor& actor, BoatFields& boat) {
    boat.busy = 1;
    switch (LeaveStep(actor.step)) {
    case LeaveStep::Steer: {
        const int32_t out[3] = {kDockX, 0, kOutZ};
        if (steerFrom(actor, out, 0x40, 0x100) < 0) {
            actor.enterStep(uint8_t(LeaveStep::Stop));
            engineTick(actor, boat);
            return;
        }
        break;
    }
    case LeaveStep::Stop:
        if (stopBoat(actor) != 0) {
            actor.enterStep(uint8_t(LeaveStep::Sail));
            engineTick(actor, boat);
            return;
        }
        break;
    case LeaveStep::Sail: {
        const int32_t away[3] = {kAwayX, 0, kOutZ};
        if (sailFrom(actor, away, 0xb8, 0x40) < 0) {
            actor.enterStep(uint8_t(LeaveStep::Arrive));
            engineTick(actor, boat);
            return;
        }
        break;
    }
    case LeaveStep::Arrive:
        if (stopBoat(actor) != 0) {
            boat.trip = uint8_t(BoatTrip::Out);
            setStoryFlag(kFlagBoatOut);
            enterSub(actor, BoatSub::Wait);
        }
        break;
    default: break;
    }
    engineTick(actor, boat);
}

// exe 0x5a6670: backs off, then rams forward (the bow's hit entry) and settles back at its dock, then waits
void boatRise(StaticActor& actor, BoatFields& boat) {
    switch (RiseStep(actor.step)) {
    case RiseStep::Init:
        boat.busy = 1;
        boat.timer = 0x1e;
        actor.velocity.z = 0;
        actor.step = uint8_t(RiseStep::Back);
        [[fallthrough]];
    case RiseStep::Back: {
        actor.velocity.z = int16_t(actor.velocity.z + 4);
        const uint8_t c = uint8_t(boat.timer - 1);
        boat.timer = c;
        if (c == 0) {
            actor.step = uint8_t(RiseStep::Brake);
            integrateVelocity(actor, 0, 0, actor.velocity.z);
            return;
        }
        break;
    }
    case RiseStep::Brake:
        actor.velocity.z = int16_t(actor.velocity.z - 4);
        if (actor.velocity.z < 1) {
            actor.velocity.z = 0;
            actor.step = uint8_t(RiseStep::Forward);
            integrateVelocity(actor, 0, 0, 0);
            return;
        }
        break;
    case RiseStep::Forward:
        actor.velocity.z = int16_t(actor.velocity.z - 0x10);
        if (actor.velocity.z < -0x100) actor.velocity.z = -0x100;
        if (-0x7400000 < actor.pos.z) {
            boat.ram |= kRamming;
            actor.velocity.z = 0x20;
            actor.step = uint8_t(RiseStep::Ram);
            integrateVelocity(actor, 0, 0, actor.velocity.z);
            return;
        }
        break;
    case RiseStep::Ram:
        if (actor.pos.z < -0x7800000) {
            actor.pos.z = kDockZ;
            actor.velocity.z = 0;
            actor.step = uint8_t(RiseStep::Settle);
            integrateVelocity(actor, 0, 0, 0);
            return;
        }
        break;
    case RiseStep::Settle:
        boat.timer = 0x5a;
        boat.busy = 0;
        actor.step = uint8_t(RiseStep::Wait);
        [[fallthrough]];
    case RiseStep::Wait: {
        const uint8_t c = uint8_t(boat.timer - 1);
        boat.timer = c;
        if (c == 0) enterSub(actor, BoatSub::Wait);
        break;
    }
    default: break;
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z);
}

// exe 0x5a67c0: the roll swings, damped by a quarter per crossing; settled -> waiting
void boatRocked(StaticActor& actor) {
    int16_t& rollSpeed = actor.acceleration.x;
    if (actor.step == 0) {
        actor.enterStep(1);
        actor.spin.z = 0x100;
        rollSpeed = 0x100;
    }
    const int16_t amp = actor.spin.z;
    const uint32_t r = uint16_t(actor.rotation.z) & 0xfff;
    actor.rotation.z = int16_t((rollSpeed >> 4) + uint16_t(actor.rotation.z));
    if (r < 0x800) rollSpeed = int16_t(rollSpeed - 0x20);
    if (r > 0x800) rollSpeed = int16_t(rollSpeed + 0x20);
    if (amp < rollSpeed) rollSpeed = amp;
    if (rollSpeed < -int(amp)) rollSpeed = int16_t(-amp);
    if ((uint16_t(actor.prevRotation.z) ^ uint16_t(actor.rotation.z)) & 0x800) {
        const int16_t n = int16_t(amp - (amp >> 2));
        actor.spin.z = n;
        if (uint16_t(n + 0x40) < 0x80) enterSub(actor, BoatSub::Wait);
    }
}

// the bobbing on the water: the vertical speed += its step within +-0x20, y += it * 0x1000
void boatBob(StaticActor& actor) {
    actor.velocity.y = int16_t(actor.velocity.y + actor.acceleration.y);
    if (actor.velocity.y > 0x20) {
        actor.velocity.y = 0x20;
        actor.acceleration.y = -1;
    }
    if (actor.velocity.y < -0x20) {
        actor.velocity.y = -0x20;
        actor.acceleration.y = 1;
    }
    actor.pos.y = int32_t(uint32_t(actor.pos.y) + uint32_t(int32_t(actor.velocity.y) * 0x1000));
}

// a blast (the global static type 7) at a 16.16 point
void blastAt(int32_t x, int32_t y, int32_t z, uint16_t size) {
    if (const uint32_t s = allocActor60(0)) { // exe 0x5e1300
        StaticActor& blast = staticActorAt(s);
        auto&& params = BlastParams(actorHeaderAt(s));
        blast.flags = kActorLive;
        blast.type = typeByte(GlobalStaticType::Blast);
        blast.loopFlags |= kActorSweepGroup2;
        blast.subtype = 0;
        blast.variant = 0;
        blast.pos.x = x;
        params.size = size;
        params.attack = 0;
        blast.pos.y = y;
        blast.pos.z = z;
    }
}
// a smoke (the stage's static type 3) at a 16.16 point
void smokeAt(int32_t x, int32_t y, int32_t z) {
    if (const uint32_t s = allocActor60(kActorStageClass)) { // exe 0x5e12f0
        StaticActor& smoke = staticActorAt(s);
        smoke.flags = kActorLive;
        smoke.pos.x = x;
        smoke.type = kSmokeType;
        smoke.variant = 0;
        smoke.loopFlags |= kActorSweepGroup2;
        smoke.pos.y = y;
        auto&& params = SmokeParams(actorHeaderAt(s));
        params.driftX = 0;
        params.driftZ = 0;
        params.life = 4;
        smoke.pos.z = z;
    }
}

// exe 0x5a6b20: sunk -- the fires put out, the sinking's timer controller, a defeat counted; every 4th tick a blast and
// smoke around it and smoke on it; it pitches down and sinks; at the end a last blast, id 9 defeated, gone
void boatSunk(StaticActor& actor, BoatFields& boat) {
    boat.busy = 1;
    if (SunkSub(actor.subState) == SunkSub::Start) {
        actor.velocity.y = 0;
        actor.spin.x = 0;
        actor.rotation.z = 0;
        actor.subState = uint8_t(SunkSub::Sinking);
        for (int k = 1; k >= 0; --k) { // the two fires told to go
            const uint8_t sl = boat.fires[size_t(k)];
            if (sl == 0) continue;
            StaticActor& fire = staticPool()[size_t(sl - kStaticClass)];
            if (fire.flags & kActorLive) BoatFireParams(fire).putOut = 1;
        }
        boat.timer = 0x78;
        if (const uint32_t e = allocActorE0()) { // exe 0x5e1440
            ControllerActor& timer = controllerActorAt(e);
            timer.flags = kActorLive;
            timer.type = kSinkTimerType;
            timer.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
            timer.variant = 0x2c;
            timer.setParamsLowWord(0x168);
        }
        countDefeat(); // exe 0x5e53e0
    } else if (SunkSub(actor.subState) != SunkSub::Sinking) {
        return;
    }
    if ((boat.timer & 3) == 0) { // every 4th tick a blast + smoke at a random offset, smoke on it
        const uint32_t r1 = rngStep();
        const int32_t x = int32_t(int(sinkingSpread[r1 & 0xf]) * 0x10000 + actor.pos.x);
        const uint32_t r2 = rngStep();
        const uint32_t r3 = rngStep();
        const int32_t y = int32_t(int(sinkingSpread[r2 & 0xf]) * 0x10000 + actor.pos.y);
        const int32_t z = int32_t(int(sinkingSpread[r3 & 0xf]) * 0x10000 + actor.pos.z);
        blastAt(x, y, z, 0x80);
        smokeAt(x, y, z);
        smokeAt(actor.pos.x, actor.pos.y, actor.pos.z);
    }
    actor.spin.x = int16_t(actor.spin.x + 4);
    if (actor.spin.x > 0x180) actor.spin.x = 0x180;
    actor.rotation.x = int16_t(actor.rotation.x - (actor.spin.x >> 4));
    if (actor.rotation.x < -0x380) actor.rotation.x = -0x380;
    if (actor.rotation.x < -0x40) actor.velocity.y = int16_t(actor.velocity.y + 8);
    actor.pos.y = int32_t(uint32_t(actor.pos.y) + uint32_t(int32_t(actor.velocity.y) * 0x1000));
    const uint8_t c = uint8_t(boat.timer - 1);
    boat.timer = c;
    if (c != 0) return;
    blastAt(actor.pos.x, actor.pos.y, actor.pos.z, 0x280); // the last blast
    enemyMarkDefeated(kIdBoatSunk);                      // exe 0x5e63c0
    freeActor(actor);
}

// exe 0x5a6240 (variant 0): the damage (sunk / rocked when idle), the sub-state, the bob, the guns
void boatMain(StaticActor& actor, BoatFields& boat) {
    const int r = boatDamage(actor, boat);
    if (r == 1) {
        actor.enterState(uint8_t(BoatState::Sunk));
        boatSunk(actor, boat);
        return;
    }
    if (r == 2 && boat.busy == 0) enterSub(actor, BoatSub::Rocked);
    switch (BoatSub(actor.subState)) {
    case BoatSub::Wait: boatWait(actor, boat); break;
    case BoatSub::ComeIn: boatComeIn(actor, boat); break;
    case BoatSub::Leave: boatLeave(actor, boat); break;
    case BoatSub::Rise: boatRise(actor, boat); break;
    case BoatSub::Rocked: boatRocked(actor); break;
    default: notPortedAt(kStoryPort, "static 0x5a5ea0 sub-state", actor.subState);
    }
    boatBob(actor);
    boatGunsTick(actor, boat);
}

// the engine sound every 0x1e ticks of the ferry
void ferryEngine(StaticActor& actor, BoatFields& boat) {
    const uint8_t c = uint8_t(boat.timer - 1);
    boat.timer = c;
    if (c == 0) {
        boat.timer = 0x1e;
        soundPlayAtPos(kEngineSound, actor.pos);
    }
}

// exe 0x5a6940 (variant 1): to (0x100, -0xe00), stopped, then (the script's step sub) 3 walkers
void boatFerry(StaticActor& actor, BoatFields& boat) {
    switch (FerrySub(actor.subState)) {
    case FerrySub::Start:
        actor.velocity.z = -0x80;
        actor.subState = uint8_t(FerrySub::Sail);
        boat.timer = 0x1e;
        soundPlayAtPos(kEngineSound, actor.pos);
        [[fallthrough]];
    case FerrySub::Sail: {
        const int32_t landing[3] = {kFerryX, actor.pos.y, kFerryZ};
        if (steerFrom(actor, landing, 0, 0x100) < 0) actor.subState = uint8_t(FerrySub::Stop);
        ferryEngine(actor, boat);
        break;
    }
    case FerrySub::Stop:
        if (stopBoat(actor) != 0) actor.subState = uint8_t(FerrySub::WaitScript);
        ferryEngine(actor, boat);
        break;
    case FerrySub::WaitScript:
        if (scriptState().stepSub == 0) break;
        boat.timer = 10;
        actor.subState = uint8_t(FerrySub::Walker1);
        [[fallthrough]];
    case FerrySub::Walker1:
    case FerrySub::Walker2:
    case FerrySub::Walker3: {
        const uint8_t c = uint8_t(boat.timer - 1);
        boat.timer = c;
        if (c == 0) {
            if (const uint32_t w = allocActor20()) { // exe 0x5e1160: a walker (type 0x2d sub 1 variant 4)
                AnimatedActor& walker = animatedActorAt(w);
                auto&& params = FerryWalkerParams(animatedActorAt(w));
                walker.flags = kActorLiveDrawn;
                walker.type = kWalkerType;
                walker.subtype = kWalkerSub;
                walker.loopFlags |= kActorSweepGroup2;
                walker.variant = kWalkerVariant;
                params.mode = 0xc1;
                params.count = 0;
                params.ferry = actor.slot;
                params.trackId = 0;
                params.pointX = 0x880;
                params.pointZ = -0xa80;
                walker.rotation.y = 0;
            }
            boat.timer = walkerTicks[actor.subState];
            actor.subState += 1u;
        }
        break;
    }
    default: break;
    }
    if (scriptState().stepSub == 1) boatBob(actor);
}

// exe 0x5a6130: the model (scenery-drawn), the hit points of its row << 4, the box; variant 0: the two walker
// spawners and the boss gauge
void boatInit(StaticActor& actor) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.flags |= kActorScenery;
    const int16_t hp = hasStaticStats(actor.type) ? int16_t(staticStats(actor.type, 0).hitPoints << 4) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.velocity.clearWords();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.acceleration.y = -1;
    actor.spin.setXY(0, 0);
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.collisionBox.pointAt(boatBox.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    if (actor.variant == 0) {
        for (int k = 1; k >= 0; --k) // the walker spawners (e0 type 0xa variants 1 / 0), their boat's slot
            if (const uint32_t e = allocActorE0()) {
                ControllerActor& spawner = controllerActorAt(e);
                spawner.flags = kActorLive;
                spawner.type = kWalkerSpawnerType;
                spawner.variant = uint8_t(k);
                spawner.loopFlags |= kActorSweepGroup2;
                spawner.params[0] = actor.slot;
            }
        hudAddBossGauge(actor, 2, 3, kBossGaugeColour); // exe 0x415d30
    }
    actor.enterState(uint8_t(BoatState::Main));
}

// exe 0x5a5ea0
void carrierBoat(StaticActor& actor) {
    auto&& boat = classState<BoatFields>(actor);
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    switch (BoatState(actor.state)) {
    case BoatState::Init: boatInit(actor); break;
    case BoatState::Main:
        if (actor.variant == 0) boatMain(actor, boat);
        else boatFerry(actor, boat);
        break;
    case BoatState::Sunk: boatSunk(actor, boat); break;
    default: notPortedAt(kStoryPort, "static 0x5a5ea0 state", actor.state);
    }
    actor.wasOnPlatform = actor.onPlatform;
    if (!(actor.flags & kActorLive)) return;
    if (boat.hurtTicks != 0) boat.hurtTicks -= 1u;
    boat.hitBox.result = 0;
    boat.hitBox.previousX = actor.prevPos.x.whole;
    boat.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t x = actor.pos.x.whole, y = actor.pos.y.whole, z = actor.pos.z.whole;
    const int yaw = actor.rotation.y;
    const uint32_t owner = hitOwner(actor.slot);
    int16_t dx, dz;
    angleVector(yaw, 0x140, dx, dz);
    const int16_t c0[3] = {int16_t(dx + x), y, int16_t(dz + z)};
    angleVector(yaw, -0xa0, dx, dz);
    const int16_t c1[3] = {int16_t(dx + x), y, int16_t(dz + z)};
    hitListSubmitCapsule(c0, 0x40, c1, 0x40, boat.hitBox, owner | hitGroup(kHitGroupBodies) | hitCapsule(1),
                         kHitAttackSolid); // exe 0x5e4190
    angleVector(yaw, 0x240, dx, dz);
    const int16_t p0[3] = {int16_t(dx + x), int16_t(y + 0x20), int16_t(dz + z)};
    hitListSubmit(p0, boat.hitBox, owner | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0xc0), kHitAttackSolid);
    angleVector(yaw, -0x280, dx, dz);
    const int16_t p1[3] = {int16_t(dx + x), y, int16_t(dz + z)};
    hitListSubmit(p1, boat.hitBox, owner | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0xc0), kHitAttackSolid);
    if (boat.ram & kRamming) { // the bow ram: a hit entry, a camera shake and the pad's vibration at it
        angleVector(yaw, 0x340, dx, dz);
        const int16_t p2[3] = {int16_t(dx + x), int16_t(y + 0x80), int16_t(dz + z)};
        hitListSubmit(p2, boat.hitBox, owner | hitGroup(kHitGroupPlayerAttacks) | kHitNoLockOn | hitRadius(0x100),
                      staticStats(actor.type, 0).damage | kHitAttackStrongPush | kHitAttackSolid);
        cameraShakeAt(0x2000, 0x300, 0x100, 0x1c00, p2); // exe 0x40eb40: the point as 16.16 (zero fractions)
        // exe 0x55ce90: the point stored as plain ints and read as 16.16 (the exe's own conversion): whole parts 0 / -1
        const int16_t plain[3] = {int16_t(uint32_t(int32_t(p2[0])) >> 16), int16_t(uint32_t(int32_t(p2[1])) >> 16),
                                  int16_t(uint32_t(int32_t(p2[2])) >> 16)};
        padVibrateAt(0x201, 0x208b0, 0x100, 0x1c00, plain);
        boat.ram &= uint8_t(~kRamming);
    }
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050(a, 1, 0xffff, 0)
}

} // namespace

void carrierBoatUpdate(const PlayerEnv&, uint32_t actor) { carrierBoat(staticActorAt(actor)); }

} // namespace od::port
