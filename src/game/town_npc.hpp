// The town NPCs, class 0x441950 (pool 0x20, type 0 of the town stages' tables; 40 actors in 25 rooms): route walkers
// and stationary NPCs that talk (the player's "use" starts the event of their message), stop in front of the player,
// look at it and at their partner, and react when bumped. Their field layout and head / look helpers are shared by
// the stage NPCs built on them (content/nino/st18_npc).
#pragma once

#include <cstdint>

#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/actor_movement.hpp"
#include "game/player/player_state.hpp"

namespace od::port {

// +0xd behaviour
constexpr uint8_t kNpcStationary = 0x80;   // stands (sub-state 1) instead of walking its route
constexpr uint8_t kNpcEventFlag = 0x40;    // its talk event gets flag 0x10
constexpr uint8_t kNpcFacesSpeaker = 0x20; // looks at the player while talking
constexpr uint8_t kNpcMayPause = 0x10;     // may pause at a waypoint
constexpr uint8_t kNpcSmallTalkBox = 0x08; // the small interaction box
constexpr uint8_t kNpcKindMask = 0x07;     // the stationary kind / the walk speed (kNpcRunKinds) / the look set
constexpr uint8_t kNpcRunKinds = 0x04;     // a walker of a kind with this bit runs
constexpr uint8_t kNpcLookKindMask = 0x03; // a walker's look behaviour by its kind:
constexpr uint8_t kNpcLooksAhead = 0x00;   // ... straight ahead
constexpr uint8_t kNpcKeepsLook = 0x03;    // ... its look mode left as it is (1 / 2: a random one of the kind's set)
// +0xf: the message of an NPC that does not talk
constexpr uint8_t kNpcNoMessage = 0xff;
// +0x15c bits
constexpr uint8_t kNpcNoTalk = 0x01;   // the player's talk is ignored (while talking)
constexpr uint8_t kNpcNoBump = 0x02;   // a bump is ignored
constexpr uint8_t kNpcSolid = 0x04;    // queued as a solid (type 0)
constexpr uint8_t kNpcMover = 0x08;    // queued as a mover
// +0x162 collision bits
constexpr uint8_t kNpcBlocked = 0x01;  // stopped by a wall or an actor (the route turns away)
constexpr uint8_t kNpcOffGrid = 0x08;  // the wall test found no cell
constexpr uint8_t kNpcHitActor = 0x20; // an actor pushed it / it pushed into one

// The step machines the town NPC and the servbot share (their own exe copies, the same steps)
// a pause's / a plain stand's watch of the player (the pause: +0xb, the plain stand: +0xa)
enum class WatchStep : uint8_t {
    Start = 0,
    LookAhead = 1,    // the head straight; the player close in front -> LookAtPlayer
    LookAtPlayer = 2, // until the player is gone
};
// the fidget's steps (+0xa)
enum class FidgetStep : uint8_t {
    Start = 0,
    NewPattern = 1,    // a random pattern of idle animations, kFidgetRepeats of them
    NextAnimation = 2, // the pattern's next bit picks the animation
    Animating = 3,     // until the key's sequence ends
};
// the talk's steps (+0xa)
enum class TalkStep : uint8_t {
    Start = 0,
    Talking = 1,  // the event's animations, until it ends
    Settling = 2, // the settle animation, then face the player
};
constexpr int16_t kFidgetRepeats = 0x10; // the idle animations of one fidget pattern

// the head's look modes (+0x15e)
enum class NpcLook : uint8_t {
    Ahead = 1,          // head straight
    Player = 2,         // at the player's head
    TargetYaw = 3,      // the yaw toward the route target (when close to the facing)
    TargetLevel = 4,    // ... and the pitch toward the target at its own height
    Partner = 5,        // at the partner NPC's head
    PartnerLevel = 6,   // at the partner at its own height
    Down = 7,
    Up = 8,
};

// The head / look fields of an NPC: the town NPC's block and the servbot's (game/servbot) keep them at different
// offsets; the look routines of both classes run on them.
struct NpcHead {
    int8_t& pitch;         // head angles (x 16) added to bone 1's decoded angles
    int8_t& yaw;
    int8_t& pitchTarget;   // turned toward by npcTurnHead
    int8_t& yawTarget;
    int16_t& targetX;      // the point the target look modes aim at (the route's waypoint)
    int16_t& targetZ;
    uint8_t& partnerSlot;  // the partner's slot id (0: none)
    uint8_t& lookMode;     // NpcLook
    uint8_t& hold;         // ticks the head stays straight
};

// a waypoint of the stage's town routes {4 links (waypoint indices; bit 7: no link), s16 x, s16 z}
struct Waypoint : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    std::array<uint8_t, 4> links{}; // +0x0
    int16_t x = 0; // +0x4
    int16_t z = 0; // +0x6
    template <class R> static Waypoint load(const R& r) {
        Waypoint v;
        for (size_t i = 0; i < v.links.size(); ++i) v.links[i] = r.template read<uint8_t>(0x0 + i * sizeof(uint8_t));
        v.x = r.template read<int16_t>(0x4);
        v.z = r.template read<int16_t>(0x6);
        return v;
    }
};
// waypoint `index` of route `route` (the route table of the stage, *0xa72244)
Waypoint routeWaypoint(int8_t route, int8_t index);

// The town NPC's own fields (spawn parameters and the slot's class block +0x14c..+0x16b)
struct TownNpcFields : ActorClassState {
    explicit TownNpcFields(AnimatedActor& actor) : route(actor.params[0]), behaviour(actor.params[1]), partnerRoute(actor.params[2]), message(actor.params[3]), previousWaypoint(actor.shownParts), collisionBits(actor.hopperBytes[0]) {}
    FieldAlias<int8_t, uint8_t> route; // its route in the stage's route table
    uint8_t& behaviour; // kNpc* behaviour bits | kind
    uint8_t& partnerRoute; // the route its partner walks (== route: none)
    uint8_t& message; // the talk event's message (0xff: it does not talk)
    int8_t waypoint = 0;
    uint8_t& previousWaypoint;
    uint8_t& collisionBits; // kNpcBlocked | kNpcOffGrid | kNpcHitActor
    ActorRef<Actor> bumpedBy; // the mover that bumped it
    int8_t headPitch = 0; // head angles (x 16) added to bone 1's decoded angles
    int8_t headYaw = 0;
    int8_t headPitchTarget = 0;
    int8_t headYawTarget = 0;
    int16_t timer = 0; // the pause / bumped ticks, the fidget's repeats
    int16_t fidgetPattern = 0; // fidget: the idle animations to play (sign bit: animation 3)
    union {
        int16_t targetX = 0;
        int16_t fidgetLookTicks;
    };
    int16_t targetZ = 0; // the waypoint z
    uint8_t bits = 0; // kNpcNoTalk | kNpcNoBump | kNpcSolid | kNpcMover
    uint8_t partnerSlot = 0; // the partner's slot id (0: none)
    uint8_t lookMode = 0; // NpcLook
    uint8_t headHold = 0; // ticks the head stays straight
    HitBoxHead hitBox;
    NpcHead head() {
        return {headPitch, headYaw, headPitchTarget, headYawTarget, targetX, targetZ, partnerSlot, lookMode, headHold};
    }

    void clearBlock() override {
        waypoint = 0;
        bumpedBy = nullptr;
        headPitch = 0;
        headYaw = 0;
        headPitchTarget = 0;
        headYawTarget = 0;
        timer = 0;
        fidgetPattern = 0;
        targetX = 0;
        targetZ = 0;
        bits = 0;
        partnerSlot = 0;
        lookMode = 0;
        headHold = 0;
        hitBox = {};
    }
};

// exe 0x4431d0(slot, out): the x / z of the actor in that slot and the height of its bone point 1 (the player's, or
// a pool-0x20 actor's) >> the stage shift with its low 5 bits cleared: where an NPC looks at it
void npcLookPoint(uint32_t slot, int16_t out[3]);
// exe 0x445150: back to the route sub-state (or standing, kNpcStationary) at step 0
void npcBackToIdle(Actor& actor, TownNpcFields& npc);
// exe 0x442430: the still stationary kind (standing, solid, not bumped, head straight while held)
void npcStandStill(AnimatedActor& actor, TownNpcFields& npc);
// the player farther than 0x180 or outside +-0x280 of the NPC's facing
bool npcPlayerGone(const Actor& actor);
// the player closer than `dist` and inside +-window / 2 of the NPC's facing
bool npcPlayerInFront(const Actor& actor, int dist, uint32_t window);
// exe 0x4446d0 (servbot: 0x462ec0): the next waypoint, a random link avoiding the previous one when there are two
void npcNextWaypoint(int8_t route, int8_t& waypoint, uint8_t& previous);
// exe 0x442790 (servbot: 0x463f70): a walker's look mode by its kind (a random one of the kind's set in `lookSets`,
// u32 per kind)
void npcPickLook(uint8_t behaviour, NpcHead& head, uint32_t lookSets);
// exe 0x441d40 (servbot: 0x462cc0): the partner close and in front -> look at it
void npcPartnerCheck(const Actor& actor, NpcHead& head);
// exe 0x445170 (servbot: 0x4e20d0): the head yaw target from the angle toward v (+-0x300, / 16)
void npcLookYaw(const Actor& actor, int8_t& yawTarget, const int16_t v[3]);
// exe 0x4451f0 (servbot: 0x464100): the head pitch target from the height of v against the NPC's bone point 1
void npcLookPitch(const AnimatedActor& actor, NpcHead& head, const int16_t v[3]);
// exe 0x4425d0 (servbot: 0x463db0): the head targets by the look mode
void npcLook(const AnimatedActor& actor, NpcHead& head);
// the head straight while held (+hold != 0: held again 0x3c ticks)
void npcHoldHead(NpcHead& head);
// the head targets cleared while held (the hold counts down)
void npcHeldHeadTick(NpcHead& head);
// exe 0x445230 / 0x445280 (servbot: 0x4e2190 / 0x4e21e0): the head yaw turned 6 a tick toward its target, then the
// pitch 2 a tick
void npcTurnHead(NpcHead& head);
// one of them (exe 0x445230 / 0x445280): the head angle turned toward its target by at most `max` a tick
void npcEaseHead(int8_t& angle, const int8_t& target, int max);
// exe 0x444080 (servbot: 0x464090): the animation (set on a change, else stepped), the head angles added to bone
// 1's decoded angles, the pose
void npcAnimate(AnimatedActor& actor, const NpcHead& head);
// exe 0x543af0 for an NPC the player talks to: the event of its message (speaker position +0x10, rotation +0x28; event
// flag 0x10 with kNpcEventFlag)
void npcStartTalk(Actor& actor, uint8_t behaviour, uint8_t message);

// exe 0x441950: the town NPC's update (ActorUpdateFn)
void townNpcUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
