// The ST24 route-walker NPC, pool-0x20 class exe 0x4440f0 (ST24 sub 0; transcribed from dash2.exe one function per
// exe function). A sibling of the town NPC exe 0x441950 (game/town_npc): the same route / stationary / talk /
// face-the-player logic with its own tables, plus the spawn byte's bits (kNoMoveTest, kNoGround: no ground snap /
// shadow / solids) and its scale. Its run routine has no bump test, so the bumped sub-state is never entered by the
// class itself.
//
// States [0x88e088] (14 entries; 4.. run into the sub table 0x88e098): 0 bind 0x4442a0, 1 run 0x4443b0, 2 / 3
// 0x52ef00 (ret). Subs [0x88e098] (10 entries; 5.. run into the step table 0x88e0ac): 0 route 0x444440, 1 stationary
// 0x4449b0, 2 talk 0x444c60 (shared with 0x441950), 3 face the player 0x444d20, 4 bumped 0x444dc0. Route steps
// [0x88e0ac] (5 entries): 0 target 0x444550, 1 walk 0x4445a0, 2 next waypoint 0x4446a0, 3 turn off a wall 0x444770,
// 4 pause 0x444880. Stationary kinds [0x88e0e0] by the behaviour's kind (3 entries): 0 plain 0x4449d0, 1 fidget
// 0x444ad0, 2 still 0x442430 (shared with 0x441950).
//
// Its fields are the town NPC's (game/town_npc TownNpcFields). Render output skipped: the shadow exe 0x5f20f0(actor,
// 0x30, 0x160, 0) (not submitted with kNoGround).
//
// The exe functions it shares with the town NPC (exe 0x4431d0, 0x442430, 0x444080, 0x4446d0, 0x445150, 0x445170,
// 0x4451f0, 0x445230 / 0x445280, the look 0x444e80 = 0x4425d0's logic) run from game/town_npc on the same fields.
#include "content/calbania/st24_npc.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/address_forms.hpp"
#include "game/angles.hpp"
#include "game/distances.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/town_npc.hpp"
#include "game/vectors.hpp"

namespace od::port {

namespace {

using address_forms::E;

// the NPC's speeds by its variant (exe table 0x88de38, 8 bytes each)
struct NpcSpeeds : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    int16_t walk = 0; // +0x0
    int16_t run = 0; // +0x2
    int16_t turn = 0; // +0x4
    int16_t hitHeight = 0; // +0x6
    template <class R> static NpcSpeeds load(const R& r) {
        NpcSpeeds v;
        v.walk = r.template read<int16_t>(0x0);
        v.run = r.template read<int16_t>(0x2);
        v.turn = r.template read<int16_t>(0x4);
        v.hitHeight = r.template read<int16_t>(0x6);
        return v;
    }
};
NpcSpeeds speedsOf(const AnimatedActor& actor) { return dataRecord<NpcSpeeds>(stateAddress(TableId::St24NpcSpeedsByVariant) + uint32_t(actor.variant) * 8, NpcSpeeds::kBytes); }
const DataTable<ScalarTable<uint16_t, 0x10>> pauseTicks{TableId::St24NpcPauseTicks};
const DataTable<ScalarTable<uint8_t, 8>> fidgetLooks{TableId::St24NpcFidgetLooks};
const DataTable<ScalarTable<uint16_t, 8>> fidgetLookTicks{TableId::St24NpcFidgetLookTicks};
const DataTable<ScalarTable<uint16_t, 4>> fidgetPatterns{TableId::St24NpcFidgetPatterns};

// the spawn byte (+7): the scale's steps and the motion bits
constexpr uint8_t kScaleStepsMask = 0x3f;
constexpr uint8_t kNoMoveTest = 0x40;
constexpr uint8_t kNoGround = 0x80;

enum class RouteNpcState : uint8_t { Bind = 0, Run = 1, Idle = 2, IdleToo = 3 };
enum class RouteNpcSub : uint8_t { Route = 0, Stationary = 1, Talk = 2, FacePlayer = 3, Bumped = 4 };
enum class RouteStep : uint8_t { Target = 0, Walk = 1, Next = 2, WallTurn = 3, Pause = 4 };
enum class StationaryKind : uint8_t { Plain = 0, Fidget = 1, Still = 2 };

// the NPC's bits by what it does (TownNpcFields::bits)
constexpr uint8_t kWalkingBits = kNpcSolid | kNpcMover;       // 0xc
constexpr uint8_t kStandingBits = kNpcNoBump | kNpcSolid;     // 6
constexpr uint8_t kTalkingBits = kNpcNoTalk | kNpcNoBump | kNpcSolid; // 7
constexpr uint8_t kWallTurnBits = kNpcMover;                  // 8: turning off an actor
constexpr uint8_t kFacingBits = kNpcSolid;                    // 4
constexpr uint8_t kHeadHoldTicks = 0x3c;
constexpr uint8_t kAnimIdle = 0, kAnimWalk = 1, kAnimRun = 2, kAnimFidget = 3, kAnimTalk = 4, kAnimSettle = 5;
constexpr uint8_t kNoAnim = 0xff;
constexpr uint32_t kNpcHitOwner = 0x400010u, kNpcHitAttack = 0x800000u;
constexpr uint32_t kTestMoveBlocked = 0x20000000;
constexpr uint32_t kWallHitMask = 0xf00;
constexpr uint32_t kWallLeft = 0x100, kWallRight = 0x200, kWallBack = 0x400, kWallFront = 0x800;

int16_t t16(int v) { return int16_t(uint16_t(uint32_t(v))); }

// exe 0x5e3bc0(p, position) with p a 16.16 point on the exe's stack (x, z): the position seen from p
int angleFrom(int32_t px, int32_t pz, const Fixed16Vec3& to) { return angleOf(px, pz, to.x, to.z); }
// the velocity's forward lane (exe 0x5e3690)
void walkForward(AnimatedActor& actor) { integrateVelocity(actor, 0, 0, actor.velocity.z); }

// exe 0x4450b0 (byte table 0x445140: kinds 0..3 walk, 4..7 run)
void startWalk(AnimatedActor& actor, TownNpcFields& npc) {
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.velocity.z = 0;
    actor.spin.y = 0;
    const NpcSpeeds v = speedsOf(actor);
    if ((npc.behaviour & kNpcKindMask) < kNpcRunKinds) {
        actor.anim = kAnimWalk;
        actor.velocity.z = int16_t(-v.walk);
        actor.spin.y = v.turn;
    } else {
        actor.anim = kAnimRun;
        actor.velocity.z = int16_t(-v.run);
        actor.spin.y = int16_t(v.turn << 1);
    }
}

// ---- route steps [0x88e0ac] -----------------------------------------------------------------------------------
void routeWalk(AnimatedActor& actor, TownNpcFields& npc);
// exe 0x444550: the waypoint's point, a look mode, then the walk at once
void routeTarget(AnimatedActor& actor, TownNpcFields& npc) {
    npc.bits = kWalkingBits;
    const Waypoint wp = routeWaypoint(npc.route, npc.waypoint);
    npc.targetX = wp.x;
    npc.targetZ = wp.z;
    NpcHead head = npc.head();
    npcPickLook(npc.behaviour, head, stateAddress(TableId::St24NpcLookSets)); // exe 0x445040
    actor.enterStep(uint8_t(RouteStep::Walk));
    routeWalk(actor, npc);
}
// exe 0x4445a0: toward the waypoint; there: the next one (or a pause)
void routeWalk(AnimatedActor& actor, TownNpcFields& npc) {
    if (actor.stepCounter == 0) {
        startWalk(actor, npc); // exe 0x4450b0
        npc.bits = kWalkingBits;
        actor.stepCounter = 1;
    }
    const int16_t tx = npc.targetX, tz = npc.targetZ;
    turnToward(actor.rotation.y,
               int(angleFrom(int32_t(uint32_t(int32_t(tx)) << 16), int32_t(uint32_t(int32_t(tz)) << 16), actor.pos)),
               actor.spin.y); // exe 0x5e3bc0 / 0x5e3db0
    walkForward(actor);
    if (uint32_t(int(tx) - int(actor.pos.x.whole) + 0x80) < 0x100u &&
        uint32_t(int(tz) - int(actor.pos.z.whole) + 0x80) < 0x100u) {
        const uint32_t r = rngStep();
        if (int32_t(0x5500aau << (r & 0x1f)) < 0 && (npc.behaviour & kNpcMayPause)) {
            npcNextWaypoint(npc.route, npc.waypoint, npc.previousWaypoint); // exe 0x4446d0
            actor.enterStep(uint8_t(RouteStep::Pause));
            return;
        }
        actor.enterStep(uint8_t(RouteStep::Next));
    }
    if (npc.collisionBits & kNpcBlocked) actor.enterStep(uint8_t(RouteStep::WallTurn));
}
// exe 0x4446a0
void routeNext(AnimatedActor& actor, TownNpcFields& npc) {
    npcNextWaypoint(npc.route, npc.waypoint, npc.previousWaypoint); // exe 0x4446d0
    actor.enterStep(uint8_t(RouteStep::Target));
    walkForward(actor);
}
// exe 0x444770: turned away from the wall (or the actor) it walked into
void routeWallTurn(AnimatedActor& actor, TownNpcFields& npc) {
    if (actor.stepCounter == 0) {
        npc.bits = kWalkingBits;
        int32_t px, pz;
        if (actor.actorHit == 0 && !actor.lastMover) {
            const uint32_t c = actor.wallHit;
            px = 0;
            pz = 0;
            if (c & kWallLeft) px = int32_t(0xff000000u);
            if (c & kWallRight) px = 0x1000000;
            if (c & kWallBack) pz = int32_t(0xff000000u);
            if (c & kWallFront) pz = 0x1000000;
        } else {
            npc.bits = kWallTurnBits;
            const int yaw = uint16_t(actor.rotation.y);
            pz = int32_t(uint32_t(int32_t(fixedCos(yaw))) << 12);
            px = int32_t(uint32_t(int32_t(fixedSin(yaw))) << 12);
        }
        px = int32_t(uint32_t(px) + uint32_t(actor.pos.x));
        pz = int32_t(uint32_t(pz) + uint32_t(actor.pos.z));
        const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleFrom(px, pz, actor.pos)));
        int turn = speedsOf(actor).turn;
        if (d < 0) turn = -turn;
        actor.spin.y = int16_t(turn * 2);
        ++actor.stepCounter;
    }
    walkForward(actor);
    actor.rotation.y += uint16_t(actor.spin.y);
    if (!(npc.collisionBits & kNpcBlocked)) actor.enterStep(uint8_t(RouteStep::Walk));
}
// exe 0x444880: a pause at the waypoint, the player watched; then the face-the-player sub-state
void routePause(AnimatedActor& actor, TownNpcFields& npc) {
    switch (WatchStep(actor.stepCounter)) {
    case WatchStep::Start:
        actor.anim = kAnimIdle;
        npc.bits = kStandingBits;
        actor.stepCounter = uint8_t(WatchStep::LookAhead);
        npc.timer = int16_t(pauseTicks[rngStep() & 0xf]);
        [[fallthrough]];
    case WatchStep::LookAhead:
        npc.lookMode = uint8_t(NpcLook::Ahead);
        if (npcPlayerInFront(actor, 0x100, 0x400)) actor.stepCounter = uint8_t(WatchStep::LookAtPlayer);
        break;
    case WatchStep::LookAtPlayer:
        npc.lookMode = uint8_t(NpcLook::Player);
        if (npcPlayerGone(actor)) {
            npc.headHold = 0;
            actor.stepCounter = uint8_t(WatchStep::LookAhead);
        }
        break;
    default: break;
    }
    NpcHead head = npc.head();
    npcHoldHead(head);
    npc.timer = int16_t(uint16_t(npc.timer) - 1u);
    if (npc.timer == 0) {
        actor.subState = uint8_t(RouteNpcSub::FacePlayer);
        actor.enterStep(0);
    }
}

// ---- sub-states [0x88e098] --------------------------------------------------------------------------------------
// exe 0x444440: the route's step, the partner / look, the player in front -> face it
void subRoute(AnimatedActor& actor, TownNpcFields& npc) {
    switch (RouteStep(actor.step)) { // table 0x88e0ac
    case RouteStep::Target: routeTarget(actor, npc); break;
    case RouteStep::Walk: routeWalk(actor, npc); break;
    case RouteStep::Next: routeNext(actor, npc); break;
    case RouteStep::WallTurn: routeWallTurn(actor, npc); break;
    case RouteStep::Pause: routePause(actor, npc); break;
    default: notPortedAt(kStoryPort, "0x444440 route step (table 0x88e0ac runs into data)", actor.step);
    }
    NpcHead head = npc.head();
    if (npc.partnerSlot != 0) npcPartnerCheck(actor, head);                                       // exe 0x4444d0
    else if (NpcLook(npc.lookMode) == NpcLook::Partner) npcPickLook(npc.behaviour, head, stateAddress(TableId::St24NpcLookSets)); // exe 0x445040
    if (npcPlayerInFront(actor, 0x100, 0x400)) {
        actor.subState = uint8_t(RouteNpcSub::FacePlayer);
        actor.enterStep(0);
    }
}
// exe 0x4449d0 (the plain stationary kind)
void standPlain(AnimatedActor& actor, TownNpcFields& npc) {
    switch (WatchStep(actor.step)) {
    case WatchStep::Start:
        actor.anim = kAnimIdle;
        npc.bits = kStandingBits;
        actor.enterStep(uint8_t(WatchStep::LookAhead));
        [[fallthrough]];
    case WatchStep::LookAhead:
        npc.lookMode = uint8_t(NpcLook::Ahead);
        if (npcPlayerInFront(actor, 0x100, 0x400)) {
            actor.enterStep(uint8_t(WatchStep::LookAtPlayer));
            NpcHead head = npc.head();
            npcHoldHead(head);
            return;
        }
        if (npc.partnerSlot != 0) {
            NpcHead head = npc.head();
            npcPartnerCheck(actor, head);
        }
        break;
    case WatchStep::LookAtPlayer:
        npc.lookMode = uint8_t(NpcLook::Player);
        if (npcPlayerGone(actor)) {
            npc.headHold = 0;
            actor.enterStep(uint8_t(WatchStep::LookAhead));
        }
        break;
    default: break;
    }
    NpcHead head = npc.head();
    npcHoldHead(head);
}
// exe 0x444ad0 (the fidget kind; jump table 0x444c50): random idle animations and look modes
void standFidget(AnimatedActor& actor, TownNpcFields& npc) {
    auto newLook = [&] {
        npc.lookMode = fidgetLooks[rngStep() & 7];
        npc.fidgetLookTicks = int16_t(fidgetLookTicks[rngStep() & 7]);
    };
    switch (FidgetStep(actor.step)) {
    case FidgetStep::Start:
        actor.anim = kAnimIdle;
        npc.bits = kStandingBits;
        newLook();
        actor.enterStep(uint8_t(FidgetStep::NewPattern));
        [[fallthrough]];
    case FidgetStep::NewPattern: {
        actor.step = uint8_t(FidgetStep::NextAnimation);
        const uint16_t v = fidgetPatterns[rngStep() & 3];
        npc.timer = kFidgetRepeats;
        npc.fidgetPattern = int16_t(v);
        [[fallthrough]];
    }
    case FidgetStep::NextAnimation:
        actor.step = uint8_t(FidgetStep::Animating);
        actor.anim = npc.fidgetPattern < 0 ? kAnimFidget : kAnimIdle;
        break;
    case FidgetStep::Animating:
        if (actor.animKey.flags != 0) {
            if (actor.anim == kAnimFidget) {
                actor.anim = kAnimSettle;
            } else {
                npc.fidgetPattern = int16_t(uint16_t(npc.fidgetPattern) << 1);
                npc.timer = int16_t(uint16_t(npc.timer) - 1u);
                actor.step = uint8_t((npc.timer != 0) + 1u);
            }
        }
        break;
    default: break;
    }
    npc.fidgetLookTicks = int16_t(uint16_t(npc.fidgetLookTicks) - 1u);
    if (npc.fidgetLookTicks == 0) newLook();
}
// exe 0x4449b0: by the stationary kind [0x88e0e0]
void subStationary(AnimatedActor& actor, TownNpcFields& npc) {
    switch (StationaryKind(npc.behaviour & kNpcKindMask)) {
    case StationaryKind::Plain: standPlain(actor, npc); break;
    case StationaryKind::Fidget: standFidget(actor, npc); break;
    case StationaryKind::Still: npcStandStill(actor, npc); break;
    default:
        notPortedAt(kStoryPort, "0x4449b0 stationary kind (table 0x88e0e0 runs into data)", npc.behaviour & kNpcKindMask);
    }
}
// exe 0x444c60: the event runs; the animation the message script asks for
void subTalk(AnimatedActor& actor, TownNpcFields& npc) {
    EventState& event = eventState();
    const TalkStep step = TalkStep(actor.step);
    if (step == TalkStep::Start) {
        npc.bits = kTalkingBits;
        actor.anim = kAnimIdle;
        event.speakerAnimation = kNoSpeakerAnimation;
        npc.lookMode = uint8_t((npc.behaviour & kNpcFacesSpeaker) ? NpcLook::Player : NpcLook::Ahead);
        npc.headPitchTarget = 0;
        actor.enterStep(uint8_t(TalkStep::Talking));
    } else if (step != TalkStep::Talking) {
        if (step != TalkStep::Settling) return;
        if (!actor.animEnded()) return;
        npc.headHold = kHeadHoldTicks;
        actor.subState = uint8_t(RouteNpcSub::FacePlayer);
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
// exe 0x444d20: faces the player until it is gone
void subFacePlayer(AnimatedActor& actor, TownNpcFields& npc) {
    if (actor.step == 0) {
        actor.anim = kAnimIdle;
        npc.lookMode = uint8_t((npc.behaviour & kNpcFacesSpeaker) ? NpcLook::Player : NpcLook::Ahead);
        npc.bits = kFacingBits;
        actor.enterStep(1);
    }
    NpcHead head = npc.head();
    npcHoldHead(head);
    if (npcPlayerGone(actor)) {
        npc.headHold = 0;
        npcBackToIdle(actor, npc); // exe 0x445150
    }
}
// exe 0x4428a0: the last mover is the player (its slot class)
bool bumpedByPlayer(const AnimatedActor& actor) { return actorSlotClass(actor.lastMover->slot) == 0; }
// exe 0x444dc0: bumped (by the player): stands until the bumper is away
void subBumped(AnimatedActor& actor, TownNpcFields& npc) {
    const Actor& by = *npc.bumpedBy; // read once on entry: the distance below is to the previous bumper
    if (actor.step == 0) {
        actor.anim = kAnimIdle;
        if (by.airborne != 0 || by.slot == 0)
            npc.lookMode = uint8_t((npc.behaviour & kNpcFacesSpeaker) ? NpcLook::Player : NpcLook::Ahead);
        npc.bits = kStandingBits;
        npc.timer = 0x1e;
        actor.enterStep(1);
    }
    if (actor.lastMover && bumpedByPlayer(actor)) {
        npc.bumpedBy = actor.lastMover;
        actor.enterStep(0);
    }
    if (uint16_t(npc.timer) != 0) npc.timer = int16_t(uint16_t(npc.timer) - 1u);
    if (uint16_t(npc.timer) == 0 && 0x180u < uint32_t(distance3(actor.pos, by.pos))) { // an unsigned compare (jbe)
        npc.headHold = 0;
        npcBackToIdle(actor, npc); // exe 0x445150
    }
}

// ---- states [0x88e088] ------------------------------------------------------------------------------------------
// exe 0x4442a0: the model, the boxes, the partner, the scale, then idle
void bind(AnimatedActor& actor, TownNpcFields& npc) {
    actor.enterState(uint8_t(RouteNpcState::Run));
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
        freeActor(actor);                         // exe 0x5e1490
        return;
    }
    actor.prevAnim = kNoAnim;
    actor.anim = kAnimIdle;
    actor.velocity.clearWords();
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.actorHit = 0;
    actor.lastMover = nullptr;
    actor.collisionBox.pointAt(TableId::St24NpcRouteNpcBox);
    if (npc.message == kNpcNoMessage) {
        actor.interactionBox = nullptr;
        actor.interaction = 0;
    } else {
        actor.interactionBox.pointAt((npc.behaviour & kNpcSmallTalkBox) ? stateAddress(TableId::St24NpcSmallTalkBox) : stateAddress(TableId::St24NpcTalkBox));
        actor.interaction = kInteractTalk;
    }
    actor.clearClassBlock(); // exe 0x40dc40
    if (uint8_t(npc.route) != npc.partnerRoute) { // the partner: the first live type-0 pool actor on its route
        uint32_t mask = actorSystem().animatedLive;
        for (size_t i = 0; mask != 0; ++i, mask <<= 1) {
            AnimatedActor& other = animatedPool()[i];
            if (int32_t(mask) < 0 && other.type == 0 && classState<TownNpcFields>(other).route == int8_t(npc.partnerRoute)) {
                npc.partnerSlot = other.slot;
                break;
            }
        }
    }
    actor.enterState(uint8_t(RouteNpcState::Run));
    const uint16_t sc = uint16_t((actor.spawnByte & kScaleStepsMask) * 8u + 0x100u);
    actor.scale.x = int16_t(sc);
    actor.scale.y = int16_t(sc);
    actor.scale.z = int16_t(sc);
    npcBackToIdle(actor, npc); // exe 0x445150
}
// exe 0x4443b0: the talk, the sub-state, the head
void run(AnimatedActor& actor, TownNpcFields& npc) {
    if (!(npc.bits & kNpcNoTalk)) {
        if (player().talksTo(actor) && eventState().active == 0) { // exe 0x573fa0
            const uint32_t param = uint32_t(int32_t(int8_t(npc.behaviour)) & kNpcEventFlag) >> 2;
            eventStart(&actor.pos, &actor.rotation, param, 0, npc.message); // exe 0x543af0
            actor.subState = uint8_t(RouteNpcSub::Talk);
            actor.enterStep(0);
        }
    }
    switch (RouteNpcSub(actor.subState)) { // table 0x88e098
    case RouteNpcSub::Route: subRoute(actor, npc); break;
    case RouteNpcSub::Stationary: subStationary(actor, npc); break;
    case RouteNpcSub::Talk: subTalk(actor, npc); break;
    case RouteNpcSub::FacePlayer: subFacePlayer(actor, npc); break;
    case RouteNpcSub::Bumped: subBumped(actor, npc); break;
    default: notPortedAt(kStoryPort, "0x4443b0 sub-state (table 0x88e098 runs into the step table)", actor.subState);
    }
    NpcHead head = npc.head();
    npcLook(actor, head); // exe 0x444e80
    if (npc.headHold != 0) {
        npc.headYawTarget = 0;
        --npc.headHold;
        npc.headPitchTarget = 0;
    }
    npcTurnHead(head); // exe 0x445230 / 0x445280
}

// exe 0x4440f0's body
void routeNpc(AnimatedActor& actor) {
    TownNpcFields& npc = classState<TownNpcFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    if ((actor.actorHit & kWallHitMask) || actor.lastMover) npc.collisionBits |= kNpcHitActor | kNpcBlocked;
    switch (RouteNpcState(actor.state)) { // table 0x88e088
    case RouteNpcState::Bind: bind(actor, npc); break;
    case RouteNpcState::Run: run(actor, npc); break;
    case RouteNpcState::Idle:
    case RouteNpcState::IdleToo: break; // 0x52ef00: ret
    default: notPortedAt(kStoryPort, "0x4440f0 state (table 0x88e088 runs into the sub table)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    npc.collisionBits = 0;
    if (!(actor.spawnByte & kNoMoveTest)) {
        uint32_t r = actorTestMove(*E, actor); // exe 0x40f570
        if (r & kTestMoveBlocked) npc.collisionBits |= kNpcOffGrid;
        if (int32_t(r) < 0) {
            r = actorPushOut(*E, actor); // exe 0x410960
            actor.wallHit = r;
            if (int32_t(r) < 0) actor.pos = actor.prevPos;
            if (r & kWallHitMask) npc.collisionBits |= kNpcBlocked;
        }
    }
    if (!(actor.spawnByte & kNoGround)) actor.pos.y.whole = int16_t(actorGround(*E, actor)); // exe 0x40f190
    NpcHead head = npc.head();
    npcAnimate(actor, head);
    npc.hitBox.result = 0;
    npc.hitBox.previousX = actor.prevPos.x.whole;
    npc.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t pos[3] = {actor.pos.x.whole, t16(actor.pos.y.whole - speedsOf(actor).hitHeight), actor.pos.z.whole};
    hitListSubmit(pos, npc.hitBox, uint32_t(actor.slot) << 24 | kNpcHitOwner, kNpcHitAttack); // exe 0x5e4120
    if (actor.spawnByte & kNoGround) return;
    // exe 0x5f20f0(actor, 0x30, 0x160, 0): shadow (render output)
    const uint8_t bits = npc.bits;
    if (bits & kNpcSolid) actorCollisionSubmitSolid(actor, SolidType::PushOut, kNoBone, 0); // exe 0x414050
    else actor.lastMover = nullptr;
    if (bits & kNpcMover) {
        actorCollisionSubmitMover(actor); // exe 0x4140b0
        return;
    }
    actor.actorHit = 0;
}

} // namespace

void st24RouteNpcUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    routeNpc(animatedActorAt(actor));
}

} // namespace od::port
