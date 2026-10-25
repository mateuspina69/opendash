// The gunship over Nino's square (ST18 static type 0x1b sub 1, exe 0x59f880; the per-stage table [[*0xa6bb8c + 0x1b * 4]
// + 1 * 4]), one routine per exe routine (story.md section 16). It flies around the player, fires pairs of shots (the
// enemy shots, pool 0x40 type 9) and dives at him; shot down (variant 1) it sinks to the square and rocks until it
// settles -> its story flag (0x2b) -> the ST18 handler 0x57f620 state 4 -> script 0x38 (story 7).
#include "content/nino/square_gunship.hpp"
#include "content/nino/square_gunship_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_state.hpp"
#include "game/gunship_flight.hpp"

#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/enemy_motion.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace square_gunship_state;

namespace {


// [0x8ce9f0]
enum class SquareGunshipState : uint8_t {
    Init = 0,  // exe 0x59f9f0
    Alive = 1, // exe 0x59fac0
    Crash = 2, // exe 0x5a28e0
    Sink = 3,  // exe 0x5a0230
};
// [0x8cea00]
enum class SquareGunshipSub : uint8_t {
    FlyStart = 0, // exe 0x59fbc0
    Fly = 1,      // exe 0x59fc00
    Dive = 2,     // exe 0x59ffc0
    Idle = 3,     // exe 0x52ef00 (also the table's entries 4 / 5)
    Idle4 = 4,
    Idle5 = 5,
};
// the fly's steps
enum class FlyStep : uint8_t { Init = 0, PickHeading = 1, Cruise = 2, Burst = 3, RunStart = 4, Run = 5, PauseStart = 6, Pause = 7 };
// the dive's steps
enum class DiveStep : uint8_t { Init = 0, Level = 1, Aim = 2, Plunge = 3, PullUp = 4 };
// the sink's sub-states
enum class SinkSub : uint8_t { Start = 0, Sink = 1 };

constexpr uint8_t kBomberVariant = 3;   // bombs and crashes
constexpr uint8_t kSinkingVariant = 1;  // (any variant but 0 / 3) sinks and rocks when shot down
const DataTable<CollisionBoxRecord> squareGunshipBox{TableId::SquareGunshipBox};
const DataTable<ScalarTable<int16_t, 8>> flyHeadings{TableId::SquareGunshipFlyHeadings};
const DataTable<ScalarTable<uint8_t, 16>> runTicks{TableId::SquareGunshipRunTicks};
const DataTable<ScalarTable<uint8_t, 16>> burstTicks{TableId::SquareGunshipBurstTicks};
constexpr int16_t kDiveCooldown = 0x258, kDiveAgain = 0xf0;
constexpr int8_t kLevelPitch = 0x30, kPullUpPitch = -0x30;
constexpr int16_t kCruiseSpeed = -0x400, kSpeedStep = -0x20;
constexpr int kPlayerNear = 0xbb8, kDiveFar = 0x2bc;
constexpr uint16_t kBurstSound = 0x1a6, kDiveSound = 0x1a7, kHurtSound = 0x8d, kStrongHurtSound = 0x8e;
constexpr uint8_t kRockTicks = 0xf;
constexpr uint32_t kBossGaugeColour = 0xffa020;
constexpr uint8_t kCruiseTicks = 0xf0;
constexpr int16_t kBombCooldown = 0xf0;
constexpr uint8_t kBombBurst = 0x10;
constexpr int kBombNear = 0x709;

// exe 0x5e3810(pos, rot, 0, 0, z) with the rotation {the dive's pitch, the yaw, 0}
void moveAlongDive(StaticActor& actor, const SquareGunshipFields& ship, int z) {
    const int16_t rot[3] = {ship.divePitch, actor.rotation.y, 0};
    moveAlongRotation(actor.pos, rot, 0, 0, z);
}

// exe 0x5a0770(angle word, angle, step): the angle stepped toward `angle` (set when within the step)
void pitchStep(int16_t& value, uint32_t angle, int step) {
    const int d = angleDiff(uint32_t(int32_t(value)), angle);
    if (uint32_t(d + step) < uint32_t(step * 2)) {
        value = int16_t(angle);
        return;
    }
    value = int16_t(value + (d < 1 ? -step : step));
}

// the altitude hold of 0x59fc00 / 0x5a0230: the forward speed toward -0x400 (exe 0x5e5510), the vertical speed toward
// the home height, the move (exe 0x5e3690), no overshoot
void holdAltitude(StaticActor& actor, const SquareGunshipFields& ship) {
    if (stepTowardLimit(actor.velocity.z, uint16_t(actor.acceleration.z), 0x400) != 0) actor.velocity.z = kCruiseSpeed;
    const int d = int(actor.pos.y.whole) - ship.homeHeight;
    actor.velocity.y = 0;
    if (d > 0) actor.velocity.y = -0x100;
    if (d < 0) actor.velocity.y = 0x100;
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    if (d != 0 && ((int(actor.pos.y.whole) - ship.homeHeight) ^ d) < 0) actor.pos.y.whole = int16_t(ship.homeHeight);
}

// exe 0x5a0500: the hits of the box (none while the rock timer runs): a strong one (or one of a 16th of the hit points)
// rocks it
GunshipHit squareGunshipHits(StaticActor& actor, SquareGunshipFields& ship) {
    if (ship.rockTicks != 0) return GunshipHit::None;
    const uint32_t v = ship.hitBox.result;
    const uint32_t n = v & kHitDamageMask;
    if (n == 0 || !(v & (kHitAttackByPlayer | kHitAttackStrong))) return GunshipHit::None;
    GunshipHit r = GunshipHit::None;
    actor.tint = kTintFlash;
    if (!(v & kHitAttackStrongPush) && int(n) < (actor.maxHitPoints >> 4)) {
        soundPlayAtPos(kHurtSound, actor.pos);
    } else {
        r = GunshipHit::Strong;
        soundPlayAtPos(kStrongHurtSound, actor.pos);
        ship.rockTicks = kRockTicks;
    }
    if (hitPointsAdd(actor, -int(n)) != 0) r = GunshipHit::Dead;
    return r;
}

// exe 0x5a0590: the room collision of the move (none while entering)
void squareGunshipCollide(const PlayerEnv& env, StaticActor& actor, SquareGunshipFields& ship) {
    uint8_t b = 0;
    if (!(ship.flight & kFlightEntering)) {
        const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        const int16_t q[3] = {actor.prevPos.x.whole, actor.prevPos.y.whole, actor.prevPos.z.whole};
        const uint32_t r = testMoveBlocked(env, p, q, actor.collisionBox, 2);
        const uint8_t water = (r & 0x20000000) ? kContactWater : 0;
        b = water;
        if (int32_t(r) < 0) {
            b = uint8_t(water | kContactWall);
            if (!(ship.behaviour & kBehaveNoPushOut)) {
                const uint32_t r2 = actorPushOutMask(env, actor, 2);
                if (r2 & 0xf00) b = uint8_t(water | kContactWall | kContactFloor);
                if (r2 & 0x3000) b |= kContactSide;
            }
        }
    }
    ship.contact = b;
}

// exe 0x59ff00 (variant 3, the tail of 0x59fc00): facing the player within +-0x280, nearer than 0x709 and the cooldown
// out -> 0x10 ticks of bombs (one every 4th); then the cooldown 0xf0, the straight run off
void bombRun(StaticActor& actor, SquareGunshipFields& ship) {
    const Player& p = player();
    const int dist = planarDistance(actor.pos, p.pos);
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(p.pos, actor.pos)));
    const uint8_t c = ship.burst;
    if (c == 0) {
        if (ship.bombCooldown == 0 && uint32_t(d + 0x280) < 0x500 && dist < kBombNear) ship.burst = kBombBurst;
        return;
    }
    if ((c & 3) == 0) dropBomb(actor, int16_t(actor.velocity.z >> 2));
    ship.burst = uint8_t(c - 1);
    if (uint8_t(c - 1) == 0) {
        ship.bombCooldown = kBombCooldown;
        ship.behaviour &= uint8_t(~kBehaveStraight);
    }
}

void fly(StaticActor& actor, SquareGunshipFields& ship);

// exe 0x59fbc0: the level pitch, the cruise speed, the rates cleared, then the fly
void flyStart(StaticActor& actor, SquareGunshipFields& ship) {
    ship.pitchTarget = kLevelPitch;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = kCruiseSpeed;
    actor.acceleration.setXY(0, 0);
    actor.spin.x = 0;
    actor.spin.y = 0;
    actor.subState = uint8_t(SquareGunshipSub::Fly);
    actor.enterStep(uint8_t(FlyStep::Init));
    fly(actor, ship);
}

// exe 0x59fc00: circle / shoot -- a heading picked (0x8cea28), a cruise (0xf0 ticks; facing the player within +-0x280
// and nearer than 0xbb8: sound 0x1a6, a burst for 0x8cea48 ticks), the burst (every other tick a shot, the sides
// alternating), a straight run (0x8cea38 ticks, no steering), a pause; far from the player once the cooldown is out:
// the dive
void fly(StaticActor& actor, SquareGunshipFields& ship) {
    // the exe passes the player's struct, not its position: its +0 / +8 words
    const int dist = planarDistance(actor.pos, playerStructAsPoint());
    switch (FlyStep(actor.step)) {
    case FlyStep::Init:
        ship.flight = 0;
        ship.pitchTarget = kLevelPitch;
        ship.behaviour = uint8_t((ship.behaviour & uint8_t(~(kBehaveStraight | kBehaveNoPushOut))) | kBehaveLevel);
        actor.acceleration.z = kSpeedStep;
        actor.enterStep(uint8_t(FlyStep::PickHeading));
        [[fallthrough]];
    case FlyStep::PickHeading:
        ship.timer = kCruiseTicks;
        ship.behaviour &= uint8_t(~kBehaveStraight);
        actor.enterStep(uint8_t(FlyStep::Cruise));
        ship.heading = flyHeadings[rngStep() & 7];
        [[fallthrough]];
    case FlyStep::Cruise: {
        const uint8_t t = uint8_t(ship.timer - 1);
        ship.timer = t;
        if (t == 0) actor.enterStep(uint8_t(FlyStep::RunStart));
        if (actor.variant == kBomberVariant) break;
        const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(player().pos, actor.pos)));
        if (uint32_t(d + 0x280) < 0x500 && dist <= kPlayerNear && !(ship.behaviour & kBehaveRocking)) {
            ship.timer = burstTicks[rngStep() & 0xf];
            actor.enterStep(uint8_t(FlyStep::Burst));
            soundPlayAtPos(kBurstSound, actor.pos);
        }
        break;
    }
    case FlyStep::Burst: {
        const uint8_t t = ship.timer;
        if (!(t & 1)) ship.flight |= (t & 3) ? kGunLeft : kGunRight;
        ship.timer = uint8_t(t - 1);
        if (uint8_t(t - 1) == 0 || (ship.behaviour & kBehaveRocking)) actor.enterStep(uint8_t(FlyStep::RunStart));
        break;
    }
    case FlyStep::RunStart:
        ship.timer = runTicks[rngStep() & 0xf];
        actor.enterStep(uint8_t(FlyStep::Run));
        ship.behaviour |= kBehaveStraight;
        [[fallthrough]];
    case FlyStep::Run: {
        const uint8_t t = uint8_t(ship.timer - 1);
        ship.timer = t;
        if (t == 0 || !(ship.behaviour & kBehaveStraight)) {
            actor.enterStep(uint8_t(FlyStep::PickHeading));
            ship.behaviour &= uint8_t(~kBehaveStraight);
        }
        break;
    }
    case FlyStep::PauseStart:
        ship.timer = runTicks[rngStep() & 0xf];
        actor.enterStep(uint8_t(FlyStep::Pause));
        [[fallthrough]];
    case FlyStep::Pause: {
        const uint8_t t = uint8_t(ship.timer - 1);
        ship.timer = t;
        if (t == 0) actor.enterStep(uint8_t(FlyStep::RunStart));
        break;
    }
    default: break;
    }
    if ((ship.behaviour & kBehaveStraight) || steerToward(actor, player().pos, 0x240, 0x18) == 0) steerDamp(actor, 0x18);
    holdAltitude(actor, ship);
    if (ship.cooldown == 0 && dist > kDiveFar) {
        actor.subState = uint8_t(SquareGunshipSub::Dive);
        actor.enterStep(uint8_t(DiveStep::Init));
    }
    if (actor.variant == kBomberVariant) bombRun(actor, ship);
}

// exe 0x59ffc0: the dive -- back to level (the forward speed to 0) while turning toward the player, then (sound 0x1a7)
// aimed 0xb4 above the player, accelerating toward that point (until near it or a wall / floor), then pulled up to the
// home height: the cooldown 0xf0, back to the fly
void dive(StaticActor& actor, SquareGunshipFields& ship) {
    switch (DiveStep(actor.step)) {
    case DiveStep::Init:
        ship.flight = kFlightNoRock | kFlightProbeWalls;
        ship.pitchTarget = kLevelPitch;
        ship.behaviour = uint8_t((ship.behaviour & uint8_t(~(kBehaveStraight | kBehaveNoPushOut))) | kBehaveLevel);
        actor.acceleration.z = kSpeedStep;
        actor.velocity.y = 0;
        actor.acceleration.y = 6;
        ship.heading = actor.rotation.y;
        actor.enterStep(uint8_t(DiveStep::Level));
        [[fallthrough]];
    case DiveStep::Level:
        actor.velocity.z = int16_t(actor.velocity.z + 0x20);
        if (actor.velocity.z > 0) actor.velocity.z = 0;
        if (steerToward(actor, player().pos, 0x240, 0x18) == 0) {
            steerDamp(actor, 0x18);
            if (actor.velocity.z == 0) actor.enterStep(uint8_t(DiveStep::Aim));
        }
        bobVertical(actor);
        moveAlongYaw(actor.pos, ship.heading, 0, actor.velocity.y, actor.velocity.z);
        return;
    case DiveStep::Aim:
        ship.pitchTarget = kLevelPitch;
        ship.divePitch = 0;
        ship.diveHeight = int16_t(player().pos.y.whole - 0xb4);
        actor.enterStep(uint8_t(DiveStep::Plunge));
        soundPlayFlat(kDiveSound);
        [[fallthrough]];
    case DiveStep::Plunge: {
        actor.velocity.z = int16_t(actor.velocity.z - 0x50);
        if (actor.velocity.z < -0x500) actor.velocity.z = -0x500;
        const Player& p = player();
        int16_t to[3] = {p.pos.x.whole, p.pos.y.whole, p.pos.z.whole};
        to[1] = ship.diveHeight;
        turnYawTo(actor.rotation.y, uint32_t(angleOf(p.pos, actor.pos)), 0x18); // exe 0x5a0710
        const int16_t me[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        pitchStep(ship.divePitch, uint32_t(pitchOf(me, to) + 0x800), 0x18); // exe 0x5a0740
        moveAlongDive(actor, ship, actor.velocity.z);
        int d = int(actor.pos.y.whole) - int(ship.diveHeight);
        if (d < 0) d = -d;
        if (d * 0x10 <= -int(actor.velocity.z) || (ship.contact & (kContactWall | kContactFloor))) {
            ship.pitchTarget = kPullUpPitch;
            actor.enterStep(uint8_t(DiveStep::PullUp));
        }
        return;
    }
    case DiveStep::PullUp: {
        const int16_t me[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        const int16_t to[3] = {me[0], int16_t(actor.pos.y.whole - 100), me[2]};
        pitchStep(ship.divePitch, uint32_t(pitchOf(me, to) + 0x800), 0x18); // exe 0x5a0740
        moveAlongDive(actor, ship, actor.velocity.z);
        int d = int(actor.pos.y.whole) - ship.homeHeight;
        if (d < 0) d = -d;
        if (d * 0x10 < -int(actor.velocity.z)) {
            ship.cooldown = kDiveAgain;
            actor.subState = uint8_t(SquareGunshipSub::Fly);
            actor.enterStep(uint8_t(FlyStep::Init));
        }
        return;
    }
    default: return;
    }
}

// exe 0x59f9f0: the model (variant 1), the hit points of the variant-1 row * 8, the box, the home height, the class
// block cleared, the dive cooldown; the boss gauge (but for the bomber)
void squareGunshipInit(StaticActor& actor, SquareGunshipFields& ship) {
    if (!bindActorModel(actor, 1)) {
        freeActor(actor);
        return;
    }
    const int16_t hp = hasStaticStats(actor.type) ? int16_t(staticStats(actor.type, 1).hitPoints << 3) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    const int32_t y = actor.pos.y.whole;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.onPlatform = 0;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = 0;
    ship.rockTicks = 0;
    ship.rollTarget = 0;
    actor.collisionBox.pointAt(squareGunshipBox.stateAddress());
    actor.airborne = 1;
    ship.homeHeight = y;
    actor.clearClassBlock(); // exe 0x40dc40
    ship.cooldown = kDiveCooldown;
    actor.enterState(uint8_t(SquareGunshipState::Alive));
    if (actor.variant != kBomberVariant) hudAddBossGauge(actor, 2, 3, kBossGaugeColour); // exe 0x415d30
}

void sink(const PlayerEnv& env, StaticActor& actor, SquareGunshipFields& ship);

// exe 0x59fac0: the hits (a strong one rocks it; dead: the sink or the crash), the sub-state, the rocking, the sway,
// the attitude, the collision, the cooldowns
void squareGunshipAlive(const PlayerEnv& env, StaticActor& actor, SquareGunshipFields& ship) {
    const GunshipHit r = squareGunshipHits(actor, ship);
    if (r == GunshipHit::Strong) {
        if (!(ship.flight & kFlightNoRock) && !(ship.behaviour & kBehaveRocking)) {
            ship.behaviour |= kBehaveRocking;
            rockInit(actor);
        }
    } else if (r == GunshipHit::Dead) {
        if (actor.variant != 0 && actor.variant != kBomberVariant) {
            actor.enterState(uint8_t(SquareGunshipState::Sink));
            sink(env, actor, ship);
            return;
        }
        actor.enterState(uint8_t(SquareGunshipState::Crash));
        gunshipCrash(env, actor, 0x200, false);
        return;
    }
    switch (SquareGunshipSub(actor.subState)) {
    case SquareGunshipSub::FlyStart: flyStart(actor, ship); break;
    case SquareGunshipSub::Fly: fly(actor, ship); break;
    case SquareGunshipSub::Dive: dive(actor, ship); break;
    case SquareGunshipSub::Idle:
    case SquareGunshipSub::Idle4:
    case SquareGunshipSub::Idle5: break;
    default: notPortedAt(kStoryPort, "static 0x59f880 sub-state", actor.subState);
    }
    if (ship.behaviour & kBehaveRocking) {
        ship.flight |= kFlightRocking;
        if (rock(actor) != 0) {
            ship.behaviour &= uint8_t(~kBehaveRocking);
            ship.flight &= uint8_t(~kFlightRocking);
        }
    }
    if (ship.flight & kFlightSway) sway(actor);
    attitude(actor);
    squareGunshipCollide(env, actor, ship);
    if (ship.cooldown != 0) ship.cooldown = int16_t(ship.cooldown - 1);
    if (ship.bombCooldown != 0) ship.bombCooldown = int16_t(ship.bombCooldown - 1);
}

// exe 0x5a0230 (variant 1): shot down -> sinks to the home height and rocks; settled -> its story flag (ST18: 0x2b);
// smoke
void sink(const PlayerEnv& env, StaticActor& actor, SquareGunshipFields& ship) {
    if (SinkSub(actor.subState) == SinkSub::Start) {
        actor.subState = uint8_t(SinkSub::Sink);
        rockInit(actor);
    }
    holdAltitude(actor, ship);
    ship.flight |= kFlightRocking;
    if (rock(actor) != 0) {
        ship.flight &= uint8_t(~kFlightRocking);
        ship.behaviour &= uint8_t(~kBehaveRocking);
        setStoryFlag(ship.settleFlag);
    }
    if (ship.flight & kFlightSway) sway(actor);
    attitude(actor);
    squareGunshipCollide(env, actor, ship);
    if (!(ship.timer & 1)) gunshipSmoke(actor);
}

// exe 0x59f880
void squareGunship(const PlayerEnv& env, StaticActor& actor) {
    SquareGunshipFields& ship = classState<SquareGunshipFields>(actor);
    const uint32_t statRow = statTableRow(actorStatTables().statics, actor.type);
    ship.resetTintBytes();
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (SquareGunshipState(actor.state)) {
    case SquareGunshipState::Init: squareGunshipInit(actor, ship); break;
    case SquareGunshipState::Alive: squareGunshipAlive(env, actor, ship); break;
    case SquareGunshipState::Crash: gunshipCrash(env, actor, 0x200, false); break;
    case SquareGunshipState::Sink: sink(env, actor, ship); break;
    default: notPortedAt(kStoryPort, "static 0x59f880 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (ship.rockTicks != 0) ship.rockTicks -= 1u;
    const uint8_t flight = ship.flight;
    gunshipFireAsked(actor, 0x60, GunshipGun::Square);
    const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    ship.hitBox.result = 0;
    ship.hitBox.previousX = actor.prevPos.x.whole;
    ship.hitBox.previousZ = actor.prevPos.z.whole;
    const StaticActorStats row = staticStatsAt(statRow + 0x10); // the variant-1 row
    uint32_t shape = (flight & kFlightDown) ? kHitNoLockOn : 0u, attack;
    if (!(flight & kFlightProbeWalls)) {
        attack = row.damage | kHitAttackSolid;
        shape = hitOwner(actor.slot) | hitHeightLevel(row.hitHeight) | shape | hitGroup(kHitGroupBodies) | hitRadius(0xc8);
    } else { // diving: its body is a thrown attack
        attack = row.deathAttack | kHitAttackSolid | kHitAttackThrown;
        shape = hitOwner(actor.slot) | shape | hitGroup(kHitGroupBodies) | hitRadius(0xe8);
    }
    hitListSubmit(p, ship.hitBox, shape, attack); // exe 0x5e4120
    // exe 0x5f20f0(actor, 0xc0, 0x100, 0): the shadow list entry (render output)
}

} // namespace

void squareGunshipUpdate(const PlayerEnv& env, uint32_t actor) { squareGunship(env, staticActorAt(actor)); }

} // namespace od::port
