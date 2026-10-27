// The mission gunships (see mission_gunships.hpp), one routine per exe routine; names are hypotheses from the observed
// behaviour.
//
// The ST15 gunship 0x59d310: states 0 init 0x59d440, 1 0x59d520 (sub-states [0x8ce8f0]: 0 0x59d610 start, 1 0x59d650
// cruise / shoot, 2 0x59d910 veer off a wall, 3 0x59da20 land and drop reinforcements, 4 0x59dca0 fly off, 5 0x59dda0
// rocked), 2 shot down 0x5a28e0 (the family's crash), 3 freed 0x5afdd0. Its point (pointX / pointZ) is the player's while
// its mode's low nibble is 0; its home height the ground's; its variant & 0xf: 0 a bomber (its count * 4 bombs), 1 lands
// near the point (sub-state 3), 2 a gunner (bursts, the left / right guns). The exe's point locals (exe 0x5e3bc0 on
// {x << 16 | stack, ., z << 16 | stack}) leave the low halves unwritten: taken as 0 here.
//
// Its twin over Glyde's base 0x5a07b0: states [0x8ceab0] 0 init 0x5a08d0, 1 0x5a09d0 (sub-states [0x8ceac0]: 0 0x5a0ad0
// .. 5 0x5a1280 as the ST15 one with its own constants and reinforcement table 0x8cea58, 6 0x5a1300 the entry: one of
// the 4 slots (the points 0x8cea98), 8 debris statics, the climb to y -0x580), 2 shot down 0x5a1720 (the crash; the 8th
// one down in ST30 (the counter 0x8ceaac) -> flag 0x20, a pool-0x20 type 0x2d and the key, static type 0x42 0x5b26d0),
// 3 freed 0x5afdd0. While entering: no attitude, collision or cooldown.
#include "content/nino/mission_gunships.hpp"
#include "content/nino/mission_gunships_state.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/gunship_flight.hpp"

#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/enemy_motion.hpp"
#include "game/explosions.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

using namespace od::port::mission_gunships_state;

namespace od::port {

using namespace mission_gunships_state;

namespace {

constexpr uint8_t kPointModeMask = kGunshipPointMask; // 0: the point follows the player; 2: the cruise also shoots it
constexpr uint8_t kPointOnPlayer = 0, kPointShootsPlayer = 2;
constexpr uint8_t kLeaveNow = kGunshipLeave;
constexpr uint8_t kNoEntrySlot = 0xff;
constexpr uint8_t kLanded = 1; // the mode once it landed / flies off

// The reinforcement the landing drops (pool 0x20 type 0x2d sub 1): its mode, count, tracked id and point
struct ReinforcementParams {
    explicit ReinforcementParams(const AnimatedActor& actor) : ReinforcementParams(const_cast<AnimatedActor&>(actor)) {}
    explicit ReinforcementParams(AnimatedActor& actor)
        : mode(actor.params[0]), count(actor.params[1]), trackId(actor.params[3]), pointX(actor.velocity.spare),
          pointZ(actor.acceleration.spare) {}
    uint8_t& mode;
    uint8_t& count;
    uint8_t& trackId;
    int16_t& pointX;
    int16_t& pointZ;
};
// A row of the reinforcement table (8 bytes): the reinforcement's mode and count, the first id it is tracked as (0:
// none), its point
struct ReinforcementRow : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    uint8_t mode = 0; // +0x0
    uint8_t count = 0; // +0x1
    uint16_t firstId = 0; // +0x2
    int16_t pointX = 0; // +0x4
    int16_t pointZ = 0; // +0x6
    template <class R> static ReinforcementRow load(const R& r) {
        ReinforcementRow v;
        v.mode = r.template read<uint8_t>(0x0);
        v.count = r.template read<uint8_t>(0x1);
        v.firstId = r.template read<uint16_t>(0x2);
        v.pointX = r.template read<int16_t>(0x4);
        v.pointZ = r.template read<int16_t>(0x6);
        return v;
    }
};
constexpr uint8_t kReinforcementType = 0x2d, kReinforcementSub = 1;

// the ST15 gunship's states / the wave's
enum class MissionGunshipState : uint8_t { Init = 0, Alive = 1, Crash = 2, Gone = 3 };
enum class MissionGunshipSub : uint8_t {
    Start = 0, Cruise = 1, Veer = 2, Land = 3, FlyOff = 4, Rocked = 5,
    Entry = 6, // the wave's
};
// the cruise's steps
enum class CruiseStep : uint8_t { Init = 0, PickRun = 1, Cruise = 2, Burst = 3, RunStart = 4, Run = 5, PauseStart = 6, Pause = 7 };
// the landing's steps
enum class LandStep : uint8_t { Init = 0, Brake = 1, Descend = 2, Drop = 3, Climb = 4 };
// the wave's entry steps
enum class EntryStep : uint8_t { Init = 0, TakeSlot = 1, Wait = 2, Climb = 3, Level = 4 };
// the wave's crash
enum class WaveCrashSub : uint8_t { Start = 0, Fall = 1, Blast = 2 };

// The constants of each of the two classes
struct MissionGunshipKind {
    TableId box;                 // its collision box
    TableId probeBox;            // the box of its wall probes
    int groundLimit;             // the ground below this is its home height
    TableId cruiseTicks;         // the cruise's ticks (u8 x 16)
    TableId margins;             // the steering limit's offsets (s16 x 8, the low byte read)
    TableId runTicks;            // the straight run's ticks (u8 x 16)
    TableId burstTicks;          // the burst's ticks (u8 x 16)
    int steerLimit;              // + the margin
    int16_t steerStep;
    int cruiseSpeed;
    uint8_t veerTicks;
    int veerLimit;
    int16_t veerStep;
    int veerSpeed;
    TableId reinforcements;      // the reinforcement table
    int leaveDistance;           // the fly-off point's distance (along the heading, backwards)
    int leaveLimit;
    int16_t leaveStep;
    int goneDistance;            // freed beyond this from the player
    int burstNear;               // the cruise's burst within this (<)
};
constexpr MissionGunshipKind kSt15Gunship = {TableId::MissionGunshipsSt15GunshipBox, TableId::MissionGunshipsSt15GunshipProbeBox, 0x1c00, TableId::MissionGunshipsSt15GunshipCruiseTicks, TableId::MissionGunshipsSt15GunshipMargins, TableId::MissionGunshipsSt15GunshipRunTicks, TableId::MissionGunshipsSt15GunshipBurstTicks,
                                             0x200, 0x10, 0x400, 0x18, 0x400, 0x20, 0x40, TableId::MissionGunshipsSt15GunshipReinforcements,
                                             -0x3800, 0x200, 0x10, 0x1c00, 0xbb9};
constexpr MissionGunshipKind kWaveGunship = {TableId::MissionGunshipsWaveGunshipBox, TableId::MissionGunshipsWaveGunshipProbeBox, 0x3800, TableId::MissionGunshipsWaveGunshipCruiseTicks, TableId::MissionGunshipsWaveGunshipMargins, TableId::MissionGunshipsWaveGunshipRunTicks, TableId::MissionGunshipsWaveGunshipBurstTicks,
                                             0x300, 0x18, 0x280, 0x10, 0x600, 0x30, 0x30, TableId::MissionGunshipsWaveGunshipReinforcements,
                                             -0x7000, 0x300, 0x18, 0x3800, 0xbb9};

constexpr int16_t kSpeedStep = -0x20, kVeerSpeedStep = -0x40, kLandSpeedStep = -0x40;
constexpr int8_t kLevelPitch = 0x30;
constexpr int kHomeAbove = 0x4b0;
constexpr int kLandHeight = 0x15e, kClimbHeight = 0x4b0;
constexpr uint8_t kAttackCooldown = 0x96;
constexpr uint8_t kDropTicks = 0x2d;
constexpr uint8_t kRockSwings = 6;
constexpr uint16_t kBurstSound = 0x189, kEntrySound = 0x1d0;
constexpr uint8_t kBombsPerCount = 4; // the count << 2

struct EntryPoint : DataRecord {
    int16_t x = 0;
    int16_t z = 0;
    static EntryPoint load(const TableReader& r) {
        EntryPoint rec;
        rec.x = r.s16(0x0);
        rec.z = r.s16(0x2);
        return rec;
    }
};
const DataTable<RecordTable<EntryPoint, 4, 4>> entryPoints{TableId::MissionGunshipsEntryPoints};
constexpr int8_t kEntrySlots = 4;
constexpr uint8_t kWaveShips = 8;
constexpr uint8_t kWaveStage = 0x30;
constexpr uint32_t kFlagKeyDropped = 0x20;
constexpr uint8_t kDebrisType = 3, kDebrisVariant = 0x10, kDebrisSmallVariant = 0x11;
constexpr uint16_t kDebrisLife = 0x14;
// The debris' spawn parameters (the stage's smoke / debris static, type 3): its drift and its life
struct DebrisParams {
    explicit DebrisParams(const StaticActor& actor) : DebrisParams(const_cast<StaticActor&>(actor)) {}
    explicit DebrisParams(StaticActor& actor) : driftX(actor.params[0]), driftZ(actor.params[1]), life(actor.params, 2) {}
    uint8_t& driftX;
    uint8_t& driftZ;
    BytesField<uint16_t, 4> life;
};
constexpr uint8_t kKeyType = 0x42;          // the stage's static 0x5b26d0
constexpr uint8_t kKeyCarrierType = 0x2d;   // a pool-0x20 type 0x2d (variant 1)
constexpr int16_t kKeyPointZ = 0x500;       // the key is thrown toward (0, 0x500)

// the sub-state s entered (the step cleared: the exe's byte and word stores)
void enterSub(StaticActor& actor, MissionGunshipSub s) {
    actor.subState = uint8_t(s);
    actor.enterStep(0);
}

// exe 0x5e3bc0 on the point {x << 16, ., z << 16}: the bearing from it to the ship. The exe's point (a stack local of
// 0x59d650) holds only the whole halves: its low words are stale stack, not reproducible; OpenDash takes them as 0 -- a
// deliberate deviation: on a tick the bearing lands at a whole number's edge the steer dead zone differs (spin.y 0x10)
int angleFromPoint(const StaticActor& actor, int16_t px, int16_t pz) {
    return angleOf(int32_t(uint32_t(uint16_t(px)) << 16), int32_t(uint32_t(uint16_t(pz)) << 16), actor.pos.x,
                   actor.pos.z);
}
// exe 0x5e3b00(position, point): the planar distance to the point
int planarToPoint(const StaticActor& actor, int16_t px, int16_t pz) {
    const int32_t point[3] = {int32_t(uint32_t(uint16_t(px)) << 16), 0, int32_t(uint32_t(uint16_t(pz)) << 16)};
    return planarDistance(actor.pos, point);
}
// exe 0x5a1c30(actor, point, lim, acc): steerRate toward the point's bearing
int steerToPoint(StaticActor& actor, int16_t px, int16_t pz, int lim, int16_t acc) {
    return steerRate(actor, uint32_t(angleFromPoint(actor, px, pz)), lim, acc);
}
// exe 0x59e4e0(actor, point, lim, acc): the turn rate toward the point, no dead zone
void steerAlways(StaticActor& actor, int16_t px, int16_t pz, int lim, int16_t acc) {
    MissionGunshipFields& ship = classState<MissionGunshipFields>(actor);
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleFromPoint(actor, px, pz)));
    ship.turnStep = 0;
    if (d < 0) ship.turnStep = int16_t(-acc);
    if (d > 0) ship.turnStep = acc;
    actor.rotation.y = int16_t(actor.rotation.y + (actor.spin.y >> 4));
    actor.spin.y = int16_t(ship.turnStep + actor.spin.y);
    if (actor.spin.y > lim) actor.spin.y = int16_t(lim);
    if (actor.spin.y < -lim) actor.spin.y = int16_t(-lim);
}
// the point on the player's while the mode asks for it
void followPlayer(MissionGunshipFields& ship) {
    if ((ship.mode & kPointModeMask) == kPointOnPlayer) {
        ship.pointX = player().pos.x.whole;
        ship.pointZ = player().pos.z.whole;
    }
}
// exe 0x5a1260: told to leave -> fly off
void leaveIfTold(StaticActor& actor, const MissionGunshipFields& ship) {
    if (ship.mode & kLeaveNow) enterSub(actor, MissionGunshipSub::FlyOff);
}
// the forward speed toward the limit (exe 0x5e5510), then the move
void cruiseMove(StaticActor& actor, int speed) {
    if (stepTowardLimit(actor.velocity.z, uint16_t(actor.acceleration.z), speed) != 0)
        actor.velocity.z = int16_t(-speed);
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
}

// exe 0x59e160 / 0x5a1af0: the room collision bits; while probing, the walls 0x400 ahead left / right; the ground under
// it (below the kind's limit) as the home height
void missionGunshipCollide(const PlayerEnv& env, StaticActor& actor, MissionGunshipFields& ship,
                           const MissionGunshipKind& kind) {
    const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const int16_t q[3] = {actor.prevPos.x.whole, actor.prevPos.y.whole, actor.prevPos.z.whole};
    uint32_t r = testMoveBlocked(env, p, q, actor.collisionBox, 2);
    const uint8_t water = (r & 0x20000000) ? kContactWater : 0;
    uint8_t b = water;
    if (int32_t(r) < 0) {
        b = uint8_t(water | kContactWall);
        if (!(ship.behaviour & kBehaveNoPushOut) && (actorPushOutMask(env, actor, 2) & 0xf00))
            b = uint8_t(water | kContactWall | kContactFloor);
    }
    bool probed = false;
    if (ship.flight & kFlightProbeWalls) {
        int16_t dx, dz;
        angleVector(int(int16_t(uint16_t(actor.rotation.y) + 0x80u)), 0x400, dx, dz);
        const int16_t left[3] = {int16_t(dx + actor.pos.x.whole), actor.pos.y.whole, int16_t(actor.pos.z.whole + dz)};
        const uint32_t leftHit = testMoveBlocked(env, left, q, CollisionBoxRef::record(stateAddress(kind.probeBox)), 2);
        angleVector(int(int16_t(uint16_t(actor.rotation.y) - 0x80u)), 0x400, dx, dz);
        const int16_t right[3] = {int16_t(dx + actor.pos.x.whole), actor.pos.y.whole, int16_t(actor.pos.z.whole + dz)};
        r = testMoveBlocked(env, right, q, CollisionBoxRef::record(stateAddress(kind.probeBox)), 2);
        if (int32_t(leftHit) < 0 && !(leftHit & 0x20000000)) {
            b |= kContactWallLeft;
            r = leftHit;
            probed = true;
        } else if (int32_t(r) < 0 && !(r & 0x20000000)) {
            b |= kContactWallRight;
            probed = true;
        }
    }
    if (!probed) r = uint32_t(actorGround(env, actor)); // exe 0x40f190
    if (!(b & kContactWater) && int32_t(r) < kind.groundLimit) ship.homeHeight = int32_t(r & 0xffff);
    ship.contact = b;
}

// exe 0x59e060 / 0x5a16a0 (variant 2): a burst of shots when facing the point (+-0x200) nearer than 0xbb9; the cooldown
// 0x96 after it
void gunnerAttack(MissionGunshipFields& ship, const MissionGunshipKind& kind, int d, int diff) {
    const uint8_t c = ship.burst;
    if (c == 0) {
        if (ship.attackCooldown == 0 && uint32_t(diff + 0x200) < 0x400 && d < 0xbb9)
            ship.burst = dataScalar<uint8_t>(stateAddress(kind.burstTicks) + (rngStep() & 0xf));
        return;
    }
    if ((c & 3) == 0) ship.flight |= (c & 7) == 0 ? kGunRight : kGunLeft;
    ship.burst = uint8_t(c - 1);
    if (uint8_t(c - 1) == 0) {
        ship.attackCooldown = kAttackCooldown;
        ship.behaviour &= uint8_t(~kBehaveStraight);
    }
}

// exe 0x59df60 / 0x5a15a0: the attack of the variant (& 0xf) toward the point: 0 bombs (count * 4), 1 lands near it,
// 2 shoots bursts
void missionGunshipAttack(StaticActor& actor, MissionGunshipFields& ship, const MissionGunshipKind& kind) {
    const int16_t px = ship.pointX, pz = ship.pointZ;
    const int d = planarToPoint(actor, px, pz);
    const int diff = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleFromPoint(actor, px, pz)));
    switch (actor.variant & 0xf) {
    case 0: {
        const uint8_t c = ship.burst;
        if (c == 0) {
            if (ship.attackCooldown == 0 && uint32_t(diff + 0x180) < 0x300 && d < 0x515)
                ship.burst = uint8_t(uint8_t(ship.count) * kBombsPerCount);
            return;
        }
        if ((c & 3) == 0) dropBomb(actor, int16_t(actor.velocity.z >> 2)); // exe 0x59e390
        ship.burst = uint8_t(c - 1);
        if (uint8_t(c - 1) == 0) {
            ship.attackCooldown = kAttackCooldown;
            ship.behaviour &= uint8_t(~kBehaveStraight);
        }
        return;
    }
    case 1:
        if (uint32_t(diff + 0x300) < 0x600 && d < 0x401) enterSub(actor, MissionGunshipSub::Land);
        return;
    case 2: gunnerAttack(ship, kind, d, diff); return;
    default: return;
    }
}

// exe 0x59d650 / 0x5a0b10: the cruise -- a run's length and steering margin picked, a cruise (facing the player near:
// sound 0x189, a burst), the burst (the guns every 4th tick), a straight run, a pause; steered to the point but on a
// straight run; a wall ahead during a straight run -> the veer; the attack; told to leave -> fly off
void missionCruise(StaticActor& actor, MissionGunshipFields& ship, const MissionGunshipKind& kind) {
    const int16_t px = ship.pointX, pz = ship.pointZ;
    switch (CruiseStep(actor.step)) {
    case CruiseStep::Init:
        ship.flight = kFlightProbeWalls;
        ship.behaviour = uint8_t((ship.behaviour & uint8_t(~(kBehaveStraight | kBehaveNoPushOut))) | kBehaveLevel);
        actor.acceleration.z = kSpeedStep;
        actor.enterStep(uint8_t(CruiseStep::PickRun));
        [[fallthrough]];
    case CruiseStep::PickRun:
        ship.timer = dataScalar<uint8_t>(stateAddress(kind.cruiseTicks) + (rngStep() & 0xf));
        ship.turnMargin = int8_t(dataScalar<uint8_t>(stateAddress(kind.margins) + (rngStep() & 7) * 2));
        actor.enterStep(uint8_t(CruiseStep::Cruise));
        [[fallthrough]];
    case CruiseStep::Cruise: {
        const uint8_t c = uint8_t(ship.timer - 1);
        ship.timer = c;
        if (c == 0) actor.enterStep(uint8_t(CruiseStep::RunStart));
        if ((ship.mode & kPointModeMask) == kPointOnPlayer || (ship.mode & kPointModeMask) == kPointShootsPlayer) {
            // the exe passes the player's struct (not its position): its +0 / +8 words
            const int d = planarDistance(actor.pos, playerStructAsPoint());
            const int diff =
                angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(player().pos, actor.pos)));
            if (uint32_t(diff + 0x200) < 0x400 && d < kind.burstNear) {
                ship.timer = dataScalar<uint8_t>(stateAddress(kind.burstTicks) + (rngStep() & 0xf));
                actor.enterStep(uint8_t(CruiseStep::Burst));
                soundPlayAtPos(kBurstSound, actor.pos);
            }
        }
        break;
    }
    case CruiseStep::Burst: {
        const uint8_t c = ship.timer;
        if ((c & 3) == 0) ship.flight |= (c & 7) == 0 ? kGunRight : kGunLeft;
        ship.timer = uint8_t(c - 1);
        if (uint8_t(c - 1) == 0) actor.enterStep(uint8_t(CruiseStep::PauseStart));
        break;
    }
    case CruiseStep::RunStart:
        ship.timer = dataScalar<uint8_t>(stateAddress(kind.runTicks) + (rngStep() & 0xf));
        actor.enterStep(uint8_t(CruiseStep::Run));
        ship.behaviour |= kBehaveStraight;
        [[fallthrough]];
    case CruiseStep::Run: {
        const uint8_t c = uint8_t(ship.timer - 1);
        ship.timer = c;
        if (c == 0 || !(ship.behaviour & kBehaveStraight)) {
            actor.enterStep(uint8_t(CruiseStep::PickRun));
            ship.behaviour &= uint8_t(~kBehaveStraight);
        } else {
            ship.behaviour |= kBehaveStraight;
        }
        break;
    }
    case CruiseStep::PauseStart:
        ship.timer = dataScalar<uint8_t>(stateAddress(kind.cruiseTicks) + (rngStep() & 0xf));
        actor.enterStep(uint8_t(CruiseStep::Pause));
        [[fallthrough]];
    case CruiseStep::Pause: {
        const uint8_t c = uint8_t(ship.timer - 1);
        ship.timer = c;
        if (c == 0) actor.enterStep(uint8_t(CruiseStep::RunStart));
        break;
    }
    default: break;
    }
    if ((ship.behaviour & kBehaveStraight) ||
        steerToPoint(actor, px, pz, ship.turnMargin + kind.steerLimit, kind.steerStep) == 0)
        steerDamp(actor, kind.steerStep);
    cruiseMove(actor, kind.cruiseSpeed);
    if ((ship.behaviour & kBehaveStraight) && (ship.contact & (kContactWallLeft | kContactWallRight))) {
        enterSub(actor, MissionGunshipSub::Veer);
        ship.turnMargin = int8_t(ship.contact);
    }
    missionGunshipAttack(actor, ship, kind);
    leaveIfTold(actor, ship);
}

// exe 0x59d610 / 0x5a0ad0: the level pitch, the cruise speed, the rates cleared, then the cruise
void missionStart(StaticActor& actor, MissionGunshipFields& ship, const MissionGunshipKind& kind) {
    ship.pitchTarget = kLevelPitch;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = int16_t(-kind.cruiseSpeed);
    actor.acceleration.setXY(0, 0);
    actor.spin.x = 0;
    actor.spin.y = 0;
    enterSub(actor, MissionGunshipSub::Cruise);
    missionCruise(actor, ship, kind);
}

// exe 0x59d910 / 0x5a0dd0: a wall ahead (the walls seen: on the right) -> turn away for a while
void missionVeer(StaticActor& actor, MissionGunshipFields& ship, const MissionGunshipKind& kind) {
    const uint8_t b = uint8_t(ship.behaviour | kBehaveLevelA);
    ship.behaviour = b;
    if (actor.step == 0) {
        ship.flight = kFlightProbeWalls;
        ship.behaviour = uint8_t((b & uint8_t(~kBehaveNoPushOut)) | kBehaveLevel);
        actor.acceleration.z = kVeerSpeedStep;
        ship.timer = kind.veerTicks;
        actor.enterStep(1);
    }
    const int off = (uint8_t(ship.turnMargin) & kContactWallRight) ? 0x600 : -0x600;
    int16_t dx, dz;
    angleVector(int(int16_t(uint16_t(actor.rotation.y) + off)), 0x400, dx, dz);
    if (steerToPoint(actor, int16_t(actor.pos.x.whole + dx), int16_t(actor.pos.z.whole + dz), kind.veerLimit,
                     kind.veerStep) == 0)
        steerDamp(actor, kind.veerStep);
    cruiseMove(actor, kind.veerSpeed);
    const uint8_t c = uint8_t(ship.timer - 1);
    ship.timer = c;
    if (c == 0) enterSub(actor, MissionGunshipSub::Cruise);
    missionGunshipAttack(actor, ship, kind);
    leaveIfTold(actor, ship);
}

// exe 0x59ded0(actor, speed, dy): the vertical speed toward the height error dy, braking in time (exe 0x5e4040); 1 once
// stopped at it
int verticalSettle(StaticActor& actor, int speed, int dy) {
    if (uint16_t(actor.acceleration.y) == uint16_t(dy)) actor.acceleration.y = int16_t(-actor.acceleration.y);
    int32_t p1 = actor.velocity.y, p2 = -int32_t(actor.acceleration.y);
    int32_t brake = 0x7fffffff;
    if (p2 != 0) {
        if (p1 < p2) {
            p1 = -p1;
            p2 = -p2;
        }
        brake = -((int32_t(uint32_t(p1) << 12) / int32_t(uint32_t(p2) << 12)) * (int32_t(uint32_t(p1) << 12) >> 1));
    }
    brake >>= 16;
    if (actor.acceleration.y > 0) dy = -dy;
    if (dy > brake) {
        if (verticalApproach(actor, speed) != 0)
            actor.velocity.y = int16_t(actor.acceleration.y < 1 ? -speed : speed);
        return 0;
    }
    if (verticalApproach(actor, 0) != 0) {
        actor.velocity.y = 0;
        return 1;
    }
    return 0;
}

// exe 0x59de20 / 0x5a1460(actor, h): settle at the home height + h (settling while moving vertically)
void settleAt(StaticActor& actor, MissionGunshipFields& ship, int h) {
    const int dy = int(actor.pos.y.whole) - int16_t(ship.homeHeight) + h;
    if (!(ship.behaviour & kBehaveSettling)) {
        if (!(ship.behaviour & kBehaveLevel)) bobVertical(actor); // exe 0x5a0670
        if (0x32 < dy) {
            actor.acceleration.y = -0x10;
            ship.behaviour |= kBehaveSettling;
        }
        if (dy < -0x32) {
            actor.acceleration.y = 0x10;
            ship.behaviour |= kBehaveSettling;
        }
        return;
    }
    if (verticalSettle(actor, 0x200, dy) != 0) {
        ship.pitchTarget = kLevelPitch;
        ship.behaviour &= uint8_t(~kBehaveSettling);
        actor.acceleration.y = int16_t(actor.acceleration.y < 1 ? 6 : -6);
    }
}

// exe 0x59e310(actor, variant): a reinforcement (pool 0x20 type 0x2d sub 1) 0x90 behind, 0x80 below, facing back
uint32_t dropReinforcement(const StaticActor& actor, uint8_t v) {
    int16_t dx, dz;
    angleVector(int(int16_t(uint16_t(actor.rotation.y) + 0x800u)), 0x90, dx, dz);
    const uint32_t r = allocActor20(); // exe 0x5e1160
    if (!r) return 0;
    AnimatedActor& drop = animatedActorAt(r);
    drop.flags = kActorLive;
    drop.type = kReinforcementType;
    drop.subtype = kReinforcementSub;
    drop.variant = v;
    drop.loopFlags |= kActorSweepGroup2;
    drop.params[0] = 0;
    drop.pos.x.whole = int16_t(actor.pos.x.whole + dx);
    drop.pos.y.whole = int16_t(actor.pos.y.whole + 0x80);
    drop.pos.z.whole = int16_t(actor.pos.z.whole + dz);
    drop.rotation.y = int16_t(uint16_t(actor.rotation.y) + 0x800u);
    return r;
}

// exe 0x59da20 / 0x5a0ee0: brake near the point, descend, drop the count's reinforcements (the row's), climb back, fly
// off
void missionLand(StaticActor& actor, MissionGunshipFields& ship, const MissionGunshipKind& kind) {
    switch (LandStep(actor.step)) {
    case LandStep::Init: {
        ship.flight = 0;
        ship.behaviour &= uint8_t(~kBehaveNoPushOut);
        actor.acceleration.z = kLandSpeedStep;
        ship.mode = kLanded;
        int16_t dx, dz;
        angleVector(int(int16_t(uint16_t((rngStep() & 0x20) + uint16_t(actor.rotation.y)) - 0x10)), 0x200, dx, dz);
        ship.pointX = int16_t(actor.pos.x.whole + dx);
        ship.pointZ = int16_t(actor.pos.z.whole + dz);
        actor.enterStep(uint8_t(LandStep::Brake));
    }
        [[fallthrough]];
    case LandStep::Brake:
        actor.velocity.z = int16_t(actor.velocity.z + 0x40);
        if (actor.velocity.z >= 0) {
            actor.velocity.z = 0;
            actor.enterStep(uint8_t(LandStep::Descend));
        }
        break;
    case LandStep::Descend:
        settleAt(actor, ship, kLandHeight);
        if (!(ship.behaviour & kBehaveSettling)) {
            actor.velocity.y = 0;
            actor.enterStep(uint8_t(((actor.variant & 0xf0) != 0x10 ? 1 : 0) + uint8_t(LandStep::Drop)));
        }
        break;
    case LandStep::Drop:
        if (actor.stepCounter == 0) {
            const ReinforcementRow row = dataRecord<ReinforcementRow>(stateAddress(kind.reinforcements) + uint32_t(int32_t(ship.reinforcementRow.get())) * 8, ReinforcementRow::kBytes);
            const uint32_t id = uint32_t(int32_t(ship.count)) - 1u + row.firstId;
            auto place = [&](uint32_t r, uint8_t trackId) {
                auto&& drop = ReinforcementParams(animatedActorAt(r));
                drop.mode = row.mode;
                drop.count = row.count;
                drop.trackId = trackId;
                drop.pointX = row.pointX;
                drop.pointZ = row.pointZ;
            };
            if (row.firstId == 0) {
                if (const uint32_t r = dropReinforcement(actor, 1)) place(r, 0);
            } else if (enemyDefeated(id)) { // exe 0x5e6420
                if (const uint32_t r = dropReinforcement(actor, 1)) {
                    place(r, uint8_t(id));
                    encounterTrackInScriptSlot(animatedActorAt(r), uint16_t(id)); // exe 0x5e6500
                    enemyClearDefeated(id);                                       // exe 0x5e63f0
                }
            }
            ship.timer = kDropTicks;
            actor.stepCounter = 1;
            bobVertical(actor);
        } else {
            const uint8_t c = uint8_t(ship.timer - 1);
            ship.timer = c;
            if (c == 0) {
                const uint8_t n = uint8_t(ship.count - 1);
                ship.count = int8_t(n);
                if (n == 0) {
                    actor.enterStep(uint8_t(LandStep::Climb));
                    bobVertical(actor);
                    break;
                }
                actor.stepCounter = 0;
            }
            bobVertical(actor);
        }
        break;
    case LandStep::Climb:
        settleAt(actor, ship, kClimbHeight);
        if (!(ship.behaviour & kBehaveSettling)) enterSub(actor, MissionGunshipSub::FlyOff);
        break;
    default: break;
    }
    steerAlways(actor, ship.pointX, ship.pointZ, 0x60, 4); // exe 0x59e4e0
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
}

// exe 0x59dca0 / 0x5a1160: away along the heading (the kind's distance out); out of the far draw distance: no lock-on;
// far from the player -> freed
void missionFlyOff(StaticActor& actor, MissionGunshipFields& ship, const MissionGunshipKind& kind) {
    if (actor.step == 0) {
        ship.mode = kLanded;
        int16_t dx, dz;
        angleVector(int(ship.leaveHeading) << 4, kind.leaveDistance, dx, dz);
        ship.pointX = int16_t(ship.pointX + dx);
        ship.pointZ = int16_t(ship.pointZ + dz);
        ship.flight = 0;
        ship.behaviour = uint8_t((ship.behaviour & uint8_t(~kBehaveNoPushOut)) | kBehaveLevel);
        actor.acceleration.z = kSpeedStep;
        actor.enterStep(1);
    }
    if (steerToPoint(actor, ship.pointX, ship.pointZ, kind.leaveLimit, kind.leaveStep) == 0)
        steerDamp(actor, kind.leaveStep);
    cruiseMove(actor, 0x500);
    const int d = planarDistance(actor.pos, player().pos);
    if (int(uint32_t(stage().depthRange) << 2) < d) ship.flight |= kFlightDown;
    if (kind.goneDistance < d) actor.enterState(uint8_t(MissionGunshipState::Gone));
}

// exe 0x59dda0 / 0x5a1280: rocked for 6 swings, then the cruise
void missionRocked(StaticActor& actor, MissionGunshipFields& ship) {
    if (actor.step == 0) {
        ship.flight = kFlightNoRock | kFlightRocking | kFlightSway;
        ship.behaviour &= uint8_t(~kBehaveNoPushOut);
        rockInit(actor); // exe 0x5a06e0
        ship.timer = kRockSwings;
        actor.enterStep(1);
    }
    if (rock(actor) != 0) {
        const uint8_t c = uint8_t(ship.timer - 1);
        ship.timer = c;
        if (c == 0) enterSub(actor, MissionGunshipSub::Cruise);
    }
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
}

// the hits: a strong one rocks it (but while diving / rocking); dead: true (the caller crashes it)
bool missionHits(StaticActor& actor, MissionGunshipFields& ship) {
    const GunshipHit r = gunshipHits(actor); // exe 0x59e0e0
    if (r == GunshipHit::Strong) {
        if (!(ship.flight & kFlightNoRock) && !(ship.behaviour & kBehaveRocking)) {
            ship.behaviour |= kBehaveRocking;
            rockInit(actor); // exe 0x5a06e0
        }
    }
    return r == GunshipHit::Dead;
}

// the rocking of a strong hit run, its end clearing it
void missionRocking(StaticActor& actor, MissionGunshipFields& ship) {
    if (ship.behaviour & kBehaveRocking) {
        ship.flight |= kFlightRocking;
        if (rock(actor) != 0) { // exe 0x59e580
            ship.behaviour &= uint8_t(~kBehaveRocking);
            ship.flight &= uint8_t(~kFlightRocking);
        }
    }
}

// the hit entry of both: the body (no lock-on while falling) of the variant-0 row's height and damage
void missionHitEntry(StaticActor& actor, MissionGunshipFields& ship, uint32_t statRow) {
    const uint8_t flight = ship.flight;
    const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    ship.hitBox.result = 0;
    ship.hitBox.previousX = actor.prevPos.x.whole;
    ship.hitBox.previousZ = actor.prevPos.z.whole;
    const StaticActorStats row = staticStatsAt(statRow);
    const uint32_t shape = hitOwner(actor.slot) | ((flight & kFlightDown) ? kHitNoLockOn : 0u) |
                           hitHeightLevel(row.hitHeight) | hitGroup(kHitGroupBodies) | hitRadius(0xc8);
    hitListSubmit(p, ship.hitBox, shape, row.damage | kHitAttackSolid); // exe 0x5e4120
}

// ---- the ST15 gunship 0x59d310 -----------------------------------------------------------------------------------

// exe 0x59d440: the model, the hit points, the box, the point and its heading, the home height above it
void st15GunshipInit(StaticActor& actor, MissionGunshipFields& ship) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    const int16_t hp = hasStaticStats(actor.type) ? int16_t(staticStats(actor.type, 0).hitPoints) : int16_t(0);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.onPlatform = 0;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = 0;
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.collisionBox.pointAt(kSt15Gunship.box);
    actor.airborne = 1;
    followPlayer(ship);
    ship.leaveHeading = int8_t(uint8_t(angleFromPoint(actor, ship.pointX, ship.pointZ) >> 4));
    ship.homeHeight = int32_t(actor.pos.y.whole) + kHomeAbove;
    actor.clearClassBlock(); // exe 0x40dc40
    actor.enterState(uint8_t(MissionGunshipState::Alive));
}

// exe 0x59d520
void st15GunshipAlive(const PlayerEnv& env, StaticActor& actor, MissionGunshipFields& ship) {
    followPlayer(ship);
    if (missionHits(actor, ship)) {
        actor.enterState(uint8_t(MissionGunshipState::Crash));
        gunshipCrash(env, actor, 0x200, false);
        return;
    }
    switch (MissionGunshipSub(actor.subState)) {
    case MissionGunshipSub::Start: missionStart(actor, ship, kSt15Gunship); break;
    case MissionGunshipSub::Cruise: missionCruise(actor, ship, kSt15Gunship); break;
    case MissionGunshipSub::Veer: missionVeer(actor, ship, kSt15Gunship); break;
    case MissionGunshipSub::Land: missionLand(actor, ship, kSt15Gunship); break;
    case MissionGunshipSub::FlyOff: missionFlyOff(actor, ship, kSt15Gunship); break;
    case MissionGunshipSub::Rocked: missionRocked(actor, ship); break;
    default: notPortedAt(kStoryPort, "static 0x59d310 sub-state", actor.subState);
    }
    missionRocking(actor, ship);
    if (ship.flight & kFlightSway) sway(actor);
    attitude(actor); // exe 0x59e420
    missionGunshipCollide(env, actor, ship, kSt15Gunship);
    if (ship.attackCooldown != 0) ship.attackCooldown -= 1u;
}

// exe 0x59d310
void st15Gunship(const PlayerEnv& env, StaticActor& actor) {
    MissionGunshipFields& ship = classState<MissionGunshipFields>(actor);
    const uint32_t statRow = statTableRow(actorStatTables().statics, actor.type);
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (MissionGunshipState(actor.state)) {
    case MissionGunshipState::Init: st15GunshipInit(actor, ship); break;
    case MissionGunshipState::Alive: st15GunshipAlive(env, actor, ship); break;
    case MissionGunshipState::Crash: gunshipCrash(env, actor, 0x200, false); break; // exe 0x5a28e0
    case MissionGunshipState::Gone: freeActor(actor); break;                       // exe 0x5afdd0
    default: notPortedAt(kStoryPort, "static 0x59d310 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    gunshipFireAsked(actor, 0x60, GunshipGun::Mission); // exe 0x59f5b0
    missionHitEntry(actor, ship, statRow);
    // exe 0x5f20f0(actor, 0xc0, 0x100, 0): the shadow list entry (render output)
}

// ---- the wave over Glyde's base 0x5a07b0 -----------------------------------------------------------------------------

// exe 0x5a1cd0: 8 debris statics (type 3 variant 0x10 / 0x11) at the ground - 10 around the ship (CRT rand)
void waveDebris(const PlayerEnv& env, StaticActor& actor) {
    for (int k = 0; k < 8; ++k) {
        const uint32_t d = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (!d) continue;
        StaticActor& debris = staticActorAt(d);
        debris.flags = kActorLive;
        debris.type = kDebrisType;
        debris.loopFlags |= kActorSweepGroup2;
        debris.variant = kDebrisVariant;
        debris.pos = actor.pos;
        debris.pos.y.whole = int16_t(actorGround(env, actor) - 10); // exe 0x40f190
        uint32_t yaw = (crtRand() & 0x3ff) + uint32_t(int32_t(actor.rotation.y)) + 0x200;
        if (crtRand() & 1) yaw ^= 0x800;
        debris.rotation.y = int16_t(yaw);
        integrateVelocity(debris, 0, 0, -0x500);
        int r;
        if (crtRand() & 1) {
            debris.variant = kDebrisSmallVariant;
            r = int(crtRand() & 0xf) + 0x20;
        } else {
            r = int(crtRand() & 0x1f) + 0x30;
        }
        int16_t dx, dz;
        angleVector(int(int16_t(uint16_t(yaw))), r, dx, dz); // exe 0x5e40c0
        auto&& params = DebrisParams(debris);
        params.life = kDebrisLife;
        params.driftX = uint8_t(uint32_t(uint16_t(dx) >> 2) & 0xffu);
        params.driftZ = uint8_t(uint32_t(uint16_t(dz) >> 2) & 0xffu);
    }
}

// exe 0x5a1300: the entry -- a free slot of the four taken (its point), a random wait, the debris and sound 0x1d0, the
// climb to y -0x580, then the cruise
void waveEntry(const PlayerEnv& env, StaticActor& actor, MissionGunshipFields& ship) {
    WaveEntrySlots& slots = waveEntrySlots();
    switch (EntryStep(actor.step)) {
    case EntryStep::Init:
        ship.flight = kFlightEntering | kFlightRocking | kFlightNoRock;
        ship.behaviour &= uint8_t(~kBehaveNoPushOut);
        actor.enterStep(uint8_t(EntryStep::TakeSlot));
        [[fallthrough]];
    case EntryStep::TakeSlot: {
        const int8_t n = slots.entering;
        if (n >= kEntrySlots) return;
        slots.entering = int8_t(n + 1);
        ship.timer = uint8_t((crtRand() & 0x1f) + 0x10); // CRT rand 0x611443
        actor.enterStep(uint8_t(EntryStep::Wait));
        uint32_t k = 0;
        for (; k < uint32_t(kEntrySlots); ++k)
            if (!(slots.taken & (1u << k))) {
                slots.taken |= 1u << k;
                break;
            }
        ship.entrySlot = int8_t(k);
        ship.pointX = entryPoints[k].x;
        ship.pointZ = entryPoints[k].z;
        return;
    }
    case EntryStep::Wait: {
        const uint8_t c = uint8_t(ship.timer - 1);
        ship.timer = c;
        if (c != 0) return;
        actor.enterStep(uint8_t(EntryStep::Climb));
        waveDebris(env, actor);
        soundPlayAtPos(kEntrySound, actor.pos);
        return;
    }
    case EntryStep::Climb:
        actor.velocity.y = int16_t(actor.velocity.y - 8);
        if (actor.velocity.y <= -0x100) actor.velocity.y = -0x100;
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        if (actor.pos.y.whole <= -0x580) actor.enterStep(uint8_t(EntryStep::Level));
        return;
    case EntryStep::Level:
        actor.velocity.y = int16_t(actor.velocity.y + 0x20);
        if (actor.velocity.y >= 0) {
            actor.velocity.y = 0;
            actor.subState = uint8_t(MissionGunshipSub::Start);
            actor.enterStep(0);
        }
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        return;
    default: return;
    }
}

// -c / 3 as the exe computes it (imul 0x55555555, sub, sar 1, + the sign)
int32_t negativeThird(int32_t c) {
    int32_t e = int32_t((int64_t(c) * 0x55555555) >> 32);
    e -= c;
    e >>= 1;
    return e + int32_t(uint32_t(e) >> 31);
}

// the wave's last ship down (ST30): a pool-0x20 type 0x2d (variant 1, mode 3) and the key thrown toward (0, 0x500),
// flag 0x20
void dropGlydeKey(StaticActor& actor) {
    const uint32_t r = allocActor20(); // exe 0x5e1160
    if (r) {
        AnimatedActor& carrier = animatedActorAt(r);
        carrier.flags = kActorLive;
        carrier.loopFlags |= kActorSweepGroup2;
        carrier.type = kKeyCarrierType;
        carrier.subtype = 0;
        carrier.variant = 1;
        carrier.pos = actor.pos;
        carrier.params[0] = 3;
        carrier.rotation.x = 0;
        carrier.rotation.y = int16_t(angleFromPoint(actor, 0, kKeyPointZ)); // exe 0x5e3bc0({0, y, 0x500}, a + 0x10)
        carrier.rotation.z = 0;
        carrier.velocity.x = 0;
        carrier.velocity.y = -0x100;
        carrier.velocity.z = int16_t(negativeThird(planarToPoint(actor, 0, kKeyPointZ)));
    }
    setStoryFlag(kFlagKeyDropped);
    if (const uint32_t s = allocActor60(kActorStageClass)) { // exe 0x5e12f0
        StaticActor& key = staticActorAt(s);
        key.flags = kActorLive;
        key.type = kKeyType;
        key.subtype = 0;
        key.variant = 0;
        key.loopFlags |= kActorSweepGroup2;
        key.pos = actor.pos;
        key.params[0] = 0;
        key.velocity.x = 0;
        key.velocity.y = -0x100;
        key.velocity.z = int16_t(negativeThird(planarToPoint(actor, 0, kKeyPointZ)));
        // the exe copies the pool-0x20 actor's angles (no null test: it would fault without one)
        if (r) key.rotation = animatedActorAt(r).rotation;
        else key.rotation.clearWords();
    }
}

// exe 0x5a1720: shot down -> spins and falls (as the family's crash); the start also frees its entry slot and, in ST30,
// counts the wave's ships down (the last one drops the key); the blast: a blast, the drops, gone
void waveCrash(const PlayerEnv& env, StaticActor& actor, MissionGunshipFields& ship) {
    const WaveCrashSub sub = WaveCrashSub(actor.subState);
    if (sub == WaveCrashSub::Blast) {
        if (const uint32_t s = allocActor60(0)) { // exe 0x5e1300
            StaticActor& blast = staticActorAt(s);
            auto&& params = BlastParams(actorHeaderAt(s));
            blast.flags = kActorLive;
            blast.type = typeByte(GlobalStaticType::Blast);
            blast.loopFlags |= kActorSweepGroup2;
            blast.subtype = 0;
            blast.variant = 0;
            blast.pos = actor.pos;
            params.size = 0xc0;
            params.attack = 4;
        }
        spawnDrops(actor.pos, staticStats(actor.type, 0).dropTable); // exe 0x5f1270
        freeActor(actor);
        return;
    }
    if (sub == WaveCrashSub::Start) {
        ship.flight = kFlightNoRock | kFlightRocking | kFlightSway;
        countDefeat(); // exe 0x5e53e0
        ship.turnStep = int16_t(actor.spin.y < 1 ? -0x80 : 0x80);
        actor.spin.x = int16_t(actor.rotation.z < 1 ? -0x40 : 0x40);
        actor.velocity.y = 0;
        ship.heading = actor.rotation.y;
        actor.acceleration.y = 4;
        ship.timer = 0x5a;
        actor.subState = uint8_t(WaveCrashSub::Fall);
        ship.behaviour &= uint8_t(~kBehaveNoPushOut);
        if (!(uint8_t(ship.entrySlot) & 0x80)) {
            WaveEntrySlots& slots = waveEntrySlots();
            slots.entering -= 1u;
            slots.taken &= uint8_t(~((1u << (uint32_t(int32_t(ship.entrySlot.get())) & 0x1f)) & 0xffu));
        }
        if (gameState().stage == kWaveStage) {
            WaveEntrySlots& slots = waveEntrySlots();
            const uint8_t n = uint8_t(slots.shipsLeft - 1);
            slots.shipsLeft = n;
            if (n == 0) dropGlydeKey(actor);
        }
    }
    if (sub == WaveCrashSub::Start || sub == WaveCrashSub::Fall) {
        actor.velocity.y = int16_t(actor.velocity.y + actor.acceleration.y);
        if (actor.velocity.y < 0x200) actor.velocity.y = 0x200;
        actor.spin.y = int16_t(actor.spin.y + ship.turnStep);
        actor.rotation.y = int16_t(actor.rotation.y + (actor.spin.y >> 4));
        if (actor.spin.y > 0x800) actor.spin.y = 0x800;
        if (actor.spin.y < -0x800) actor.spin.y = -0x800;
        if (uint16_t(actor.rotation.z + 0x200) < 0x400) actor.rotation.z = int16_t(actor.spin.x + actor.rotation.z);
        moveAlongYaw(actor.pos, ship.heading, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
        uint32_t r = actorTestMove(env, actor);                                       // exe 0x40f570
        if (int32_t(r) < 0) {
            queueBreakableWallHit(r, 0xff);
            r = actorPushOut(env, actor); // exe 0x410960
            if (int32_t(r) < 0) actor.pos = actor.prevPos;
            if (r & 0xf00) actor.velocity.z = 0;
            if (r & 0x3000) actor.velocity.y = 0;
            if (0x200 < uint16_t(actor.rotation.z + 0x100)) actor.subState = uint8_t(WaveCrashSub::Blast);
        }
        const uint8_t t = uint8_t(ship.timer - 1);
        ship.timer = t;
        if (t == 0) actor.subState = uint8_t(WaveCrashSub::Blast);
    }
    if (!(ship.timer & 1)) gunshipSmoke(actor);
    if (ship.timer < 0x4b) ship.flight |= kFlightDown;
}

// exe 0x5a08d0: as the ST15 one, no entry slot yet, starting with the entry; the wave's counters reset
void waveInit(StaticActor& actor, MissionGunshipFields& ship) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    const int16_t hp = hasStaticStats(actor.type) ? int16_t(staticStats(actor.type, 0).hitPoints) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.collisionBox.pointAt(kWaveGunship.box);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    ship.entrySlot = int8_t(kNoEntrySlot);
    actor.onPlatform = 0;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = 0;
    followPlayer(ship);
    ship.leaveHeading = int8_t(uint8_t(angleFromPoint(actor, ship.pointX, ship.pointZ) >> 4));
    ship.homeHeight = int32_t(actor.pos.y.whole) + kHomeAbove;
    actor.clearClassBlock(); // exe 0x40dc40
    actor.enterState(uint8_t(MissionGunshipState::Alive));
    actor.subState = uint8_t(MissionGunshipSub::Entry);
    WaveEntrySlots& slots = waveEntrySlots();
    slots.entering = 0;
    slots.taken = 0;
    slots.shipsLeft = kWaveShips;
}

// exe 0x5a09d0
void waveAlive(const PlayerEnv& env, StaticActor& actor, MissionGunshipFields& ship) {
    followPlayer(ship);
    if (missionHits(actor, ship)) { // exe 0x59e0e0
        actor.enterState(uint8_t(MissionGunshipState::Crash));
        waveCrash(env, actor, ship);
        return;
    }
    switch (MissionGunshipSub(actor.subState)) {
    case MissionGunshipSub::Start: missionStart(actor, ship, kWaveGunship); break;
    case MissionGunshipSub::Cruise: missionCruise(actor, ship, kWaveGunship); break;
    case MissionGunshipSub::Veer: missionVeer(actor, ship, kWaveGunship); break;
    case MissionGunshipSub::Land: missionLand(actor, ship, kWaveGunship); break;
    case MissionGunshipSub::FlyOff: missionFlyOff(actor, ship, kWaveGunship); break;
    case MissionGunshipSub::Rocked: missionRocked(actor, ship); break; // 0x5a1280 = 0x59dda0 byte for byte
    case MissionGunshipSub::Entry: waveEntry(env, actor, ship); break;
    default: notPortedAt(kStoryPort, "static 0x5a07b0 sub-state", actor.subState);
    }
    missionRocking(actor, ship);
    if (ship.flight & kFlightEntering) return;
    if (ship.flight & kFlightSway) sway(actor); // exe 0x5a0600
    attitude(actor);                            // exe 0x59e420
    missionGunshipCollide(env, actor, ship, kWaveGunship);
    if (ship.attackCooldown != 0) ship.attackCooldown -= 1u;
}

// exe 0x5a07b0
void glydeBaseGunship(const PlayerEnv& env, StaticActor& actor) {
    MissionGunshipFields& ship = classState<MissionGunshipFields>(actor);
    const uint32_t statRow = statTableRow(actorStatTables().statics, actor.type);
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (MissionGunshipState(actor.state)) {
    case MissionGunshipState::Init: waveInit(actor, ship); break;
    case MissionGunshipState::Alive: waveAlive(env, actor, ship); break;
    case MissionGunshipState::Crash: waveCrash(env, actor, ship); break;
    case MissionGunshipState::Gone: freeActor(actor); break; // exe 0x5afdd0
    default: notPortedAt(kStoryPort, "static 0x5a07b0 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    gunshipFireAsked(actor, 0x60, GunshipGun::Mission); // exe 0x59f5b0
    missionHitEntry(actor, ship, statRow);
}

} // namespace

void missionGunshipUpdate(const PlayerEnv& env, uint32_t actor) { st15Gunship(env, staticActorAt(actor)); }
void glydeBaseGunshipUpdate(const PlayerEnv& env, uint32_t actor) { glydeBaseGunship(env, staticActorAt(actor)); }

} // namespace od::port
