// ST1F (outside Glyde's base, story 8): Appo and Dah, Shu's brothers -- the pool-0x20 type 0x43 classes of the
// stage's phase table 0x8e76f0 (pool 0x20 [*0xa6d300]) other than 0x4c4980 / 0x4c6650 (content/calbania/appo_dah_a).
// Transcribed from dash2.exe one function per exe function (conventions as game/story.cpp). Render output skipped: the
// shadow list exe 0x5f20f0 (as every other actor port); the models are drawn from their bound model like every actor.
//
// Public functions (namespace od::port, ActorUpdateFn signature; content/calbania/register registers them):
//   appoDahFightUpdate     exe 0x4c1b30  type 0x43 sub 0: Appo (variant 0) / Dah (variant 1) in the fight in front of
//                                       the base (records 0x88ac28 / 0x88ac3c of the ST1F spawner 0x41ef20; also
//                                       0x88ad54 / 0x88ad68 / 0x88adb8 / 0x88adcc). States [0x894ccc]: 0 init, 1 the
//                                       fight (sub-states [0x894ce0]: walk the waypoint graph 0x894c08, stand, throw at
//                                       the nearest enemy (pool 0x40 type 0x15), run off, falls / knock-backs), 2 at
//                                       the gate idle, 3 at the gate (flags 0x581 + v / 0x580; flag 0x585: placed for
//                                       script 0x1f), 4 lifted / carried / thrown by the player (exe 0x573a60). Hit by
//                                       the player: -500 balance; life 0 -> flag 0x680 + v.
//   script1dAppoDahUpdate  exe 0x4c5f90  type 0x43 sub 1: the two of script 0x1d (records 0x8a3420 / 0x8a3434);
//                                       variant 0 -> 0x4c6070, else 0x4c6310 ([0x894e88 / 0x894e8c + +0xc * 4]),
//                                       keyed on the script block's step sub, state and step tick
//   script1fAppoDahUpdate  exe 0x4c6ae0  type 0x43 sub 3: the two of script 0x1f (records 0x8a3748 / 0x8a375c /
//                                       0x8a3770 / 0x8a3784), [0x894eb8 + +0xc * 4] = 0x4c6bb0
//   appoDahBombUpdate      exe 0x5c8220  pool 0x40 type 0x15 (phase table 0x8e7908): the bomb of the throw; states
//                                       [0x8d138c] 0 init, 1 flight (hit entry, billboard 0x2c), 2 freed
// The lift-and-throw over the wall of script 0x20 is the script's own (content/calbania/square_defence_scripts, script 0x20); its
// cast is sub 4 (0x4c6650, records 0x8a3940..0x8a397c).
//
// The exe's stack locals left uninitialised by the run-off sub-state (when it does not call exe 0x4c26d0) are taken as
// "no enemy" (distance 0x7fffffff) with the angle = the actor's address (its argument slot, which the exe reuses for the
// angle).
#include <cstdint>

#include "content/calbania/appo_dah_face.hpp"
#include "content/calbania/appo_dah_b_state.hpp"
#include "content/calbania/appo_dah_fight.hpp"
#include "content/calbania/ch3_story.hpp"
#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/address_forms.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/game_state.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/rewards.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/territory.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace appo_dah_b_state;

namespace {

using address_forms::E;

constexpr uint8_t kNoAnim = 0xff;
// collision bits (AppoDahFightFields::collision)
constexpr uint8_t kFightWallX = 0x01, kFightWallZ = 0x02, kFightLanded = 0x04, kFightLedge = 0x08;
constexpr uint8_t kFightWalls = kFightWallX | kFightWallZ;
// flags (AppoDahFightFields::flags)
constexpr uint8_t kFightNoHitEntry = 0x01;
constexpr uint8_t kFightNoHits = 0x02;
constexpr uint8_t kFightNoWallFall = 0x04;  // a wall does not knock it back
constexpr uint8_t kFightNoFall = 0x08;      // a ledge does not make it fall
constexpr uint8_t kFightThrown = 0x10;      // its hit entry is a thrown body's
constexpr uint8_t kFightKeepGround = 0x20;  // it follows the ground (no ledge)
constexpr uint8_t kFightReflect = 0x40;     // bounces off the walls
constexpr uint8_t kFightFalling = kFightReflect | kFightNoFall | kFightNoWallFall | kFightNoHitEntry;
constexpr uint8_t kFightKnocked = kFightReflect | kFightNoFall | kFightNoWallFall | kFightNoHits;
constexpr uint8_t kFightPushed = kFightNoWallFall | kFightNoHits;
constexpr uint8_t kFightCarried = kFightReflect | kFightNoHitEntry;
constexpr uint8_t kFightRunning = kFightNoWallFall | kFightNoHitEntry;
constexpr uint8_t kFightAtGate = 0x22;      // kFightKeepGround | kFightNoHits
constexpr uint8_t kFightGateIdle = kFightNoHits;

enum class FightState : uint8_t { Init = 0, Fight = 1, GateIdle = 2, AtGate = 3, Lifted = 4 }; // [0x894ccc]
enum class FightSub : uint8_t { Start = 0, Walk = 1, WalkOn = 2, WalkBack = 3, Stand = 4, Throw = 5, RunOff = 6,
                                Stop = 7, Fall = 8, KnockedBack = 9, Pushed = 0xa, WallFall = 0xb }; // [0x894ce0]
enum class GateIdleSub : uint8_t { Idle = 0, Fall = 1, Knocked = 2 };                               // [0x894d10]
enum class AtGateSub : uint8_t { Start = 0, Placed = 1, Arrive = 2, Fall = 3, Hit = 4 };            // [0x894d1c]
enum class LiftedSub : uint8_t { Carried = 0, Thrown = 1, ThrownDown = 2 };                          // [0x894d30]
enum class WaypointStep : uint8_t { Pick = 0, WalkRight = 1, WalkLeft = 2 };

constexpr uint8_t kAnimStandStill = 0, kAnimStand = 1, kAnimWalk = 2, kAnimCarried = 3, kAnimCarriedDown = 4,
                  kAnimKnockedFront = 5, kAnimKnockedBack = 6, kAnimDownFront = 7, kAnimDownBack = 8,
                  kAnimUpFront = 9, kAnimUpBack = 0xa, kAnimThrow = 0xb, kAnimThrowEnd = 0xc, kAnimStop = 0xd,
                  kAnimLieFront = 0xe, kAnimLieBack = 0xf;
constexpr uint16_t kSoundBounce = 0x147, kSoundLand = 0x146, kSoundWall = 0x148;
constexpr uint16_t kHurtAppo = 0x1ad, kHurtDah = 0x1b1, kThrowAppo = 0x1ac, kThrowDah = 0x1b0, kCarriedAppo = 0x1ae,
                   kCarriedDah = 0x1b2;
constexpr int16_t kFightGravity = 0x40;
constexpr int kBalanceLoss = -500;
constexpr uint32_t kFlagDahAtGate = 0x580, kFlagAtGate = 0x581; // + the variant: Appo / Dah reached the gate
constexpr uint32_t kFlagScript1fPlaces = 0x585;
constexpr uint32_t kFlagAppoDahHurt = 0x680;                    // + the variant: knocked out in the fight (grey)
constexpr uint8_t kGateWaypoint = 0xb, kWaypointRows = 0xb;
constexpr uint8_t kAppoDahType = 0x43, kBombType = 0x15;
constexpr uint32_t kHitDamageMask = 0xfff, kHitKnocks = 0x1e0000, kHitCostsBalance = 0x440000, kHitPush = 0x10000,
                   kHitThrownHit = 0x20000;
// the throw's arm: the bomb in the hand shown, the empty hand hidden
constexpr size_t kEmptyHandPart = 2, kBombHandPart = 6;

// a row of the waypoint graph (exe table 0x894c08, 14 bytes): its point by the variant, the mask its next waypoint is
// picked by, the next waypoints
struct WaypointRow : DataRecord {
    static constexpr uint32_t kBytes = 0xd;
    std::array<int16_t, 2> x{}; // +0x0
    std::array<int16_t, 2> z{}; // +0x4
    int8_t choiceMask = 0; // +0x8
    std::array<uint8_t, 4> next{}; // +0x9
    template <class R> static WaypointRow load(const R& r) {
        WaypointRow v;
        for (size_t i = 0; i < v.x.size(); ++i) v.x[i] = r.template read<int16_t>(0x0 + i * sizeof(int16_t));
        for (size_t i = 0; i < v.z.size(); ++i) v.z[i] = r.template read<int16_t>(0x4 + i * sizeof(int16_t));
        v.choiceMask = r.template read<int8_t>(0x8);
        for (size_t i = 0; i < v.next.size(); ++i) v.next[i] = r.template read<uint8_t>(0x9 + i * sizeof(uint8_t));
        return v;
    }
};
constexpr uint32_t kWaypointRowSize = 14;
WaypointRow waypointRow(int k) { return dataRecord<WaypointRow>(stateAddress(TableId::AppoDahBWaypoints) + uint32_t(int32_t(k)) * kWaypointRowSize, WaypointRow::kBytes); }
// the gate places (x / z, by the variant against flag 0x580) and script 0x1f's
const DataTable<ScalarTable<int16_t, 2>> gateX{TableId::AppoDahBGateX};
const DataTable<ScalarTable<int16_t, 2>> gateZ{TableId::AppoDahBGateZ};
const DataTable<ScalarTable<int16_t, 2>> script1fX{TableId::AppoDahBScript1fX};
const DataTable<ScalarTable<int16_t, 2>> script1fZ{TableId::AppoDahBScript1fZ};

uint16_t voiceOf(const AnimatedActor& actor, uint16_t appo, uint16_t dah) { return actor.variant == 0 ? appo : dah; }
void shadow(const AnimatedActor&) {} // exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow list entry (render output)
void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
// exe 0x5e3bc0 with a stack point whose 16.16 fractions are left unset (taken as 0): the actor seen from (x, z)
int angleFromPoint(int16_t x, int16_t z, const Fixed16Vec3& to) {
    return angleOf(int32_t(uint32_t(uint16_t(x)) << 16), int32_t(uint32_t(uint16_t(z)) << 16), to.x, to.z);
}
// the dust puff of the slides (on the odd frames: pool 0xa0 type 0 variant 3 at the actor)
void dust(const AnimatedActor& actor) {
    if (!(gameState().frame & 1)) return;
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    e.flags = kActorLive;
    e.loopFlags |= uint8_t(kActorSweepGroup2 | kActorLateLoop);
    e.type = 0;
    e.variant = 3;
    e.pos.x = actor.pos.x.whole;
    e.pos.y = actor.pos.y.whole;
    e.pos.z = actor.pos.z.whole;
    e.setParamsLowWord(0x40); // the puff's size
}
// the tint from its grey level (5:5:5)
void applyGrey(AnimatedActor& actor, const AppoDahFightFields& f) {
    const uint32_t c = uint32_t(int32_t(f.grey));
    actor.tint = uint16_t((((c << 5) | c) << 5) | c);
}
void showPart(AnimatedActor& actor, size_t part) { actor.parts[part].flags &= uint8_t(~kPartHidden); }
void hidePart(AnimatedActor& actor, size_t part) { actor.parts[part].flags |= kPartHidden; }
// the empty hand back (the throw's bomb hand hidden)
void handsEmpty(AnimatedActor& actor) {
    showPart(actor, kEmptyHandPart);
    hidePart(actor, kBombHandPart);
}

// ==== exe 0x4c1b30: the fight =========================================================================================
void enterSub(AnimatedActor& actor, uint8_t sub) { // the sub-state at its first step (the word store of the steps)
    actor.subState = sub;
    actor.restartStep();
}
template <class Sub> void enterSub(AnimatedActor& actor, Sub sub) { enterSub(actor, uint8_t(sub)); }
void moveForward(AnimatedActor& actor) { moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, actor.velocity.z); } // exe 0x5e36f0
// the area in front of the gate
bool inGateArea(const AnimatedActor& actor) {
    return uint16_t(uint16_t(actor.pos.x.whole) - 0x1300) < 0x400 && uint16_t(uint16_t(actor.pos.z.whole) + 0x300) < 0x450;
}
// flag 0x581 + v (Dah: also 0x580 when Appo is not there yet)
void gateFlags(const AnimatedActor& actor) {
    setStoryFlag(kFlagAtGate + actor.variant);
    if (actor.variant != 0 && !storyFlag(kFlagAtGate)) setStoryFlag(kFlagDahAtGate);
}
bool atGate(const AnimatedActor& actor) { return storyFlag(kFlagAtGate + actor.variant); }
// exe 0x4c1fb0: standing (animation 1), the motion cleared
void stand(AnimatedActor& actor) {
    actor.anim = kAnimStand;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = 0;
    actor.acceleration.z = 0;
}
// exe 0x4c4790: the hits taken: the damage of a knocking hit -> the life (its direction, the flash); dead -> grey 8 +
// flag 0x680 + v, -1; damaged 1; a push (0x10000) 2; none 0
int hitsTaken(AnimatedActor& actor, AppoDahFightFields& f) {
    const uint32_t h = f.hitBox.result;
    const uint32_t d = h & kHitDamageMask;
    if (d != 0) {
        uint32_t taken = 0;
        if (h & kHitKnocks) {
            f.knockYaw = uint16_t(f.hitBox.contact.x);
            actor.tint = kTintFlash;
            taken = d;
            if (h & kHitCostsBalance) balanceAdd(kBalanceLoss); // exe 0x5e51d0
        }
        if (hitPointsAdd(actor, -int(taken)) != 0) { // exe 0x5e52f0
            f.grey = 8;
            setStoryFlag(kFlagAppoDahHurt + actor.variant);
            return -1;
        }
        if (taken != 0) return 1;
    }
    if (h & kHitPush) {
        f.knockYaw = uint16_t(f.hitBox.contact.x);
        return 2;
    }
    return 0;
}
// exe 0x4c4850: the walls (push-out / territory), the ground; 1 = walked off a ledge
int collide(AnimatedActor& actor, AppoDahFightFields& f) {
    f.collision = 0;
    uint32_t r;
    if (int32_t(actorTestMove(*E, actor)) < 0) { // exe 0x40f570
        r = actorPushOut(*E, actor);             // exe 0x410960
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if (r & 0x300) f.collision |= kFightWallX;
        if (r & 0xc00) f.collision |= kFightWallZ;
        if ((f.flags & kFightReflect) && reflectRotationOffWalls(actor.rotation, r)) f.collision |= kFightLanded;
    } else {
        r = uint32_t(territoryViolated(actor.pos)); // exe 0x5e6ab0
        if (r != 0) {
            r = territoryPushBack(actor, int(r)); // exe 0x5e6b40
            if (r & 0x300) f.collision |= kFightWallX;
            if (r & 0xc00) f.collision |= kFightWallZ;
        }
    }
    const int g = actorGround(*E, actor); // exe 0x40f190
    if (actor.airborne == 0) {
        if (!(f.flags & kFightKeepGround) && g - actor.pos.y.whole > 0x10) {
            f.collision |= kFightLedge;
            return 1;
        }
        actor.pos.y.whole = int16_t(g);
        return 0;
    }
    if (!(f.flags & kFightReflect) && (r & 0x3000)) {
        actor.airborne = 0;
        actor.pos.y.whole = int16_t(g);
        f.collision |= kFightLanded;
    }
    return 0;
}
// exe 0x4c4640(actor, yaw): landed -> a bounce (vy = -vy / 2, the speed / 2, sound 0x147), 1; else it flies on
int arc(AnimatedActor& actor, AppoDahFightFields& f, int yaw) {
    if (f.collision & kFightLanded) {
        const int vy = actor.velocity.y;
        actor.velocity.z = int16_t(actor.velocity.z >> 1);
        actor.velocity.y = int16_t((-vy) >> 1);
        actor.rotation.x = 0;
        actor.airborne = 1;
        sound(kSoundBounce, actor);
        return 1;
    }
    moveAlongYaw(actor.pos, yaw, 0, actor.velocity.y, actor.velocity.z);
    actor.velocity.y += uint16_t(actor.acceleration.y);
    return 0;
}
// exe 0x4c2260: the next waypoint (the previous = the current = the old / the new); the gate's waypoint -> the gate
// (state 3 sub 2); 1 in 8 a stop
int nextWaypoint(AnimatedActor& actor, AppoDahFightFields& f) {
    f.prevWaypoint = f.waypoint;
    f.waypoint = f.nextWaypoint;
    if (uint8_t(f.nextWaypoint) == kGateWaypoint) {
        actor.enterState(uint8_t(FightState::AtGate), uint8_t(AtGateSub::Arrive));
        return 0;
    }
    if ((rngStep() & 7) == 0) {
        enterSub(actor, FightSub::Stop);
        return 1;
    }
    actor.step = uint8_t(WaypointStep::Pick);
    return 1;
}
// exe 0x4c26d0(actor, &slot, &distance, &angle): the nearest live pool-0x20 actor that is not Appo / Dah (x / z); -1
// none
int nearestEnemy(const AnimatedActor& actor, int32_t& slot, int32_t& dist, int32_t& angle) {
    angle = 0;
    slot = -1;
    dist = 0x7fffffff;
    for (int i = 31; i >= 0; --i) {
        const AnimatedActor& e = animatedPool()[size_t(i)];
        if (!(e.flags & kActorQueued) || e.type == kAppoDahType) continue;
        const int d = planarDistance(e.pos, actor.pos); // exe 0x5e3b00
        if (dist > d) {
            dist = d;
            slot = i;
        }
    }
    if (slot < 0) return -1;
    angle = angleOf(animatedPool()[size_t(slot)].pos, actor.pos); // exe 0x5e3bc0
    return 0;
}
// the knock-backs' "turn around from the hit": facing the hit within a quarter turn
bool facesHit(const AnimatedActor& actor, const AppoDahFightFields& f) {
    int e = int(uint16_t(actor.rotation.y) & 0xfff) - int(f.knockYaw & 0xfff);
    if (uint32_t(e + 0x800) > 0x1000u) e = int((uint32_t(-e) ^ 0xfffu) + 1u);
    return uint32_t(e + 0x400) < 0x800u;
}
// the hit's damage clamped to 0x10..0x30, << 4
int hitPower(const AppoDahFightFields& f) {
    int p = int(f.hitBox.result & kHitDamageMask);
    if (p > 0x30) p = 0x30;
    else if (p < 0x10) p = 0x10;
    return p << 4;
}
// the first waypoint row (0..0xa) with an x past the actor's; none -> (w0, w1)
void waypointAhead(const AnimatedActor& actor, AppoDahFightFields& f, uint8_t w0, uint8_t w1) {
    const int16_t x = actor.pos.x.whole;
    int k = 0;
    for (; k < kWaypointRows; ++k) {
        if (waypointRow(k).x[actor.variant] > x) {
            f.waypoint = int8_t(k);
            f.prevWaypoint = int8_t(k);
            break;
        }
    }
    if (k == kWaypointRows) {
        f.waypoint = int8_t(w0);
        f.prevWaypoint = int8_t(w1);
    }
}
// the knock-backs' "back to the gate route": in the yard -> waypoint 3 heading 0x400
bool yardToRoute(AnimatedActor& actor, AppoDahFightFields& f) {
    if (uint16_t(uint16_t(actor.pos.x.whole) - 0xa00) < 0x600 && uint16_t(uint16_t(actor.pos.z.whole) + 0xb00) < 0xa00) {
        actor.rotation.y = 0x400;
        f.prevWaypoint = 3;
        f.waypoint = 3;
        enterSub(actor, FightSub::WalkOn);
        return true;
    }
    return false;
}

// ---- state 1 sub-states [0x894ce0] -----------------------------------------------------------------------------
// exe 0x4c1f90: stands, then walks
void fightStart(AnimatedActor& actor, AppoDahFightFields& f) {
    stand(actor);
    f.flags = 0;
    enterSub(actor, FightSub::Walk);
}
// the waypoint walks exe 0x4c1fd0 / 0x4c22d0: Pick picks the next waypoint of its row, the walks go until its x is
// passed; true when exe 0x4c2260 ran (0x4c22d0 returns there)
bool walkWaypoint(AnimatedActor& actor, AppoDahFightFields& f) {
    const WaypointRow target = waypointRow(f.nextWaypoint);
    const int16_t tx = target.x[actor.variant], tz = target.z[actor.variant];
    switch (WaypointStep(actor.step)) {
    case WaypointStep::Pick: {
        f.flags = 0;
        actor.anim = kAnimWalk;
        actor.velocity.z = int16_t(0xfe00);
        const uint32_t r = rngStep();
        const WaypointRow row = waypointRow(f.waypoint);
        const uint32_t idx = uint32_t(int32_t(row.choiceMask)) & r;
        f.nextWaypoint = int8_t(row.next[idx]);
        const WaypointRow next = waypointRow(f.nextWaypoint);
        const int16_t nx = next.x[actor.variant], nz = next.z[actor.variant];
        const int ang = angleFromPoint(nx, nz, actor.pos);
        actor.step = uint8_t(nx <= actor.pos.x.whole ? WaypointStep::WalkLeft : WaypointStep::WalkRight);
        turnYawTo(actor.rotation.y, uint32_t(ang), 0x40);
        moveForward(actor);
        return false;
    }
    case WaypointStep::WalkRight:
        turnYawTo(actor.rotation.y, uint32_t(angleFromPoint(tx, tz, actor.pos)), 0x20);
        moveForward(actor);
        if (tx < actor.pos.x.whole) {
            nextWaypoint(actor, f);
            return true;
        }
        return false;
    case WaypointStep::WalkLeft:
        turnYawTo(actor.rotation.y, uint32_t(angleFromPoint(tx, tz, actor.pos)), 0x20);
        moveForward(actor);
        if (tx > actor.pos.x.whole) {
            nextWaypoint(actor, f);
            return true;
        }
        return false;
    default: return false;
    }
}
// exe 0x4c1fd0: walks the waypoints; an enemy within 0x800 -> a throw (1 in 4) or the run-off
void fightWalk(AnimatedActor& actor, AppoDahFightFields& f) {
    walkWaypoint(actor, f);
    int32_t slot, dist, angle;
    if (nearestEnemy(actor, slot, dist, angle) == 0 && dist < 0x800) {
        if ((rngStep() & 3) == 0) enterSub(actor, FightSub::Throw);
        else enterSub(actor, FightSub::RunOff);
    }
}
// exe 0x4c24f0: walks back along the route (the waypoint - 1 every 0x1b ticks, 1 in 4 a next waypoint)
void fightWalkBack(AnimatedActor& actor, AppoDahFightFields& f) {
    if (actor.step == 0) {
        f.flags = 0;
        actor.anim = kAnimWalk;
        actor.velocity.z = int16_t(0xfe00);
        const WaypointRow target = waypointRow(f.nextWaypoint);
        const int16_t tx = target.x[actor.variant], tz = target.z[actor.variant];
        const int ang = angleFromPoint(tx, tz, actor.pos);
        actor.step = uint8_t(tx <= actor.pos.x.whole ? WaypointStep::WalkLeft : WaypointStep::WalkRight);
        turnYawTo(actor.rotation.y, uint32_t(ang), 0x40);
        moveForward(actor);
        f.timer = 0x1b;
        return;
    }
    if (actor.step > 2) return;
    const WaypointRow target = waypointRow(f.nextWaypoint);
    const int16_t tx = target.x[actor.variant], tz = target.z[actor.variant];
    turnYawTo(actor.rotation.y, uint32_t(angleFromPoint(tx, tz, actor.pos)), 0x40);
    moveForward(actor);
    f.timer = int16_t(uint16_t(f.timer) - 1u);
    if (f.timer != 0) return;
    if ((rngStep() & 3) != 0) {
        const uint8_t w = uint8_t(uint8_t(f.nextWaypoint) - 1u);
        f.nextWaypoint = int8_t(w);
        if (w == 0) f.nextWaypoint = int8_t((rngStep() & 3) + 1u);
        actor.step = uint8_t(tx <= actor.pos.x.whole ? WaypointStep::WalkLeft : WaypointStep::WalkRight);
        f.timer = 0x1b;
        return;
    }
    if (nextWaypoint(actor, f)) enterSub(actor, FightSub::RunOff);
}
// exe 0x4c26b0: stands
void fightStand(AnimatedActor& actor, AppoDahFightFields& f) {
    if (actor.step != 0) return;
    f.flags = 0;
    actor.anim = kAnimStandStill;
    actor.enterStep(1);
}
enum class ThrowStep : uint8_t { Raise = 0, Aim = 1, Lower = 2 };
// exe 0x4c2780: throws at the nearest enemy (animation 0xb, at its frame 9 a bomb: pool 0x40 type 0x15)
void fightThrow(AnimatedActor& actor, AppoDahFightFields& f) {
    switch (ThrowStep(actor.step)) {
    case ThrowStep::Raise:
        f.flags = 0;
        actor.anim = kAnimThrow;
        actor.enterStep(uint8_t(ThrowStep::Aim));
        f.knockYaw = 0x40; // the throw's ticks
        hidePart(actor, kEmptyHandPart);
        showPart(actor, kBombHandPart);
        return;
    case ThrowStep::Aim: {
        f.knockYaw = uint16_t(f.knockYaw - 1u);
        int32_t slot = 0, dist = 0, angle = 0;
        if (int16_t(f.knockYaw) == 0 || nearestEnemy(actor, slot, dist, angle) != 0) {
            actor.anim = kAnimThrowEnd;
            actor.step = uint8_t(ThrowStep::Lower);
            return;
        }
        turnYawTo(actor.rotation.y, uint32_t(angle), 0x100);
        if (uint8_t(actor.animKey.frame) != 9) return;
        sound(voiceOf(actor, kThrowAppo, kThrowDah), actor);
        const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
        if (address == 0) return;
        ProjectileActor& s = projectileActorAt(address);
        AppoDahBombFields& bomb = classState<AppoDahBombFields>(projectileActorAt(address));
        s.flags = kActorLive;
        s.type = kBombType;
        s.loopFlags |= kActorSweepGroup2;
        s.pos = actor.pos;
        s.pos.y.whole -= 0x60u;
        const uint32_t i = (uint32_t(uint16_t(actor.rotation.y)) >> 6) & 0x3f;
        s.pos.x.whole -= uint16_t((int32_t(sin64(i)) << 5) >> 12);
        s.pos.z.whole -= uint16_t((int32_t(cos64(i)) << 5) >> 12);
        s.velocity.setXY(0, 0);
        s.velocity.z = int16_t(0xfd00);
        s.rotation.x = int16_t(pitchOf(s.pos, animatedPool()[size_t(slot)].pos) + 0x780); // exe 0x5e3ca0
        s.rotation.y = actor.rotation.y;
        s.rotation.z = 0;
        bomb.ticks = 0x64;
        bomb.attack = int8_t(animatedStats(actor.type).attack);
        return;
    }
    case ThrowStep::Lower:
        if (!actor.animEnded()) return;
        handsEmpty(actor);
        enterSub(actor, FightSub::Walk);
        return;
    default: return;
    }
}
enum class RunOffStep : uint8_t { Start = 0, FromEnemy = 1, FromPlayer = 2 };
// exe 0x4c2980: runs from the nearest enemy (or the player) for a while
void fightRunOff(AnimatedActor& actor, AppoDahFightFields& f) {
    int32_t slot = 0, dist = 0x7fffffff, angle = int32_t(slotIdentityWord(actor)); // see the file comment
    switch (RunOffStep(actor.step)) {
    case RunOffStep::Start:
        f.flags = 0;
        actor.anim = kAnimWalk;
        actor.velocity.z = int16_t(0xfe00);
        actor.step = uint8_t(RunOffStep::FromEnemy);
        if (nearestEnemy(actor, slot, dist, angle) != 0) {
            enterSub(actor, FightSub::Walk);
        } else {
            if (planarDistance(player().pos, actor.pos) < dist && (rngStep() & 0xf) == 0) {
                angle = angleOf(player().pos, actor.pos);
                actor.step = uint8_t(RunOffStep::FromPlayer);
            }
            f.timer = 0x30;
        }
        break;
    case RunOffStep::FromEnemy:
        f.timer = int16_t(uint16_t(f.timer) - 1u);
        if (f.timer == 0 || nearestEnemy(actor, slot, dist, angle) != 0) enterSub(actor, FightSub::Walk);
        break;
    case RunOffStep::FromPlayer:
        f.timer = int16_t(uint16_t(f.timer) - 1u);
        if (f.timer == 0) enterSub(actor, FightSub::Walk);
        break;
    default: break;
    }
    const int sh = int(actor.variant) + 1;
    if (actor.pos.z.whole < -0x300) {
        if (dist < 0x400) angle += -0x400 - (int32_t(f.timer) << sh);
        else if (dist < 0x800) angle -= 0x200;
    } else {
        if (dist < 0x400) angle += (int32_t(f.timer) << sh) + 0x400;
        else if (dist < 0x800) angle += 0x200;
    }
    turnYawTo(actor.rotation.y, uint32_t(angle), 0x40);
    moveForward(actor);
}
// exe 0x4c2b40: a stop (animation 0xd until it ends)
void fightStop(AnimatedActor& actor, AppoDahFightFields& f) {
    if (actor.step == 0) {
        f.flags = 0;
        actor.anim = kAnimStop;
        actor.velocity.z = 0;
        actor.enterStep(1);
    }
    if (actor.animEnded()) enterSub(actor, FightSub::Walk);
}
// the landing animations: knocked 5 / 6 -> down 7 / 8 -> up 9 / 10 (at the gate: lying 0xe / 0xf)
uint8_t downAnim(const AnimatedActor& actor) { return uint8_t((actor.anim != kAnimKnockedFront) + kAnimDownFront); }
uint8_t upAnim(const AnimatedActor& actor, uint8_t first) { return uint8_t((actor.anim != kAnimDownFront) + first); }
enum class FallStep : uint8_t { Start = 0, Fall = 1, Land = 2, Down = 3, Up = 4 };
// exe 0x4c2b80: walked off a ledge -- falls, lands
void fightFall(AnimatedActor& actor, AppoDahFightFields& f) {
    switch (FallStep(actor.step)) {
    case FallStep::Start:
        f.flags = kFightFalling;
        actor.step = uint8_t(FallStep::Fall);
        actor.anim = actor.velocity.z < 0 ? kAnimKnockedFront : kAnimKnockedBack;
        actor.acceleration.y = kFightGravity;
        [[fallthrough]];
    case FallStep::Fall:
        if (arc(actor, f, actor.rotation.y)) {
            actor.airborne = 0;
            actor.velocity.y = 0;
            actor.step = uint8_t(FallStep::Land);
        }
        [[fallthrough]];
    case FallStep::Land:
        if (actor.airborne == 0 && actor.animEnded()) {
            actor.step = uint8_t(FallStep::Down);
            actor.anim = downAnim(actor);
        }
        return;
    case FallStep::Down:
        if (actor.animEnded()) {
            actor.step = uint8_t(FallStep::Up);
            actor.anim = upAnim(actor, kAnimUpFront);
        }
        return;
    case FallStep::Up:
        if (actor.animEnded()) enterSub(actor, FightSub::Walk);
        return;
    default: return;
    }
}
// the knock-back's slide (its last steps): slides, dust; true once stopped
bool slide(AnimatedActor& actor, int yaw, int vy) {
    moveAlongYaw(actor.pos, yaw, 0, vy, actor.velocity.z);
    actor.velocity.z += uint16_t(actor.acceleration.z);
    dust(actor);
    return actor.velocity.z >= 0;
}
// the last step of the knock-backs: back to the fight or the gate
void getUpEnd(AnimatedActor& actor, AppoDahFightFields& f, bool searchRoute) {
    f.blinkTicks = 0x40;
    if (atGate(actor)) {
        actor.enterState(uint8_t(FightState::AtGate), uint8_t(AtGateSub::Arrive));
        return;
    }
    if (yardToRoute(actor, f)) return;
    if (searchRoute && (rngStep() & 7) != 0) {
        const int16_t x = actor.pos.x.whole;
        int k = kWaypointRows;
        for (; k != 0; --k)
            if (waypointRow(k).x[actor.variant] < x) break;
        f.nextWaypoint = int8_t(k == 0 ? 1 : k);
        enterSub(actor, FightSub::WalkBack);
        return;
    }
    enterSub(actor, FightSub::RunOff);
}
// the knock-back's turn: facing the hit -> its direction kept (animation 5), else turned round (animation 6); the
// direction the slide runs (0 / 0x800) when `writeDir`
void knockFacing(AnimatedActor& actor, AppoDahFightFields& f, bool writeDir) {
    const uint16_t h = f.knockYaw;
    if (facesHit(actor, f)) {
        actor.rotation.y = int16_t(h);
        if (writeDir) f.knockYaw = 0;
        actor.anim = kAnimKnockedFront;
    } else {
        if (!writeDir) actor.anim = kAnimKnockedBack;
        actor.rotation.y = int16_t(uint16_t(h - 0x800u));
        if (writeDir) {
            f.knockYaw = 0x800;
            actor.anim = kAnimKnockedBack;
        }
    }
}
enum class KnockStep : uint8_t { Hit = 0, Fly = 1, Slide = 2, Down = 3, Up = 4, Done = 5 };
// exe 0x4c2c60: hit -- knocked back through the air, slides, gets up
void fightKnockedBack(AnimatedActor& actor, AppoDahFightFields& f) {
    switch (KnockStep(actor.step)) {
    case KnockStep::Hit: {
        actor.enterStep(uint8_t(KnockStep::Fly));
        f.flags = kFightKnocked;
        actor.anim = kAnimDownFront;
        knockFacing(actor, f, true);
        const int p = hitPower(f);
        actor.acceleration.z = 0x20;
        actor.velocity.x = 0;
        actor.velocity.z = int16_t(-p);
        if (actor.airborne != 0) actor.velocity.y -= uint16_t(p);
        else actor.velocity.y = int16_t(-p);
        actor.airborne = 1;
        actor.acceleration.y = kFightGravity;
        sound(voiceOf(actor, kHurtAppo, kHurtDah), actor);
        [[fallthrough]];
    }
    case KnockStep::Fly:
        if (arc(actor, f, actor.rotation.y - int16_t(f.knockYaw))) {
            actor.airborne = 0;
            actor.velocity.z = int16_t(uint16_t(uint16_t(actor.velocity.z) << 1));
            ++actor.step;
            actor.velocity.y = 0;
            f.flags &= uint8_t(~(kFightReflect | kFightNoFall));
            sound(kSoundLand, actor);
        }
        return;
    case KnockStep::Slide:
        if (slide(actor, actor.rotation.y - int16_t(f.knockYaw), 0)) ++actor.step;
        return;
    case KnockStep::Down:
        if (actor.animEnded()) {
            actor.anim = downAnim(actor);
            ++actor.step;
        }
        return;
    case KnockStep::Up:
        if (actor.animEnded()) {
            actor.anim = upAnim(actor, kAnimUpFront);
            ++actor.step;
        }
        return;
    case KnockStep::Done:
        if (actor.animEnded()) getUpEnd(actor, f, true);
        return;
    default: return;
    }
}
enum class PushStep : uint8_t { Hit = 0, Slide = 1, Down = 2, Up = 3, Done = 4 };
// exe 0x4c2fb0: pushed (0x10000) -- slides away from the push, gets up
void fightPushed(AnimatedActor& actor, AppoDahFightFields& f) {
    switch (PushStep(actor.step)) {
    case PushStep::Hit:
        f.flags = kFightPushed;
        actor.enterStep(uint8_t(PushStep::Slide));
        knockFacing(actor, f, false);
        actor.velocity.z = int16_t(0xfe00);
        actor.acceleration.z = 0x20;
        actor.velocity.x = 0;
        if (actor.airborne != 0) actor.acceleration.y = kFightGravity;
        else actor.velocity.y = 0;
        sound(voiceOf(actor, kHurtAppo, kHurtDah), actor);
        sound(kSoundLand, actor);
        [[fallthrough]];
    case PushStep::Slide:
        if (slide(actor, int16_t(f.knockYaw), actor.velocity.y)) ++actor.step;
        return;
    case PushStep::Down:
        if (actor.animEnded()) {
            actor.anim = downAnim(actor);
            ++actor.step;
        }
        return;
    case PushStep::Up:
        if (actor.animEnded()) {
            actor.anim = upAnim(actor, kAnimUpFront);
            ++actor.step;
        }
        return;
    case PushStep::Done:
        if (actor.animEnded()) getUpEnd(actor, f, false);
        return;
    default: return;
    }
}
// exe 0x4c31f0: bumped into a wall -- falls back, gets up facing a cardinal direction
void fightWallFall(AnimatedActor& actor, AppoDahFightFields& f) {
    switch (PushStep(actor.step)) {
    case PushStep::Hit:
        f.flags = kFightPushed;
        f.knockYaw = uint16_t(uint16_t(actor.rotation.y) - 0x800u);
        actor.enterStep(uint8_t(PushStep::Slide));
        actor.anim = kAnimKnockedBack;
        actor.velocity.z = int16_t(0xfe00);
        actor.acceleration.z = 0x20;
        actor.velocity.x = 0;
        if (actor.airborne != 0) actor.acceleration.y = kFightGravity;
        else actor.velocity.y = 0;
        sound(kSoundWall, actor);
        sound(kSoundLand, actor);
        [[fallthrough]];
    case PushStep::Slide:
        if (slide(actor, int16_t(f.knockYaw), actor.velocity.y)) ++actor.step;
        return;
    case PushStep::Down:
        if (actor.animEnded()) {
            ++actor.step;
            actor.anim = kAnimDownBack;
        }
        return;
    case PushStep::Up:
        if (actor.animEnded()) {
            ++actor.step;
            actor.anim = kAnimUpBack;
        }
        return;
    case PushStep::Done: {
        if (!actor.animEnded()) return;
        const uint16_t y = (uint16_t(actor.rotation.y) - 0x800u) & 0xfff;
        actor.rotation.y = int16_t(y);
        if (y < 0x200) actor.rotation.y = 0;
        else if (y < 0x600) actor.rotation.y = 0x400;
        else if (y < 0xa00) actor.rotation.y = 0x800;
        else actor.rotation.y = int16_t(y >= 0xe00 ? 0u : 0xc00u);
        if (atGate(actor)) {
            actor.enterState(uint8_t(FightState::AtGate), uint8_t(AtGateSub::Arrive));
            return;
        }
        if (uint16_t(uint16_t(actor.pos.x.whole) - 0xa00) < 0x600) {
            actor.rotation.y = 0x400;
            f.prevWaypoint = 3;
            f.waypoint = 3;
            enterSub(actor, FightSub::WalkOn);
            return;
        }
        if (f.waypoint != f.prevWaypoint) {
            f.waypoint = f.prevWaypoint;
        } else if (f.waypoint != 3) {
            f.prevWaypoint = 3;
            f.waypoint = 3;
        } else {
            waypointAhead(actor, f, 5, 7);
        }
        enterSub(actor, FightSub::WalkOn);
        return;
    }
    default: return;
    }
}

// ---- state 2 sub-states [0x894d10] (at the gate, idle) ---------------------------------------------------------
// exe 0x4c3580: idle; 1 in 32 an emote
void gateIdle(AnimatedActor& actor, AppoDahFightFields& f) {
    if (actor.step == 0) {
        f.flags = 0;
        actor.enterStep(1);
    }
    if ((rngStep() & 0x1f) == 0) f.emote = int8_t(actor.anim == kAnimLieFront ? 1 : 2);
}
// exe 0x4c35e0: falls, lands (lying)
void gateFall(AnimatedActor& actor, AppoDahFightFields& f) {
    switch (FallStep(actor.step)) {
    case FallStep::Start:
        f.flags = kFightFalling;
        actor.step = uint8_t(FallStep::Fall);
        actor.anim = actor.velocity.z < 0 ? kAnimKnockedFront : kAnimKnockedBack;
        actor.acceleration.y = kFightGravity;
        [[fallthrough]];
    case FallStep::Fall:
        if (arc(actor, f, actor.rotation.y)) {
            actor.airborne = 0;
            actor.velocity.y = 0;
            actor.step = uint8_t(FallStep::Land);
            f.flags &= uint8_t(~kFightNoHits);
        }
        [[fallthrough]];
    case FallStep::Land:
        if (actor.airborne == 0 && actor.animEnded()) {
            actor.step = uint8_t(FallStep::Down);
            actor.anim = downAnim(actor);
        }
        return;
    case FallStep::Down:
        if (actor.animEnded()) {
            actor.step = uint8_t(FallStep::Up);
            actor.anim = upAnim(actor, kAnimLieFront);
        }
        return;
    case FallStep::Up:
        if (actor.animEnded()) enterSub(actor, GateIdleSub::Idle);
        return;
    default: return;
    }
}
enum class GateKnockStep : uint8_t { Hit = 0, Fly = 1, Slide = 2, Down = 3, Lie = 4 };
// exe 0x4c36d0: hit -- knocked back, lies (animations 0xe / 0xf)
void gateKnocked(AnimatedActor& actor, AppoDahFightFields& f) {
    switch (GateKnockStep(actor.step)) {
    case GateKnockStep::Hit: {
        actor.enterStep(uint8_t(GateKnockStep::Fly));
        f.flags = kFightKnocked;
        actor.anim = kAnimDownFront;
        knockFacing(actor, f, true);
        const int p = hitPower(f);
        actor.velocity.z = int16_t(-p);
        actor.velocity.y = int16_t(-p);
        actor.acceleration.z = 0x20;
        actor.velocity.x = 0;
        actor.airborne = 1;
        actor.acceleration.y = kFightGravity;
        sound(voiceOf(actor, kHurtAppo, kHurtDah), actor);
        [[fallthrough]];
    }
    case GateKnockStep::Fly:
        if (arc(actor, f, actor.rotation.y - int16_t(f.knockYaw))) {
            actor.airborne = 0;
            actor.velocity.z = int16_t(uint16_t(uint16_t(actor.velocity.z) << 1));
            ++actor.step;
            actor.velocity.y = 0;
            f.flags &= uint8_t(~(kFightReflect | kFightNoFall | kFightNoHits));
            sound(kSoundLand, actor);
        }
        return;
    case GateKnockStep::Slide:
        if (slide(actor, actor.rotation.y - int16_t(f.knockYaw), 0)) ++actor.step;
        return;
    case GateKnockStep::Down:
        if (actor.animEnded()) {
            actor.anim = downAnim(actor);
            ++actor.step;
        }
        return;
    case GateKnockStep::Lie:
        if (!actor.animEnded()) return;
        if (atGate(actor)) {
            actor.enterState(uint8_t(FightState::AtGate), uint8_t(AtGateSub::Arrive));
            return;
        }
        actor.anim = upAnim(actor, kAnimLieFront);
        enterSub(actor, GateIdleSub::Idle);
        return;
    default: return;
    }
}

// ---- state 3 sub-states [0x894d1c] (at the gate) ---------------------------------------------------------------
// the gate place's index: the variant against flag 0x580
uint32_t gatePlace(const AnimatedActor& actor) {
    return uint32_t(actor.variant) != uint32_t(storyFlag(kFlagDahAtGate)) ? 1u : 0u;
}
// exe 0x4c39e0: placed at the gate, facing the player (flag 0x585: script 0x1f's places, the player standing on it)
void gatePlaced(AnimatedActor& actor, AppoDahFightFields& f) {
    if (actor.step == 0) {
        f.flags = kFightAtGate;
        actor.enterStep(1);
        f.carried = 0;
        actor.airborne = 0;
        actor.velocity.clearWords();
        actor.rotation.x = 0;
        actor.rotation.z = 0;
        const uint32_t k = gatePlace(actor);
        if (storyFlag(kFlagScript1fPlaces)) {
            if (k == 0) {
                actor.anim = 0x1b;
                actor.rotation.y = 0x74a;
                f.animKey = 0x15;
            } else {
                actor.anim = 0x19;
                actor.rotation.y = 0xb6;
                f.animKey = 0x26;
            }
            actor.pos.x.whole = script1fX[k];
            actor.pos.z.whole = script1fZ[k];
            actor.pos.y.whole = int16_t(0xfe00);
            if (actor.variant == 0) f.standOn = 1;
        } else {
            actor.anim = kAnimStandStill;
            actor.pos.x.whole = gateX[k];
            actor.pos.z.whole = gateZ[k];
            f.flags &= uint8_t(~kFightNoHits);
        }
    }
    if (storyFlag(kFlagScript1fPlaces)) {
        f.ridden = playerRides(actor) ? 1 : 0; // exe 0x59beb0
        return;
    }
    turnYawTo(actor.rotation.y, uint32_t(angleOf(player().pos, actor.pos)), 0x100);
}
// exe 0x4c39b0: stands, then placed at once
void gateStart(AnimatedActor& actor, AppoDahFightFields& f) {
    stand(actor);
    f.flags = kFightGateIdle;
    actor.anim = kAnimStandStill;
    enterSub(actor, AtGateSub::Placed);
    gatePlaced(actor, f);
}
enum class ArriveStep : uint8_t { Start = 0, GetUp = 1, Walk = 2, Arrived = 3 };
// exe 0x4c3b10: arrived -- the gate flags, walks to the gate place
void gateArrive(AnimatedActor& actor, AppoDahFightFields& f) {
    switch (ArriveStep(actor.step)) {
    case ArriveStep::Start: {
        f.flags = kFightRunning;
        actor.flags |= kActorDrawn;
        actor.rotation.x = 0;
        actor.rotation.z = 0;
        actor.velocity.z = int16_t(0xff80);
        gateFlags(actor);
        const uint8_t an = actor.anim;
        actor.step = uint8_t(ArriveStep::GetUp);
        if (an == kAnimDownFront || an == kAnimLieFront) actor.anim = kAnimUpFront;
        else if (an == kAnimDownBack || an == kAnimLieBack) actor.anim = kAnimUpBack;
        else if (an == kAnimWalk) {
            actor.anim = kAnimStand;
            actor.step = uint8_t(ArriveStep::Walk);
        }
        return;
    }
    case ArriveStep::GetUp:
        if (actor.animEnded()) {
            actor.anim = kAnimStand;
            actor.step = uint8_t(ArriveStep::Walk);
        }
        return;
    case ArriveStep::Walk: {
        const uint32_t k = gatePlace(actor);
        const int16_t tx = gateX[k], tz = gateZ[k];
        turnYawTo(actor.rotation.y, uint32_t(angleFromPoint(tx, tz, actor.pos)), 0x100);
        integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
        if (uint16_t(uint16_t(actor.pos.x.whole) - uint16_t(tx) + 0x10) < 0x20 &&
            uint16_t(uint16_t(actor.pos.z.whole) - uint16_t(tz) + 0x10) < 0x20)
            actor.step = uint8_t(ArriveStep::Arrived);
        return;
    }
    case ArriveStep::Arrived: enterSub(actor, AtGateSub::Placed); return;
    default: return;
    }
}
// exe 0x4c3ca0: falls, lands
void gateFallDown(AnimatedActor& actor, AppoDahFightFields& f) {
    switch (FallStep(actor.step)) {
    case FallStep::Start:
        f.flags = kFightFalling;
        actor.step = uint8_t(FallStep::Fall);
        actor.anim = kAnimStand;
        actor.acceleration.y = kFightGravity;
        [[fallthrough]];
    case FallStep::Fall:
        if (arc(actor, f, actor.rotation.y)) {
            actor.airborne = 0;
            actor.velocity.y = 0;
            enterSub(actor, AtGateSub::Placed);
        }
        return;
    default: return;
    }
}
enum class GateHitStep : uint8_t { Hit = 0, Down = 1, Up = 2, Done = 3 };
// exe 0x4c3d00: hit at the gate (no knock-back)
void gateHit(AnimatedActor& actor, AppoDahFightFields& f) {
    switch (GateHitStep(actor.step)) {
    case GateHitStep::Hit:
        f.flags = kFightPushed;
        actor.enterStep(uint8_t(GateHitStep::Down));
        knockFacing(actor, f, false);
        actor.velocity.clearWords();
        actor.acceleration.clearWords();
        if (actor.airborne != 0) actor.acceleration.y = kFightGravity;
        else actor.velocity.y = 0;
        sound(voiceOf(actor, kHurtAppo, kHurtDah), actor);
        [[fallthrough]];
    case GateHitStep::Down:
        if (actor.animEnded()) {
            actor.anim = downAnim(actor);
            ++actor.step;
        }
        return;
    case GateHitStep::Up:
        if (actor.animEnded()) {
            actor.anim = upAnim(actor, kAnimUpFront);
            ++actor.step;
        }
        return;
    case GateHitStep::Done:
        if (actor.animEnded()) enterSub(actor, AtGateSub::Placed);
        return;
    default: return;
    }
}

// ---- state 4 sub-states [0x894d30] (lifted by the player) ------------------------------------------------------
// the walls / the ground of the throws, then the gate area's flags
void flightTail(AnimatedActor& actor, AppoDahFightFields& f) {
    collide(actor, f);
    if (f.collision & kFightWalls) {
        if (actor.airborne != 0) {
            if (rngStep() & 1) actor.rotation.y += uint16_t(rngStep() & 0x1ff);
            else actor.rotation.y -= uint16_t(rngStep() & 0x1ff);
            actor.velocity.z = int16_t(actor.velocity.z >> 1);
            actor.velocity.y = int16_t(0xfe00);
            sound(kSoundWall, actor);
        }
    }
    if (actor.airborne == 0 && inGateArea(actor)) gateFlags(actor);
}
// hit while thrown (0x20000): turned round, a spark, -500 balance
void thrownHit(AnimatedActor& actor, AppoDahFightFields& f) {
    if (!(f.hitBox.result & kHitThrownHit)) return;
    actor.rotation.y += 0x800u;
    f.flags |= kFightNoHitEntry;
    spawnHitSparkWithoutSize(kActorSweepGroup2, actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole - 0x40)),
                             actor.pos.z.whole); // exe 0x5e5400
    balanceAdd(kBalanceLoss);                    // exe 0x5e51d0
}
// the throws' "landed -> slide" step
void landSlide(AnimatedActor& actor, AppoDahFightFields& f) {
    actor.velocity.z = int16_t(uint16_t(uint16_t(actor.velocity.z) << 1));
    actor.airborne = 0;
    actor.acceleration.z = 0x20;
    actor.velocity.y = 0;
    f.flags = uint8_t((f.flags & uint8_t(~kFightReflect)) | kFightKeepGround);
}
enum class ThrownStep : uint8_t { Fly = 0, Slide = 1, Down = 2, Up = 3 };
// exe 0x4c4030: thrown (alive) -- flies, lands, slides, gets up
void liftedThrown(AnimatedActor& actor, AppoDahFightFields& f) {
    thrownHit(actor, f);
    switch (ThrownStep(actor.step)) {
    case ThrownStep::Fly:
        if (arc(actor, f, actor.rotation.y)) {
            landSlide(actor, f);
            ++actor.step;
            actor.anim = kAnimDownFront;
            sound(kSoundLand, actor);
        }
        break;
    case ThrownStep::Slide:
        if (slide(actor, actor.rotation.y, actor.velocity.y)) ++actor.step;
        break;
    case ThrownStep::Down:
        if (actor.animEnded()) {
            ++actor.step;
            actor.anim = kAnimUpFront;
        }
        break;
    case ThrownStep::Up:
        if (!actor.animEnded()) break;
        if (atGate(actor)) {
            actor.enterState(uint8_t(FightState::AtGate), uint8_t(AtGateSub::Arrive));
        } else {
            waypointAhead(actor, f, 8, 5);
            actor.enterState(uint8_t(FightState::Fight));
        }
        f.flags &= uint8_t(~kFightKeepGround);
        break;
    default: break;
    }
    flightTail(actor, f);
}
enum class TumbleStep : uint8_t { Fly = 0, Roll = 1, Turn = 2, Done = 3 };
// exe 0x4c4320: thrown with no life left -- tumbles, rolls on the ground
void liftedThrownDown(AnimatedActor& actor, AppoDahFightFields& f) {
    thrownHit(actor, f);
    switch (TumbleStep(actor.step)) {
    case TumbleStep::Fly:
        actor.rotation.x += 0x200u;
        if (arc(actor, f, actor.rotation.y)) {
            landSlide(actor, f);
            sound(kSoundLand, actor);
            ++actor.step;
        }
        break;
    case TumbleStep::Roll: {
        actor.rotation.x += 0x200u;
        moveAlongYaw(actor.pos, actor.rotation.y, 0, actor.velocity.y, actor.velocity.z);
        actor.velocity.z += uint16_t(actor.acceleration.z);
        dust(actor);
        const int16_t vz = actor.velocity.z;
        if (vz <= -0x100) break;
        if (int16_t(uint16_t(actor.rotation.x) & 0xfff) > 0x800) {
            actor.rotation.y += 0xfc00u;
            actor.anim = kAnimLieFront;
            actor.velocity.x = vz;
            actor.acceleration.x = actor.acceleration.z;
        } else {
            actor.rotation.y += 0x400u;
            actor.anim = kAnimLieBack;
            actor.velocity.x = int16_t(-vz);
            actor.acceleration.x = int16_t(-actor.acceleration.z);
        }
        actor.velocity.z = 0;
        actor.acceleration.z = 0;
        actor.rotation.x = 0;
        actor.step = uint8_t(TumbleStep::Turn);
        break;
    }
    case TumbleStep::Turn: {
        moveAlongYaw(actor.pos, actor.rotation.y, actor.velocity.x, actor.velocity.y, 0);
        const int16_t acc = actor.acceleration.x;
        actor.velocity.x += uint16_t(acc);
        const int16_t vx = actor.velocity.x;
        if (acc > 0 ? vx >= 0 : vx <= 0) actor.step = uint8_t(TumbleStep::Done);
        break;
    }
    case TumbleStep::Done:
        f.flags &= uint8_t(~kFightKeepGround);
        if (atGate(actor)) actor.enterState(uint8_t(FightState::AtGate), uint8_t(AtGateSub::Arrive));
        else actor.enterState(uint8_t(FightState::GateIdle));
        return;
    default: break;
    }
    flightTail(actor, f);
}
// exe 0x4c3e60: carried -- at the player's hands until thrown (alive / down) or dropped
void liftedCarried(AnimatedActor& actor, AppoDahFightFields& f) {
    if (actor.step == 0) {
        f.flags = kFightCarried;
        actor.step = 1;
        f.carried = 1;
        actor.anim = actor.hitPoints < 0 ? kAnimCarriedDown : kAnimCarried;
        applyGrey(actor, f);
        f.timer = 0;
    }
    if (actor.hitPoints >= 0) {
        if (f.timer == 0) {
            if ((rngStep() & 0xf) == 0) {
                sound(voiceOf(actor, kCarriedAppo, kCarriedDah), actor);
                f.timer = 0x28;
            }
        } else {
            f.timer = int16_t(uint16_t(f.timer) - 1u);
        }
    }
    carryAtHands(actor, 0, -0x18, 0);         // exe 0x573ab0
    if (const int rel = int(carryReleased())) { // exe 0x573950
        f.flags &= uint8_t(~kFightNoHitEntry);
        f.carried = 0;
        f.knockYaw = uint16_t(player().rotation.y);
        if (rel == 1) {
            int32_t v[3];
            playerAimVector(0, -0x100, -0x380, v); // exe 0x5e3e50
            actor.velocity.y = int16_t(v[1]);
            actor.velocity.z = int16_t(v[2]);
            f.flags |= kFightThrown;
        } else {
            actor.velocity.y = int16_t(0xfd00);
            actor.velocity.z = int16_t(0xffc0);
        }
        actor.velocity.x = 0;
        actor.acceleration.x = 0;
        actor.acceleration.y = kFightGravity;
        actor.rotation.y = int16_t(f.knockYaw);
        actor.airborne = 1;
        carryDropFromPlayer(actor); // exe 0x573990
        if (actor.hitPoints < 0) {
            enterSub(actor, LiftedSub::ThrownDown);
            liftedThrownDown(actor, f);
        } else {
            enterSub(actor, LiftedSub::Thrown);
            liftedThrown(actor, f);
        }
    }
    if (actor.hitPoints < 0 && (rngStep() & 0x1f) == 0) f.emote = 3;
}

// ---- states [0x894ccc] -----------------------------------------------------------------------------------------
// exe 0x4c1c90: the model, the boxes, the life, waypoint 0; flag 0x581 + v -> at the gate, else the fight
void fightInit(AnimatedActor& actor, AppoDahFightFields& f) {
    actor.flags |= kActorDrawn;
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0(actor, +6)
        freeActor(actor);                         // exe 0x5e1490
        return;
    }
    actor.rotation.x = 0;
    actor.rotation.z = 0;
    actor.anim = kAnimStand;
    actor.prevAnim = kNoAnim;
    actor.runFlags |= kActorRunMarked;
    const uint32_t life = hasAnimatedStats(actor.type) ? uint32_t(animatedStats(actor.type).hitPoints) : 0;
    actor.maxHitPoints = int16_t(life);
    actor.collisionBox.pointAt(TableId::AppoDahBFightBox);
    actor.hitPoints = int16_t(life << 4);
    actor.interactionBox.pointAt(TableId::AppoDahBFightLiftBox);
    actor.interaction = kInteractLift;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock();
    f.hitBox.previousX = actor.pos.x.whole;
    f.hitBox.previousZ = actor.pos.z.whole;
    f.hitBox.result = 0;
    f.waypoint = 0;
    f.nextWaypoint = 0;
    f.prevWaypoint = 0;
    f.blinkTicks = 0;
    f.grey = 0x10;
    f.animKey = 0;
    f.standOn = 0;
    if (atGate(actor)) {
        actor.interaction = 0;
        actor.enterState(uint8_t(FightState::AtGate));
    } else {
        actor.enterState(uint8_t(FightState::Fight));
    }
    actor.pos.y.whole = int16_t(actorGround(*E, actor)); // exe 0x40f190
    registerEnemy(actor.slot, 0);                         // exe 0x5e5c10
    f.carried = 0;
}
void gateIdleState(AnimatedActor& actor, AppoDahFightFields& f);
// state 1's sub-state
void fightSub(AnimatedActor& actor, AppoDahFightFields& f) {
    switch (FightSub(actor.subState)) {
    case FightSub::Start: fightStart(actor, f); return;
    case FightSub::Walk: fightWalk(actor, f); return;
    case FightSub::WalkOn: walkWaypoint(actor, f); return;
    case FightSub::WalkBack: fightWalkBack(actor, f); return;
    case FightSub::Stand: fightStand(actor, f); return;
    case FightSub::Throw: fightThrow(actor, f); return;
    case FightSub::RunOff: fightRunOff(actor, f); return;
    case FightSub::Stop: fightStop(actor, f); return;
    case FightSub::Fall: fightFall(actor, f); return;
    case FightSub::KnockedBack: fightKnockedBack(actor, f); return;
    case FightSub::Pushed: fightPushed(actor, f); return;
    case FightSub::WallFall: fightWallFall(actor, f); return;
    default: notPortedAt(kStoryPort, "class 0x4c1b30 state 1 sub", actor.subState);
    }
}
// lifted -> state 4 (the hit entry off)
bool liftCheck(AnimatedActor& actor, AppoDahFightFields& f) {
    if (!playerLiftTargetCheck(actor, 0)) return false; // exe 0x573a60
    actor.enterState(uint8_t(FightState::Lifted));
    f.flags |= kFightNoHitEntry;
    return true;
}
// exe 0x4c1db0: the fight
void fightState(AnimatedActor& actor, AppoDahFightFields& f) {
    applyGrey(actor, f);
    const uint8_t blink = f.blinkTicks;
    if (blink != 0) {
        const uint8_t n = uint8_t(blink - 1u);
        f.blinkTicks = n;
        if (n == 0) {
            actor.flags |= kActorDrawn;
            f.flags &= uint8_t(~kFightNoHitEntry);
        } else if (n & 3) {
            actor.flags |= kActorDrawn;
        } else {
            actor.flags &= uint8_t(~kActorDrawn);
        }
    } else if (!(f.flags & kFightNoHits)) {
        const int h = hitsTaken(actor, f);
        if (h != 0) {
            handsEmpty(actor);
            if (h <= 0) {
                actor.enterState(uint8_t(FightState::GateIdle), uint8_t(GateIdleSub::Knocked));
                gateIdleState(actor, f);
                return;
            }
            enterSub(actor, (h & 2) ? FightSub::Pushed : FightSub::KnockedBack);
        }
    }
    fightSub(actor, f);
    if (f.blinkTicks != 0) f.flags |= kFightNoHitEntry;
    if (collide(actor, f)) {
        actor.airborne = 1;
        if (!(f.flags & kFightNoFall)) enterSub(actor, FightSub::Fall);
    } else if ((f.collision & kFightWalls) && !(f.flags & kFightNoWallFall)) {
        enterSub(actor, FightSub::WallFall);
    }
    if (playerLiftTargetCheck(actor, 0)) {
        f.blinkTicks = 0;
        actor.flags |= kActorDrawn;
        handsEmpty(actor);
        actor.enterState(uint8_t(FightState::Lifted));
        f.flags |= kFightNoHitEntry;
        return;
    }
    if (actor.airborne == 0 && inGateArea(actor)) {
        gateFlags(actor);
        if (actor.subState < uint8_t(FightSub::KnockedBack))
            actor.enterState(uint8_t(FightState::AtGate), uint8_t(AtGateSub::Arrive));
    }
}
// exe 0x4c3480: at the gate, idle (no life left)
void gateIdleState(AnimatedActor& actor, AppoDahFightFields& f) {
    applyGrey(actor, f);
    if (!(f.flags & kFightNoHits) && (hitsTaken(actor, f) & 1)) enterSub(actor, GateIdleSub::Knocked);
    switch (GateIdleSub(actor.subState)) {
    case GateIdleSub::Idle: gateIdle(actor, f); break;
    case GateIdleSub::Fall: gateFall(actor, f); break;
    case GateIdleSub::Knocked: gateKnocked(actor, f); break;
    default: notPortedAt(kStoryPort, "class 0x4c1b30 state 2 sub", actor.subState);
    }
    if (collide(actor, f)) {
        actor.airborne = 1;
        if (!(f.flags & kFightNoFall)) enterSub(actor, GateIdleSub::Fall);
    }
    if (liftCheck(actor, f)) return;
    if (actor.airborne == 0 && inGateArea(actor)) gateFlags(actor);
}
// exe 0x4c3930: at the gate
void atGateState(AnimatedActor& actor, AppoDahFightFields& f) {
    applyGrey(actor, f);
    if (!(f.flags & kFightNoHits) && hitsTaken(actor, f) != 0) enterSub(actor, AtGateSub::Hit);
    switch (AtGateSub(actor.subState)) {
    case AtGateSub::Start: gateStart(actor, f); break;
    case AtGateSub::Placed: gatePlaced(actor, f); break;
    case AtGateSub::Arrive: gateArrive(actor, f); break;
    case AtGateSub::Fall: gateFallDown(actor, f); break;
    case AtGateSub::Hit: gateHit(actor, f); break;
    default: notPortedAt(kStoryPort, "class 0x4c1b30 state 3 sub", actor.subState);
    }
    if (collide(actor, f)) {
        actor.airborne = 1;
        if (!(f.flags & kFightNoFall)) enterSub(actor, AtGateSub::Fall);
    }
}
// exe 0x4c3e40: lifted / thrown
void liftedState(AnimatedActor& actor, AppoDahFightFields& f) {
    switch (LiftedSub(actor.subState)) {
    case LiftedSub::Carried: liftedCarried(actor, f); return;
    case LiftedSub::Thrown: liftedThrown(actor, f); return;
    case LiftedSub::ThrownDown: liftedThrownDown(actor, f); return;
    default: notPortedAt(kStoryPort, "class 0x4c1b30 state 4 sub", actor.subState);
    }
}
// exe 0x4c46b0: the emote effect (1..3 -> pool 0xa0 type 3 variant 0x12..0x14 at bone 1 + an offset)
void emote(AnimatedActor& actor, AppoDahFightFields& f) {
    const int k = f.emote;
    int16_t v[3] = {0, 0, 0};
    if (k == 1) v[2] = -0xe;
    else if (k == 3) {
        v[1] = -9;
        v[2] = -0xf;
    }
    int16_t o[3];
    bonePointOffset(actor, 1, v, o); // exe 0x5e1970
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    e.flags = kActorLiveDrawn;
    e.type = 3;
    e.pos.x = o[0];
    e.pos.y = o[1];
    e.pos.z = o[2];
    e.setRecordYaw(0); // the exe copies an unset stack word here
    switch (k) {
    case 1:
        e.variant = 0x12;
        e.setParamsLowWord(uint16_t(actor.rotation.y));
        return;
    case 2:
        e.variant = 0x13;
        e.setParamsLowWord(uint16_t(uint16_t(actor.rotation.y) + 0x3d4u));
        return;
    case 3:
        e.variant = 0x14;
        e.setParamsLowWord(uint16_t(actor.rotation.y));
        return;
    default: return;
    }
}
// exe 0x4c1b30's body
void appoDahFight(AnimatedActor& actor) {
    AppoDahFightFields& f = classState<AppoDahFightFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    f.animKey = 0;
    f.emote = 0;
    switch (FightState(actor.state)) {
    case FightState::Init: fightInit(actor, f); break;
    case FightState::Fight: fightState(actor, f); break;
    case FightState::GateIdle: gateIdleState(actor, f); break;
    case FightState::AtGate: atGateState(actor, f); break;
    case FightState::Lifted: liftedState(actor, f); break;
    default: notPortedAt(kStoryPort, "class 0x4c1b30 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStepPose(actor);  // exe 0x5e1e80
    else animSetPose(actor, actor.anim, f.animKey);         // exe 0x5e1c30
    if (f.emote != 0) emote(actor, f);
    const AnimatedActorStats row = animatedStats(actor.type);
    const uint8_t flags = f.flags;
    const ShortVec4* bp = bonePointOrNull(actor, 0); // exe 0x5e1810
    f.hitBox.result = 0;
    f.hitBox.previousX = actor.prevPos.x.whole;
    f.hitBox.previousZ = actor.prevPos.z.whole;
    if (!(flags & kFightNoHitEntry)) {
        if (bp == 0) notPorted(kStoryPort, "class 0x4c1b30: exe 0x5e1810 returned null");
        const ShortVec4& point = *bp;
        const int16_t p[3] = {point.x, point.y, point.z};
        if (!(flags & kFightThrown))
            hitListSubmit(p, f.hitBox, uint32_t(actor.slot) << 24 | 0xa8800au, 0x830000); // exe 0x5e4120
        else
            hitListSubmit(p, f.hitBox, uint32_t(actor.slot) << 24 | 0x20800au, uint32_t(row.bodyAttack) | 0x840000u);
    }
    shadow(actor); // (not while lifted at its first step: the shadow is render output)
    if (f.standOn != 0) actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050(actor, 1, 0xffff, 0)
}

constexpr uint8_t kPuppetHold = 0x01; // the animation restarted at its key every tick (the pose held)
enum class PuppetState : uint8_t { Bind = 0, Act0 = 1, Act1 = 2, Act2 = 3, Act3 = 4, Act4 = 5 };
enum class BindSub : uint8_t { Bind = 0, WaitScript = 1 };
constexpr uint16_t kHurtTint = 0x2108;
// the routines of the script puppets (exe tables 0x894e88 by the variant / 0x894eb8: the named routines of
// engine/routines.csv)
const DataTable<RoutineTable<4>> script1dRoutines{TableId::AppoDahScript1dRoutines};
const DataTable<RoutineTable<4>> script1fRoutines{TableId::AppoDahScript1fRoutines};

// the state 0 of the script puppets: the model, the box, the class block cleared, the pose held, the mouth closed
bool puppetBind(AnimatedActor& actor, ScriptPuppetFields& f, uint32_t box, uint8_t anim) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0(actor, +6)
        freeActor(actor);
        return false;
    }
    actor.collisionBox.pointAt(box);
    actor.clearClassBlock();
    actor.anim = anim;
    actor.prevAnim = kNoAnim;
    f.castFlags |= kPuppetHold;
    appoDahFaceReset(actor, f.mouth);
    return true;
}
// state 0: bound once, then waiting for the script
void puppetStart(AnimatedActor& actor, ScriptPuppetFields& f, uint8_t anim, uint32_t box) {
    if (BindSub(actor.subState) == BindSub::Bind) {
        if (puppetBind(actor, f, box, anim)) actor.subState = uint8_t(BindSub::WaitScript);
        return;
    }
    if (scriptState().state != 0) actor.enterState(uint8_t(PuppetState::Act0));
}
// the animation asked, the pose free
void playFree(AnimatedActor& actor, ScriptPuppetFields& f, uint8_t anim) {
    actor.anim = anim;
    f.castFlags &= uint8_t(~kPuppetHold);
}
// the state `st` once the step sub is `sub`
void followSub(AnimatedActor& actor, uint8_t sub, PuppetState st) {
    if (scriptState().stepSub == sub) actor.enterState(uint8_t(st));
}
void place(AnimatedActor& actor, uint16_t x, uint16_t y, uint16_t z) {
    actor.pos.x.whole = int16_t(x);
    actor.pos.y.whole = int16_t(y);
    actor.pos.z.whole = int16_t(z);
}
enum class AppoAct1Step : uint8_t { Place = 0, Wait = 1, Done = 2 };
enum class AppoAct2Step : uint8_t { Place = 0, Turn = 1, Talk = 2, Wait = 3, TalkMore = 4, Pause = 5, TalkOn = 6,
                                    TalkLast = 7, Done = 8 };
enum class AppoAct4Step : uint8_t { Wait = 0, Leave = 1 };
// exe 0x4c6070: Appo of script 0x1d (variant 0)
void script1dAppo(AnimatedActor& actor, ScriptPuppetFields& f) {
    const int32_t t = scriptState().stepTick;
    switch (PuppetState(actor.state)) {
    case PuppetState::Bind: puppetStart(actor, f, 0x16, stateAddress(TableId::AppoDahBScript1dPuppetBox)); return;
    case PuppetState::Act0: followSub(actor, 1, PuppetState::Act1); return;
    case PuppetState::Act1:
        switch (AppoAct1Step(actor.step)) {
        case AppoAct1Step::Place:
            place(actor, 0xeed4, 0xfd21, 0xf914);
            actor.step = uint8_t(AppoAct1Step::Wait);
            break;
        case AppoAct1Step::Wait:
            if (t == 0x3c) {
                playFree(actor, f, 0x16);
                actor.step = uint8_t(AppoAct1Step::Done);
            }
            break;
        default: break;
        }
        followSub(actor, 2, PuppetState::Act2);
        return;
    case PuppetState::Act2:
        switch (AppoAct2Step(actor.step)) {
        case AppoAct2Step::Place:
            place(actor, 0xeed4, 0xfd21, 0xf92c);
            actor.step = uint8_t(AppoAct2Step::Turn);
            break;
        case AppoAct2Step::Turn:
            if (t == 0xa) {
                appoDahFaceReset(actor, f.mouth);
                playFree(actor, f, 0x13);
                ++actor.step;
            }
            break;
        case AppoAct2Step::Talk:
            appoDahTalk(actor, f.mouth);
            if (t == 0x9b) {
                appoDahFaceReset(actor, f.mouth);
                ++actor.step;
            }
            break;
        case AppoAct2Step::Wait:
            if (t == 0x9e) {
                playFree(actor, f, 0x14);
                ++actor.step;
            }
            break;
        case AppoAct2Step::TalkMore:
            appoDahTalk(actor, f.mouth);
            if (t == 0x154) {
                appoDahFaceReset(actor, f.mouth);
                ++actor.step;
            }
            break;
        case AppoAct2Step::Pause:
            if (t == 0x15e) ++actor.step;
            break;
        case AppoAct2Step::TalkOn:
            appoDahTalk(actor, f.mouth);
            if (t == 0x1ae) {
                appoDahFaceReset(actor, f.mouth);
                ++actor.step;
            }
            break;
        case AppoAct2Step::TalkLast:
            appoDahTalk(actor, f.mouth);
            if (t == 0x1c2) {
                playFree(actor, f, 0x15);
                ++actor.step;
            }
            break;
        default: break;
        }
        followSub(actor, 3, PuppetState::Act3);
        return;
    case PuppetState::Act3: followSub(actor, 4, PuppetState::Act4); return;
    case PuppetState::Act4:
        if (AppoAct4Step(actor.step) == AppoAct4Step::Wait) {
            if (actor.animEnded()) {
                playFree(actor, f, kAnimWalk);
                actor.step = uint8_t(AppoAct4Step::Leave);
            }
        } else if (AppoAct4Step(actor.step) == AppoAct4Step::Leave) {
            integrateVelocity(actor, 0, 0, -0x180); // exe 0x5e3690
        }
        return;
    default: return;
    }
}
enum class DahAct1Step : uint8_t { Wait = 0, Raise = 1, Talk = 2, Done = 3 };
enum class DahAct2Step : uint8_t { Place = 0, Wait = 1, Done = 2 };
// the hands of script 0x1d's Dah (the raised hand shown / hidden)
constexpr size_t kRaisedHandPart = 7;
// exe 0x4c6310: Dah of script 0x1d (variant 1)
void script1dDah(AnimatedActor& actor, ScriptPuppetFields& f) {
    const int32_t t = scriptState().stepTick;
    switch (PuppetState(actor.state)) {
    case PuppetState::Bind: puppetStart(actor, f, 0x10, stateAddress(TableId::AppoDahBScript1dPuppetBox)); return;
    case PuppetState::Act0: followSub(actor, 1, PuppetState::Act1); return;
    case PuppetState::Act1:
        switch (DahAct1Step(actor.step)) {
        case DahAct1Step::Wait:
            if (t == 0x1e) {
                playFree(actor, f, 0x10);
                hidePart(actor, kEmptyHandPart);
                showPart(actor, kRaisedHandPart);
                actor.step = uint8_t(DahAct1Step::Raise);
            }
            break;
        case DahAct1Step::Raise:
            if (t == 0x20) {
                appoDahFaceReset(actor, f.mouth);
                actor.step = uint8_t(DahAct1Step::Talk);
            }
            break;
        case DahAct1Step::Talk:
            appoDahTalk(actor, f.mouth);
            if (t == 0x7a) {
                appoDahFaceReset(actor, f.mouth);
                actor.step = uint8_t(DahAct1Step::Done);
            }
            break;
        default: break;
        }
        followSub(actor, 2, PuppetState::Act2);
        return;
    case PuppetState::Act2:
        switch (DahAct2Step(actor.step)) {
        case DahAct2Step::Place:
            place(actor, 0xef12, 0xfd2a, 0xf961);
            actor.step = uint8_t(DahAct2Step::Wait);
            [[fallthrough]];
        case DahAct2Step::Wait:
            if (t == 2) {
                playFree(actor, f, 0x11);
                hidePart(actor, kRaisedHandPart);
                showPart(actor, kEmptyHandPart);
                actor.step = uint8_t(DahAct2Step::Done);
            }
            break;
        default: break;
        }
        followSub(actor, 3, PuppetState::Act3);
        return;
    case PuppetState::Act3: followSub(actor, 4, PuppetState::Act4); return;
    case PuppetState::Act4:
        if (actor.step == 0) {
            actor.rotation.y = 0xc80;
            playFree(actor, f, 0x12);
            actor.step = 1;
        }
        return;
    default: return;
    }
}
// the class tail of exe 0x4c5f90 / 0x4c6ae0: the ground, the animation (the pose held), the shadow
void puppetTail(AnimatedActor& actor, ScriptPuppetFields& f) {
    if (!(actor.flags & kActorLive)) return;
    if (f.noGround == 0) actor.pos.y.whole = int16_t(actorGround(*E, actor)); // exe 0x40f190
    if (actor.anim == actor.prevAnim && !(f.castFlags & kPuppetHold)) animStepPose(actor); // exe 0x5e1e80
    else animSetPose(actor, actor.anim, f.animKey);                                      // exe 0x5e1c30
    shadow(actor);
}
// exe 0x4c5f90's body: the routine of its variant's table
void script1dPuppet(AnimatedActor& actor) {
    ScriptPuppetFields& f = classState<ScriptPuppetFields>(actor);
    castSavePose(actor);
    const size_t base = actor.variant == 0 ? 0 : 1;
    const size_t k = base + size_t(int32_t(f.routine));
    switch (script1dRoutines->idAt(k)) {
    case RoutineId::AppoDahScript1dAppo: script1dAppo(actor, f); break;
    case RoutineId::AppoDahScript1dDah: script1dDah(actor, f); break;
    default: notPortedAt(kStoryPort, "class 0x4c5f90 variant fn", script1dRoutines->pointerAt(k));
    }
    puppetTail(actor, f);
}

enum class Script1fAct0Step : uint8_t { Start = 0, Talk = 1, Walk = 2, WalkTalk = 3, Wait = 4, Answer = 5, Pause = 6,
                                        Done = 7 };
enum class Script1fAct1Step : uint8_t { Wait = 0, Talk = 1, Done = 2 };
enum class Script1fAct3Step : uint8_t { Wait = 0, TalkFirst = 1, WaitOther = 2, TalkSecond = 3, Done = 4 };
// exe 0x4c6bb0: Appo / Dah of script 0x1f
void script1fRoutine(AnimatedActor& actor, ScriptPuppetFields& f) {
    const int32_t t = scriptState().stepTick;
    switch (PuppetState(actor.state)) {
    case PuppetState::Bind:
        if (BindSub(actor.subState) == BindSub::Bind) {
            if (!puppetBind(actor, f, stateAddress(TableId::AppoDahBScript1fPuppetBox), 0)) return;
            if (storyFlag(kFlagAppoDahHurt + actor.variant)) actor.tint = kHurtTint; // knocked out in the fight: grey
            actor.subState = uint8_t(BindSub::WaitScript);
            return;
        }
        if (scriptState().state != 0) actor.enterState(uint8_t(PuppetState::Act0));
        return;
    case PuppetState::Act0:
        switch (Script1fAct0Step(actor.step)) {
        case Script1fAct0Step::Start:
            playFree(actor, f, 0);
            if (f.speaker == 0) {
                actor.step = uint8_t(Script1fAct0Step::Wait);
                break;
            }
            actor.step = uint8_t(Script1fAct0Step::Talk);
            [[fallthrough]];
        case Script1fAct0Step::Talk:
            appoDahTalk(actor, f.mouth);
            if (t == 0x33) {
                actor.anim = kAnimStand;
                f.animKey = 0xa;
                ++actor.step;
            }
            break;
        case Script1fAct0Step::Walk:
            integrateVelocity(actor, 0, 0, -0x80);
            appoDahTalk(actor, f.mouth);
            if (t == 0x3c) {
                f.animKey = 0;
                actor.anim = 0x19;
                ++actor.step;
            }
            break;
        case Script1fAct0Step::WalkTalk:
            integrateVelocity(actor, 0, 0, -0x80);
            appoDahTalk(actor, f.mouth);
            if (t == 0x46) {
                appoDahFaceReset(actor, f.mouth);
                actor.step = uint8_t(Script1fAct0Step::Done);
            }
            break;
        case Script1fAct0Step::Wait:
            if (t == 0x37) {
                playFree(actor, f, 0x1a);
                ++actor.step;
            }
            break;
        case Script1fAct0Step::Answer:
            appoDahTalk(actor, f.mouth);
            if (t == 0x49) {
                appoDahFaceReset(actor, f.mouth);
                ++actor.step;
            }
            break;
        case Script1fAct0Step::Pause:
            if (t == 0x50) {
                actor.anim = 0x1b;
                actor.step = uint8_t(Script1fAct0Step::Done);
            }
            break;
        default: break;
        }
        followSub(actor, 1, PuppetState::Act1);
        return;
    case PuppetState::Act1:
        switch (Script1fAct1Step(actor.step)) {
        case Script1fAct1Step::Wait:
            if (t == 0x1e) actor.step = uint8_t((actor.variant != 0) + 1u);
            break;
        case Script1fAct1Step::Talk:
            appoDahTalk(actor, f.mouth);
            if (t == 0x64) {
                appoDahFaceReset(actor, f.mouth);
                actor.step = uint8_t(Script1fAct1Step::Done);
            }
            break;
        default: break;
        }
        followSub(actor, 2, PuppetState::Act2);
        return;
    case PuppetState::Act2: followSub(actor, 3, PuppetState::Act3); return;
    case PuppetState::Act3:
        switch (Script1fAct3Step(actor.step)) {
        case Script1fAct3Step::Wait:
            if (t == 0xa) actor.step = uint8_t((actor.variant == 0) + 1u);
            return;
        case Script1fAct3Step::TalkFirst:
            appoDahTalk(actor, f.mouth);
            if (t == 0x3c) {
                appoDahFaceReset(actor, f.mouth);
                actor.step = uint8_t(Script1fAct3Step::Done);
            }
            return;
        case Script1fAct3Step::WaitOther:
            if (t == 0x50) actor.step = uint8_t(Script1fAct3Step::TalkSecond);
            return;
        case Script1fAct3Step::TalkSecond:
            appoDahTalk(actor, f.mouth);
            if (t == 0x96) {
                appoDahFaceReset(actor, f.mouth);
                actor.step = uint8_t(Script1fAct3Step::Done);
            }
            return;
        default: return;
        }
    default: return;
    }
}
// exe 0x4c6ae0's body
void script1fPuppet(AnimatedActor& actor) {
    ScriptPuppetFields& f = classState<ScriptPuppetFields>(actor);
    castSavePose(actor);
    const size_t k = size_t(int32_t(f.routine));
    if (script1fRoutines->idAt(k) == RoutineId::AppoDahScript1fPuppet) script1fRoutine(actor, f);
    else notPortedAt(kStoryPort, "class 0x4c6ae0 variant fn", script1fRoutines->pointerAt(k));
    puppetTail(actor, f);
}

// ---- pool 0x40 class exe 0x5c8220 (ST1F phase table 0x8e7908 type 0x15): the bomb Appo / Dah throw. The previous
// position / rotation and airborne byte, states [0x8d138c]: 0 0x5c8260 (init), 1 0x5c82d0 (flight + hit entry + the
// billboard, draw command 0x2c), 2 0x5d0bc0 (freed).
enum class BombState : uint8_t { Init = 0, Flight = 1, Free = 2 };
void appoDahBomb(ProjectileActor& actor) {
    AppoDahBombFields& f = classState<AppoDahBombFields>(actor);
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    switch (BombState(actor.state)) { // [0x8d138c]
    case BombState::Init: { // exe 0x5c8260
        actor.enterState(uint8_t(BombState::Flight));
        actor.runFlags |= kActorWarpKept;
        actor.collisionBox.pointAt(TableId::AppoDahBBombBox);
        f.hitBox.previousX = actor.pos.x.whole;
        f.hitBox.previousZ = actor.pos.z.whole;
        f.hitBox.result = 0;
        const uint32_t r = rngStep();
        f.spin = uint16_t((r & 0x1fu) << 7);
        actor.acceleration.y = 0;
        return;
    }
    case BombState::Flight: { // exe 0x5c82d0
        moveAlongRotation(actor.pos, actor.rotation, actor.velocity.x, actor.velocity.y, actor.velocity.z); // exe 0x5e37b0
        const uint16_t g = uint16_t(actor.acceleration.y);
        actor.velocity.y += g;
        if (gameState().frame & 1u) actor.acceleration.y = int16_t(uint16_t(g + 1u));
        bool dead = false;
        const int16_t t = f.ticks;
        if (t > 0) {
            f.ticks = int16_t(t - 1);
            if (t - 1 == 0) dead = true;
        }
        if (!dead && playerShieldBlocks(actor, 0x20)) dead = true;                // exe 0x5737b0
        if (!dead && int32_t(actorTestMove(*E, actor)) < 0) dead = true;         // exe 0x40f570
        if (!dead && (f.hitBox.result & 0x30000u)) dead = true;
        if (dead) {
            actor.enterState(uint8_t(BombState::Free));
            return;
        }
        const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        f.hitBox.previousX = actor.prevPos.x.whole;
        f.hitBox.previousZ = actor.prevPos.z.whole;
        f.hitBox.result = 0;
        hitListSubmit(pos, f.hitBox, uint32_t(actor.slot) << 24 | 0xc08006u,
                      uint32_t(int32_t(f.attack)) | 0x100000u); // exe 0x5e4120
        // the billboard: a 0x2c draw command at the cursor (word 0 unwritten), drawn every tick
        dc::emit({0, 0x2c808080u,
                  uint32_t(uint16_t(actor.pos.x.whole)) | uint32_t(uint16_t(actor.pos.y.whole)) << 16, 0x7d5080e0u,
                  uint32_t(int32_t(actor.pos.z.whole)), 0xf80ffu, 8u, 0x9fe0u, 0u, 0x9fffu},
                 actor.at, false);
        return;
    }
    case BombState::Free: freeActor(actor); return; // exe 0x5d0bc0 -> 0x5e1500
    default: notPortedAt(kStoryPort, "0x5c8220 state (table 0x8d138c)", actor.state);
    }
}

} // namespace

void appoDahFightUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    appoDahFight(animatedActorAt(actor));
}

void script1dAppoDahUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    script1dPuppet(animatedActorAt(actor));
}

void script1fAppoDahUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    script1fPuppet(animatedActorAt(actor));
}

void appoDahBombUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    appoDahBomb(projectileActorAt(actor));
}

} // namespace od::port
