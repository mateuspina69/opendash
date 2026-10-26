// The town NPC class 0x441950 and the head / look / route helpers it shares with its sibling classes (see
// town_npc.hpp).
#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/town_npc.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/collision.hpp"
#include "game/distances.hpp"
#include "game/event_state.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_list.hpp"
#include "game/message.hpp"
#include "game/event.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

namespace {

const PlayerEnv* E = nullptr;

constexpr const char* kTownNpcPort = "town NPC"; // the area of its port-gap errors

// the states (+8; table 0x88dc28)
enum class NpcState : uint8_t {
    Init = 0,      // exe 0x441af0
    Main = 1,      // exe 0x441bf0
    Inert = 2,     // exe 0x52ef00 (ret): nothing runs
    InertCopy = 3, // the table's second entry for the same empty routine
};
// the sub-states of the main state (+9; table 0x88dc38)
enum class NpcSub : uint8_t {
    Route = 0,      // exe 0x441cb0
    Stationary = 1, // exe 0x442180
    Talk = 2,       // exe 0x444c60
    FacePlayer = 3, // exe 0x442470
    Bumped = 4,     // exe 0x442510
};
// the route sub-state's steps (+0xa; table 0x88dc4c)
enum class RouteStep : uint8_t {
    Target = 0,       // exe 0x441dc0
    Walk = 1,         // exe 0x441e10
    NextWaypoint = 2, // exe 0x441f10
    WallTurn = 3,     // exe 0x441f40
    Pause = 4,        // exe 0x442050
};
// the stationary kinds (+0xd & 7; table 0x88dc80, 3 entries)
enum class StandKind : uint8_t {
    Plain = 0,  // exe 0x4421a0
    Fidget = 1, // exe 0x4422a0
    Still = 2,  // exe 0x442430
};

// animations
constexpr uint8_t kAnimStand = 0;
constexpr uint8_t kAnimWalk = 1;
constexpr uint8_t kAnimRun = 2;
constexpr uint8_t kAnimFidget = 3;
constexpr uint8_t kAnimTalk = 4;   // the talk loop (while the message asks for nothing)
constexpr uint8_t kAnimSettle = 5; // the end of a fidget / of the talk
constexpr uint8_t kNoAnim = 0xff;

constexpr uint8_t kHeadHoldTicks = 0x3c;
constexpr int kPlayerNear = 0x100;       // the player stops a walker / is looked at within this distance ...
constexpr uint32_t kFrontWindow = 0x400; // ... and inside +-0x200 of the facing
constexpr int kPlayerGone = 0x180;
constexpr uint32_t kGoneWindow = 0x500;  // ... or outside +-0x280 of it
constexpr int kPartnerNear = 0x200;
constexpr uint32_t kLookWindow = 0x200;  // the partner / the route target is looked at inside +-0x100 of the facing
constexpr int kArrive = 0x80;            // a waypoint is reached within +-0x80 on x and z
constexpr uint32_t kPauseChance = 0x5500aau; // bit (r & 0x1f) from the top set: pause at the waypoint
constexpr int16_t kBumpedTicks = 0x1e;
constexpr uint32_t kTalkFlagParam = 0x10; // the event's flag of kNpcEventFlag
constexpr int32_t kWallTurnReach = 0x1000000; // the point the wall turn steers to: 0x100 units off the NPC
// the head's look limits (angles in 4096 a turn; the head angles are / 16)
constexpr int kHeadYawRange = 0x300;   // the yaw toward a point, clamped to +-this
constexpr int kHeadPitchDeadZone = 0x40; // a point within this height of the eyes: the head level
constexpr int kHeadPitchTilt = 0xc0;   // ... else tilted this much up / down
constexpr int kHeadEaseYaw = 6, kHeadEasePitch = 2; // the head's turn a tick (head units)
constexpr int16_t kLookPointHeightMask = int16_t(0xffe0); // a look point's height loses its low 5 bits

// the exe's town NPC tables
struct NpcSpeeds : DataRecord {
    int16_t walk = 0;
    int16_t run = 0;
    int16_t turn = 0;
    int16_t height = 0; // the hit entry's centre above the feet
    static NpcSpeeds load(const TableReader& r) {
        NpcSpeeds rec;
        rec.walk = r.s16(0);
        rec.run = r.s16(2);
        rec.turn = r.s16(4);
        rec.height = r.s16(6);
        return rec;
    }
};
const DataTable<RecordTable<NpcSpeeds, 17, 8>> npcSpeeds{TableId::TownNpcSpeeds};
const DataTable<ScalarTable<int16_t, 16>> pauseTicks{TableId::TownNpcPauseTicks};
const DataTable<ScalarTable<uint8_t, 8>> fidgetLooks{TableId::TownNpcFidgetLooks};
const DataTable<ScalarTable<uint16_t, 8>> fidgetLookTicks{TableId::TownNpcFidgetLookTicks};
const DataTable<ScalarTable<uint16_t, 4>> fidgetPatterns{TableId::TownNpcFidgetPatterns};
const DataTable<CollisionBoxRecord> npcCollisionBox{TableId::TownNpcCollisionBox};
const DataTable<InteractionBoxRecord> npcTalkBox{TableId::TownNpcTalkBox};
const DataTable<InteractionBoxRecord> npcSmallTalkBox{TableId::TownNpcSmallTalkBox};

// the stage's route table: u32 per route, the address of its waypoints
struct RouteTable : DataRecord {
    static constexpr uint32_t kBytes = 0x200;
    std::array<uint32_t, 0x80> routes{}; // +0x0
    template <class R> static RouteTable load(const R& r) {
        RouteTable v;
        for (size_t i = 0; i < v.routes.size(); ++i) v.routes[i] = r.template read<uint32_t>(0x0 + i * sizeof(uint32_t));
        return v;
    }
};
// a look-set table: u32 per kind, the address of its 8 look modes (0: none)
struct LookSets : DataRecord {
    static constexpr uint32_t kBytes = 0x20;
    std::array<uint32_t, 8> sets{}; // +0x0
    template <class R> static LookSets load(const R& r) {
        LookSets v;
        for (size_t i = 0; i < v.sets.size(); ++i) v.sets[i] = r.template read<uint32_t>(0x0 + i * sizeof(uint32_t));
        return v;
    }
};
struct LookSet : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    std::array<uint8_t, 8> modes{}; // +0x0
    template <class R> static LookSet load(const R& r) {
        LookSet v;
        for (size_t i = 0; i < v.modes.size(); ++i) v.modes[i] = r.template read<uint8_t>(0x0 + i * sizeof(uint8_t));
        return v;
    }
};

// the player's direction against the NPC's facing
int playerAngleDiff(const Actor& actor) {
    return angleDiff(uint32_t(actor.rotation.y), uint32_t(angleOf(player().pos, actor.pos)));
}
// the angle (& 0xfff) from the 16.16 point (x, z) to the actor
int angleFromPoint(const Actor& actor, int32_t x, int32_t z) {
    return atanAngle(float(int32_t(uint32_t(actor.pos.x) - uint32_t(x))),
                     float(int32_t(uint32_t(actor.pos.z) - uint32_t(z)))) &
           kAngleMask;
}

// exe 0x442800: walk or run (by the kind) at the variant's speed
void npcStartWalk(AnimatedActor& actor, TownNpcFields& npc) {
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.velocity.z = 0;
    actor.spin.y = 0;
    const NpcSpeeds speeds = npcSpeeds[actor.variant];
    if (!(npc.behaviour & kNpcRunKinds)) {
        actor.anim = kAnimWalk;
        actor.velocity.z = wrap16(-speeds.walk);
        actor.spin.y = speeds.turn;
    } else {
        actor.anim = kAnimRun;
        actor.velocity.z = wrap16(-speeds.run);
        actor.spin.y = wrap16(speeds.turn << 1);
    }
}
// exe 0x5e3690: the NPC moved by its velocity along its yaw
void npcMove(Actor& actor) { integrateVelocity(actor, 0, 0, actor.velocity.z); }

void routeWalk(AnimatedActor& actor, TownNpcFields& npc);
// exe 0x441dc0: the next waypoint becomes the target, a look mode picked, then the walk starts
void routeTarget(AnimatedActor& actor, TownNpcFields& npc) {
    npc.bits = kNpcSolid | kNpcMover;
    const Waypoint wp = routeWaypoint(npc.route, npc.waypoint);
    npc.targetX = wp.x;
    npc.targetZ = wp.z;
    NpcHead head = npc.head();
    npcPickLook(npc.behaviour, head, stateAddress(TableId::TownNpcLookSets));
    actor.enterStep(uint8_t(RouteStep::Walk));
    routeWalk(actor, npc);
}
// exe 0x441e10: turn toward the target and walk; at the waypoint maybe pause, else the next one; blocked -> turn away
void routeWalk(AnimatedActor& actor, TownNpcFields& npc) {
    if (actor.stepCounter == 0) {
        npcStartWalk(actor, npc);
        npc.bits = kNpcSolid | kNpcMover;
        actor.stepCounter = 1;
    }
    const int16_t tx = npc.targetX, tz = npc.targetZ;
    turnToward(actor.rotation.y,
               angleFromPoint(actor, int32_t(uint32_t(int32_t(tx)) << 16), int32_t(uint32_t(int32_t(tz)) << 16)),
               actor.spin.y);
    npcMove(actor);
    if (uint32_t(int(tx) - int(actor.pos.x.whole) + kArrive) < uint32_t(2 * kArrive) &&
        uint32_t(int(tz) - int(actor.pos.z.whole) + kArrive) < uint32_t(2 * kArrive)) {
        const uint32_t r = rngStep();
        if (int32_t(kPauseChance << (r & 0x1f)) < 0 && (npc.behaviour & kNpcMayPause)) {
            npcNextWaypoint(npc.route, npc.waypoint, npc.previousWaypoint);
            actor.enterStep(uint8_t(RouteStep::Pause));
            return;
        }
        actor.enterStep(uint8_t(RouteStep::NextWaypoint));
    }
    if (npc.collisionBits & kNpcBlocked) actor.enterStep(uint8_t(RouteStep::WallTurn));
}
// exe 0x441f10: the next waypoint, then a new target
void routeNext(AnimatedActor& actor, TownNpcFields& npc) {
    npcNextWaypoint(npc.route, npc.waypoint, npc.previousWaypoint);
    actor.enterStep(uint8_t(RouteStep::Target));
    npcMove(actor);
}
// exe 0x441f40: turn (twice the turn speed) toward a point off the wall it hit (or ahead when an actor blocked it),
// walking on, until it is no longer blocked
void routeWallTurn(AnimatedActor& actor, TownNpcFields& npc) {
    if (actor.stepCounter == 0) {
        npc.bits = kNpcSolid | kNpcMover;
        int32_t px, pz; // a point beside the NPC: off the wall it hit, else ahead
        if (actor.actorHit == 0u && actor.lastMover == nullptr) {
            const uint32_t c = actor.wallHit;
            pz = 0;
            px = 0;
            if (c & kWallOnXPlus) px = -kWallTurnReach;
            if (c & kWallOnXMinus) px = kWallTurnReach;
            if (c & kWallOnZPlus) pz = -kWallTurnReach;
            if (c & kWallOnZMinus) pz = kWallTurnReach;
        } else {
            npc.bits = kNpcMover;
            const int y = uint16_t(actor.rotation.y) & kAngleMask;
            pz = fixedCos(y) << 12;
            px = fixedSin(y) << 12;
        }
        px = int32_t(uint32_t(px) + uint32_t(actor.pos.x));
        pz = int32_t(uint32_t(pz) + uint32_t(actor.pos.z));
        const int d = angleDiff(uint32_t(actor.rotation.y), uint32_t(angleFromPoint(actor, px, pz)));
        int16_t turn = npcSpeeds[actor.variant].turn;
        if (d < 0) turn = int16_t(-turn);
        actor.spin.y = wrap16(turn * 2);
        ++actor.stepCounter;
    }
    npcMove(actor);
    actor.rotation.y = wrap16(actor.rotation.y + actor.spin.y);
    if (!(npc.collisionBits & kNpcBlocked)) actor.enterStep(uint8_t(RouteStep::Walk));
}
// exe 0x442050: a pause at a waypoint (the table's ticks), watching the player; then face the player
void routePause(AnimatedActor& actor, TownNpcFields& npc) {
    const WatchStep step = WatchStep(actor.stepCounter);
    if (step == WatchStep::Start) {
        actor.anim = kAnimStand;
        npc.bits = kNpcNoBump | kNpcSolid;
        actor.stepCounter = uint8_t(WatchStep::LookAhead);
        const uint32_t r = rngStep();
        npc.timer = pauseTicks[r & 0xf];
    }
    if (step == WatchStep::Start || step == WatchStep::LookAhead) {
        npc.lookMode = uint8_t(NpcLook::Ahead);
        if (npcPlayerInFront(actor, kPlayerNear, kFrontWindow)) actor.stepCounter = uint8_t(WatchStep::LookAtPlayer);
    } else if (step == WatchStep::LookAtPlayer) {
        npc.lookMode = uint8_t(NpcLook::Player);
        if (npcPlayerGone(actor)) {
            npc.headHold = 0;
            actor.stepCounter = uint8_t(WatchStep::LookAhead);
        }
    }
    NpcHead head = npc.head();
    npcHoldHead(head);
    --npc.timer;
    if (npc.timer == 0) {
        actor.subState = uint8_t(NpcSub::FacePlayer);
        actor.enterStep(0);
    }
}
// exe 0x441cb0: the route's step, the partner look, and the player close in front -> face it
void subRoute(AnimatedActor& actor, TownNpcFields& npc) {
    switch (RouteStep(actor.step)) {
    case RouteStep::Target: routeTarget(actor, npc); break;
    case RouteStep::Walk: routeWalk(actor, npc); break;
    case RouteStep::NextWaypoint: routeNext(actor, npc); break;
    case RouteStep::WallTurn: routeWallTurn(actor, npc); break;
    case RouteStep::Pause: routePause(actor, npc); break;
    default: notPortedAt(kTownNpcPort, "route step", actor.step);
    }
    NpcHead head = npc.head();
    if (npc.partnerSlot == 0) {
        if (npc.lookMode == uint8_t(NpcLook::Partner)) npcPickLook(npc.behaviour, head, stateAddress(TableId::TownNpcLookSets));
    } else {
        npcPartnerCheck(actor, head);
    }
    if (distance3(player().pos, actor.pos) < kPlayerNear) {
        if (uint32_t(playerAngleDiff(actor) + int(kFrontWindow / 2)) < kFrontWindow) {
            actor.subState = uint8_t(NpcSub::FacePlayer);
            actor.enterStep(0);
        }
    }
}
// exe 0x4421a0: the plain stationary kind: stands, looks at the player while it is close in front, else at the partner
void standPlain(AnimatedActor& actor, TownNpcFields& npc) {
    NpcHead head = npc.head();
    const WatchStep step = WatchStep(actor.step);
    if (step == WatchStep::Start) {
        actor.anim = kAnimStand;
        npc.bits = kNpcNoBump | kNpcSolid;
        actor.enterStep(uint8_t(WatchStep::LookAhead));
    } else if (step != WatchStep::LookAhead) {
        if (step == WatchStep::LookAtPlayer) {
            npc.lookMode = uint8_t(NpcLook::Player);
            if (npcPlayerGone(actor)) {
                npc.headHold = 0;
                actor.enterStep(uint8_t(WatchStep::LookAhead));
            }
        }
        npcHoldHead(head);
        return;
    }
    npc.lookMode = uint8_t(NpcLook::Ahead);
    if (npcPlayerInFront(actor, kPlayerNear, kFrontWindow)) {
        actor.enterStep(uint8_t(WatchStep::LookAtPlayer));
        npcHoldHead(head);
        return;
    }
    if (npc.partnerSlot) npcPartnerCheck(actor, head);
    npcHoldHead(head);
}
// exe 0x4422a0: the fidget stationary kind: random look modes, patterns of idle animations (3 / 5)
void standFidget(AnimatedActor& actor, TownNpcFields& npc) {
    auto newLook = [&] {
        uint32_t r = rngStep();
        npc.lookMode = fidgetLooks[r & 7];
        r = rngStep();
        npc.fidgetLookTicks = int16_t(fidgetLookTicks[r & 7]);
    };
    switch (FidgetStep(actor.step)) {
    case FidgetStep::Start:
        actor.anim = kAnimStand;
        npc.bits = kNpcNoBump | kNpcSolid;
        newLook();
        actor.enterStep(uint8_t(FidgetStep::NewPattern));
        [[fallthrough]];
    case FidgetStep::NewPattern: {
        actor.step = uint8_t(FidgetStep::NextAnimation);
        const uint32_t r = rngStep();
        const uint16_t pattern = fidgetPatterns[r & 3];
        npc.timer = kFidgetRepeats;
        npc.fidgetPattern = int16_t(pattern);
        [[fallthrough]];
    }
    case FidgetStep::NextAnimation:
        actor.step = uint8_t(FidgetStep::Animating);
        actor.anim = npc.fidgetPattern < 0 ? kAnimFidget : kAnimStand;
        break;
    case FidgetStep::Animating:
        if (actor.animKey.flags != 0) {
            if (actor.anim == kAnimFidget) {
                actor.anim = kAnimSettle;
            } else {
                npc.fidgetPattern <<= 1;
                --npc.timer;
                // the repeats left -> the next animation, else a new pattern
                actor.step = uint8_t(npc.timer != 0 ? FidgetStep::NextAnimation : FidgetStep::NewPattern);
            }
        }
        break;
    default: break;
    }
    --npc.fidgetLookTicks;
    if (npc.fidgetLookTicks == 0) newLook();
}
// exe 0x442180: the stationary kind's routine
void subStationary(AnimatedActor& actor, TownNpcFields& npc) {
    switch (StandKind(npc.behaviour & kNpcKindMask)) {
    case StandKind::Plain: standPlain(actor, npc); break;
    case StandKind::Fidget: standFidget(actor, npc); break;
    case StandKind::Still: npcStandStill(actor, npc); break;
    default: notPortedAt(kTownNpcPort, "stationary kind (table 0x88dc80 has 3 entries)", npc.behaviour & kNpcKindMask);
    }
}
// exe 0x444c60: the event runs (the speaker animations its message asks for), then the settle animation
void subTalk(AnimatedActor& actor, TownNpcFields& npc) {
    EventState& event = eventState();
    const TalkStep step = TalkStep(actor.step);
    if (step == TalkStep::Start) {
        npc.bits = kNpcNoTalk | kNpcNoBump | kNpcSolid;
        actor.anim = kAnimStand;
        event.speakerAnimation = kNoSpeakerAnimation;
        npc.lookMode = uint8_t((npc.behaviour & kNpcFacesSpeaker) ? NpcLook::Player : NpcLook::Ahead);
        npc.headPitchTarget = 0;
        actor.enterStep(uint8_t(TalkStep::Talking));
    } else if (step != TalkStep::Talking) {
        if (step != TalkStep::Settling) return;
        if (!actor.animEnded()) return;
        npc.headHold = kHeadHoldTicks;
        actor.subState = uint8_t(NpcSub::FacePlayer);
        actor.enterStep(0);
        return;
    }
    if (event.speakerAnimation < kSpeakerAnimationCount) {
        actor.anim = event.speakerAnimation;
        event.speakerAnimation = kNoSpeakerAnimation;
    } else if (actor.animKey.flags == kKeyHold) {
        actor.anim = kAnimTalk;
    }
    if (event.active == 0) {
        actor.anim = kAnimSettle;
        actor.enterStep(uint8_t(TalkStep::Settling));
    }
}
// exe 0x442470: stands facing the player until it is gone, then back to the route / standing
void subFacePlayer(AnimatedActor& actor, TownNpcFields& npc) {
    if (actor.step == 0) {
        actor.anim = kAnimStand;
        npc.lookMode = uint8_t((npc.behaviour & kNpcFacesSpeaker) ? NpcLook::Player : NpcLook::Ahead);
        npc.bits = kNpcSolid;
        actor.enterStep(1);
    }
    NpcHead head = npc.head();
    npcHoldHead(head);
    if (npcPlayerGone(actor)) {
        npc.headHold = 0;
        npcBackToIdle(actor, npc);
    }
}
// exe 0x4428a0: the mover that hit it is the player
bool bumpedByPlayer(const Actor& actor) { return actorSlotClass(actor.lastMover->slot) == kPlayerClass; }
// exe 0x442510: bumped -- stands (looking at the player when it bumped it from the air or it is the player) for
// kBumpedTicks, again when the player bumps it again, until the player is gone
void subBumped(AnimatedActor& actor, TownNpcFields& npc) {
    if (actor.step == 0) {
        actor.anim = kAnimStand;
        if (npc.bumpedBy == nullptr) notPorted(kTownNpcPort, "bumped by no actor (the exe reads address 0x50)");
        const Actor& by = *npc.bumpedBy;
        if (by.airborne != 0 || by.slot == kPlayerClass)
            npc.lookMode = uint8_t((npc.behaviour & kNpcFacesSpeaker) ? NpcLook::Player : NpcLook::Ahead);
        npc.bits = kNpcNoBump | kNpcSolid;
        npc.timer = kBumpedTicks;
        actor.enterStep(1);
    }
    if (actor.lastMover != nullptr && bumpedByPlayer(actor)) {
        npc.bumpedBy = actor.lastMover;
        actor.enterStep(0);
    }
    if (npc.timer != 0) --npc.timer;
    if (npc.timer == 0 && uint32_t(kPlayerGone) < uint32_t(distance3(player().pos, actor.pos))) {
        npc.headHold = 0;
        npcBackToIdle(actor, npc);
    }
}
// exe 0x441bf0: the bump and talk tests, the sub-state, the head
void npcMain(AnimatedActor& actor, TownNpcFields& npc) {
    if (!(npc.bits & kNpcNoBump) && actor.lastMover != nullptr && bumpedByPlayer(actor)) {
        npc.bumpedBy = actor.lastMover;
        actor.subState = uint8_t(NpcSub::Bumped);
        actor.enterStep(0);
    }
    if (!(npc.bits & kNpcNoTalk)) {
        if (player().talksTo(actor) && eventState().active == 0) {
            npcStartTalk(actor, npc.behaviour, npc.message);
            actor.subState = uint8_t(NpcSub::Talk);
            actor.enterStep(0);
        }
    }
    switch (NpcSub(actor.subState)) {
    case NpcSub::Route: subRoute(actor, npc); break;
    case NpcSub::Stationary: subStationary(actor, npc); break;
    case NpcSub::Talk: subTalk(actor, npc); break;
    case NpcSub::FacePlayer: subFacePlayer(actor, npc); break;
    case NpcSub::Bumped: subBumped(actor, npc); break;
    default: notPortedAt(kTownNpcPort, "sub-state", actor.subState);
    }
    NpcHead head = npc.head();
    npcLook(actor, head);
    npcHeldHeadTick(head);
    npcTurnHead(head);
}
// exe 0x441af0: the model (variant +6), the boxes (none to talk without a message), the class block cleared, the
// partner: the first live type-0 actor of the pool whose route is this one's partner route; then back to idle
void npcInit(AnimatedActor& actor, TownNpcFields& npc) {
    actor.enterState(uint8_t(NpcState::Main));
    if (!bindActorModel(actor, actor.variant)) {
        freeActor(actor);
        return;
    }
    const uint8_t message = npc.message;
    actor.prevAnim = kNoAnim;
    actor.anim = kAnimStand;
    actor.velocity.clearWords();
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.actorHit = 0;
    actor.lastMover = nullptr;
    actor.collisionBox.pointAt(npcCollisionBox.stateAddress());
    if (message == kNpcNoMessage) {
        actor.interactionBox = nullptr;
        actor.interaction = 0;
    } else {
        actor.interactionBox.pointAt(((npc.behaviour & kNpcSmallTalkBox) ? npcSmallTalkBox : npcTalkBox).stateAddress());
        actor.interaction = kInteractTalk;
    }
    actor.clearClassBlock();
    if (uint8_t(npc.route) != npc.partnerRoute) {
        uint32_t mask = actorSystem().animatedLive;
        if (mask != 0) {
            size_t i = 0;
            while (int32_t(mask) >= 0 || animatedPool()[i].type != 0 ||
                   uint8_t(classState<TownNpcFields>(animatedActorAt(animatedPool()[i].at)).route) != npc.partnerRoute) {
                ++i;
                mask <<= 1;
                if (mask == 0) {
                    actor.enterState(uint8_t(NpcState::Main));
                    npcBackToIdle(actor, npc);
                    return;
                }
            }
            npc.partnerSlot = animatedPool()[i].slot;
        }
    }
    actor.enterState(uint8_t(NpcState::Main));
    npcBackToIdle(actor, npc);
}
// exe 0x441950's body: the state, then the room collision, the ground, the animation, the hit entry and the actor
// collision queues
void townNpc(AnimatedActor& actor) {
    TownNpcFields& npc = classState<TownNpcFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    if ((actor.actorHit & kSolidOnSides) || actor.lastMover != nullptr) npc.collisionBits |= kNpcBlocked | kNpcHitActor;
    switch (NpcState(actor.state)) {
    case NpcState::Init: npcInit(actor, npc); break;
    case NpcState::Main: npcMain(actor, npc); break;
    case NpcState::Inert:
    case NpcState::InertCopy: break;
    default: notPortedAt(kTownNpcPort, "state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    npc.collisionBits = 0;
    uint32_t r = actorTestMove(*E, actor);
    if (r & kMoveNoCell) npc.collisionBits |= kNpcOffGrid;
    if (r & kMoveBlocked) {
        r = actorPushOut(*E, actor);
        actor.wallHit = r;
        if (r & kMoveBlocked) actor.pos = actor.prevPos;
        if (r & kWallOnSides) npc.collisionBits |= kNpcBlocked;
    }
    actor.pos.y.whole = int16_t(actorGround(*E, actor));
    NpcHead head = npc.head();
    npcAnimate(actor, head);
    npc.hitBox.result = 0;
    npc.hitBox.previousX = actor.prevPos.x.whole;
    npc.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t pos[3] = {actor.pos.x.whole, wrap16(actor.pos.y.whole - npcSpeeds[actor.variant].height),
                            actor.pos.z.whole};
    hitListSubmit(pos, static_cast<HitBoxHead&>(npc.hitBox), hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | hitRadius(0x40),
                  kHitAttackHarmless);
    // exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow list entry (render output)
    const uint8_t bits = npc.bits;
    if (bits & kNpcSolid) actorCollisionSubmitSolid(actor, SolidType::PushOut, kNoBone, 0);
    else actor.lastMover = nullptr;
    if (bits & kNpcMover) {
        actorCollisionSubmitMover(actor);
        return;
    }
    actor.actorHit = 0;
}

} // namespace

void npcBackToIdle(Actor& actor, TownNpcFields& npc) {
    actor.subState = (npc.behaviour & kNpcStationary) ? uint8_t(NpcSub::Stationary) : uint8_t(NpcSub::Route);
    actor.enterStep(0);
}

void npcStandStill(AnimatedActor& actor, TownNpcFields& npc) {
    if (actor.step == 0) {
        actor.anim = kAnimStand;
        npc.bits = kNpcNoBump | kNpcSolid;
        npc.lookMode = uint8_t(NpcLook::Ahead);
        actor.enterStep(1);
    }
    NpcHead head = npc.head();
    npcHoldHead(head);
}

Waypoint routeWaypoint(int8_t route, int8_t index) {
    const uint32_t waypoints = dataRecord<RouteTable>(stage().townRoutes, RouteTable::kBytes).routes[size_t(int32_t(route))];
    return dataRecord<Waypoint>(waypoints + uint32_t(int32_t(index)) * 8, Waypoint::kBytes);
}

void npcLookPoint(uint32_t slot, int16_t out[3]) {
    const uint32_t cls = actorSlotClass(slot);
    const int sh = stage().coordinateShift & 0x1f;
    const Actor* act;
    if (cls == kPlayerClass) {
        act = &player();
        out[1] = int16_t(player().bonePoints[1].y >> sh);
    } else if (cls == kAnimatedClass) {
        const AnimatedActor& a = animatedPool()[slot - kAnimatedClass];
        act = &a;
        out[1] = int16_t(bonePoint(a, 1, kTownNpcPort).y >> sh);
    } else {
        notPortedAt(kTownNpcPort, "exe 0x4431d0 slot class", cls);
    }
    out[1] = int16_t(out[1] & kLookPointHeightMask);
    out[0] = act->pos.x.whole;
    out[2] = act->pos.z.whole;
}

bool npcPlayerGone(const Actor& actor) {
    const int dist = distance3(player().pos, actor.pos);
    return kPlayerGone < dist || kGoneWindow < uint32_t(playerAngleDiff(actor) + int(kGoneWindow / 2));
}

bool npcPlayerInFront(const Actor& actor, int dist, uint32_t window) {
    if (distance3(player().pos, actor.pos) >= dist) return false;
    return uint32_t(playerAngleDiff(actor) + int(window / 2)) < window;
}

void npcNextWaypoint(int8_t route, int8_t& waypoint, uint8_t& previous) {
    const Waypoint wp = routeWaypoint(route, waypoint);
    int count = 0; // the links in use (bit 7 clear), from the first
    for (int k = 3; k >= 0; --k) count += int(uint8_t(~wp.links[size_t(k)]) >> 7);
    uint8_t next;
    if (count < 2) {
        next = wp.links[0];
    } else {
        const uint32_t r = rngStep();
        int k = int(int64_t(r & 0xffff) % count);
        if (wp.links[size_t(k)] == previous) k = (k + 1) % count;
        next = wp.links[size_t(k)];
    }
    const uint8_t prev = uint8_t(waypoint);
    waypoint = int8_t(next);
    previous = prev;
}

void npcPickLook(uint8_t behaviour, NpcHead& head, uint32_t lookSets) {
    if (behaviour & kNpcStationary) return;
    const uint32_t kind = behaviour & kNpcKindMask;
    const uint32_t look = kind & kNpcLookKindMask;
    if (look == kNpcLooksAhead) {
        head.lookMode = uint8_t(NpcLook::Ahead);
    } else if (look != kNpcKeepsLook) {
        const uint32_t r = rngStep();
        const uint32_t set = dataRecord<LookSets>(lookSets, LookSets::kBytes).sets[kind];
        if (!set) notPortedAt(kTownNpcPort, "null look-mode set of the kind", kind);
        head.lookMode = dataRecord<LookSet>(set, LookSet::kBytes).modes[r & 7];
    }
}

void npcPartnerCheck(const Actor& actor, NpcHead& head) {
    int16_t self[3], other[3];
    npcLookPoint(actor.slot, self);
    npcLookPoint(head.partnerSlot, other);
    if (planarDistance16(self, other) >= kPartnerNear) return;
    const int d = angleDiff(uint32_t(actor.rotation.y), uint32_t(angleOf(other, self)));
    if (uint32_t(d + int(kLookWindow / 2)) < kLookWindow) head.lookMode = uint8_t(NpcLook::Partner);
}

void npcLookYaw(const Actor& actor, int8_t& yawTarget, const int16_t v[3]) {
    const int ang = atanAngle(float(int(actor.pos.x.whole) - int(v[0])), float(int(actor.pos.z.whole) - int(v[2])));
    int d = angleDiff(uint32_t(actor.rotation.y), uint32_t(ang));
    if (kHeadYawRange < d) {
        yawTarget = int8_t(kHeadYawRange >> 4);
        return;
    }
    if (d < -kHeadYawRange) d = -kHeadYawRange;
    yawTarget = int8_t(d >> 4);
}

void npcLookPitch(const AnimatedActor& actor, NpcHead& head, const int16_t v[3]) {
    const int dy = int(bonePoint(actor, 1, kTownNpcPort).y) - int(v[1]);
    int p = 0;
    if (kHeadPitchDeadZone < dy) p = -kHeadPitchTilt;
    if (dy < -kHeadPitchDeadZone) p = kHeadPitchTilt;
    head.pitchTarget = int8_t(p >> 4);
}

void npcLook(const AnimatedActor& actor, NpcHead& head) {
    int16_t v[3] = {head.targetX, 0, head.targetZ};
    // the game's point for modes 3 / 4: x = z = the target's x << 16 (it stores the same value twice)
    const int32_t p = int32_t(uint32_t(int32_t(head.targetX)) << 16);
    int d;
    switch (NpcLook(head.lookMode)) {
    case NpcLook::Ahead:
        head.pitchTarget = 0;
        head.yawTarget = 0;
        return;
    case NpcLook::Player:
        npcLookPoint(kPlayerClass, v); // slot 0: the player
        npcLookYaw(actor, head.yawTarget, v);
        npcLookPitch(actor, head, v);
        return;
    case NpcLook::TargetYaw:
        head.pitchTarget = 0;
        d = angleDiff(uint32_t(actor.rotation.y), uint32_t(angleFromPoint(actor, p, p)));
        break;
    case NpcLook::TargetLevel:
        v[1] = actor.pos.y.whole;
        npcLookPitch(actor, head, v);
        d = angleDiff(uint32_t(actor.rotation.y), uint32_t(angleFromPoint(actor, p, p)));
        break;
    case NpcLook::Partner:
    case NpcLook::PartnerLevel:
        if (!head.partnerSlot) return;
        npcLookPoint(head.partnerSlot, v);
        if (head.lookMode == uint8_t(NpcLook::PartnerLevel)) v[1] = actor.pos.y.whole;
        npcLookYaw(actor, head.yawTarget, v);
        npcLookPitch(actor, head, v);
        return;
    case NpcLook::Down:
        head.pitchTarget = int8_t(-(kHeadPitchTilt >> 4));
        head.yawTarget = 0;
        return;
    case NpcLook::Up:
        head.pitchTarget = int8_t(kHeadPitchTilt >> 4);
        head.yawTarget = 0;
        return;
    default: return;
    }
    if (uint32_t(d + int(kLookWindow / 2)) < kLookWindow) {
        npcLookYaw(actor, head.yawTarget, v);
        return;
    }
    head.yawTarget = 0;
}

void npcHoldHead(NpcHead& head) {
    if (head.hold) head.hold = kHeadHoldTicks;
}

void npcHeldHeadTick(NpcHead& head) {
    if (head.hold != 0) {
        head.yawTarget = 0;
        --head.hold;
        head.pitchTarget = 0;
    }
}

void npcEaseHead(int8_t& angle, const int8_t& target, int max) {
    int d = angleDiff(uint32_t(int32_t(angle)), uint32_t(int32_t(target)));
    if (uint32_t(d + max) > uint32_t(2 * max)) {
        if (0 < d) {
            angle = int8_t(angle + max);
            return;
        }
        if (d < 0) d = -max;
    }
    angle = int8_t(angle + d);
}

void npcTurnHead(NpcHead& head) {
    npcEaseHead(head.yaw, head.yawTarget, kHeadEaseYaw);
    npcEaseHead(head.pitch, head.pitchTarget, kHeadEasePitch);
}

void npcAnimate(AnimatedActor& actor, const NpcHead& head) {
    if (actor.anim == actor.prevAnim) animStep(actor);
    else animSet(actor, actor.anim, 0);
    auto&& bone1 = decodedFrame().entries[2]; // bone 1's decoded angles
    bone1.x = int16_t(uint16_t(wrap16(int(head.pitch) << 4) + uint16_t(bone1.x)));
    bone1.y = int16_t(uint16_t(wrap16(int(head.yaw) << 4) + uint16_t(bone1.y)));
    animPose(actor);
}

void npcStartTalk(Actor& actor, uint8_t behaviour, uint8_t message) {
    const uint32_t param = (behaviour & kNpcEventFlag) ? kTalkFlagParam : 0;
    eventStart(&actor.pos, &actor.rotation, param, 0, message); // exe 0x543af0
}

void townNpcUpdate(const PlayerEnv& env, uint32_t address) {
    E = &env;
    townNpc(animatedActorAt(address));
}

} // namespace od::port
