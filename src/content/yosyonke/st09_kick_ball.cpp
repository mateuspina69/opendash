// ST09's kick ball, static class 0x5b4a50: the goal game. Its shadow (exe 0x5f20f0) is render output and left out.
//
// Variant 1 only from story 2. Kicked (its box's result carries kBallKicked): rolled along the player's yaw (sound
// 0x297, costs 0x10 zenny, the kicks counted), tumbling, bouncing off walls (0x29a; the floor slows it, 0x298) and
// actors; into one of the two goals (x / y / z windows) -> sound 0x299, Scored: after 0xf ticks and with window 4 free,
// pays kicks * 0x10 zenny and opens message 0x64 / 0x65 / 0x66 (the stage's bank, by variant and goal), freed.
#include "content/yosyonke/st09_kick_ball.hpp"
#include "content/yosyonke/st09_kick_ball_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/collision.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/message.hpp"
#include "game/message_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/rewards.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st09_kick_ball_state;

namespace {

enum class BallState : uint8_t {
    Init = 0,    // exe 0x5b4a80
    Rolling = 1, // exe 0x5b4b00
    Scored = 2,  // exe 0x5b4f90
};
// +9 of Rolling
enum class BallSub : uint8_t {
    Ready = 0,  // a kick starts it
    Kicked = 1, // the kick's cool-down
};

constexpr uint8_t kBallHit = 0x1;     // the box was kicked this tick
constexpr uint8_t kBallRolling = 0x2; // kicked, rolling (the actor collision on)
constexpr uint8_t kBallGoalB = 0x4;   // it went into the second goal

constexpr uint32_t kBallKicked = 0x80000; // the attack flag the player's kick hands the ball's box
constexpr uint8_t kAlwaysVariant = 0, kFromStory2Variant = 1;
constexpr uint8_t kFromStory = 2;
const DataTable<CollisionBoxRecord> ballCollisionBox{TableId::St09KickBallCollisionBox};
const DataTable<InteractionBoxRecord> ballLiftBox{TableId::St09KickBallLiftBox};
constexpr uint16_t kKickSound = 0x297, kRollSound = 0x298, kGoalSound = 0x299, kBounceSound = 0x29a;
constexpr int32_t kKickCost = 0x10;
constexpr uint16_t kKickCoolDown = 0xa, kGoalDelay = 0xf;
constexpr int8_t kGoalWindow = 4;
constexpr int16_t kGoalMessageB = 0x65, kGoalMessageA = 0x66;

int16_t wrapAngle(uint32_t v) { // >= 0x800 -> -0x1000 (s16)
    return int16_t(v) >= 0x800 ? int16_t(uint16_t(v - 0x1000u)) : int16_t(uint16_t(v));
}

// exe 0x5b4b00: the ball rolling
void ballRolling(const PlayerEnv& env, StaticActor& actor) {
    BallFields& f = classState<BallFields>(actor);
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    f.playerAngle = wrapAngle(uint32_t(angleOf(actor.pos, player().pos))); // exe 0x5e3bc0
    if (BallSub(actor.subState) == BallSub::Kicked) {
        --f.timer;
        if (f.timer == 0) {
            f.flags &= uint8_t(~kBallHit);
            actor.subState = uint8_t(BallSub::Ready);
        }
    } else if (BallSub(actor.subState) == BallSub::Ready && (f.flags & kBallHit)) {
        soundPlayAtPos(kKickSound, actor.pos); // exe 0x5d5380
        f.flags |= kBallRolling;
        f.timer = kKickCoolDown;
        f.heading = player().rotation.y;
        ++f.kicks;
        actor.velocity.y = int16_t(0x280u - uint16_t(cameraState().lookPitch));
        actor.velocity.z = 0x300;
        actor.acceleration.y = -0x30;
        zennyAdd(-kKickCost); // exe 0x5e51d0
        ++actor.subState;
    }
    actor.velocity.y += actor.acceleration.y;
    actor.velocity.z += actor.acceleration.z;
    moveAlongYaw(actor.pos, f.heading, 0, -int32_t(actor.velocity.y), -int32_t(actor.velocity.z)); // exe 0x5e36f0
    if (f.flags & kBallRolling) {
        actor.rotation.x = wrapAngle(uint16_t(actor.rotation.x) + 0x140u);
        actor.rotation.y = wrapAngle(uint16_t(actor.rotation.y) + 0x10u);
        actor.rotation.z = wrapAngle(uint16_t(actor.rotation.z) + 0xa0u);
    }
    const int x = actor.pos.x.whole, y = actor.pos.y.whole, z = actor.pos.z.whole;
    const bool yIn = y >= -0x80 && y <= -0x60;
    if (x >= -0x830 && x <= -0x7d0 && yIn && z >= -0xfd0 && z <= -0xfa8) { // the first goal
        soundPlayAtPos(kGoalSound, actor.pos);
        f.timer = kGoalDelay;
        actor.setDrawn(false);
        actor.enterStateKeepingStep(uint8_t(BallState::Scored));
        return;
    }
    if (x >= 0x1030 && x <= 0x1058 && yIn && z >= 0xd0 && z <= 0x130) { // the second goal
        soundPlayAtPos(kGoalSound, actor.pos);
        f.flags |= kBallGoalB;
        f.timer = kGoalDelay;
        actor.enterStateKeepingStep(uint8_t(BallState::Scored));
        actor.setDrawn(false);
        return;
    }
    if (int32_t(actorTestMove(env, actor)) < 0) {   // exe 0x40f570
        const uint32_t r = actorPushOut(env, actor); // exe 0x410960
        if (r & kWallOnSides) {
            soundPlayAtPos(kBounceSound, actor.pos);
            int16_t pitch = 0, yaw = f.heading;
            reflectRotationOffWalls(pitch, yaw, r); // exe 0x410740
            f.heading = yaw;
        }
        if (r & kWallOnYPlus) { // the floor: it slows (7 .. 10 / 16 of the speed, the fall reversed)
            actor.velocity.y =
                int16_t(0u - ((((rngStep() & 3) + 7u) * uint32_t(int32_t(actor.velocity.y))) >> 4));
            actor.velocity.z = int16_t((((rngStep() & 3) + 7u) * uint32_t(int32_t(actor.velocity.z))) >> 4);
            const int vy = actor.velocity.y;
            if ((vy < 0 ? -vy : vy) <= 0x20) { // at rest
                actor.velocity.y = 0;
                actor.velocity.z = 0;
                actor.acceleration.y = 0;
                if (f.rollingWord()) {
                    f.clearRollingWord();
                    soundPlayAtPos(kGoalSound, actor.pos);
                    // the pitch and the roll snapped to the nearest quarter turn
                    for (int16_t* lane : {&actor.rotation.x, &actor.rotation.z}) {
                        uint16_t v = uint16_t((uint16_t(*lane) + 0x200u) & 0xfc00u);
                        if (int16_t(v) >= 0x800) v = uint16_t(v - 0x1000u);
                        if (int16_t(v) < -0x800) v = uint16_t(v + 0x1000u);
                        *lane = int16_t(v);
                    }
                }
            } else {
                soundPlayAtPos(kRollSound, actor.pos);
            }
        } else if (r & kWallOnYMinus) { // the ceiling
            soundPlayAtPos(kBounceSound, actor.pos);
            actor.velocity.y = int16_t(0u - uint16_t(actor.velocity.y));
        }
    }
    if (f.flags & kBallRolling) {
        if (actor.actorHit != 0) {
            soundPlayAtPos(kBounceSound, actor.pos);
            if (actor.actorHit & kSolidAboveOrBelow) actor.actorHit = actor.actorHit & 0xf0ffu;
            int16_t pitch = 0, yaw = f.heading;
            reflectOffActorHit(pitch, yaw, actor.actorHit); // exe 0x4108b0
            f.heading = yaw;
        } else {
            actorCollisionSubmitMover(actor); // exe 0x4140b0
        }
    }
    actor.actorHit = 0;
    if (f.box.result & kBallKicked) f.flags |= kBallHit;
    f.box.result = 0;
    f.box.previousX = actor.prevPos.x.whole;
    f.box.previousZ = actor.prevPos.z.whole;
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    hitListSubmit(pos, f.box, hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x20),
                  kHitAttackSolid); // exe 0x5e4120
}

// exe 0x5b4f90: after the goal's delay and with the goal window free, the kicks paid back and the goal's message
void ballScored(StaticActor& actor) {
    BallFields& f = classState<BallFields>(actor);
    if (f.timer != 0) {
        --f.timer;
        return;
    }
    if (windowStatus(kGoalWindow) != 0) return; // exe 0x557810
    const bool goalB = (f.flags & kBallGoalB) != 0;
    const int16_t message = actor.variant == kAlwaysVariant
                                ? int16_t(((uint32_t(int32_t(int8_t(f.flags))) & kBallGoalB) | 0x190) >> 2)
                                : int16_t(goalB ? kGoalMessageB : kGoalMessageA);
    zennyAdd(int32_t(f.kicks) << 4);
    windowOpen(kGoalWindow, kMessageBank, message); // exe 0x557390
    freeActor(actor);                               // exe 0x5e1540
}

// exe 0x5b4a50 (its states: the exe's table 0x8d0930)
void kickBall(const PlayerEnv& env, StaticActor& actor) {
    switch (BallState(actor.state)) {
    case BallState::Init:
        if ((actor.variant == kFromStory2Variant && gameState().story < kFromStory) || !bindActorModel(actor, 0)) {
            freeActor(actor);
            return;
        }
        // exe 0x40dc40(+0x28, +0x68): the rotations, the motion and contact block cleared one byte at a time
        actor.rotation.clearBytes();
        actor.prevRotation.clearBytes();
        actor.clearMotion();
        actor.clearClassBlock(); // exe 0x40dc40(+0xbc, +0xc8)
        actor.collisionBox.pointAt(ballCollisionBox.stateAddress());
        actor.interactionBox.pointAt(ballLiftBox.stateAddress());
        actor.interaction = kInteractLift;
        actor.enterState(uint8_t(BallState::Rolling));
        return;
    case BallState::Rolling: ballRolling(env, actor); return;
    case BallState::Scored: ballScored(actor); return;
    default: notPortedAt(kStoryPort, "0x5b4a50 state (table 0x8d0930)", actor.state);
    }
}

} // namespace

void kickBallUpdate(const PlayerEnv& env, uint32_t actor) { kickBall(env, staticActorAt(actor)); }

} // namespace od::port
