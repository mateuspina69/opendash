// The fire ruin's lava boss (ST28 sub 0's boss room; pool 0x20 type 0x48 sub 0, exe 0x4cd440; hypothesis names),
// spawned by the room handler 0x583080 (record 0x8c97b0, content/saul_kada/fire_ruin_rooms), which tracks its hit
// points and whether it is in the arena's box.
// Its tick: the previous pose, its tint (red while sinking), the state [0x8951fc] (0 bind 0x4cd560, 1 active 0x4cd660,
// 2 freed 0x468fa0), its animation (decoded only while it lies thrown), the stomp's spin (its pose's part 1 turned, a
// sound every 11 ticks), the pose. The shadow is render output.
// Active (0x4cd660): a hit of the player's -> flashed, its hit points (beaten: story flags 0x55 / 0x53, a defeat
// counted); else the damage it took adds up and past its row's threshold provokes it (unless a bomb is out); not lifted
// yet and the player lifts it -> lifted. Then its sub-state [0x895208] and its hit entries: five spheres on bones 1 / 0
// and one on bone 7 (its body's box).
// Sub-states: 0 start 0x4cda30, 1 approach 0x4cda40 (walks to the arena's point; provoked: an attack -- the shells far
// off or with a bomb thrown, else a bomb / two orbiting bombs by its hit points; its steps 1..5 a wall, a step up / down,
// a drop; at the point: story flags 0x55 / 0x54), 2 defeat 0x4cdfb0, 3 lifted 0x4ce150 ([0x895240]: carried 0x4ce170,
// thrown 0x4ce250, landed 0x4ce300 -- a lava cell sinks it, 2 hit points a tick), 4 get up 0x4ce530, 5 charge 0x4ce780
// (spinning, hopping onto the player, the stomp), 6 bomb 0x4cee00, 7 lob 0x4cf180 (seven shells), 8 orbiting bombs
// 0x4cf390.
// The arena's points (exe s16 tables 0x895230 / 0x895250 / 0x895268) are 16.16 points with only their high words
// written (the low words are stack garbage in the exe, 0 here; exe 0x5e3bc0 subtracts whole dwords, so the angle may
// differ in its last bit).
#include "content/saul_kada/st28_lava_boss.hpp"
#include "content/saul_kada/st28_lava_boss_state.hpp"

#include "content/saul_kada/st28_boss_shell.hpp"
#include "content/saul_kada/st28_fire_bomb.hpp"

#include "game/actor_animation.hpp"
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
#include "game/footstep_dust.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace st28_lava_boss_state;

namespace {

enum class BossState : uint8_t { Bind = 0, Active = 1, Free = 2 };
enum class BossSub : uint8_t {
    Start = 0, Approach = 1, Defeat = 2, Lifted = 3, GetUp = 4, Charge = 5, Bomb = 6, Lob = 7, OrbitingBombs = 8
};
enum class ApproachStep : uint8_t { Walk = 0, Blocked = 1, TurnBack = 2, StepUp = 3, StepOnto = 4, Drop = 5 };
enum class LiftedStep : uint8_t { Carried = 0, Thrown = 1, Landed = 2 };
enum class BossAnim : uint8_t {
    Idle = 0, Walk = 1, ChargeStart = 2, SpinStart = 3, Spin = 4, Throw = 5, AfterThrow = 6, ThrowPair = 7,
    AfterThrowPair = 8, Lob = 9, FallForward = 0xd, FallBack = 0xe, Down = 0xf, Stomp = 0x10, StompLand = 0x11,
    Bounce = 0x13, LandOnBack = 0x14, GetUp = 0x15, LiftedSpinning = 0x16, LiftedCharging = 0x17, LiftedUpright = 0x18
};
// pose (+0x167)
constexpr uint8_t kPoseUpright = 0x01, kPoseSpinning = 0x02, kPoseInLava = 0x08, kPoseBombThrown = 0x10,
                  kPoseAfterStomp = 0x20;
// status (+0x169)
constexpr uint8_t kDecodeOnly = 0x01, kAirborne = 0x02, kLiftedOnce = 0x04, kSinking = 0x08, kSpinning = 0x10,
                  kProvoked = 0x20, kReflected = 0x40, kChargeUsed = 0x80;
// the fall's result (+0x16a)
constexpr uint8_t kFellOnWall = 0x01, kFellOffLedge = 0x02, kFellLanded = 0x04;
constexpr uint32_t kFlagFightOver = 0x55, kFlagBossBeaten = 0x53, kFlagBossReachedEnd = 0x54;
constexpr uint16_t kHurtSound = 0x8d, kLiftedSound = 0x22d, kSpinStopSound = 0x22a, kThudSound = 0x227,
                   kLavaSound = 0x228, kSplashSound = 0x22c, kChargeSound = 0x223, kSpinSound = 0x229, kLobSound = 0x226;
constexpr uint8_t kTintRedNormal = 0x10, kTintRedSinking = 0x1f, kTintBlueGreen = 0x42;
constexpr size_t kHeldBombParts = 2; // its parts 2 / 3 (and 4 / 5 for the pair): the bombs in its hands
constexpr uint8_t kDustVariant = 1;

void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
void setAnim(AnimatedActor& actor, BossAnim anim) { actor.anim = uint8_t(anim); }
int ground(const PlayerEnv& env, const AnimatedActor& actor) { return actorGround(env, actor); } // exe 0x40f190
int playerDistance(const AnimatedActor& actor) { return planarDistance(player().pos, actor.pos); }
int angleToPlayer(const AnimatedActor& actor) { return angleOf(player().pos, actor.pos); }
// a sub-state's first step (the exe's byte stores at +9 / +0xa / +0xb)
void enterSubBytes(AnimatedActor& actor, BossSub sub) {
    actor.subState = uint8_t(sub);
    actor.step = 0;
    actor.stepCounter = 0;
}
void showPart(AnimatedActor& actor, size_t part, bool shown) {
    if (shown) actor.parts[part].flags &= uint8_t(~kPartHidden);
    else actor.parts[part].flags |= kPartHidden;
}
// the arena point of a table entry {x, y, z} as a 16.16 point (the low words 0)
void tablePoint(uint32_t table, uint32_t index, int32_t out[3]) {
    const ShortVec3 entry = dataShortVec3(table + index * 8);
    out[0] = int32_t(uint32_t(uint16_t(entry.x)) << 16);
    out[1] = int32_t(uint32_t(uint16_t(entry.y)) << 16);
    out[2] = int32_t(uint32_t(uint16_t(entry.z)) << 16);
}
int bearingFrom(const AnimatedActor& actor, const int32_t point[3]) {
    return angleOf(point[0], point[2], actor.pos.x, actor.pos.z);
}
// the lava test of 0x4ce300 / 0x4ce780: the floor cell under it has the floor bit and a ground kind of 5..8
bool onLava(const AnimatedActor& actor, RoutineId fn) {
    const uint32_t c = gridCellUnder(actor.pos); // exe 0x40f030
    if (!c) notPortedAt(kStoryPort, "exe 0x40f030 returned null (the exe reads [0]), in", routineStateValue(fn));
    const uint16_t word = GridCell(c).word;
    if (!(word & kCellFloor)) return false;
    const int kind = word & 0x1f;
    return kind >= 5 && kind <= 8;
}
// its position kept across a probing move
struct SavedPosition {
    int32_t x, y, z;
};
SavedPosition savePosition(const AnimatedActor& actor) { return {actor.pos.x, actor.pos.y, actor.pos.z}; }
void restorePosition(AnimatedActor& actor, const SavedPosition& p) {
    actor.pos.x = p.x;
    actor.pos.y = p.y;
    actor.pos.z = p.z;
}
// the fall's speed: capped at 0x200, else accelerating
void fallSpeed(AnimatedActor& actor) {
    if (actor.velocity.y > 0x200) actor.velocity.y = 0x200;
    else actor.velocity.y = int16_t(uint16_t(uint16_t(actor.acceleration.y) + uint16_t(actor.velocity.y)));
}
void footDust(const AnimatedActor& actor, int groundY) { // exe 0x5ad170
    const int16_t point[3] = {actor.pos.x.whole, int16_t(groundY), actor.pos.z.whole};
    footstepDustAt(actor, kDustVariant, point);
}
void bonePoint(const AnimatedActor& actor, uint32_t bone, int16_t x, int16_t y, int16_t z, int16_t out[3]) {
    const int16_t v[3] = {x, y, z};
    bonePointOffset(actor, bone, v, out); // exe 0x5e1970
}

// ---- its movement --------------------------------------------------------------------------------------------------
void setGround(const AnimatedActor& actor, BossFields& boss) {
    boss.groundBefore = uint16_t(actor.pos.y.whole);
    boss.groundNow = uint16_t(actor.pos.y.whole);
}
// exe 0x4cf7b0: the walk -- its height follows the ground, bobbing by the phase's sine >> 6
void walkBob(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    const uint16_t y0 = uint16_t(actor.pos.y.whole);
    actor.pos.y.whole = int16_t(boss.groundNow);
    const uint16_t g = uint16_t(ground(env, actor) - 0xc0);
    boss.groundNow = g;
    actor.pos.y.whole = int16_t(uint16_t(uint16_t(g - boss.groundBefore) + y0));
    boss.walkPhase += 0xccu;
    boss.groundBefore = boss.groundNow;
    if (int16_t(boss.walkPhase) > 0xfff) {
        boss.walkPhase = 0;
        actor.pos.y.whole = int16_t(boss.groundNow);
    }
    boss.walkPhase &= 0xfffu;
    integrateVelocity(actor, 0, fixedSin(boss.walkPhase & 0xfff) >> 6, 0); // exe 0x5e3690
}
// exe 0x4cf700: the move resolved; its fall's result
int32_t resolveFall(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    int32_t r = 0;
    if (int32_t(actorTestMove(env, actor)) < 0) { // exe 0x40f570
        r = int32_t(actorPushOut(env, actor));     // exe 0x410960
        if (r < 0) actor.pos = actor.prevPos;
        if (r & kWallOnSides) boss.fallResult |= kFellOnWall;
    }
    const int g = ground(env, actor);
    if (!(boss.status & kAirborne)) {
        if (g - actor.pos.y.whole > 0x20) {
            boss.fallResult |= kFellOffLedge;
            return r;
        }
        actor.pos.y.whole = int16_t(g);
        return r;
    }
    if (r & kWallOnY) {
        actor.airborne = 0;
        actor.pos.y.whole = int16_t(g);
        boss.fallResult |= kFellLanded;
    }
    return r;
}
// the yaw reflected off the wall kept in the timer (its yaw unchanged)
void wallTurn(AnimatedActor& actor, BossFields& boss, int32_t r) {
    boss.timer = uint16_t(actor.rotation.y);
    reflectRotationOffWalls(actor.rotation, uint32_t(r)); // exe 0x410740
    const int16_t reflected = actor.rotation.y;
    actor.rotation.y = int16_t(boss.timer);
    boss.timer = uint16_t(reflected);
}
// back to the start sub-state
void backToStart(AnimatedActor& actor, BossFields& boss) {
    boss.walkPhase = 0;
    enterSubBytes(actor, BossSub::Start);
    setGround(actor, boss);
}

// exe 0x4cd560: its model, the hit points of its row x 32, its boxes, on the ground, the boss gauge
void bind(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    setAnim(actor, BossAnim::Idle);
    actor.prevAnim = 0xff;
    const uint16_t hp = hasAnimatedStats(actor.type)
                            ? uint16_t(uint32_t(animatedStats(actor.type, actor.subtype).hitPoints) << 5)
                            : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::St28LavaBossStandingBox);
    actor.interactionBox.pointAt(TableId::St28LavaBossLiftBox);
    actor.interaction = kInteractLift;
    actor.velocity.setXY(0, 0);
    actor.velocity.setZSpare(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    boss.bombOut = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    boss.hitBox.result = 0;
    boss.bodyBox.result = 0;
    boss.status = 0;
    boss.inBox = 0;
    boss.pose = kPoseUpright;
    boss.damageTaken = 0;
    const uint16_t g = uint16_t(ground(env, actor) - 0xc0);
    actor.pos.y.whole = int16_t(g);
    boss.groundBefore = g;
    boss.groundNow = g;
    actor.enterState(uint8_t(BossState::Active));
    boss.tintRed = kTintRedNormal;
    hudAddBossGauge(actor, 0, 4, 0x22aaaa); // exe 0x415d30
}

// ---- the approach (exe 0x4cda40) -----------------------------------------------------------------------------------
// walking to the arena's point, or (provoked) picking an attack; its steps 1..5 a wall, a step up / down, a drop
void approach(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    const uint8_t in = pointInCellBox(actor.pos, CellBox(stateAddress(TableId::St28LavaBossArenaBox))) ? 1 : 0;
    boss.inBox = in;
    boss.inBoxWord = in;
    bool provokable = true;
    switch (ApproachStep(actor.step)) {
    case ApproachStep::Walk:
        if (boss.status & kProvoked) {
            if (ground(env, actor) - actor.pos.y.whole > 0x100) {
                actor.step = uint8_t(ApproachStep::Drop);
                actor.velocity.y = 0;
                actor.acceleration.y = 0x18;
                break;
            }
            if ((playerDistance(actor) >= 0xc00 || (boss.pose & kPoseBombThrown)) && !(boss.status & kChargeUsed)) {
                enterSubBytes(actor, BossSub::Lob);
                boss.status |= kChargeUsed;
                break;
            }
            enterSubBytes(actor, actor.hitPoints < actor.maxHitPoints / 2 ? BossSub::OrbitingBombs : BossSub::Bomb);
        } else {
            int32_t point[3];
            tablePoint(stateAddress(TableId::St28LavaBossArenaPoints), boss.inBox, point);
            turnYawTo(actor.rotation.y, uint32_t(bearingFrom(actor, point)), 0x20);
            if ((boss.status & kReflected) && facingWithin(actor, point[0], point[2], 0x80))
                boss.status &= uint8_t(~kReflected);
            if (facingWithin(actor, point[0], point[2], 0x80) || (boss.status & kReflected)) {
                integrateVelocity(actor, 0, 0, -0x100);
                const int32_t r = int32_t(actorPushOut(env, actor));
                if (r & kWallOnSides) {
                    wallTurn(actor, boss, r);
                    ++actor.step;
                    provokable = false;
                    break;
                }
                if (ground(env, actor) - actor.pos.y.whole > 0x100) {
                    actor.step = uint8_t(ApproachStep::Drop);
                    actor.velocity.y = 0;
                    actor.acceleration.y = 0x18;
                } else {
                    walkBob(env, actor, boss);
                }
            }
        }
        break;
    case ApproachStep::Blocked: {
        provokable = false;
        const SavedPosition saved = savePosition(actor);
        integrateVelocity(actor, 0, 0, -0x100);
        actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + 0xfe00u);
        const int32_t r = int32_t(actorPushOut(env, actor));
        if (!(r & (kWallOnSides | kWallOnY))) {
            actor.step = uint8_t(ApproachStep::StepUp);
            actor.velocity.y = 0;
            actor.acceleration.y = 0x18;
        } else {
            ++actor.step;
        }
        restorePosition(actor, saved);
        break;
    }
    case ApproachStep::TurnBack:
        provokable = false;
        if (turnYawTo(actor.rotation.y, uint32_t(int32_t(int16_t(boss.timer))), 0x20) == 0) {
            actor.step = uint8_t(ApproachStep::Walk);
            boss.status |= kReflected;
        }
        break;
    case ApproachStep::StepUp: {
        provokable = false;
        fallSpeed(actor);
        integrateVelocity(actor, 0, -actor.velocity.y, 0);
        const SavedPosition saved = savePosition(actor);
        integrateVelocity(actor, 0, 0, -0x100);
        const int32_t r = int32_t(actorPushOut(env, actor));
        restorePosition(actor, saved);
        if (r & (kWallOnSides | kWallOnY)) break;
        ++actor.step;
        setGround(actor, boss);
        break;
    }
    case ApproachStep::StepOnto: {
        provokable = false;
        const SavedPosition saved = savePosition(actor);
        integrateVelocity(actor, 0, 0, -0x100);
        const int g = ground(env, actor);
        const int gy = g - 0xc0;
        restorePosition(actor, saved);
        if (actor.pos.y.whole > gy) {
            integrateVelocity(actor, 0, -actor.velocity.y, 0);
            if (actor.pos.y.whole > gy) break;
        } else {
            integrateVelocity(actor, 0, actor.velocity.y, 0);
            if (actor.pos.y.whole < gy) break;
        }
        actor.pos.y.whole = int16_t(gy);
        actor.step = uint8_t(ApproachStep::Walk);
        setGround(actor, boss);
        break;
    }
    case ApproachStep::Drop: {
        provokable = false;
        fallSpeed(actor);
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        actorPushOut(env, actor);
        const int gy = ground(env, actor) - 0xc0;
        if (actor.pos.y.whole <= gy) break;
        actor.pos.y.whole = int16_t(gy);
        actor.step = uint8_t(ApproachStep::Walk);
        setGround(actor, boss);
        break;
    }
    default: provokable = false; break;
    }
    if (provokable && facingWithin(actor, player().pos, 0x80) && playerDistance(actor) < 0x1000 && boss.bombOut == 0)
        boss.status |= kProvoked; // 0x4cdc7d
    if (boss.inBox == 0) return;
    int32_t point[3];
    tablePoint(stateAddress(TableId::St28LavaBossArenaPoints), boss.inBox, point);
    if (planarDistance(actor.pos, point) >= 0x100) return;
    setStoryFlag(kFlagFightOver);
    setStoryFlag(kFlagBossReachedEnd);
}

// exe 0x4cdfb0: the defeat -- falling forward (upright) or back then down, then a blast at bone 1 and freed; a death
// explosion at a random bone every 4th tick
void defeat(AnimatedActor& actor, BossFields& boss) {
    switch (actor.step) {
    case 0:
        if (boss.pose & kPoseUpright) {
            setAnim(actor, BossAnim::FallForward);
            actor.step = 2;
            boss.timer = 0x1e;
        } else {
            setAnim(actor, BossAnim::FallBack);
            actor.step = 1;
        }
        return;
    case 1:
        if (!actor.animEnded()) return;
        setAnim(actor, BossAnim::Down);
        actor.step = 2;
        boss.timer = 0x1e;
        return;
    case 2: break;
    default: return;
    }
    if (actor.animEnded() && boss.timer != 0) {
        boss.timer -= 1u;
        int16_t p[3];
        bonePoint(actor, 1, 0, 0, 0, p);
        spawnBlastAt(p[0], p[1], p[2], 0x80, 0);
        actor.state = uint8_t(BossState::Free);
    }
    if (gameState().frame & 3) return;
    const uint32_t n = rngStep();
    int16_t p[3];
    bonePoint(actor, n & 7, 0, 0, 0, p);
    spawnDeathExplosionAt(p[0], p[1], p[2], 0x60, 0x28);
}

// ---- lifted (exe 0x4ce150, [0x895240]) -----------------------------------------------------------------------------
void thrown(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss);
// exe 0x4ce170: in the player's hands until thrown (along his aim) or dropped
void carried(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    carryAtHands(actor, 0, actor.interactionBox.value() != stateAddress(TableId::St28LavaBossLiftBox) ? -0x40 : 0, 0); // exe 0x573ab0
    const CarryRelease release = carryReleased();                                   // exe 0x573950
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
    boss.status = uint8_t((boss.status & ~(kChargeUsed | kAirborne)) | kAirborne);
    actor.airborne = 1;
    thrown(env, actor, boss);
}
// exe 0x4ce250: flying; landing -> the bounce (its pose: on its back after a spinning lift)
void thrown(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    const int32_t r = resolveFall(env, actor, boss);
    if (boss.fallResult & kFellOnWall) reflectRotationOffWalls(actor.rotation, uint32_t(r));
    if (!(boss.fallResult & kFellLanded)) return;
    actor.stepCounter = 0;
    actor.velocity.y = int16_t(0xfc00);
    ++actor.step;
    actor.acceleration.y = 0x80;
    sound(kThudSound, actor);
    if (!(boss.pose & kPoseSpinning)) {
        setAnim(actor, BossAnim::Bounce);
        return;
    }
    boss.pose = kPoseUpright;
    setAnim(actor, BossAnim::LandOnBack);
    showPart(actor, kHeldBombParts, true);
}
// the landing's way out to the get-up (a bomb out noted)
void toGetUp(AnimatedActor& actor, BossFields& boss) {
    actor.interactionBox.pointAt(TableId::St28LavaBossLiftBox);
    actor.subState = uint8_t(BossSub::GetUp);
    actor.step = 1;
    actor.stepCounter = 0;
    boss.bombOutAtGetUp = boss.bombOut != 0 ? 1 : 0;
    setAnim(actor, BossAnim::GetUp);
}
// sinking in lava: 2 a tick and 2 hit points a tick (beaten: the story flags)
bool sinkAndBurn(AnimatedActor& actor, int g) {
    if (!(gameState().frame & 7)) footDust(actor, g);
    return hitPointsAdd(actor, -2) != 0; // exe 0x5e52f0
}
// exe 0x4ce300: one bounce, then landed (on lava: sinking); -> the get-up
void landed(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    if (actor.stepCounter <= 1) {
        actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        const int32_t r = resolveFall(env, actor, boss);
        if (boss.fallResult & kFellOnWall) reflectRotationOffWalls(actor.rotation, uint32_t(r));
        if (!(boss.fallResult & kFellLanded)) return;
        if (actor.stepCounter != 0) {
            ++actor.stepCounter;
            boss.status &= uint8_t(~kAirborne);
            uint16_t landing = kThudSound;
            if (onLava(actor, RoutineId::St28LavaBossLanding)) {
                boss.timer = 0x1e;
                boss.pose |= kPoseInLava;
                landing = kLavaSound;
            }
            sound(landing, actor);
            sound(kSplashSound, actor);
            return;
        }
        actor.stepCounter = 1;
        actor.velocity.y = int16_t(-(actor.velocity.y / 2));
        sound(kThudSound, actor);
        return;
    }
    if (boss.pose & kPoseInLava) {
        const int g = ground(env, actor);
        if (g + 0x40 >= actor.pos.y.whole) {
            boss.status |= kSinking;
            actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + 2u);
        } else if (boss.timer != 0) {
            boss.timer -= 1u;
        } else {
            toGetUp(actor, boss);
        }
        if (!sinkAndBurn(actor, g)) return;
        setStoryFlag(kFlagFightOver);
        setStoryFlag(kFlagBossBeaten);
        return;
    }
    if (!actor.animEnded()) return;
    toGetUp(actor, boss);
}

// exe 0x4ce530: getting up (out of lava: higher), then an attack (a bomb out: the shells far off, else back to the
// start)
void getUp(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    switch (actor.step) {
    case 0:
        setAnim(actor, BossAnim::GetUp);
        actor.step = 1;
        return;
    case 1:
        if (int8_t(actor.keyIndex) <= 2) return;
        if (boss.pose & kPoseInLava) {
            actor.velocity.y = 0x145;
            actor.acceleration.y = 0x17;
        } else {
            actor.velocity.y = 0x98;
            actor.acceleration.y = 8;
        }
        actor.step = 2;
        return;
    case 2: {
        actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) - uint16_t(actor.acceleration.y)));
        integrateVelocity(actor, 0, -actor.velocity.y, 0);
        const SavedPosition saved = savePosition(actor);
        actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + 0xfe00u);
        const int16_t g = int16_t(ground(env, actor));
        restorePosition(actor, saved);
        if (actor.pos.y.whole >= int(g) - 0x3b) return;
        actor.acceleration.y = 0x40;
        actor.velocity.y = 0x200;
        actor.step = 3;
        return;
    }
    case 3:
        if (!actor.animEnded()) return;
        boss.pose &= uint8_t(~kPoseInLava);
        boss.status &= uint8_t(~kSinking);
        actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + 0xff7bu);
        setAnim(actor, BossAnim::Walk);
        actor.collisionBox.pointAt(TableId::St28LavaBossStandingBox);
        actor.pos.y.whole = int16_t(ground(env, actor) - 0xc0);
        boss.pose = kPoseUpright;
        actor.step = 4;
        return;
    case 4:
        if (!actor.animEnded()) return;
        if (boss.bombOutAtGetUp != 0) {
            if (playerDistance(actor) > 0x500) {
                boss.walkPhase = 0;
                boss.status &= uint8_t(~kLiftedOnce);
                actor.subState = uint8_t(BossSub::Lob);
            } else {
                boss.status &= uint8_t(~(kLiftedOnce | kProvoked));
                setAnim(actor, BossAnim::Idle);
                boss.walkPhase = 0;
                actor.subState = uint8_t(BossSub::Start);
            }
            actor.step = 0;
            actor.stepCounter = 0;
            setGround(actor, boss);
            return;
        }
        boss.status &= uint8_t(~kLiftedOnce);
        setGround(actor, boss);
        actor.step = 0;
        actor.stepCounter = 0;
        if (actor.maxHitPoints / 2 >= actor.hitPoints) {
            actor.subState = uint8_t(BossSub::OrbitingBombs);
            return;
        }
        actor.subState = uint8_t(BossSub::Bomb);
        boss.pose |= kPoseAfterStomp;
        return;
    default: return;
    }
}

// the spin's count of the charge's steps 3 / 4: the timer 0..9, the phase the spin's angle
void spinCount(BossFields& boss) {
    uint16_t c = boss.timer;
    if (c == 9) {
        c = 0;
        boss.walkPhase = 0;
    } else {
        if (c == 4) boss.walkPhase = 0x800;
        else boss.walkPhase += 0xfe67u;
        c = uint16_t(c + 1);
    }
    boss.timer = c;
}
// its yaw and its travel yaw swapped
void swapTravelYaw(AnimatedActor& actor, BossFields& boss) {
    const int16_t yaw = actor.rotation.y;
    actor.rotation.y = boss.travelYaw;
    boss.travelYaw = yaw;
}
// exe 0x4ce780: the charge -- spinning, hopping onto the player (its count), the stomp; lava sinks it
void charge(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    const uint8_t step = actor.step;
    switch (step) {
    case 0:
        turnYawTo(actor.rotation.y, uint32_t(angleToPlayer(actor)), 0x60);
        if (facingWithin(actor, player().pos, 0x80) && uint16_t(actor.pos.y.whole) == boss.groundNow) {
            setAnim(actor, BossAnim::ChargeStart);
            actor.step = 1;
            boss.pose = kPoseSpinning;
            return;
        }
        walkBob(env, actor, boss);
        return;
    case 1:
    case 2:
        if (!actor.animEnded()) return;
        if (step == 2) {
            setAnim(actor, BossAnim::Spin);
            actor.interactionBox.pointAt(TableId::St28LavaBossSpinningLiftBox);
            boss.status |= kSpinning;
            actor.stepCounter = 0;
            actor.step = 3;
            boss.walkPhase = 0x667;
            boss.timer = 5;
            actor.velocity.z = 0;
            actor.acceleration.z = int16_t(0xff80);
            return;
        }
        setAnim(actor, BossAnim::SpinStart);
        sound(kChargeSound, actor);
        boss.spinSoundTimer = 0xc;
        actor.step = 2;
        return;
    case 3:
        spinCount(boss);
        if (facingWithin(actor, player().pos, 0x200) && actor.stepCounter == 0) {
            if (playerDistance(actor) > 0x400) turnYawTo(actor.rotation.y, uint32_t(angleToPlayer(actor)), 0x20);
            if (actor.velocity.z < -0x400) actor.velocity.z = int16_t(0xfc00);
            else actor.velocity.z = int16_t(uint16_t(uint16_t(actor.acceleration.z) + uint16_t(actor.velocity.z)));
        } else {
            const uint16_t v = uint16_t(actor.velocity.z);
            if (int16_t(v) > 0) {
                actor.step = 4;
                return;
            }
            const int16_t yaw = actor.rotation.y;
            if (actor.stepCounter == 0) {
                boss.travelYaw = yaw;
                actor.acceleration.z = 0x14;
                actor.stepCounter = 1;
            } else {
                actor.rotation.y = boss.travelYaw;
                boss.travelYaw = yaw;
            }
            actor.velocity.z = int16_t(uint16_t(uint16_t(actor.acceleration.z) + v));
            turnYawTo(actor.rotation.y, uint32_t(angleToPlayer(actor)), 0x10);
            const int16_t kept = actor.rotation.y;
            actor.rotation.y = boss.travelYaw;
            turnYawTo(actor.rotation.y, uint32_t(angleToPlayer(actor)), 0x40);
            boss.travelYaw = actor.rotation.y;
            actor.rotation.y = kept;
        }
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        if (actor.stepCounter != 0) swapTravelYaw(actor, boss);
        actorPushOut(env, actor);
        if (actor.velocity.z == 0) ++actor.step;
        actor.pos.y.whole = int16_t(ground(env, actor) - 0xc0);
        return;
    case 4:
        if (boss.count == 0) {
            setAnim(actor, BossAnim::Stomp);
            actor.step = 5;
            actor.stepCounter = 0;
            if (!(boss.status & kSpinning)) return;
            boss.status &= uint8_t(~kSpinning);
            sound(kSpinStopSound, actor);
            return;
        }
        spinCount(boss);
        turnYawTo(actor.rotation.y, uint32_t(angleToPlayer(actor)), 0x60);
        if (facingWithin(actor, player().pos, 0x80)) {
            actor.step = 3;
            actor.stepCounter = 0;
            actor.velocity.z = 0;
            actor.acceleration.z = int16_t(0xff80);
            boss.count -= 1u;
        }
        return;
    case 5:
        if (!actor.animEnded()) return;
        boss.pose = kPoseUpright;
        setAnim(actor, BossAnim::StompLand);
        actor.interactionBox.pointAt(TableId::St28LavaBossLiftBox);
        actor.step = 6;
        actor.velocity.y = 0;
        actor.acceleration.y = 0x10;
        boss.status |= kAirborne;
        actor.airborne = 1;
        return;
    case 6:
        actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        resolveFall(env, actor, boss);
        if (!(boss.fallResult & kFellLanded)) return;
        if (actor.stepCounter == 0) {
            actor.velocity.y = int16_t(-(actor.velocity.y / 2));
            actor.stepCounter = 1;
            setAnim(actor, BossAnim::Bounce);
            actor.pos.y.whole = int16_t(ground(env, actor) + 0x85);
            actor.collisionBox.pointAt(TableId::St28LavaBossLyingBox);
            return;
        }
        boss.status &= uint8_t(~kAirborne);
        actor.step = 7;
        if (onLava(actor, RoutineId::St28LavaBossFall)) boss.pose |= kPoseInLava;
        return;
    case 7:
        if (boss.pose & kPoseInLava) {
            const int g = ground(env, actor);
            if (g + 0x40 >= actor.pos.y.whole) {
                boss.status |= kSinking;
                if (!(gameState().frame & 7)) footDust(actor, g);
                actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + 2u);
                if (hitPointsAdd(actor, -2) == 0) return; // exe 0x5e52f0
                enterSubBytes(actor, BossSub::Defeat);
                return;
            }
        } else if (!actor.animEnded()) {
            return;
        }
        boss.timer = 0x1e;
        actor.step = 8;
        return;
    case 8:
        if (boss.pose & kPoseInLava) {
            const int g = ground(env, actor);
            if (g + 0x40 >= actor.pos.y.whole) {
                actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + 2u);
            } else if (boss.timer == 0) {
                actor.subState = uint8_t(BossSub::GetUp);
                actor.step = 0;
                actor.stepCounter = 0;
                boss.bombOutAtGetUp = 1;
            } else {
                boss.timer -= 1u;
            }
            if (!sinkAndBurn(actor, g)) return;
            actor.subState = uint8_t(BossSub::Defeat);
            actor.step = 0;
            return;
        }
        if (boss.timer == 0) {
            actor.subState = uint8_t(BossSub::GetUp);
            actor.step = 0;
            actor.stepCounter = 0;
            boss.bombOutAtGetUp = 1;
            return;
        }
        boss.timer -= 1u;
        return;
    default: return;
    }
}

// the bombs' walk back (step 5 of 0x4cee00 / 0x4cf390): to the box's point until the bomb is gone
void bombWalk(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss, uint32_t box, uint32_t points) {
    if (actor.animEnded()) boss.pose |= kPoseBombThrown;
    if (boss.bombOut == 0) {
        ++actor.step;
        boss.status &= uint8_t(~kProvoked);
        return;
    }
    const uint8_t in = pointInCellBox(actor.pos, CellBox(box)) ? 1 : 0;
    boss.inBox = in;
    boss.inBoxWord = in;
    int32_t point[3];
    tablePoint(points, in, point);
    turnYawTo(actor.rotation.y, uint32_t(bearingFrom(actor, point)), 0x20);
    integrateVelocity(actor, 0, 0, -0x80);
    const int32_t r = int32_t(actorPushOut(env, actor));
    if (r & kWallOnSides) {
        wallTurn(actor, boss, r);
        ++actor.step;
        return;
    }
    if (ground(env, actor) - actor.pos.y.whole > 0x100) {
        ++actor.step;
        return;
    }
    walkBob(env, actor, boss);
}
// the held bombs' parts shown / hidden (the pair: four parts)
void showHeldBombs(AnimatedActor& actor, bool pair, bool shown) {
    showPart(actor, kHeldBombParts, shown);
    showPart(actor, kHeldBombParts + 1, shown);
    if (pair) {
        showPart(actor, kHeldBombParts + 2, shown);
        showPart(actor, kHeldBombParts + 3, shown);
    }
}
// exe 0x4cee00 (a bomb) / 0x4cf390 (the orbiting pair): turned to the player, the throw (the bomb at its key 12), then
// waiting / walking back until the bomb is gone
void bombAttack(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss, bool pair) {
    const uint8_t step = actor.step;
    switch (step) {
    case 0:
        turnYawTo(actor.rotation.y, uint32_t(angleToPlayer(actor)), 0x60);
        if (facingWithin(actor, player().pos, 0x80)) {
            setAnim(actor, pair ? BossAnim::ThrowPair : BossAnim::Throw);
            actor.step = 1;
            boss.timer = 0x14;
            return;
        }
        walkBob(env, actor, boss);
        return;
    case 1:
        if (actor.animEnded()) {
            if (boss.timer != 0) {
                boss.timer -= 1u;
                return;
            }
            setAnim(actor, pair ? BossAnim::AfterThrowPair : BossAnim::AfterThrow);
            actor.step = 2;
            return;
        }
        if (int8_t(actor.keyIndex) != 0xc) return;
        {
            int16_t p[3];
            bonePoint(actor, 0, -0x18, 0x5d, -0x9f, p);
            spawnFireBomb(actor, p, pair ? FireBombVariant::Centre : FireBombVariant::Homing); // exe 0x5c9160
        }
        boss.bombOut = 1;
        showHeldBombs(actor, pair, false);
        return;
    case 2:
        if (!actor.animEnded()) return;
        setAnim(actor, BossAnim::AfterThrow);
        actor.step = 3;
        return;
    case 3:
    case 4:
        if (!actor.animEnded()) return;
        if (step == 3) {
            if (!pair && (boss.pose & kPoseAfterStomp)) {
                boss.walkPhase = 0;
                enterSubBytes(actor, BossSub::Charge);
                setGround(actor, boss);
                boss.count = 3;
                showPart(actor, kHeldBombParts, true);
                showPart(actor, kHeldBombParts + 1, true);
                return;
            }
            setAnim(actor, BossAnim::ChargeStart);
        }
        actor.step = uint8_t(step + 1u);
        return;
    case 5:
        if (pair) bombWalk(env, actor, boss, stateAddress(TableId::St28LavaBossPairBox), stateAddress(TableId::St28LavaBossPairPoints));
        else bombWalk(env, actor, boss, stateAddress(TableId::St28LavaBossBombBox), stateAddress(TableId::St28LavaBossBombPoints));
        return;
    case 6:
        setAnim(actor, BossAnim::Walk);
        boss.pose &= uint8_t(~kPoseBombThrown);
        showHeldBombs(actor, pair, true);
        actor.step = 7;
        return;
    case 7:
        if (!actor.animEnded()) return;
        setAnim(actor, BossAnim::Idle);
        backToStart(actor, boss);
        return;
    default: return;
    }
}

// exe 0x4cf180: seven shells, then back to the start
void lob(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    switch (actor.step) {
    case 0:
        if (ground(env, actor) - actor.pos.y.whole > 0x100) {
            actor.step = 4;
            actor.velocity.y = 0;
            actor.acceleration.y = 0x18;
            return;
        }
        if (facingWithin(actor, player().pos, 0x80)) {
            actor.step = 1;
            boss.count = 7;
            boss.status &= uint8_t(~kChargeUsed);
            walkBob(env, actor, boss);
            return;
        }
        turnYawTo(actor.rotation.y, uint32_t(angleToPlayer(actor)), 0x60);
        walkBob(env, actor, boss);
        return;
    case 1:
        setAnim(actor, BossAnim::Lob);
        sound(kLobSound, actor);
        actor.step = 2;
        return;
    case 2:
        if (!actor.animEnded()) return;
        if (boss.count != 0) {
            lobBossShell(actor, playerDistance(actor) <= 0xc00); // exe 0x5ca900
            boss.count -= 1u;
            actor.step = 1;
            return;
        }
        setAnim(actor, BossAnim::Idle);
        actor.step = 3;
        boss.timer = 0x1e;
        boss.status &= uint8_t(~kProvoked);
        return;
    case 3:
        if (boss.timer != 0) {
            boss.timer -= 1u;
            return;
        }
        backToStart(actor, boss);
        return;
    case 4: {
        fallSpeed(actor);
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        actorPushOut(env, actor);
        const int gy = ground(env, actor) - 0xc0;
        if (gy < actor.pos.y.whole) {
            actor.step = 0;
            actor.pos.y.whole = int16_t(gy);
            setGround(actor, boss);
        }
        return;
    }
    default: return;
    }
}

// its hit entries: five spheres on bones 1 / 0 (the hit box), one on bone 7 (its body's)
void submitHits(const AnimatedActor& actor, BossFields& boss, const AnimatedActorStats& stats) {
    const uint32_t damage = (boss.status & kLiftedOnce) ? 0u : uint32_t(stats.attack);
    const uint8_t pose = boss.pose;
    boss.bodyBox.previousX = actor.prevPos.x.whole;
    boss.hitBox.previousX = actor.prevPos.x.whole;
    boss.bodyBox.previousZ = actor.prevPos.z.whole;
    boss.hitBox.previousZ = actor.prevPos.z.whole;
    boss.bodyBox.result = 0;
    boss.hitBox.result = 0;
    const uint32_t attack = damage | ((pose & kPoseBombThrown) ? kHitAttackDeflects : kHitAttackSolid);
    const uint32_t owner = hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight) | hitGroup(kHitGroupBodies);
    struct Sphere {
        uint32_t bone;
        int16_t x, y, z;
        uint32_t shape;
    };
    static constexpr Sphere kSpheres[5] = {{1, 0, 0x20, -0xe0, hitRadius(0x64)},
                                           {0, 0x40, -0x40, 0, kHitNoLockOn | hitRadius(0x3c)},
                                           {0, -0x40, -0x40, 0, kHitNoLockOn | hitRadius(0x3c)},
                                           {0, 0, -0x40, 0x40, kHitNoLockOn | hitRadius(0x3c)},
                                           {0, 0, -0x40, -0x58, kHitNoLockOn | hitRadius(0x64)}};
    int16_t p[3];
    for (const Sphere& s : kSpheres) {
        bonePoint(actor, s.bone, s.x, s.y, s.z, p);
        hitListSubmit(p, boss.hitBox, owner | s.shape, attack); // exe 0x5e4120
    }
    bonePoint(actor, 7, 0, -0x40, 0, p);
    hitListSubmit(p, boss.bodyBox, owner | kHitNoLockOn | hitRadius(0xa0), damage | kHitAttackDeflects);
}
// exe 0x4cd660
void active(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    const AnimatedActorStats stats = animatedStats(actor.type, actor.subtype);
    boss.fallResult = 0;
    const uint32_t hit = boss.hitBox.result;
    if (hit & kHitAttackByPlayer) {
        actor.tint = kTintFlash;
        const uint16_t hp0 = uint16_t(actor.hitPoints);
        if (hitPointsAdd(actor, -int(hit & kHitDamageMask))) { // exe 0x5e52f0
            setStoryFlag(kFlagFightOver);
            setStoryFlag(kFlagBossBeaten);
            countDefeat();
        } else {
            boss.damageTaken += uint16_t(hp0 - uint16_t(actor.hitPoints));
            if (int16_t(boss.damageTaken) > int(stats.bodyAttack) && !(boss.status & kProvoked)) {
                boss.damageTaken = 0;
                if (boss.bombOut == 0) boss.status |= kProvoked;
            }
        }
        sound(kHurtSound, actor);
    } else if (!(boss.status & kLiftedOnce) && playerLiftTargetCheck(actor, 0)) { // exe 0x573a60
        boss.status |= kLiftedOnce;
        sound(kLiftedSound, actor);
        if (boss.status & kSpinning) {
            boss.status &= uint8_t(~kSpinning);
            sound(kSpinStopSound, actor);
        }
        actor.subState = uint8_t(BossSub::Lifted);
        actor.step = uint8_t(LiftedStep::Carried);
        actor.collisionBox.pointAt(TableId::St28LavaBossLyingBox);
        boss.pose &= uint8_t(~(kPoseInLava | kPoseAfterStomp));
        for (size_t k = 0; k < 4; ++k) showPart(actor, kHeldBombParts + k, true);
        if (boss.pose & kPoseUpright) {
            setAnim(actor, BossAnim::LiftedUpright);
        } else if (boss.pose & kPoseSpinning) {
            setAnim(actor, BossAnim::LiftedCharging);
        } else {
            setAnim(actor, BossAnim::LiftedSpinning);
            showPart(actor, kHeldBombParts, true);
        }
    }
    switch (BossSub(actor.subState)) { // [0x895208]
    case BossSub::Start: // exe 0x4cda30
        actor.subState = uint8_t(BossSub::Approach);
        actor.airborne = 1;
        break;
    case BossSub::Approach: approach(env, actor, boss); break;
    case BossSub::Defeat: defeat(actor, boss); break;
    case BossSub::Lifted: // exe 0x4ce150: [0x895240]
        switch (LiftedStep(actor.step)) {
        case LiftedStep::Carried: carried(env, actor, boss); break;
        case LiftedStep::Thrown: thrown(env, actor, boss); break;
        case LiftedStep::Landed: landed(env, actor, boss); break;
        default: notPortedAt(kStoryPort, "0x4ce150 step past [0x895240]", actor.step);
        }
        break;
    case BossSub::GetUp: getUp(env, actor, boss); break;
    case BossSub::Charge: charge(env, actor, boss); break;
    case BossSub::Bomb: bombAttack(env, actor, boss, false); break;          // exe 0x4cee00
    case BossSub::Lob: lob(env, actor, boss); break;                          // exe 0x4cf180
    case BossSub::OrbitingBombs: bombAttack(env, actor, boss, true); break; // exe 0x4cf390
    default: notPortedAt(kStoryPort, "0x4cd660 sub-state past [0x895208]", actor.subState);
    }
    submitHits(actor, boss, stats);
}
void lavaBoss(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x4cd440
    BossFields& boss = classState<BossFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    const uint8_t red = (boss.status & kSinking) ? kTintRedSinking : kTintRedNormal;
    boss.tintRed = red;
    actor.tint = uint16_t(red | kTintBlueGreen << 8); // (the exe's two byte stores)
    switch (BossState(actor.state)) { // [0x8951fc]
    case BossState::Bind: bind(env, actor, boss); break;
    case BossState::Active: active(env, actor, boss); break;
    case BossState::Free: freeActor(actor); break; // exe 0x468fa0
    default: notPortedAt(kStoryPort, "0x4cd440 state past [0x8951fc]", actor.state);
    }
    if (actor.anim != actor.prevAnim) animSet(actor, actor.anim, 0);  // exe 0x5e1c50
    else if (boss.status & kDecodeOnly) animDecodeCurrent(actor);     // exe 0x5e1f30
    else animStep(actor);                                              // exe 0x5e1ea0
    if (boss.status & kSpinning) {
        LaneVec4 body = decodedFrame().entries[1]; // part 0's angles: the spin
        body.y = int16_t(uint16_t(boss.walkPhase + uint16_t(body.y)));
        if (boss.spinSoundTimer != 0) {
            boss.spinSoundTimer -= 1u;
        } else {
            sound(kSpinSound, actor);
            boss.spinSoundTimer = 10;
        }
    }
    animPose(actor); // exe 0x5e2d90
}

} // namespace

void lavaBossUpdate(const PlayerEnv& env, uint32_t actor) { lavaBoss(env, animatedActorAt(actor)); }

} // namespace od::port
