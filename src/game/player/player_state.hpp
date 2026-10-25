// The player's state: the exe's struct at 0xa71d20 as a native object. Its head is the shared
// actor layout (game/actor_state: position, rotation, velocities, collision, hit points, root matrix); the fields past
// it are the player's own: the two animation channels, the input copy and the button map, the weapons' arms, the
// carry, the lock-on, the status effects, the equipment and the pose's bones. Also the player's state machine (+8 state,
// +9 sub-state, +0xa each sub-state's steps), its animation and sound ids and the bits of its flag words.
//
// The head as the player uses it: rotation.x is the aim pitch (the camera's look pitch follows it), rotation.y the body
// yaw; velocity is local (z < 0 forward), turned by yaw - directionOffset.y when integrated; spin.x / spin.y are the aim
// pitch's / the yaw's turn speeds; stepCounter (+0xb) is a per-sub-state parameter (see PlayerSub).
#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>

#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/field_alias.hpp"
#include "game/hit_list.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port {

constexpr uint32_t kPlayer = 0xa71d20;
inline constexpr const char* kPlayerArea = "player"; // the area of the player's port-gap errors

// ---- the state machine ---------------------------------------------------------------------------------------------
// +8 (the state table 0x8c4f5c)
enum class PlayerState : uint8_t {
    Init = 0,     // exe 0x575330: the fields reset, then Playing
    Playing = 1,  // exe 0x575440: the sub-states run on the pad
    Dead = 2,     // exe 0x57b6d0: collision only; the game over
    Scripted = 3, // exe 0x57b710: a script / cutscene drives the animation
};

// +9 (the sub-state tables A 0x8c4fc0 (input) / B 0x8c4f6c (motion, animation)). stepCounter (+0xb) is: Idle - wait
// for the held directions to change (after a quick turn / a hit); Walk - 1 walking backwards; KnockBack / KnockDown -
// 1 hit from behind; SideHop - its side (bit 0: right) and the immunity kept (bit 1); EventPose / ItemGet - the first pose's
// ticks left
enum class PlayerSub : uint8_t {
    Idle = 0,
    Walk = 1,         // the walk modifier held: slow forward / backward
    Run = 2,          // the run, with directional starts and stops (move direction +0xee)
    Skate = 3,        // the jet skates
    TurnInPlace = 4,  // the stick sideways: turn standing
    QuickTurn = 5,    // the use button with a direction away from the facing: a 180-degree turn
    SideStep = 6,     // the walk modifier with left / right
    Jump = 7,         // the standing jump (and falling)
    RunningJump = 8,  // the running jump; its steps 6 / 7 are every jump's landing
    SkateFall = 9,    // the ground lost while skating
    SideHop = 10,     // the hop sideways (a landing with left / right, the recovery from a knock-back)
    BarHang = 11,     // hanging from an actor's bar (game/player/player_hang)
    LedgeHang = 12,   // hanging from a ledge
    Look = 13,        // the look button held: the stick aims
    KnockBack = 14,   // hit: the slide back
    KnockDown = 15,   // hit hard: thrown, lying, getting up (or dead)
    Lift = 16,        // lifting an actor (set by the lifted actor, game/carry)
    Held = 17,        // held by a grabbing actor (playerGrab)
    Talk = 18,        // talking (set by the message system)
    EventPose = 19,   // an event's pose (event modes 1 / 3)
    ItemGet = 20,     // the item-get pose (event mode 2)
};

// +0xa: each sub-state's steps
enum class IdleStep : uint8_t { Enter = 0, Standing = 1 };
enum class WalkStep : uint8_t { Enter = 0, Walking = 1 };
enum class RunStep : uint8_t {
    Begin = 0,         // the start animation of the move direction
    BeginHold = 1,     // until its frame 1
    BeginTurn = 2,     // turning toward the stick, then BeginStraight
    BeginStraight = 3, // until the start animation ends -> the run animation
    Settling = 4,      // until the run's frame 5
    Running = 5,
    Stop = 6,          // the stop animation of the move direction
    Stopping = 7,      // until its landing frame -> Idle
};
enum class SkateStep : uint8_t {
    Kneel = 0, KneelWait = 1, Launch = 2, EngineStart = 3, Straight = 4, TurnOneWay = 5, TurnOtherWay = 6, Stop = 7,
    Brake = 8,
};
enum class TurnStep : uint8_t { Enter = 0, Turning = 1 };
enum class QuickTurnStep : uint8_t { Enter = 0, Turning = 1 };
enum class SideStepStep : uint8_t { Enter = 0, Stepping = 1 };
enum class JumpStep : uint8_t { Crouch = 0, TakeOff = 1, Rising = 2, Released = 3, Peak = 4, Falling = 5 };
enum class RunningJumpStep : uint8_t {
    TakeOff = 0, Rising = 1, RisingHeld = 2, Released = 3, Peak = 4, Falling = 5,
    Landing = 6, // every jump's landing: the landing animation by the direction, or straight into the run
    Landed = 7,
};
enum class SkateFallStep : uint8_t { Hop = 0, HopRising = 1, Falling = 2, Land = 3, Stop = 4 };
enum class SideHopStep : uint8_t { Crouch = 0, Launch = 1, Air = 2, Falling = 3, Land = 4 };
enum class BarHangStep : uint8_t { Grab = 0, GrabWait = 1, Hang = 2, PullUp = 3, Flip = 4, Drop = 5 };
enum class LedgeHangStep : uint8_t { Grab = 0, GrabWait = 1, Hang = 2, Climb = 3, ClimbJump = 4, Airborne = 5 };
enum class KnockBackStep : uint8_t { Hit = 0, Slide = 1, Slowing = 2, Recover = 3 };
enum class KnockDownStep : uint8_t {
    Thrown = 0, ThrownAgain = 1, Airborne = 2, Lie = 3, Stop = 4, Bounce = 5, LieAgain = 6, StopAgain = 7, Dead = 8,
};
enum class LiftStep : uint8_t { Lift = 0, Lifting = 1 };
enum class HeldStep : uint8_t { Grabbed = 0, Held = 1, Struggle = 2, GrabbedStruggling = 3, Struggling = 4 };
enum class TalkingStep : uint8_t { Enter = 0, Turned = 1, Talking = 2, Closing = 3 };
enum class EventPoseStep : uint8_t { Enter = 0, FirstPose = 1, Wait = 2, End = 3 };
enum class DeadStep : uint8_t { Enter = 0, GameOver = 1 }; // the Dead state keeps its step in +9

// +0xee / +0xef: the move direction relative to the facing (the stick's quadrant)
enum class MoveDir : int8_t { Forward = 0, Left = 1, Back = 2, Right = 3 };
// the quadrant of a move direction's offset angle (exe's split of (angle + 0x200) & 0xfff: the bounds 0x400 / 0xc00
// belong to the lower quadrant)
inline MoveDir moveDirOfAngle(int angle) {
    const uint32_t a = (uint32_t(angle) + uint32_t(kQuarterTurn / 2)) & kAngleMask;
    if (a < uint32_t(kHalfTurn)) return a <= uint32_t(kQuarterTurn) ? MoveDir::Forward : MoveDir::Left;
    return a <= uint32_t(kHalfTurn + kQuarterTurn) ? MoveDir::Back : MoveDir::Right;
}
// the move direction's offset angle (a quarter turn per direction)
inline int16_t moveDirAngle(MoveDir d) { return int16_t(int(d) * kQuarterTurn); }

// stepCounter (+0xb) values
constexpr uint8_t kWalkBackward = 1;   // Walk
constexpr uint8_t kHitFromBehind = 1;  // KnockBack / KnockDown
constexpr uint8_t kHopRight = 1;       // SideHop bit 0 (clear: to the left)
constexpr uint8_t kHopKeepImmunity = 2; // SideHop bit 1: the hit immunity is not ended by the hop
constexpr uint8_t kLatchDirections = 1; // Idle: wait until the held directions change

// ---- animation and sound ids ---------------------------------------------------------------------------------------
// The animation ids (the main channel +0xa0 / the upper body +0xb0): < 0x60 the player's table, 0x60.. the special
// weapon arm's, 0x70.. the buster arm's, 0x80.. the stage's (game/player/player_animation)
enum class PlayerAnim : uint8_t {
    Idle = 0x00,
    Run = 0x01,
    Walk = 0x02,
    Shuffle = 0x03,          // walking backwards / turning in place
    SideStepRight = 0x04,
    SideStepLeft = 0x05,
    RunStopRight = 0x0b,     // the run's stops by the move direction (right / left / back)
    RunStopLeft = 0x0d,
    RunStopBack = 0x0f,
    JumpCrouch = 0x10,
    JumpRise = 0x11,
    JumpFall = 0x12,
    JumpLand = 0x13,
    RunningJumpRise = 0x14,
    RunningJumpFall = 0x15,
    RunningJumpLand = 0x16,
    LandIntoRun = 0x17,
    HopRight = 0x18,
    HopLeft = 0x19,
    HopAir = 0x1a,
    HopLand = 0x1b,
    HopFall = 0x1c,
    HangGrab = 0x1d,
    Hang = 0x1e,
    LedgeClimb = 0x1f,
    KnockBackFront = 0x20,
    KnockBackBehind = 0x21,
    KnockDownFront = 0x22,
    KnockDownLandFront = 0x23,
    BounceFront = 0x24,
    GetUpFront = 0x25,
    KnockDownBehind = 0x26,
    KnockDownLandBehind = 0x27,
    BounceBehind = 0x28,
    GetUpBehind = 0x29,
    LandForward = 0x2a,      // the landings by the move direction
    LandLeft = 0x2b,
    LandRight = 0x2c,
    LandBack = 0x2d,
    SkateKneel = 0x30,
    Skate = 0x31,
    SkateStop = 0x32,
    SkateLeanOneWay = 0x33,       // the skates' lean held one way / the other
    SkateLeanOtherWay = 0x34,
    SkateTurnOneWay = 0x35,       // into the lean
    SkateTurnOneWayBack = 0x36,   // back to straight
    SkateTurnOtherWay = 0x37,
    SkateTurnOtherWayBack = 0x38,
    SkateSwitchToOtherWay = 0x39, // from one lean to the other
    SkateSwitchToOneWay = 0x3a,
    SkateHop = 0x3b,
    SkateFall = 0x3c,
    SkateLand = 0x3d,
    SkateLandOn = 0x3e,      // landed with the walk modifier held: back to skating
    SkateLandStop = 0x3f,
    SpecialTownArm = 0x40,   // the special weapon's arm when its type is 1
    Kick = 0x41,             // the buster's arm in a town (buster type 1: the kick)
    BarPullUp = 0x42,
    BarFlip = 0x43,
    Lift = 0x48,
    CarryThrow = 0x49,       // the upper body's throw / put-down (also the idle while carrying)
    CarryHold = 0x4a,        // the upper body's hold
    Talk = 0x50,
    TalkEnd = 0x51,
    TalkLine = 0x52,
    DoorPush = 0x53,         // pushing a hinged door open (the door transitions)
    EventPose = 0x54,
    EventPoseHold = 0x55,
    ItemGet = 0x56,
    ItemGetHold = 0x57,
    ClimbUp = 0x58,          // the ladder / hole transition's climb (hypothesis: by the direction it moves the player)
    ClimbDown = 0x59,
    Struggle = 0x5a,
    HeldPose = 0x5b,
    SpecialArm = 0x60,       // the special weapon's arm, standing
    SpecialArmMoving = 0x61, // ... moving / jumping (the Blade Arm: its first swing's end)
    BladeSecondSwing = 0x62,
    BladeLanding = 0x63,
    BusterArm = 0x70,        // the buster's arm, standing
    BusterArmMoving = 0x71,  // ... moving / jumping
    None = 0xff,             // no animation (forces the next request)
};
// the event bits of the body channel's keys (AnimKey events)
constexpr uint8_t kAnimEventLand = 0x01;     // == 1: a landing frame (the move goes on)
constexpr uint8_t kAnimEventCue = 0x20;      // a scripted animation's sound cue
constexpr uint8_t kAnimEventStep = 0x40;     // a scripted animation's step
constexpr uint8_t kAnimEventFootfall = 0x80; // a foot touches down (the step / footstep sounds, a splash in water)
constexpr uint8_t kSpecialArmAnims = 0x60; // the special weapon arm's animations 0x60..0x6f
constexpr uint8_t kBusterArmAnims = 0x70;  // the buster arm's 0x70..0x7f
constexpr uint8_t kStageAnims = 0x80;      // the stage's own 0x80.. (+0x298)

// The sounds the player plays at its position (game/audio_logic soundPlayAtPlayer)
enum class PlayerSound : uint16_t {
    LockOn = 0x87,
    Step = 0x90,
    Footstep = 0x91,
    Jump = 0x92,
    Land = 0x93,
    Grab = 0x94,
    Hurt = 0x96,
    KnockedDown = 0x97,
    Bounce = 0x98,
    FallLand = 0x99,
    SideStep = 0x9c,
    SkateEngine = 0x9d, // the jet skates' engine loop
    SkateTurn = 0x9e,
    SkateStop = 0x9f,
    Hop = 0xa0,
    Lift = 0xa1,
    Throw = 0xa2,
    ScriptCue = 0x140, // the scripted animation's sound key
};

// ---- the bits of the flag words --------------------------------------------------------------------------------------
// +0x13c (armFlags): the weapons' arms
constexpr uint16_t kArmMixMask = 0x0003;       // the upper body channel's mix mode while the arm is up
constexpr uint16_t kArmUpperOnly = 0x0004;     // the arm animation plays on the upper body only (the legs move)
constexpr uint16_t kArmSpecialShot = 0x0010;   // the special weapon shoots this tick
constexpr uint16_t kArmBusterShot = 0x0020;    // the buster shoots this tick
constexpr uint16_t kArmShots = kArmSpecialShot | kArmBusterShot;
constexpr uint16_t kArmSpecial = 0x0040;       // the special weapon's arm is up
constexpr uint16_t kArmSpecialSpentMask = 0x0dff; // the flags kept when the special energy ran out (the arm disarmed,
                                                  // its queued / refire bits cleared)
constexpr uint16_t kArmBuster = 0x0080;        // the buster's arm is up
constexpr uint16_t kArmUp = kArmSpecial | kArmBuster;
constexpr uint16_t kArmReady = 0x0100;         // a weapon may fire (exe 0x572230)
constexpr uint16_t kArmSpecialEnergy = 0x0200; // the special weapon's gauge pays a shot
constexpr uint16_t kArmBusterFree = 0x0400;    // the buster's shots in flight are below its limit
constexpr uint16_t kArmSpecialReady = kArmReady | kArmSpecialEnergy;
constexpr uint16_t kArmBusterReady = kArmReady | kArmBusterFree;
constexpr uint16_t kArmSpecialQueued = 0x1000; // a special shot asked for during a move
constexpr uint16_t kArmBusterQueued = 0x2000;  // a buster shot asked for during a move
constexpr uint16_t kArmSpecialRefire = 0x4000; // the special weapon fires again (the trigger held)
constexpr uint16_t kArmBusterRefire = 0x8000;  // the buster fires again
constexpr uint16_t kArmAnyRequest = kArmUp | kArmSpecialRefire | kArmBusterRefire; // an arm up or a refire asked for
constexpr uint16_t kArmDownMask = 0xff38;      // the arm put down: the mix bits, kArmUpperOnly and the arms cleared
constexpr uint16_t kArmOffMask = 0x3f38;       // ... and the refires too
// +0x140 (carryFlags)
constexpr uint16_t kCarryHolding = 0x0003;     // an actor is in the hands (the pose's arms hold it)
constexpr uint16_t kCarryReleased = 0x0030;    // the actor leaves the hands this tick
constexpr uint16_t kCarryThrowing = 0x00c0;    // the throw / put-down plays (exe 0x573870)
constexpr uint16_t kCarryThrowQueued = 0x3000; // a throw asked for during a move
// +0x144 (lockRequest)
constexpr uint16_t kLockSpecial = 0x1; // the special weapon's lock (its trigger held with the lock option)
constexpr uint16_t kLockBuster = 0x2;  // the buster's
constexpr uint16_t kLockFired = 0x4;   // the lock reached: the arm was raised
constexpr uint16_t kLockKeepAim = 0x8; // the aim stays when the lock button is released
// +0xd3 (lockFlags)
constexpr uint8_t kLockPitchOn = 0x1; // the aim pitch reached the target (and the camera pitch follows it)
constexpr uint8_t kLockYawOn = 0x2;   // the body yaw reached the target
// +0xb8 (status): the low nibble the effects running on a timer, the next one their floor form (re-armed while standing
// on the floor), the second byte the marker actors spawned for each
constexpr uint16_t kStatusDrain = 0x01;     // 1 hit point every 16 ticks (floor form: every tick unless kLegsNoBurn)
constexpr uint16_t kStatusBurn = 0x02;      // 1 hit point every 8 ticks (floor form: 2 unless kLegsNoBurn)
constexpr uint16_t kStatusSlowed = 0x04;    // the speed cut (drag 0xc)
constexpr uint16_t kStatusOrbit = 0x08;     // four orbiting markers; the special weapon's gauge drains twice as fast
constexpr uint16_t kStatusDrainFloor = 0x10;
constexpr uint16_t kStatusBurnFloor = 0x20;
constexpr uint16_t kStatusSlowedFloor = 0x40;
constexpr uint16_t kStatusOrbitFloor = 0x80;
constexpr uint16_t kStatusMarkerShift = 8;  // the marker bit of effect e: e << kStatusMarkerShift
constexpr uint16_t kStatusTickMask = 0x0fff; // cleared at the end of every tick: the top nibble
constexpr uint16_t kStatusEffectMask = 0x000f; // the four effects (kStatusDrain .. kStatusOrbit)
constexpr uint16_t kStatusMarkerMask = 0x0f00; // their markers (e << kStatusMarkerShift)
constexpr uint16_t kStatusArrived = 0x1000;  // set by a room's arrival when markers were up (cleared at the tick's end)
constexpr uint16_t kStatusTickResetKeep = 0x1f0f; // kept by the field tick's reset (exe 0x574830): the floor forms
                                                  // (the floor gives them again) and the top bits cleared
// +0xc2 (inputMode)
constexpr uint8_t kInputReplay = 0x40; // the replay pad 0xa6c3e8 drives the player (a scripted walk)
constexpr uint8_t kInputOff = 0x80;    // no input (and no trigger volume)
// +0xec (immunity)
constexpr int8_t kImmuneHeld = -1;     // immune until cleared (the hit's reaction)
constexpr int8_t kImmuneBlinking = 1;  // immune for +0xed ticks, blinking
constexpr int8_t kImmuneSteady = 2;    // immune for +0xed ticks, shown
// +0xc7 (liftMode)
constexpr uint8_t kLiftFloats = 0x1;   // the lifted actor lets the player float down (jump held)
// +0x1d0 (interactionTarget): mode << 24 | the actor's address
constexpr uint32_t kInteractTalkMode = 0x1000000; // the actor the player talks to (circle)
constexpr uint32_t kInteractLiftMode = 0x2000000; // the actor the player lifts (triangle)
constexpr uint32_t kInteractBarMode = 0x8000000;  // the bar the player hangs from (game/player/player_hang)
constexpr uint32_t kInteractActorMask = 0xffffff;
// +0x1a3 (legPart): the leg parts the code tests
constexpr uint8_t kLegsJetSkates = 1;  // the jet skates (sub-state Skate on the ground)
constexpr uint8_t kLegsWaterSkates = 2; // skates on water
constexpr uint8_t kLegsNoBurn = 3;     // no sinking into the kind-1 floor and no strong burn
constexpr uint8_t kLegsSlowProof = 5;  // the kind-2 floor does not slow

// ---- the weapons the code tests (+0x18c / +0x18e: the special weapon's id; the arm handlers by id: player_weapons)
constexpr int8_t kChargeWeapon = 10;      // the weapon with the charge marker (no shot type of its own)
constexpr int8_t kBladeArmWeapon = 11;    // the Blade Arm (its arm keeps the swing pose, no aiming)
constexpr int8_t kExtinguisherWeapon = 15; // the special weapon of weapon mode 2 (the extinguisher, ST1E)
constexpr int8_t kTownBuster = 1;         // +0x18c / +0x18d == 1: the town's weapon (the buster's kick)

// ---- the player's buttons (the map +0x11c: 16 masks of the pad's held / pressed bits) ----------------------------------
enum class PlayerButton : uint8_t {
    Forward = 0, Back = 1, TurnLeft = 2, TurnRight = 3, Left = 4, Right = 5,
    Walk = 6,    // the walk / skate modifier (circle held or the stick half tilted: held bit 1)
    Use = 7,     // talk, doors, the quick turn (circle)
    Jump = 8,    // cross
    Fire = 9,    // the buster (square)
    Special = 10, // the special weapon, lift / throw (triangle)
    Look = 11,   // l2
    Aim = 12,    // r1: with Lock the aim mode
    Lock = 13,   // r2: the lock-on
};

// ---- the records the player's fields point at / hold -------------------------------------------------------------------
// What the player interacts with: the mode (kInteract*Mode) and the actor (the exe keeps them in one word: mode << 24
// | the actor's address)
struct InteractionTarget {
    uint8_t mode = 0; // kInteract*Mode >> 24
    ActorRef<ActorHeader> actor;

    // none (the exe's word 0)
    bool none() const { return mode == 0 && !actor; }
    void clear() {
        mode = 0;
        actor = nullptr;
    }
    // the mode word (kInteract*Mode) and the actor
    void set(uint32_t modeWord, const ActorHeader& a) {
        mode = uint8_t(modeWord >> 24);
        actor = const_cast<ActorHeader*>(&a);
    }
    // the target is `a` with one of the mode bits `modeWord`
    bool is(const ActorHeader& a, uint32_t modeWord) const {
        return actor.get() == &a && (uint32_t(mode) << 24 & modeWord) != 0;
    }
    bool hasMode(uint32_t modeWord) const { return (uint32_t(mode) << 24 & modeWord) != 0; }
    // the word as the exe keeps it (the timeline's log)
    uint32_t word() const { return uint32_t(mode) << 24 | (actor ? actor->at & kInteractActorMask : 0u); }
};

// One animation channel (the body, the upper body; the actors' animation fields)
struct AnimChannel {
    DataPtr<> script;                 // the animation's first key
    DataPtr<> keyAt;                  // the current key
    AnimKey key;                      // its copy (ticks: left; the request's step goes into its frame byte)
    uint8_t anim = 0;                 // the animation asked for (PlayerAnim)
    uint8_t prevAnim = 0;             // the last tick's (0xff: none)
    uint8_t frameData = 0;            // the animation's frame-data index
    uint8_t keyIndex = 0;             // the key's index in the script
};
// A channel's blend toward the next frame (the body's, the upper body's)
struct AnimBlend {
    int8_t targetFrame = 0; // the frame blended toward
    uint8_t steps = 0;      // 0x80 | the blend steps (& 0xf); 0: no blend

    // no blend (the exe's word store)
    void clear() {
        targetFrame = 0;
        steps = 0;
    }
};
// One idle-variation track: a table of keys {u8 value, u8 base ticks, u8 random ticks mask, s8 next} (exe data)
// stepped with the RNG (game/player/player_animation)
struct IdleKey : DataRecord {
    static constexpr uint32_t kBytes = 4;
    uint8_t value = 0;     // the variation shown (the face)
    uint8_t baseTicks = 0; // its ticks: base + (RNG & mask)
    uint8_t ticksMask = 0;
    int8_t next = 0;       // -1: hold; bit 7: jump to key next & 0x7f; else the next key
    template <class R> static IdleKey load(const R& r) { return {{}, r.u8(0), r.u8(1), r.u8(2), r.s8(3)}; }
};
struct IdleTrack {
    DataPtr<IdleKey> start; // the table's first key
    DataPtr<IdleKey> key;   // the current key
    int8_t table = 0;              // the table selected (0xff: none)
    int8_t ticks = 0;              // the current key's ticks
    int8_t count = 0;              // ticks shown so far
    int8_t pad = 0;                // the byte after them (kept by the slot's copies)
};
// One pose matrix: the 9 shorts of a rotation, its padding short, the s32 translation (the player's bones;
// game/player/player_pose)
struct PoseMatrix {
    StateArray<int16_t, 10> rotation{}; // the 9 elements and the padding short
    StateArray<int32_t, 3> translation{};

    // this matrix = o (the exe's 8-dword copy)
    void copyFrom(const PoseMatrix& o) { *this = o; }
    void copyFrom(const struct LanePoseMatrix& o); // (below)
};
// A pose matrix over lanes (the pose's current matrix: the geometry scratch's +0x00, game/geometry_scratch)
struct LanePoseMatrix {
    std::array<Lane16, 10> rotation; // the 9 elements and the padding short
    std::array<Lane32, 3> translation;

    void copyFrom(const PoseMatrix& o) {
        for (size_t i = 0; i < 10; ++i) rotation[i] = o.rotation[i];
        for (size_t i = 0; i < 3; ++i) translation[i] = o.translation[i];
    }
    PoseMatrix get() const {
        PoseMatrix m;
        for (size_t i = 0; i < 10; ++i) m.rotation[i] = rotation[i].get();
        for (size_t i = 0; i < 3; ++i) m.translation[i] = translation[i].get();
        return m;
    }
};
inline void PoseMatrix::copyFrom(const LanePoseMatrix& o) { *this = o.get(); }
// a bone's point (s16, << stage shift) and the short after it: the bone's colour (a teleport flashes them)
struct PlayerBonePoint : ShortVec3 {
    uint16_t colour = 0;
};
constexpr uint32_t kPlayerBones = 15; // 0 body, 1 head, 2..4 left arm, 5..7 right arm, 8 hips, 9..11 / 12..14 the legs
constexpr uint32_t kBodyBone = 0;     // the body: the kick's start, the shield's aim
constexpr uint32_t kHeadBone = 1;
constexpr uint32_t kLeftUpperArmBone = 2;
constexpr uint32_t kLeftForearmBone = 3; // the held special weapons' direction from it to the hand
constexpr uint32_t kLeftHandBone = 4;    // the special weapons' muzzle
constexpr uint32_t kRightUpperArmBone = 5;
constexpr uint32_t kRightForearmBone = 6;
constexpr uint32_t kRightHandBone = 7;   // the buster's muzzle
constexpr uint32_t kHipsBone = 8;
constexpr uint32_t kLeftThighBone = 9;
constexpr uint32_t kLeftShinBone = 10;
constexpr uint32_t kLeftFootBone = 11;
constexpr uint32_t kRightThighBone = 12;
constexpr uint32_t kRightShinBone = 13;
constexpr uint32_t kRightFootBone = 14;

constexpr size_t kOwnedPartSlots = 0x20; // the buster parts the player owns
constexpr size_t kOwnedPartWords = 9;    // ... and the four bytes after them, as dwords
constexpr size_t kWeaponLevelSlots = 20; // the weapons' stat level slots (8 bytes each)

// The player: one object for the life of the program. The session's start constructs it anew (resetPlayer: every
// member zero, as the exe's clear of its bytes); playerInit (game/game_init) sets the fields it sets and keeps the
// others, which the read map shows read before anything writes them in a session.
struct Player : Actor {
    int16_t& lastTurnStep = spin.spare; // spin's spare lane: the last turn step (exe 0x571bb0)
    AnimChannel body;
    AnimChannel upper;
    int8_t scriptAnim = 0;     // the animation a script queued (state Scripted; -1: none)
    int8_t scriptAnimStep = 0;
    uint8_t invulnerable = 0;  // no hit-list entry, no damage (set by scripts)
    uint8_t padB7 = 0;         // the byte after it (cleared with it by the init's word store)
    uint16_t status = 0;       // kStatus*
    uint8_t pulled = 0;        // pulled toward the point pullX / pullZ (a whirlpool / a trap)
    int8_t wadeDepth = 0;      // the depth of the water / the floor the player sinks into
    int8_t drag = 0;           // the speed cut of this tick (the speed * (16 - drag) / 16)
    uint8_t slippery = 0;      // the floor is slippery (the velocities glide and decay)
    uint8_t inWater = 0;       // in water (splashes, jumps x 5/4, gravity / 2)
    int8_t burden = 0;         // > 0: the speeds cut by burden / 0x40, gravity raised by burden / 16
    int8_t channelMix = 0;     // the upper body channel's mix mode (the pose's channel sources)
    uint8_t leanLocked = 0;    // the lean and the head turn held at 0
    uint8_t inputMode = 0;     // kInputReplay / kInputOff
    uint8_t padC3 = 0;
    uint8_t recentreAim = 0;   // the aim pitch and the camera return to level
    uint8_t lockSuspended = 0; // a move that does not turn to the lock-on target
    uint8_t jumpBuffered = 0;  // the jump pressed early (taken when the move allows)
    uint8_t liftMode = 0;      // the lift started by the lifted actor (its mode: kLiftFloats)
    uint8_t specialFiring = 0; // a held special weapon fires: the Blade Arm (1 / 2: its swing), the extinguisher, the
                               // Shining Laser, the drill (1); cleared every tick
    uint8_t charge = 0;        // the special weapon's charge (marker spawned 1; & 0xf the level)
    uint8_t suction = 0;       // the vacuum arm's suction (cleared every tick)
    uint8_t padCB = 0;
    uint8_t lockTarget = 0;    // the lock-on target's slot (0: none)
    uint8_t softTarget = 0;    // the aim's target without the lock (the screen centre's)
    uint8_t lockRemembered = 0; // the target the lock time counts for
    uint8_t softRemembered = 0; // the soft target remembered with it (the hit pass copies the pair)
    uint8_t lockHeldTicks = 0;  // OpenDash (modern aim): the ticks the lock was held on a target that sent no point
    int8_t lockSoundTicks = 0; // the countdown to the lock-on sound
    uint8_t lockReleaseTicks = 0; // the countdown after the lock button is released
    int8_t lockTicks = 0;      // the ticks the lock stayed on the same target
    uint8_t lockFlags = 0;     // kLockPitchOn | kLockYawOn
    uint16_t latchedDirections = 0; // the directions held when a quick turn / hit started
    int16_t quickTurnSpeed = 0;
    ShortVec4 pushVelocity;    // the external velocity (stepping off a moving platform, a conveyor)
    int16_t pullX = 0;         // the point the player is pulled toward and the pull
    int16_t pullY = 0;         // (set with the point by the ST37 trap; the pull reads x / z)
    int16_t pullZ = 0;
    int16_t pullPad = 0;
    int16_t pullStrength = 0;
    int16_t pullRadius = 0;
    int8_t immunity = 0;       // kImmune* (0: none)
    int8_t immunityTicks = 0;
    ShortVec3 directionOffset; // the move direction's angles (y: the yaw offset of the stick)
    int16_t directionPad = 0;  // the short after them (cleared with them)
    Fixed16Vec3 ledgePoint;    // the ledge's grab point (16.16)
    int16_t lean = 0;          // the body lean (eased toward leanTarget)
    int16_t leanTarget = 0;
    int16_t headTurn = 0;      // the head turn (eased toward headTurnTarget)
    int16_t headTurnTarget = 0;
    uint16_t padType = 0;      // the pad type word (7: analog)
    uint16_t held = 0;         // the buttons held / pressed / released this tick
    uint16_t pressed = 0;
    uint16_t released = 0;
    uint16_t doubleTapped = 0;
    uint16_t repeated = 0;
    int8_t turnAxis = 0;       // the sticks after the remap: the turn, the look, the move x / y
    int8_t lookAxis = 0;
    int8_t moveX = 0;
    int8_t moveY = 0;
    StateArray<uint16_t, 16> buttonMap{}; // the PlayerButton masks
    uint16_t armFlags = 0;     // kArm*
    int8_t armStep = 0;        // the arm handler's step
    uint8_t armStepPad = 0;
    uint16_t carryFlags = 0;   // kCarry*
    uint8_t carryStep = 0;     // the throw's step
    uint8_t carryStepPad = 0;
    uint16_t lockRequest = 0;  // kLock*
    int16_t lockRequestTicks = 0;
    int8_t skating = 0;        // on the jet skates (hovering: the ground snap reaches 0x10 only)
    int8_t skateHoldTicks = 0; // the walk modifier held toward the skates
    uint8_t skateSpray = 0;
    int8_t skateEngine = 0;    // the engine loop's voice handle
    StateArray<IdleTrack, 2> idleTracks{};
    ShortVec4 lockAimPoint;    // the lock-on target's hit point (the shots aim at it)
    ShortVec4 screenAimPoint;  // the screen centre's aim point
    ActorRef<ActorHeader> lockedActor;     // the lock-on target actor (any pool; null: none)
    ActorRef<ActorHeader> homingTarget;    // the homing shots' target (game/shots)
    ActorRef<ActorHeader> softTargetActor; // the soft target's actor
    StateArray<int16_t, 4> statusTicks{}; // the timers of kStatusDrain / Burn / Slowed / Orbit
    int16_t hurtSum = 0;       // the damage of the recent hits (decays by 4 a tick)
    int16_t hurtCount = 0;     // the recent hits
    int8_t activeSpecial = 0;  // the special weapon in use (0: none) and the buster's type (1: the
    int8_t activeBuster = 0;   // town's kick, 2..: the field buster)
    int8_t specialWeapon = 0;  // the equipped special weapon / buster (the room's weapon mode picks
    int8_t busterWeapon = 0;   // the active ones from them)
    int16_t energyReserve = 0; // the special weapon's energy: 0 self-recharging, 0x7fff infinite
    int16_t energyPool = 0;    // the energy the gauge refills from
    int16_t gaugeMax = 0;      // the gauge: its maximum, its refill rate, a shot's cost, its level
    int16_t gaugeRate = 0;
    int16_t shotCost = 0;
    int16_t gauge = 0;
    int16_t busterShotsInFlight = 0;
    int8_t weaponMode = 0;     // the room's weapon mode (kWeaponMode*)
    uint8_t specialAllowed = 0;
    StateArray<int8_t, 2> idleFaces{}; // the idle tracks' current values (the face)
    int8_t helmetPart = 0;     // the equipment: the helmet (also the body file), the legs, the armour
    int8_t legPart = 0;
    int8_t armourPart = 0;
    StateArray<uint8_t, 2> handShapes{}; // the hands' shapes (hypothesis: the carried-object hands; the bank walk
                                         // clears them, the pose does not model them)
    uint8_t handShapesPad = 0;
    StateArray<int8_t, 3> busterParts{}; // the buster's three part slots (the parts' indices)
    uint8_t hitHeightLevel = 0; // the height level of its hit-list entry (hits within one level hurt; the armour's:
                                // the status sub-screen sets it)
    StateArray<uint8_t, kOwnedPartSlots> ownedParts{}; // the buster parts owned, in the player's order (0 ends)
    StateArray<uint8_t, 4> unreadPartBytes{}; // four bytes after them the save keeps (their reader is not traced)
    InteractionTarget interactionTarget; // the actor the player talks to / lifts / hangs from
    HitBox solidHit;           // what a solid pushing the player hands over (game/actor_collision)
    HitBox hitBox;             // the hit list's result box of the player's entry
    StateArray<int8_t, kWeaponLevelSlots * 8> weaponLevels{}; // the stat levels of each weapon (w * 8 + WeaponStat)
    uint16_t hiddenBones = 0;  // the pose's per-bone hide mask
    uint8_t animPaused = 0;    // the animation's ticks do not run
    uint8_t animPausedPad = 0;
    uint32_t stageAnims = 0;   // the stage's animation records (ids 0x80..; their address in the loaded data)
    uint32_t stageAnimsExtra = 0; // the bank's second animation record (+ 4; its reader is not traced)
    StateArray<uint8_t, 4> beforeBlends{}; // the four bytes before the blends (kept by the slot's copies)
    AnimBlend bodyBlend;
    AnimBlend upperBlend;
    uint32_t ambientColour = 0; // the model's light colour (0x808080 neutral)
    uint32_t stageAnimMarker = 0; // -1 at the init and the stage's animation load, 0 when a water sink or a hold ends
                                  // (its reader is not traced)
    StateArray<PoseMatrix, kPlayerBones> bones{};
    StateArray<PlayerBonePoint, kPlayerBones> bonePoints{}; // the bones' points and colours

    // ---- the pad ----
    // the mask of button b / held this tick / pressed this tick
    uint16_t button(PlayerButton b) const { return buttonMap[size_t(b)]; }
    bool holds(PlayerButton b) const { return (held & button(b)) != 0; }
    bool pressedNow(PlayerButton b) const { return (pressed & button(b)) != 0; }
    // the four direction buttons (forward, back, left, right)
    uint16_t directionButtons() const {
        return uint16_t(button(PlayerButton::Right) | button(PlayerButton::Left) | button(PlayerButton::Back) |
                        button(PlayerButton::Forward));
    }

    // ---- the move direction (and the previous one) ----
    MoveDir moveDir() const { return MoveDir(moveDir_); }
    MoveDir prevMoveDir() const { return MoveDir(prevMoveDir_); }
    void setMoveDir(MoveDir d) { moveDir_ = int8_t(d); }
    void setPrevMoveDir(MoveDir d) { prevMoveDir_ = int8_t(d); }

    // ---- the animation channels ----
    // the body's / the upper body's animation ended (its key holds the last frame: flags kKeyHold)
    bool bodyAnimEnded() const { return int8_t(body.key.flags) == -1; }
    bool upperAnimEnded() const { return int8_t(upper.key.flags) == -1; }

    // ---- the state machine ----
    // the state s at sub-state 0 (the exe's dword store over +8..+0xb)
    void enterState(PlayerState s) { ActorHeader::enterState(uint8_t(s)); }
    // the state / the sub-state as their enums
    PlayerState playerState() const { return PlayerState(state); }
    PlayerSub sub() const { return PlayerSub(subState); }
    // sub-state s at its step 0 with the step counter c (the exe's byte store at +9, word store at +0xa)
    void enterSub(PlayerSub s, uint8_t c = 0) {
        subState = uint8_t(s);
        enterStep(0, c);
    }
    // ... at the step st
    template <class Step> void enterSubAt(PlayerSub s, Step st, uint8_t c = 0) {
        subState = uint8_t(s);
        enterStep(uint8_t(st), c);
    }
    // sub-state s at the step st, the step counter kept (the exe's byte stores at +9 and +0xa)
    template <class Step> void moveToSub(PlayerSub s, Step st) {
        subState = uint8_t(s);
        step = uint8_t(st);
    }
    // the step as the sub-state's step enum
    template <class Step> Step stepAs() const { return Step(step); }
    // the step (byte store: the step counter kept) / the step with the counter cleared (word store)
    template <class Step> void setStep(Step st) { step = uint8_t(st); }
    template <class Step> void resetStep(Step st, uint8_t c = 0) { enterStep(uint8_t(st), c); }

    // ---- the flag words' bits set / cleared (the exe stores the byte of the word that holds them) ----
    void setArmBits(uint16_t bits) { armFlags = uint16_t(armFlags | bits); }
    void setStatusBits(uint16_t bits) { status = uint16_t(status | bits); }
    void setCarryBits(uint16_t bits) { carryFlags = uint16_t(carryFlags | bits); }
    void setHeldBits(uint16_t bits) { held = uint16_t(held | bits); }
    void clearLockRequestBits(uint16_t bits) { lockRequest = uint16_t(lockRequest & ~bits); }

    // ---- the words the exe stores whole ----
    // the lock-on target bytes and the lock's countdowns cleared
    void clearLockOnTarget() {
        clearTargetSlots();
        lockSoundTicks = 0;
        lockReleaseTicks = 0;
    }
    // ... the target bytes only (the hit pass's selection found none: the exe's dword store)
    void clearTargetSlots() {
        lockTarget = 0;
        softTarget = 0;
        lockRemembered = 0;
        softRemembered = 0;
    }
    // the arm handler's step (and its padding byte: the exe's word store)
    void resetArmStep(uint8_t s) {
        armStep = int8_t(s);
        armStepPad = 0;
    }
    // the throw's step (the exe's word store)
    void resetCarryStep() {
        carryStep = 0;
        carryStepPad = 0;
    }
    // off the skates and the hold count cleared (the exe's word store)
    void stopSkating() {
        skating = 0;
        skateHoldTicks = 0;
    }
    // the status and the pull / wade bytes after it cleared (the exe's dword store)
    void clearStatusWords() {
        status = 0;
        pulled = 0;
        wadeDepth = 0;
    }
    // the move direction's angles and the short after them cleared (the exe's two dword stores)
    void clearDirectionOffsetWords() {
        directionOffset = {};
        directionPad = 0;
    }
    // ... only the second dword (the z lane and the short after it)
    void clearDirectionOffsetZWord() {
        directionOffset.z = 0;
        directionPad = 0;
    }

    // the buster's part slots and the hit height level as one dword (the save's copy, a new run's store)
    uint32_t busterPartsWord() const {
        return uint32_t(uint8_t(busterParts[0])) | uint32_t(uint8_t(busterParts[1])) << 8 |
               uint32_t(uint8_t(busterParts[2])) << 16 | uint32_t(hitHeightLevel) << 24;
    }
    void setBusterPartsWord(uint32_t v) {
        for (size_t i = 0; i < 3; ++i) busterParts[i] = int8_t(uint8_t(v >> (8 * i)));
        hitHeightLevel = uint8_t(v >> 24);
    }
    // the owned parts and the four bytes after them as dwords (kOwnedPartWords; the save's copy, a new run's clear)
    uint32_t ownedPartsWord(size_t i) const {
        uint32_t w = 0;
        for (size_t b = 0; b < 4; ++b) w |= uint32_t(ownedPartsByte(i * 4 + b)) << (8 * b);
        return w;
    }
    void setOwnedPartsWord(size_t i, uint32_t v) {
        for (size_t b = 0; b < 4; ++b) ownedPartsByte(i * 4 + b) = uint8_t(v >> (8 * b));
    }
    // weapon w's eight level bytes as two dwords (words 2w and 2w + 1: the licence exam's copy)
    uint32_t weaponLevelWord(size_t i) const {
        uint32_t w = 0;
        for (size_t b = 0; b < 4; ++b) w |= uint32_t(uint8_t(weaponLevels[i * 4 + b])) << (8 * b);
        return w;
    }
    void setWeaponLevelWord(size_t i, uint32_t v) {
        for (size_t b = 0; b < 4; ++b) weaponLevels[i * 4 + b] = int8_t(uint8_t(v >> (8 * b)));
    }

    // the root matrix, its padding short and the root translation = the pose matrix m (the exe's 0x20-byte copy)
    void setRootPose(const PoseMatrix& m) {
        for (size_t i = 0; i < 9; ++i) rootMatrix.e[i] = m.rotation[i];
        rootPad = m.rotation[9];
        for (size_t i = 0; i < 3; ++i) rootTranslation[i] = m.translation[i];
    }

    // exe 0x573fa0: the player's talk target is `actor` (the interaction query picked it in talk mode)
    bool talksTo(const ActorHeader& actor) const { return interactionTarget.is(actor, kInteractTalkMode); }

private:
    uint8_t& ownedPartsByte(size_t i) { return i < kOwnedPartSlots ? ownedParts[i] : unreadPartBytes[i - kOwnedPartSlots]; }
    uint8_t ownedPartsByte(size_t i) const {
        return i < kOwnedPartSlots ? ownedParts[i] : unreadPartBytes[i - kOwnedPartSlots];
    }
    int8_t moveDir_ = 0;
    int8_t prevMoveDir_ = 0;
};

// the player (one object for the life of the program; the exe's address kPlayer is its identity in the states still
// in the exe's layout)
Player& player();
// The 0x10 bytes after the player's struct (exe 0xa72220..0xa7222f): the player's reset clears them with the struct
// (exe 0x5e00e0(1)); their reader is not traced
struct PlayerTail {
    StateArray<uint8_t, 0x10> bytes{};
};
PlayerTail& playerTail();
// the player's struct read as a 16.16 point, as some classes hand the exe's distance routine the struct itself for its
// position: its +0 / +4 / +8 words (the head, the type word, the state word)
Fixed16Vec3 playerStructAsPoint();
// exe 0x5e00e0(1)'s player part: the player's bytes cleared (the player constructed anew, every member zero) and the
// 0x10 bytes after them in the game state
void resetPlayer();

} // namespace od::port
