// The pickups (see pickups.hpp).
#include "game/pickups.hpp"

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_points.hpp"
#include "game/message.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/rewards.hpp"
#include "game/net_world.hpp"
#include "game/shots/shot_common.hpp"
#include "game/story_flags.hpp"
#include "game/vectors.hpp"

namespace od::port {

namespace {

inline constexpr const char* kPickupsArea = "pickups";

// A drop table (exe data 0x8993d0, 0x18 bytes each): per pickup kind the counts of each size and the chance (RNG & 0xff
// at most) of the kinds past the first
struct DropRow : DataRecord {
    static constexpr uint32_t kBytes = 8;
    std::array<uint8_t, 7> counts{}; // +0
    uint8_t chance = 0;              // +7
    template <class R> static DropRow load(const R& r) {
        DropRow d;
        for (size_t i = 0; i < d.counts.size(); ++i) d.counts[i] = r.u8(i);
        d.chance = r.u8(7);
        return d;
    }
};
constexpr uint32_t kDropTableSize = 0x18;
constexpr uint32_t kDropRows = 3; // a table's rows (8 bytes each)
// row `kind` of drop table `table`
DropRow dropRow(uint32_t table, uint32_t kind) {
    if (kind >= kDropRows) tableIndexError(kind, kDropRows);
    return dataRecord<DropRow>(stateAddress(TableId::PickupsDropTables) + table * kDropTableSize + kind * DropRow::kBytes, DropRow::kBytes);
}

constexpr uint32_t kFlagNoDrops = 0x246;     // the defeated enemies drop nothing (a scripted fight)
constexpr uint8_t kLaunchedVariant = 1;      // a pickup launched with a velocity of its spawner's
// the eight spread yaws an aimed launch picks from (exe data)
const DataTable<ScalarTable<uint16_t, 8>> launchSpreadYaws{TableId::PickupsLaunchSpreadYaws};
constexpr int kLaunchJitter = 0x7f;
// the collected pickup's phases (the header's sub-state); the rise's countdown is the step byte
enum class CollectSub : uint8_t { Give = 0, Rise = 1 };
constexpr uint8_t kRiseTicks = 8;

// the amounts by size (exe data): zenny, weapon energy, life
const DataTable<ScalarTable<int16_t, 8>> zennyAmounts{TableId::PickupsZennyAmounts};
const DataTable<ScalarTable<int16_t, 8>> energyAmounts{TableId::PickupsEnergyAmounts};
const DataTable<ScalarTable<int16_t, 8>> lifeAmounts{TableId::PickupsLifeAmounts};
// the sprites of the energy and the life (the zenny's is its size)
constexpr uint8_t kEnergySprite = 7, kLifeSprite = 8;
const DataTable<CollisionBoxRecord> pickupBox{TableId::PickupsPickupBox};
constexpr uint16_t kTimeout = 0x100;
constexpr int16_t kGravity = 0x30;
constexpr int kSettleSpeed = 0x41; // a bounce slower than this settles (with a forward speed below 0x11)
constexpr int kRollReach = 0xa0, kCollectReach = 0x40;
constexpr int16_t kRollSpeed = 0x240;
constexpr int kGroundSnap = 0x30;
constexpr uint32_t kCollectLift = 0xb00000; // the collected pickup rises from 0xb0 above the player (16.16)
constexpr uint16_t kZennySound = 0x85, kRefillSound = 0x86;
constexpr uint8_t kVacuumPulls = 0x02; // the player's suction byte while the vacuum arm pulls
constexpr int kVacuumGrabLevel = 2;    // above this level of the vacuum arm the pickup jumps to the player

// the difficulty's multiplier of the amounts (/ 16): 4 at the easiest, 1 normally, 1/2 at the hardest
int amountMultiplier() {
    const uint8_t difficulty = gameState().difficulty;
    return difficulty == 0 ? 0x40 : (difficulty != kHardestDifficulty ? 0x10 : 8);
}

// exe 0x562740: the vacuum arm's pull (the player's suction byte & 2): within the weapon's range the pickup moves to
// the player by (level + 1) * 0x80 a tick (above level 2 it jumps there and the pull ends); 1 = pulled
int vacuumPull(StaticActor& actor) {
    Player& p = player();
    const int w = p.activeSpecial;
    if (!(p.suction & kVacuumPulls)) return 0;
    const int range = weaponStat(p, w, WeaponStat::Range);
    if (distance3(actor.pos, p.pos) > range) return 0;
    const int level = weaponLevel(p, w, WeaponStat::Extra);
    if (level > kVacuumGrabLevel) {
        p.suction ^= kVacuumPulls;
        actor.pos.x = p.pos.x;
        actor.prevPos.x = p.pos.x;
        actor.pos.y = p.pos.y;
        actor.prevPos.y = p.pos.y;
        actor.pos.z = p.pos.z;
        actor.prevPos.z = p.pos.z;
        return 1;
    }
    // exe 0x5e36f0(pos, the angle to the player, 0, 0, (level + 1) * 0x80) through the actor's yaw
    const int16_t yaw = actor.rotation.y;
    actor.rotation.y = int16_t(angleOf(actor.pos, p.pos));
    integrateVelocity(actor, 0, 0, (level + 1) * 0x80);
    actor.rotation.y = yaw;
    return 1;
}

// the wall / floor reflection of exe 0x562440 / 0x5624f0: a bounce off a floor halves the speed; slow enough it
// settles (`settles`: on the floor and the next state)
void bounce(const PlayerEnv& env, StaticActor& actor, bool settles) {
    if (int32_t(actorTestMove(env, actor)) >= 0) return;
    const uint32_t r = actorPushOut(env, actor);
    if (!reflectRotationOffWalls(actor.rotation, r)) return;
    const int16_t v = actor.velocity.y;
    const int speed = v < 0 ? -v : v;
    if (settles) {
        if (speed < kSettleSpeed && actor.velocity.z < 0x11) {
            actor.velocity.z = 0;
            actor.velocity.y = 0;
            actor.airborne = 0;
            ++actor.state;
        } else {
            actor.velocity.z = int16_t(actor.velocity.z >> 1);
            actor.velocity.y = wrap16(-(v >> 1));
            actor.airborne = kAirborneFalling;
        }
    } else if (speed < kSettleSpeed) {
        actor.velocity.y = 0;
        actor.airborne = 0;
    } else {
        actor.velocity.y = wrap16(-(v >> 1));
        actor.airborne = kAirborneFalling;
    }
}

// the collected pickup's gift: the zenny, the weapon energy or the life by its size (exe 0x5e51a0 / 0x5e5390 /
// 0x5e52f0)
void give(const StaticActor& actor, const PickupFields& pickup) {
    const int mult = amountMultiplier();
    const size_t size = size_t(int32_t(pickup.size));
    switch (PickupKind(pickup.kind.get())) {
    case PickupKind::Zenny:
        zennyAdd(zennyAmounts[size] * mult >> 4);
        soundPlayAtPos(kZennySound, actor.pos);
        break;
    case PickupKind::WeaponEnergy:
        weaponEnergyAdd(player(), energyAmounts[size] * mult >> 4);
        soundPlayAtPos(kRefillSound, actor.pos);
        break;
    case PickupKind::Life:
        hitPointsAdd(player(), wrap16(lifeAmounts[size] * mult >> 4));
        soundPlayAtPos(kRefillSound, actor.pos);
        break;
    default: break;
    }
}

} // namespace

namespace {

// netplay host: a drop's pickup, as the clients spawn it (game/net_world)
void notePickupSpawned(const StaticActor& d) {
    const NetWorldHooks& net = netWorldHooks();
    if (!net.pickupSpawned) return;
    const auto& pickup = heldClassStateOf<PickupFields>(d);
    PickupSpawn p;
    p.pos[0] = d.pos.x;
    p.pos[1] = d.pos.y;
    p.pos[2] = d.pos.z;
    p.kind = pickup ? uint8_t(pickup->kind) : 0;
    p.size = pickup ? uint8_t(pickup->size) : 0;
    p.variant = d.variant;
    p.velocityXY = d.velocity.xyWord();
    p.velocityZSpare = d.velocity.zSpareWord();
    p.yaw = d.rotation.y;
    net.pickupSpawned(p);
}

// exe 0x5f1270's body on a point read through `x` / `y` / `z` (each pickup reads the point again)
template <class X, class Y, class Z> void spawnDropsAt(X x, Y y, Z z, uint32_t table) {
    if (storyFlag(kFlagNoDrops)) return;
    for (uint32_t kind = 0; kind < kDropRows; ++kind) {
        const DropRow row = dropRow(table, kind);
        for (uint32_t size = 0; size < row.counts.size(); ++size)
            for (uint32_t n = row.counts[size]; n != 0; --n) {
                if (kind >= 1 && (rngStep() & 0xff) > row.chance) continue;
                const uint32_t address = allocActor60(0);
                if (address == 0) continue;
                StaticActor& d = staticActorAt(address);
                d.flags = kActorLive;
                d.type = typeByte(GlobalStaticType::Pickup);
                d.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
                d.variant = 0;
                d.pos.x = x();
                d.pos.y = y();
                auto&& pickup = classState<PickupFields>(d);
                pickup.size = int8_t(size);
                d.pos.z = z();
                pickup.kind = int8_t(kind);
                notePickupSpawned(d);
            }
    }
}

} // namespace

void spawnDrops(const Fixed16Vec3& pos, uint32_t table) {
    spawnDropsAt([&] { return int32_t(pos.x); }, [&] { return int32_t(pos.y); }, [&] { return int32_t(pos.z); },
                 table);
}

void spawnDrops(const int32_t pos[3], uint32_t table) {
    spawnDropsAt([&] { return pos[0]; }, [&] { return pos[1]; }, [&] { return pos[2]; }, table);
}

void spawnLaunchedPickup(const Fixed16Vec3& pos, const ShortVec4& velocity, uint8_t kind, uint8_t size) {
    const int32_t at[3] = {pos.x, pos.y, pos.z};
    spawnLaunchedPickup(at, velocity.xyWord(), velocity.zSpareWord(), kind, size);
}

void spawnLaunchedPickup(const int32_t pos[3], uint32_t velocityXY, uint32_t velocityZSpare, uint8_t kind,
                         uint8_t size) {
    if (storyFlag(kFlagNoDrops)) return;
    const uint32_t address = allocActor60(0);
    if (address == 0) return;
    StaticActor& d = staticActorAt(address);
    auto&& pickup = classState<PickupFields>(d);
    d.flags = kActorLive;
    d.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    d.type = typeByte(GlobalStaticType::Pickup);
    d.variant = kLaunchedVariant;
    d.pos.x = pos[0];
    d.pos.y = pos[1];
    d.pos.z = pos[2];
    d.velocity.setWords(velocityXY, velocityZSpare);
    const uint16_t aim = uint16_t(d.velocity.spare); // the launch's aim (kPickupAimed)
    if (aim & kPickupAimed) {
        rngStep();
        const uint32_t r = rngStep();
        d.rotation.y = int16_t(launchSpreadYaws[r & 7] + (aim & kAngleMask) + (r & 0xff) - kLaunchJitter);
    } else {
        d.rotation.y = int16_t(rngStep() & kAngleMask);
    }
    pickup.kind = int8_t(kind);
    pickup.size = int8_t(size);
    notePickupSpawned(d);
}

void spawnPickup(const PickupSpawn& p) {
    const uint32_t address = allocActor60(0);
    if (address == 0) return;
    StaticActor& d = staticActorAt(address);
    auto&& pickup = classState<PickupFields>(d);
    d.flags = kActorLive;
    d.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    d.type = typeByte(GlobalStaticType::Pickup);
    d.variant = p.variant;
    d.pos.x = p.pos[0];
    d.pos.y = p.pos[1];
    d.pos.z = p.pos[2];
    d.velocity.setWords(p.velocityXY, p.velocityZSpare);
    d.rotation.y = p.yaw;
    pickup.kind = int8_t(p.kind);
    pickup.size = int8_t(p.size);
}

void spawnLaunchedDrops(const Fixed16Vec3& pos, const ShortVec4& velocity, uint32_t table) {
    const int32_t at[3] = {pos.x, pos.y, pos.z};
    spawnLaunchedDrops(at, velocity.xyWord(), velocity.zSpareWord(), table);
}

void spawnLaunchedDrops(const int32_t pos[3], uint32_t velocityXY, uint32_t velocityZSpare, uint32_t table) {
    if (storyFlag(kFlagNoDrops)) return;
    for (uint32_t kind = 0; kind < kDropRows; ++kind) {
        const DropRow row = dropRow(table, kind);
        for (uint32_t size = 0; size < row.counts.size(); ++size)
            for (uint32_t n = row.counts[size]; n != 0; --n) {
                if (kind >= 1 && (rngStep() & 0xff) > row.chance) continue;
                spawnLaunchedPickup(pos, velocityXY, velocityZSpare, uint8_t(kind), uint8_t(size));
            }
    }
}

void pickupUpdate(const PlayerEnv& env, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    auto&& pickup = classState<PickupFields>(actor);
    actor.prevPos = actor.pos;
    switch (PickupState(actor.state)) {
    case PickupState::Launch: {
        actor.runFlags |= kActorWarpKept;
        if (actor.variant == 0) {
            uint32_t r = rngStep();
            actor.velocity.x = 0;
            actor.rotation.y = int16_t(r & kAngleMask);
            r = rngStep();
            actor.velocity.y = wrap16(((-8 - int(r & 0xf)) - pickup.size) * 0x20);
            r = rngStep();
            actor.velocity.z = wrap16(((0x10 - int(r & 0xf)) - pickup.size) * 0x20);
        }
        actor.rotation.x = 0;
        actor.rotation.z = 0;
        pickup.drawPitch = 0;
        pickup.drawYaw = 0;
        pickup.drawRoll = 0;
        pickup.bob = 0;
        pickup.timeout = kTimeout;
        pickup.bobPhase = 0;
        pickup.spinSpeed = int8_t((pickup.size * 5 >> 1) + 6);
        actor.airborne = kAirborneFalling;
        actor.onPlatform = 0;
        actor.collisionBox.pointAt(pickupBox.stateAddress());
        const PickupKind kind = PickupKind(pickup.kind.get());
        if (kind == PickupKind::Zenny)
            pickup.sprite = uint8_t(pickup.size);
        else if (kind == PickupKind::WeaponEnergy)
            pickup.sprite = kEnergySprite;
        else if (kind == PickupKind::Life)
            pickup.sprite = kLifeSprite;
        actorPushOut(env, actor);
        actor.airborne = kAirborneFalling;
        ++actor.state;
        break;
    }
    case PickupState::Bounce:
        actor.velocity.y = wrap16(actor.velocity.y + kGravity);
        integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
        vacuumPull(actor);
        bounce(env, actor, true);
        pickup.setDrawPose(uint16_t(uint32_t(uint8_t(pickup.timeout)) << 8), uint16_t(actor.rotation.y));
        break;
    case PickupState::Rest: {
        if (actor.airborne == 0) {
            const uint32_t phase = uint8_t(uint8_t(pickup.bobPhase) + 1) & 0x3fu;
            pickup.bobPhase = uint16_t(phase);
            pickup.bob = int16_t((cos64(phase) >> 8) - 8);
        } else {
            actor.velocity.y = wrap16(actor.velocity.y + kGravity);
            pickup.bobPhase = 0;
            pickup.bob = 0;
        }
        bool test = true;
        if (actor.velocity.z == 0 && actor.velocity.y == 0) {
            test = vacuumPull(actor) != 0;
        } else {
            integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
            vacuumPull(actor);
        }
        if (test) bounce(env, actor, false);
        if (distance3(actor.pos, player().pos) < kRollReach) {
            actor.rotation.y = int16_t(angleOf(actor.pos, player().pos));
            actor.velocity.z = kRollSpeed;
        } else {
            actor.velocity.z = 0;
        }
        pickup.drawPitch = 0;
        pickup.drawYaw = uint16_t(pickup.drawYaw + pickup.spinSpeed * -0x10 + 0x80);
        break;
    }
    case PickupState::Collected: {
        const Player& p = player();
        actor.pos.x = p.pos.x;
        actor.pos.y = int32_t(uint32_t(p.pos.y) - kCollectLift);
        actor.pos.z = p.pos.z;
        if (CollectSub(actor.subState) == CollectSub::Give) {
            give(actor, pickup);
            actor.step = kRiseTicks;
            ++actor.subState;
            pickup.spinSpeed = int8_t(pickup.spinSpeed * 3 >> 2);
        } else if (CollectSub(actor.subState) == CollectSub::Rise) {
            const uint8_t c = uint8_t(actor.step - 1);
            actor.step = c;
            if (c != 0) {
                pickup.bob = int16_t(pickup.bob - 8);
                pickup.drawYaw = uint16_t(pickup.drawYaw + 0x100u);
                pickup.drawPitch = 0;
                break;
            }
            freeActor(actor);
        }
        break;
    }
    default: notPortedAt(kPickupsArea, "pickup state", actor.state);
    }
    if (actor.airborne == 0) {
        const int ground = actorGround(env, actor) - actor.collisionBox->maxY;
        if (actor.pos.y.whole < ground - kGroundSnap)
            actor.airborne = kAirborneFalling;
        else
            actor.pos.y = int32_t(uint32_t(ground) << 16);
    }
    if (actor.state < uint8_t(PickupState::Collected)) {
        if (distance3(actor.pos, player().pos) < kCollectReach && player().hitPoints >= 0) {
            actor.state = uint8_t(PickupState::Collected);
        } else if (!storyFlag(kFlagPickupsStay)) {
            const uint16_t t = uint16_t(pickup.timeout - 1);
            pickup.timeout = t;
            if (t == 0) {
                freeActor(actor);
                return;
            }
        }
    }
    // exe 0x562810: an entry in the pickup draw list 0x6d006c (render output); blinking while the timeout < 0x41
}

} // namespace od::port
