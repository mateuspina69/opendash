// The servbots, class 0x4628b0 (see servbot.hpp).
#include "game/actor_state.hpp"
#include "game/servbot.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/collision.hpp"
#include "game/distances.hpp"
#include "game/event_state.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/actors.hpp"
#include "game/vectors.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/message.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

const PlayerEnv* E = nullptr;

constexpr const char* kServbotPort = "servbot"; // the area of its port-gap errors

// the states (+8; table 0x88fbe0)
enum class ServbotState : uint8_t {
    Init = 0,   // exe 0x4629f0
    Main = 1,   // exe 0x462af0
    Lifted = 2, // exe 0x463b70
};
// the sub-states of the lifted state (+9; table 0x88fc9c)
enum class LiftedSub : uint8_t {
    InHands = 0, // exe 0x463b90
    Thrown = 1,  // exe 0x463c90
};
// the sub-states of the main state (+9; table 0x88fbec)
enum class ServbotSub : uint8_t {
    Route = 0,       // exe 0x462c30
    Stationary = 1,  // exe 0x463180
    Talk = 2,        // exe 0x463440
    FacePlayer = 3,  // exe 0x4634e0
    Inert = 4,       // exe 0x52ef00 (ret): nothing runs
    Dazed = 5,       // exe 0x463570
    KnockedBack = 6, // exe 0x463660 (hit from the front)
    GetUp = 7,       // exe 0x463780
    Thrown = 8,      // exe 0x4637c0 (hit from behind)
    GetUpBack = 9,   // exe 0x463910
    Fall = 10,       // exe 0x463950
    Bounce = 11,     // exe 0x463ab0
};
// the route steps (+0xa; table 0x88fc1c)
enum class RouteStep : uint8_t {
    Target = 0,       // exe 0x462d40
    Walk = 1,         // exe 0x462d90
    NextWaypoint = 2, // exe 0x462e90
    WallTurn = 3,     // exe 0x462f60
    Pause = 4,        // exe 0x463060
};
// the stationary kinds (+0xd & 7; table 0x88fc50, 3 entries)
enum class StandKind : uint8_t {
    Plain = 0,  // exe 0x4631a0
    Fidget = 1, // exe 0x4632a0
    Still = 2,  // exe 0x463400
};

// +0x164 bits
constexpr uint8_t kServbotNoTalk = 0x01;
constexpr uint8_t kServbotStanding = 0x02;   // set by the standing / talking steps (read by nothing here)
constexpr uint8_t kServbotMover = 0x04;      // queued as a mover
constexpr uint8_t kServbotLifted = 0x08;     // lifted by the player
constexpr uint8_t kServbotNoHit = 0x10;      // hits are not checked
constexpr uint8_t kServbotNoReaction = 0x20; // a hit does not knock it down
constexpr uint8_t kServbotBouncing = 0x40;   // walls reflect its yaw; the ground does not stop it
constexpr uint8_t kServbotAttackEntry = 0x80; // its hit entry is the attacking kind
constexpr uint8_t kServbotAirborneBits = kServbotNoHit | kServbotNoReaction | kServbotBouncing;
// +0x16a collision bits
constexpr uint8_t kServbotBlocked = 0x01;    // stopped by a wall or an actor
constexpr uint8_t kServbotLanded = 0x02;     // landed / bounced off a wall
constexpr uint8_t kServbotGroundLost = 0x04; // the ground more than kGroundLossDrop below
constexpr uint8_t kServbotOffGrid = 0x08;
constexpr uint8_t kServbotHitActor = 0x20;   // an actor pushed it / it pushed into one
// +0x16b: what losing the ground leads to
constexpr uint8_t kGroundLossFall = 1;
constexpr uint8_t kGroundLossBounce = 2;
constexpr int kGroundLossDrop = 0x10; // the ground this far below a standing servbot: lost

// the knocked-down sub-states' steps (+0xa)
enum class DazedStep : uint8_t { Start = 0, Dazed = 1, DazedLoop = 2, Waiting = 3 };
enum class KnockStep : uint8_t { Start = 0, Flying = 1, Down = 2, Waiting = 3 };
enum class FallStep : uint8_t { Start = 0, Falling = 1, Landing = 2, LandedHard = 3, Waiting = 4 };

// animations
constexpr uint8_t kAnimStand = 0;
constexpr uint8_t kAnimWalk = 1;
constexpr uint8_t kAnimRun = 2;
constexpr uint8_t kAnimDazed = 5;
constexpr uint8_t kAnimDazedLoop = 6;
constexpr uint8_t kAnimGetUp = 7;
constexpr uint8_t kAnimThrown = 9;
constexpr uint8_t kAnimThrownDown = 10;
constexpr uint8_t kAnimGetUpBack = 0xb;
constexpr uint8_t kAnimLifted = 0xd;     // in the player's hands
constexpr uint8_t kAnimLiftedDown = 0xe; // ... knocked out (its hit points below 0)
constexpr uint8_t kAnimBounce = 0x10;
constexpr uint8_t kAnimFall = 0x15;
constexpr uint8_t kAnimLand = 0x16;
constexpr uint8_t kAnimLandHard = 0x17;
constexpr uint8_t kNoAnim = 0xff;

constexpr uint16_t kSoundThud = 0xab;
constexpr uint16_t kSoundHit = 0x8d;

constexpr uint32_t kDizzyCell = 6; // the texture cell of the knocked-down face
constexpr uint8_t kHeadHoldTicks = 0x3c;
constexpr int kPlayerNear = 0x100;
constexpr uint32_t kFrontWindow = 0x400;
constexpr uint32_t kFrontHalf = 0x800; // a hit inside +-0x400 of the facing came from the front
constexpr int kArrive = 0x80;
constexpr int32_t kWallTurnReach = 0x1000000; // the point the wall turn steers to: 0x100 units off the servbot
constexpr uint32_t kPauseChance = 0x5500aau;
constexpr uint32_t kLandHardChance = 0x24249048u; // bit (r & 0x1f) from the top set: the hard landing
constexpr int16_t kWalkSpeed = -0x30, kRunSpeed = -0x80, kWalkTurn = 0x18, kRunTurn = 0x30;
constexpr int16_t kGravity = 0x40;
constexpr int16_t kKnockUp = -0x180;
constexpr int16_t kFallForward = -0x80; // the forward speed of a fall
constexpr uint8_t kServbotType = 0xb;   // its type in the stage tables (the partner search compares it)
// let go from the player's hands: thrown along the player's aim, or dropped (the player hurt)
constexpr int16_t kThrowUp = -0x100, kThrowForward = -0x400;
constexpr int16_t kDropUp = -0x200, kDropForward = -0x100;
constexpr uint8_t kThrownBits = kServbotAttackEntry | kServbotAirborneBits | kServbotMover;
constexpr uint8_t kDroppedBits = kServbotAirborneBits | kServbotLifted | kServbotMover;

// the exe's servbot tables
const DataTable<ScalarTable<int16_t, 16>> pauseTicks{TableId::ServbotPauseTicks};
const DataTable<ScalarTable<uint8_t, 8>> fidgetLooks{TableId::ServbotFidgetLooks};
const DataTable<ScalarTable<uint16_t, 8>> fidgetLookTicks{TableId::ServbotFidgetLookTicks};
const DataTable<ScalarTable<uint16_t, 4>> fidgetPatterns{TableId::ServbotFidgetPatterns};
const DataTable<ScalarTable<int8_t, 16>> dazedTicks{TableId::ServbotDazedTicks};
const DataTable<ScalarTable<int8_t, 16>> downTicks{TableId::ServbotDownTicks};
const DataTable<CollisionBoxRecord> servbotCollisionBox{TableId::ServbotCollisionBox};
const DataTable<InteractionBoxRecord> servbotTalkBox{TableId::ServbotTalkBox};
const DataTable<InteractionBoxRecord> servbotSmallTalkBox{TableId::ServbotSmallTalkBox};

// exe 0x5e3280(actor, n): the model's texture cell n (its colour / face)
void setTextureCell(AnimatedActor& actor, uint32_t n) { setTexturePageCell(actor.texturePage, n); }

// exe 0x464050: back to the route (or standing), its own colour
void backToIdle(AnimatedActor& actor, ServbotFields& bot) {
    actor.subState = (bot.behaviour & kNpcStationary) ? uint8_t(ServbotSub::Stationary) : uint8_t(ServbotSub::Route);
    actor.enterStep(0);
    setTextureCell(actor, bot.colour);
}
// exe 0x463fe0: walk or run by the kind
void startWalk(AnimatedActor& actor, ServbotFields& bot) {
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.velocity.z = 0;
    actor.spin.y = 0;
    if (!(bot.behaviour & kNpcRunKinds)) {
        actor.anim = kAnimWalk;
        actor.velocity.z = kWalkSpeed;
        actor.spin.y = kWalkTurn;
    } else {
        actor.anim = kAnimRun;
        actor.velocity.z = kRunSpeed;
        actor.spin.y = kRunTurn;
    }
}
// exe 0x5e3690: moved by its velocity along its yaw
void moveForward(Actor& actor) { integrateVelocity(actor, 0, 0, actor.velocity.z); }
// the player close in front of it
bool playerClose(const Actor& actor) { return npcPlayerInFront(actor, kPlayerNear, kFrontWindow); }
// the angle (& 0xfff) from the 16.16 point (x, z) to the actor
int angleFromPoint(const Actor& actor, int32_t x, int32_t z) {
    return atanAngle(float(int32_t(uint32_t(actor.pos.x) - uint32_t(x))),
                     float(int32_t(uint32_t(actor.pos.z) - uint32_t(z)))) &
           kAngleMask;
}

void routeWalk(AnimatedActor& actor, ServbotFields& bot);
// exe 0x462d40: the next waypoint becomes the target, a look mode picked, then the walk starts
void routeTarget(AnimatedActor& actor, ServbotFields& bot) {
    bot.bits = kServbotMover;
    const Waypoint wp = routeWaypoint(bot.route, bot.waypoint);
    bot.targetX = wp.x;
    bot.targetZ = wp.z;
    NpcHead head = bot.head();
    npcPickLook(bot.behaviour, head, stateAddress(TableId::ServbotLookSets));
    actor.enterStep(uint8_t(RouteStep::Walk));
    routeWalk(actor, bot);
}
// exe 0x462d90: turn toward the target and walk; at the waypoint maybe pause, else the next one; blocked -> turn away
void routeWalk(AnimatedActor& actor, ServbotFields& bot) {
    if (actor.stepCounter == 0) {
        startWalk(actor, bot);
        bot.bits = kServbotMover;
        actor.stepCounter = 1;
    }
    const int16_t tx = bot.targetX, tz = bot.targetZ;
    turnToward(actor.rotation.y,
               angleFromPoint(actor, int32_t(uint32_t(int32_t(tx)) << 16), int32_t(uint32_t(int32_t(tz)) << 16)),
               actor.spin.y);
    moveForward(actor);
    if (uint32_t(int(tx) - int(actor.pos.x.whole) + kArrive) < uint32_t(2 * kArrive) &&
        uint32_t(int(tz) - int(actor.pos.z.whole) + kArrive) < uint32_t(2 * kArrive)) {
        const uint32_t r = rngStep();
        if (int32_t(kPauseChance << (r & 0x1f)) < 0 && (bot.behaviour & kNpcMayPause)) {
            npcNextWaypoint(bot.route, bot.waypoint, bot.previousWaypoint);
            actor.enterStep(uint8_t(RouteStep::Pause));
            return;
        }
        actor.enterStep(uint8_t(RouteStep::NextWaypoint));
    }
    if (bot.collisionBits & kServbotBlocked) actor.enterStep(uint8_t(RouteStep::WallTurn));
}
// exe 0x462e90: the next waypoint, then a new target
void routeNext(AnimatedActor& actor, ServbotFields& bot) {
    npcNextWaypoint(bot.route, bot.waypoint, bot.previousWaypoint);
    actor.enterStep(uint8_t(RouteStep::Target));
    moveForward(actor);
}
// exe 0x462f60: turn (twice the walk's turn) toward a point off the wall it hit (or ahead when an actor blocked it),
// walking on, until it is no longer blocked
void routeWallTurn(AnimatedActor& actor, ServbotFields& bot) {
    if (actor.stepCounter == 0) {
        bot.bits = kServbotMover;
        int32_t px, pz; // a point beside the servbot: off the wall it hit, else ahead
        if (actor.actorHit == 0u && actor.lastMover == nullptr) {
            const uint32_t c = actor.wallHit;
            pz = 0;
            px = 0;
            if (c & kWallOnXPlus) px = -kWallTurnReach;
            if (c & kWallOnXMinus) px = kWallTurnReach;
            if (c & kWallOnZPlus) pz = -kWallTurnReach;
            if (c & kWallOnZMinus) pz = kWallTurnReach;
        } else {
            const int y = uint16_t(actor.rotation.y) & kAngleMask;
            pz = fixedCos(y) << 12;
            px = fixedSin(y) << 12;
        }
        px = int32_t(uint32_t(px) + uint32_t(actor.pos.x));
        pz = int32_t(uint32_t(pz) + uint32_t(actor.pos.z));
        const int d = angleDiff(uint32_t(actor.rotation.y), uint32_t(angleFromPoint(actor, px, pz)));
        actor.spin.y = d < 0 ? int16_t(-2 * kWalkTurn) : int16_t(2 * kWalkTurn); // (((d >= 0) - 1 & 0xffd0) + 0x18) * 2
        ++actor.stepCounter;
    }
    moveForward(actor);
    actor.rotation.y = int16_t(uint16_t(actor.rotation.y + actor.spin.y));
    if (!(bot.collisionBits & kServbotBlocked)) actor.enterStep(uint8_t(RouteStep::Walk));
}
// exe 0x463060: a pause at a waypoint (the table's ticks), watching the player; then face the player
void routePause(AnimatedActor& actor, ServbotFields& bot) {
    const WatchStep step = WatchStep(actor.stepCounter);
    if (step == WatchStep::Start) {
        actor.anim = kAnimStand;
        bot.bits = kServbotStanding;
        actor.stepCounter = uint8_t(WatchStep::LookAhead);
        const uint32_t r = rngStep();
        bot.timer = pauseTicks[r & 0xf];
    }
    if (step == WatchStep::Start || step == WatchStep::LookAhead) {
        bot.lookMode = uint8_t(NpcLook::Ahead);
        if (playerClose(actor)) actor.stepCounter = uint8_t(WatchStep::LookAtPlayer);
    } else if (step == WatchStep::LookAtPlayer) {
        bot.lookMode = uint8_t(NpcLook::Player);
        if (npcPlayerGone(actor)) {
            bot.headHold = 0;
            actor.stepCounter = uint8_t(WatchStep::LookAhead);
        }
    }
    NpcHead head = bot.head();
    npcHoldHead(head);
    --bot.timer;
    if (bot.timer == 0) {
        actor.subState = uint8_t(ServbotSub::FacePlayer);
        actor.enterStep(0);
    }
}
// exe 0x462c30: the route's step, the partner look, and the player close in front -> face it
void subRoute(AnimatedActor& actor, ServbotFields& bot) {
    bot.groundLossReaction = kGroundLossFall;
    switch (RouteStep(actor.step)) {
    case RouteStep::Target: routeTarget(actor, bot); break;
    case RouteStep::Walk: routeWalk(actor, bot); break;
    case RouteStep::NextWaypoint: routeNext(actor, bot); break;
    case RouteStep::WallTurn: routeWallTurn(actor, bot); break;
    case RouteStep::Pause: routePause(actor, bot); break;
    default: notPortedAt(kServbotPort, "route step", actor.step);
    }
    NpcHead head = bot.head();
    if (bot.partnerSlot == 0) {
        if (bot.lookMode == uint8_t(NpcLook::Partner)) npcPickLook(bot.behaviour, head, stateAddress(TableId::ServbotLookSets));
    } else {
        npcPartnerCheck(actor, head);
    }
    if (playerClose(actor)) {
        actor.subState = uint8_t(ServbotSub::FacePlayer);
        actor.enterStep(0);
    }
}
// exe 0x4631a0: the plain stationary kind: stands, looks at the player while it is close in front, else the partner
void standPlain(AnimatedActor& actor, ServbotFields& bot) {
    NpcHead head = bot.head();
    const WatchStep step = WatchStep(actor.step);
    if (step == WatchStep::Start) {
        actor.anim = kAnimStand;
        bot.bits = kServbotStanding;
        actor.enterStep(uint8_t(WatchStep::LookAhead));
    } else if (step != WatchStep::LookAhead) {
        if (step == WatchStep::LookAtPlayer) {
            bot.lookMode = uint8_t(NpcLook::Player);
            if (npcPlayerGone(actor)) {
                bot.headHold = 0;
                actor.enterStep(uint8_t(WatchStep::LookAhead));
            }
        }
        npcHoldHead(head);
        return;
    }
    bot.lookMode = uint8_t(NpcLook::Ahead);
    if (playerClose(actor)) {
        actor.enterStep(uint8_t(WatchStep::LookAtPlayer));
        npcHoldHead(head);
        return;
    }
    if (bot.partnerSlot) npcPartnerCheck(actor, head);
    npcHoldHead(head);
}
// exe 0x4632a0: the fidget stationary kind: random look modes and repeats (the servbot plays no idle animation)
void standFidget(AnimatedActor& actor, ServbotFields& bot) {
    auto newLook = [&] {
        uint32_t r = rngStep();
        bot.lookMode = fidgetLooks[r & 7];
        r = rngStep();
        bot.fidgetLookTicks = int16_t(fidgetLookTicks[r & 7]);
    };
    switch (FidgetStep(actor.step)) {
    case FidgetStep::Start:
        actor.anim = kAnimStand;
        bot.bits = kServbotStanding;
        newLook();
        actor.enterStep(uint8_t(FidgetStep::NewPattern));
        [[fallthrough]];
    case FidgetStep::NewPattern: {
        actor.step = uint8_t(FidgetStep::NextAnimation);
        const uint32_t r = rngStep();
        const uint16_t pattern = fidgetPatterns[r & 3];
        bot.timer = kFidgetRepeats;
        bot.fidgetPattern = int16_t(pattern);
        [[fallthrough]];
    }
    case FidgetStep::NextAnimation: actor.step = uint8_t(FidgetStep::Animating); break;
    case FidgetStep::Animating:
        if (actor.animKey.flags != 0) {
            bot.fidgetPattern <<= 1;
            --bot.timer;
            actor.step = uint8_t(bot.timer != 0 ? FidgetStep::NextAnimation : FidgetStep::NewPattern);
        }
        break;
    default: break;
    }
    --bot.fidgetLookTicks;
    if (bot.fidgetLookTicks == 0) newLook();
}
// exe 0x463400: the still stationary kind: stands, head straight while held
void standStill(AnimatedActor& actor, ServbotFields& bot) {
    if (actor.step == 0) {
        actor.anim = kAnimStand;
        bot.bits = kServbotStanding;
        bot.lookMode = uint8_t(NpcLook::Ahead);
        actor.enterStep(1);
    }
    NpcHead head = bot.head();
    npcHoldHead(head);
}
// exe 0x463440: the event runs, the head on the player; then face the player
void subTalk(AnimatedActor& actor, ServbotFields& bot) {
    EventState& event = eventState();
    const TalkStep step = TalkStep(actor.step);
    if (step == TalkStep::Start) {
        bot.bits = kServbotNoTalk | kServbotStanding;
        actor.anim = kAnimStand;
        event.speakerAnimation = kNoSpeakerAnimation;
        bot.lookMode = uint8_t((bot.behaviour & kNpcFacesSpeaker) ? NpcLook::Player : NpcLook::Ahead);
        bot.headPitchTarget = 0;
        actor.enterStep(uint8_t(TalkStep::Talking));
    } else if (step != TalkStep::Talking) {
        if (step != TalkStep::Settling) return;
        if (!actor.animEnded()) return;
        bot.headHold = kHeadHoldTicks;
        actor.subState = uint8_t(ServbotSub::FacePlayer);
        actor.enterStep(0);
        return;
    }
    int16_t v[3];
    npcLookPoint(kPlayerClass, v); // slot 0: the player
    npcLookYaw(actor, bot.headYawTarget, v);
    if (event.active == 0) actor.enterStep(uint8_t(TalkStep::Settling));
}
// exe 0x4634e0: stands facing the player until it is gone, then back to the route / standing
void subFacePlayer(AnimatedActor& actor, ServbotFields& bot) {
    if (actor.step == 0) {
        actor.anim = kAnimStand;
        bot.lookMode = uint8_t(NpcLook::Player);
        bot.bits = 0;
        actor.enterStep(1);
    }
    NpcHead head = bot.head();
    npcHoldHead(head);
    if (npcPlayerGone(actor)) {
        bot.headHold = 0;
        backToIdle(actor, bot);
    }
}
// exe 0x463d30(actor, yaw): landed / bounced (+0x16a & 2): sound, halved and reflected speeds, no pitch, airborne;
// else (a wall: the sound) the move along the yaw (exe 0x5e36f0; every caller passes +0x2a) and gravity +0x42
int airStep(AnimatedActor& actor, ServbotFields& bot) {
    if (bot.collisionBits & kServbotLanded) {
        soundPlayAtPos(kSoundThud, actor.pos);
        actor.velocity.z = int16_t(actor.velocity.z >> 1);
        actor.velocity.y = int16_t(uint32_t(-int(actor.velocity.y)) >> 1);
        actor.rotation.x = 0;
        actor.airborne = 1;
        return 1;
    }
    if (bot.collisionBits & kServbotBlocked) soundPlayAtPos(kSoundThud, actor.pos);
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    actor.velocity.y = int16_t(uint16_t(actor.velocity.y + actor.acceleration.y));
    return 0;
}
// exe 0x463570 (after a bounce / being thrown): dazed, a random wait, then get up
void subDazed(AnimatedActor& actor, ServbotFields& bot) {
    switch (DazedStep(actor.step)) {
    case DazedStep::Start:
        actor.anim = kAnimDazed;
        setTextureCell(actor, kDizzyCell);
        soundPlayAtPos(kSoundThud, actor.pos);
        bot.bits = actor.hitPoints < 0 ? uint8_t(kServbotLifted | kServbotNoHit | kServbotNoReaction) : 0;
        bot.groundLossReaction = kGroundLossBounce;
        actor.enterStep(uint8_t(DazedStep::Dazed));
        return;
    case DazedStep::Dazed:
        if (actor.animEnded()) {
            actor.anim = kAnimDazedLoop;
            actor.enterStep(uint8_t(DazedStep::DazedLoop));
        }
        return;
    case DazedStep::DazedLoop:
        if (actor.animEnded()) {
            actor.enterStep(uint8_t(DazedStep::Waiting));
            const uint32_t r = rngStep();
            bot.timer = int16_t(dazedTicks[r & 0xf]);
        }
        return;
    case DazedStep::Waiting:
        --bot.timer;
        if (bot.timer == 0) {
            actor.subState = uint8_t(ServbotSub::GetUp);
            actor.enterStep(0);
        }
        return;
    default: return;
    }
}
// exe 0x463660 (6: the hit came from the front -- knocked back along its hit yaw) / 0x4637c0 (8: from behind --
// thrown forward, facing the hit yaw + 0x800); then down, a random wait, get up (7 / 9)
void subKnocked(AnimatedActor& actor, ServbotFields& bot, bool fromBehind) {
    switch (KnockStep(actor.step)) {
    case KnockStep::Start:
        actor.anim = fromBehind ? kAnimThrown : kAnimDazed;
        setTextureCell(actor, kDizzyCell);
        bot.bits = kServbotNoTalk | kServbotAirborneBits;
        bot.groundLossReaction = 0;
        actor.velocity.y = kKnockUp;
        actor.velocity.z = fromBehind ? int16_t(-kKnockUp) : kKnockUp;
        if (fromBehind) {
            actor.acceleration.y = kGravity;
            actor.airborne = 1;
            actor.rotation.y = int16_t(uint16_t(bot.hitYaw + kHalfTurn));
        } else {
            actor.rotation.y = bot.hitYaw;
            actor.acceleration.y = kGravity;
            actor.airborne = 1;
        }
        actor.enterStep(uint8_t(KnockStep::Flying));
        [[fallthrough]];
    case KnockStep::Flying:
        if (airStep(actor, bot) != 0) {
            actor.anim = fromBehind ? kAnimThrownDown : kAnimDazedLoop;
            soundPlayAtPos(kSoundThud, actor.pos);
            if (fromBehind) {
                actor.velocity.z = 0;
                actor.airborne = 0;
                bot.bits &= uint8_t(~kServbotAirborneBits);
            } else {
                bot.bits &= uint8_t(~kServbotAirborneBits);
                actor.airborne = 0;
            }
            bot.groundLossReaction = kGroundLossBounce;
            actor.enterStep(uint8_t(KnockStep::Down));
            if (fromBehind) moveForward(actor);
            return;
        }
        break;
    case KnockStep::Down:
        if (actor.animEnded()) {
            actor.enterStep(uint8_t(KnockStep::Waiting));
            const uint32_t r = rngStep();
            bot.timer = int16_t(downTicks[r & 0xf]);
            if (fromBehind) moveForward(actor);
            return;
        }
        break;
    case KnockStep::Waiting:
        --bot.timer;
        if (bot.timer == 0) {
            actor.subState = uint8_t(fromBehind ? ServbotSub::GetUpBack : ServbotSub::GetUp);
            actor.enterStep(0);
        }
        break;
    default: break;
    }
    if (fromBehind) moveForward(actor);
}
// exe 0x463780 (7, animation 7) / 0x463910 (9, animation 0xb): get up, then back to the route / standing
void subGetUp(AnimatedActor& actor, ServbotFields& bot, uint8_t anim) {
    if (actor.step == 0) {
        actor.anim = anim;
        bot.bits = kServbotNoTalk;
        bot.groundLossReaction = kGroundLossBounce;
        actor.step = 1;
        return;
    }
    if (actor.animEnded()) backToIdle(actor, bot);
}
// exe 0x463950 (10: the ground fell away; the spawn drop): falls, lands (sometimes hard: dazed, then get up)
void subFall(AnimatedActor& actor, ServbotFields& bot) {
    switch (FallStep(actor.step)) {
    case FallStep::Start:
        actor.anim = kAnimFall;
        actor.velocity.x = 0;
        actor.velocity.y = kKnockUp;
        actor.velocity.z = kFallForward;
        actor.airborne = 1;
        setTextureCell(actor, bot.colour);
        bot.bits = kServbotNoTalk;
        bot.groundLossReaction = 0;
        actor.enterStep(uint8_t(FallStep::Falling));
        [[fallthrough]];
    case FallStep::Falling: {
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        actor.velocity.y = int16_t(uint16_t(actor.velocity.y + kGravity));
        if (actor.airborne != 0) return;
        const uint32_t r = rngStep();
        if (int32_t(kLandHardChance << (r & 0x1f)) >= 0 && actor.hitPoints >= 0) {
            actor.anim = kAnimLand;
            actor.enterStep(uint8_t(FallStep::Landing));
            bot.groundLossReaction = kGroundLossBounce;
            return;
        }
        actor.anim = kAnimLandHard;
        actor.enterStep(uint8_t(FallStep::LandedHard));
        setTextureCell(actor, kDizzyCell);
        return;
    }
    case FallStep::Landing:
        if (actor.animEnded()) backToIdle(actor, bot);
        return;
    case FallStep::LandedHard:
        if (actor.animEnded()) {
            const uint32_t r = rngStep();
            bot.timer = int16_t(dazedTicks[r & 0xf]);
            actor.enterStep(uint8_t(FallStep::Waiting));
        }
        return;
    case FallStep::Waiting:
        --bot.timer;
        if (bot.timer == 0) {
            actor.subState = uint8_t(ServbotSub::GetUpBack);
            actor.enterStep(0);
        }
        return;
    default: return;
    }
}
// exe 0x463ab0 (11) + 0x463b10: drops bouncing off the walls, one more step after landing, then dazed
void subBounce(AnimatedActor& actor, ServbotFields& bot) {
    if (actor.step == 0) {
        actor.anim = kAnimBounce;
        setTextureCell(actor, kDizzyCell);
        bot.bits |= kServbotBouncing;
        bot.groundLossReaction = 0;
        actor.velocity.y = kGravity; // drops from the gravity's speed
        actor.acceleration.y = kGravity;
        actor.velocity.z = 0;
        actor.airborne = 1;
        actor.enterStep(1);
    }
    if (airStep(actor, bot) == 0) return;
    if (actor.stepCounter == 0) {
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        actor.velocity.y = int16_t(uint16_t(actor.velocity.y + actor.acceleration.y));
        actor.stepCounter = 1;
        return;
    }
    actor.airborne = 0;
    bot.bits &= uint8_t(~kServbotBouncing);
    actor.subState = uint8_t(ServbotSub::Dazed);
    actor.enterStep(0);
}
// exe 0x464220: a hit in its box: white flash, sound 0x8d, the hit yaw = the contact's angle
bool hitCheck(AnimatedActor& actor, ServbotFields& bot) {
    if ((bot.hitBox.result & kHitDamageMask) == 0) return false;
    actor.tint = kTintFlash;
    soundPlayAtPos(kSoundHit, actor.pos);
    bot.hitYaw = bot.hitBox.contact.x;
    return true;
}
// exe 0x464140: wall test / push-out (reflected while bouncing) / ground; 1 = the ground fell away (more than 0x10
// below while standing)
int collide(AnimatedActor& actor, ServbotFields& bot) {
    uint32_t pushed = 0;
    bot.collisionBits = 0;
    const uint32_t r = actorTestMove(*E, actor);
    if (r & kMoveNoCell) bot.collisionBits |= kServbotOffGrid;
    if (r & kMoveBlocked) {
        pushed = actorPushOut(*E, actor);
        if (pushed & kMoveBlocked) actor.pos = actor.prevPos;
        if (pushed & kWallOnSides) bot.collisionBits = kServbotBlocked;
        if ((bot.bits & kServbotBouncing) && reflectRotationOffWalls(actor.rotation, pushed) != 0)
            bot.collisionBits |= kServbotLanded;
    }
    const int g = actorGround(*E, actor);
    if (actor.airborne == 0) {
        if (kGroundLossDrop < g - actor.pos.y.whole) {
            bot.collisionBits |= kServbotGroundLost;
            return 1;
        }
        actor.pos.y.whole = int16_t(g);
        return 0;
    }
    if (!(bot.bits & kServbotBouncing) && (pushed & (kWallOnYPlus | kWallOnYMinus))) {
        actor.airborne = 0;
        actor.pos.y.whole = int16_t(g);
        bot.collisionBits |= kServbotLanded;
    }
    return 0;
}
// exe 0x462af0: the talk and hit tests, the sub-state, the head, the collision (the ground lost -> fall / bounce),
// the player's lift
void servbotMain(AnimatedActor& actor, ServbotFields& bot) {
    if (!(bot.bits & kServbotNoTalk) && player().talksTo(actor) && eventState().active == 0) {
        npcStartTalk(actor, bot.behaviour, bot.message);
        actor.subState = uint8_t(ServbotSub::Talk);
        actor.step = 0;
        actor.stepCounter = 0;
    }
    if (!(bot.bits & kServbotNoHit) && eventState().active == 0) {
        const bool hit = hitCheck(actor, bot);
        if (!(bot.bits & kServbotNoReaction) && hit) {
            bot.lookMode = uint8_t(NpcLook::Ahead);
            const int d = angleDiff(uint32_t(actor.rotation.y), uint32_t(bot.hitYaw));
            actor.subState =
                uint8_t(uint32_t(d + int(kFrontHalf / 2)) < kFrontHalf ? ServbotSub::KnockedBack : ServbotSub::Thrown);
            actor.step = 0;
            actor.stepCounter = 0;
        }
    }
    switch (ServbotSub(actor.subState)) {
    case ServbotSub::Route: subRoute(actor, bot); break;
    case ServbotSub::Stationary: // exe 0x463180
        switch (StandKind(bot.behaviour & kNpcKindMask)) {
        case StandKind::Plain: standPlain(actor, bot); break;
        case StandKind::Fidget: standFidget(actor, bot); break;
        case StandKind::Still: standStill(actor, bot); break;
        default:
            notPortedAt(kServbotPort, "stationary kind (table 0x88fc50 has 3 entries)", bot.behaviour & kNpcKindMask);
        }
        break;
    case ServbotSub::Talk: subTalk(actor, bot); break;
    case ServbotSub::FacePlayer: subFacePlayer(actor, bot); break;
    case ServbotSub::Inert: break;
    case ServbotSub::Dazed: subDazed(actor, bot); break;
    case ServbotSub::KnockedBack: subKnocked(actor, bot, false); break;
    case ServbotSub::GetUp: subGetUp(actor, bot, kAnimGetUp); break;
    case ServbotSub::Thrown: subKnocked(actor, bot, true); break;
    case ServbotSub::GetUpBack: subGetUp(actor, bot, kAnimGetUpBack); break;
    case ServbotSub::Fall: subFall(actor, bot); break;
    case ServbotSub::Bounce: subBounce(actor, bot); break;
    default: notPortedAt(kServbotPort, "sub-state", actor.subState);
    }
    NpcHead head = bot.head();
    npcLook(actor, head);
    npcHeldHeadTick(head);
    npcTurnHead(head);
    if (collide(actor, bot) != 0) {
        const uint8_t k = bot.groundLossReaction;
        if (k == kGroundLossFall || k == kGroundLossBounce) {
            actor.subState = uint8_t(k == kGroundLossFall ? ServbotSub::Fall : ServbotSub::Bounce);
            actor.step = 0;
            actor.stepCounter = 0;
        }
    }
    if ((actor.flags & kActorLive) && eventState().active == 0 && playerLiftTargetCheck(actor, 0)) {
        actor.state = uint8_t(ServbotState::Lifted);
        actor.subState = 0;
        actor.step = 0;
        actor.stepCounter = 0;
        bot.bits |= kServbotLifted;
    }
}
void liftedThrown(AnimatedActor& actor, ServbotFields& bot);
// exe 0x463b90: at the player's hands (the dizzy face); let go: thrown along the player's aim (its hit entry the
// attacking kind) or dropped, its yaw the player's, put at the player, bouncing
void liftedInHands(AnimatedActor& actor, ServbotFields& bot) {
    bot.collisionBits = 0;
    if (actor.step == 0) {
        actor.step = 1;
        actor.anim = actor.hitPoints < 0 ? kAnimLiftedDown : kAnimLifted;
        setTextureCell(actor, kDizzyCell);
    }
    carryAtHands(actor, 0, 0, 0); // exe 0x573ab0
    const CarryRelease r = carryReleased(); // exe 0x573950
    if (r == CarryRelease::Held) return;
    if (r == CarryRelease::Thrown) {
        int32_t aim[3];
        playerAimVector(0, kThrowUp, kThrowForward, aim); // exe 0x5e3e50
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(aim[1]);
        actor.velocity.z = int16_t(aim[2]);
        bot.bits = kThrownBits;
    } else {
        actor.velocity.y = kDropUp;
        actor.velocity.z = kDropForward;
        bot.bits = kDroppedBits;
    }
    actor.acceleration.y = kGravity;
    actor.rotation.y = player().rotation.y;
    actor.airborne = 1;
    carryDropFromPlayer(actor); // exe 0x573990
    setTextureCell(actor, kDizzyCell);
    bot.groundLossReaction = 0;
    actor.anim = kAnimBounce;
    actor.subState = uint8_t(LiftedSub::Thrown);
    actor.enterStep(0);
    liftedThrown(actor, bot);
}
// exe 0x463c90: flying (a solid body, or an actor it ran into, turns it back: lifted again unless knocked out);
// landed: one more step, then dazed
void liftedThrown(AnimatedActor& actor, ServbotFields& bot) {
    if ((bot.hitBox.result & kHitAttackSolid) || actor.actorHit != 0u) {
        actor.rotation.y = int16_t(uint16_t(actor.rotation.y + kHalfTurn));
        if (actor.hitPoints >= 0) bot.bits |= kServbotLifted;
    }
    if (airStep(actor, bot) != 0) {
        if (actor.stepCounter == 0) {
            integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
            actor.velocity.y = int16_t(uint16_t(actor.velocity.y + actor.acceleration.y));
            ++actor.stepCounter;
            collide(actor, bot);
            return;
        }
        actor.state = uint8_t(ServbotState::Main);
        actor.subState = uint8_t(ServbotSub::Dazed);
        actor.enterStep(0);
        actor.airborne = 0;
        bot.bits &= uint8_t(~kServbotBouncing);
    }
    collide(actor, bot);
}
// exe 0x463b70: the lifted state's sub-state
void servbotLifted(AnimatedActor& actor, ServbotFields& bot) {
    switch (LiftedSub(actor.subState)) {
    case LiftedSub::InHands: liftedInHands(actor, bot); break;
    case LiftedSub::Thrown: liftedThrown(actor, bot); break;
    default: notPortedAt(kServbotPort, "lifted sub-state (table 0x88fc9c has 2 entries)", actor.subState);
    }
}
// exe 0x4629f0: the model, the boxes, the class block cleared, the partner servbot; then back to idle
void servbotInit(AnimatedActor& actor, ServbotFields& bot) {
    auto enterMain = [&] {
        actor.state = uint8_t(ServbotState::Main);
        actor.subState = 0;
        actor.step = 0;
        actor.stepCounter = 0;
    };
    enterMain();
    if (!bindActorModel(actor, actor.variant)) {
        freeActor(actor);
        return;
    }
    actor.prevAnim = kNoAnim;
    actor.anim = kAnimStand;
    actor.velocity.clearWords();
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.actorHit = 0;
    actor.lastMover = nullptr;
    actor.collisionBox.pointAt(servbotCollisionBox.stateAddress());
    actor.interactionBox.pointAt(((bot.behaviour & kNpcSmallTalkBox) ? servbotSmallTalkBox : servbotTalkBox).stateAddress());
    actor.interaction = uint8_t((bot.message == kNpcNoMessage ? 0u : kInteractTalk) | kInteractLift);
    actor.clearClassBlock();
    if (uint8_t(bot.route) != bot.partnerRoute) { // the partner: a live servbot whose route is this one's partner route
        uint32_t mask = actorSystem().animatedLive;
        if (mask != 0) {
            size_t i = 0;
            while (int32_t(mask) >= 0 || animatedPool()[i].type != kServbotType ||
                   uint8_t(classState<ServbotFields>(animatedPool()[i]).route) != bot.partnerRoute) {
                ++i;
                mask <<= 1;
                if (mask == 0) {
                    enterMain();
                    backToIdle(actor, bot);
                    return;
                }
            }
            bot.partnerSlot = animatedPool()[i].slot;
        }
    }
    enterMain();
    backToIdle(actor, bot);
}
// exe 0x4628b0's body: the state, the animation, the hit entry, the mover queue
void servbot(AnimatedActor& actor) {
    ServbotFields& bot = classState<ServbotFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.tint = kTintNeutral;
    actor.savePreviousContact();
    if ((actor.actorHit & kSolidOnSides) || actor.lastMover != nullptr)
        bot.collisionBits |= kServbotBlocked | kServbotHitActor;
    switch (ServbotState(actor.state)) {
    case ServbotState::Init: servbotInit(actor, bot); break;
    case ServbotState::Main: servbotMain(actor, bot); break;
    case ServbotState::Lifted: servbotLifted(actor, bot); break;
    default: notPortedAt(kServbotPort, "state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    NpcHead head = bot.head();
    npcAnimate(actor, head);
    bot.hitBox.result = 0;
    bot.hitBox.previousX = actor.prevPos.x.whole;
    bot.hitBox.previousZ = actor.prevPos.z.whole;
    const AnimatedActorStats stats = animatedStats(actor.type);
    uint32_t param, param2;
    if (!(bot.bits & kServbotAttackEntry) && actor.state != uint8_t(ServbotState::Lifted)) {
        param2 = kHitAttackHarmless | kHitAttackSolid;
        param = hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight) | hitGroup(kHitGroupBodies) | kHitCompareHeights |
                kHitContactMotion | hitRadius(0x20);
    } else {
        param2 = stats.bodyAttack | kHitAttackByPlayer;
        param = hitOwner(actor.slot) | hitGroup(kHitGroupPlayerAttacks) | kHitNoLockOn | kHitContactMotion | hitRadius(0x30);
    }
    const ShortVec4& point = bonePoint(actor, 0, kServbotPort);
    const int16_t pos[3] = {point.x, point.y, point.z};
    hitListSubmit(pos, bot.hitBox, param, param2);
    // exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow list entry (render output)
    if (bot.bits & kServbotMover) {
        actorCollisionSubmitMover(actor); // exe 0x4140b0
        return;
    }
    actor.actorHit = 0;
}

} // namespace

void servbotUpdate(const PlayerEnv& env, uint32_t address) {
    E = &env;
    servbot(animatedActorAt(address));
}

} // namespace od::port
