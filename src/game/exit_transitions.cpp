// The exits' transitions (see exit_transitions.hpp).
#include "game/actor_state.hpp"
#include "game/exit_transitions_state.hpp"
#include "game/exit_transitions.hpp"

#include <cstdlib>

#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/display.hpp"
#include "game/door_actor.hpp"
#include "game/door_leaves.hpp"
#include "game/fade.hpp"
#include "game/fade_state.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/room_scripts.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::exit_transitions_state;

namespace od::port {

using namespace exit_transitions_state;

namespace {

constexpr const char* kExitArea = "transition port"; // the area of its port-gap errors

// +4 of the kinds whose states come from a state table: the start, the step sequence, the end
enum class KindState : uint8_t { Start = 0, Sequence = 1, End = 2 };
// +5 of their ends: before / after the room switched (the sliding leaves stop first)
enum class EndSub : uint8_t { BeforeSwitch = 0, AfterSwitch = 1 };
enum class LeavesEndSub : uint8_t { StopLeaves = 0, BeforeSwitch = 1, AfterSwitch = 2 };
// +5 of the door actor's start: the door looked for, then the player waited for
enum class DoorActorStartSub : uint8_t { FindDoor = 0, WaitPlayer = 1 };
// +4 of the frozen kind (no state table)
enum class FrozenState : uint8_t { Start = 0, BeforeSwitch = 1, AfterSwitch = 2 };
// +4 of the timed kinds (ladder, push, teleporter; no state table): the replay pad (the teleporter: and the RNG), the
// start once the player is script driven, the timed part, the handshake, the end after the switch
enum class TimedState : uint8_t { PadOn = 0, Start = 1, Timed = 2, BeforeSwitch = 3, AfterSwitch = 4 };
// the script's actors: the door leaves
constexpr size_t kFirstLeaf = 0, kSecondLeaf = 1;

// ---- the script's own bytes (its parts of the block's script bytes, by script state / kind) -------------------------

// ---- the exe's tables of the transitions ----------------------------------------------------------------------------
// a yaw quadrant's axis signs (s8 pairs by yaw >> 10: [i], [i + 1]); the door kinds inline the walk-out's placement
// with their own copies of the table (the same bytes)
using AxisSigns = DataTable<ScalarTable<int8_t, 5>>;
const AxisSigns passageSigns{TableId::PassageAxisSigns};         // exe 0x88c7c0
const AxisSigns hingedSigns{TableId::HingedDoorAxisSigns};       // exe 0x88c5c4
const AxisSigns doubleSigns{TableId::DoubleDoorAxisSigns};       // exe 0x88c62c
const AxisSigns slidingPairSigns{TableId::SlidingPairAxisSigns}; // exe 0x88c68c
const AxisSigns walkOffSigns{TableId::WalkOffAxisSigns};         // exe 0x88c89c
// a transition camera's view (the exit's view byte picks it)
struct TransitionView : DataRecord {
    int16_t pitch = 0;
    int16_t yawOffset = 0; // from the player's yaw
    int16_t distance = 0;
    static TransitionView load(const TableReader& r) { return {{}, r.s16(0), r.s16(2), r.s16(4)}; }
};
template <size_t N> using ViewTable = DataTable<RecordTable<TransitionView, N, 8>>;
const ViewTable<0x100> passageViews{TableId::PassageViews};         // exe 0x88c7a8
const ViewTable<8> hingedViews{TableId::HingedDoorViews};           // exe 0x88c578
const ViewTable<0x100> doubleViews{TableId::DoubleDoorViews};       // exe 0x88c600
const ViewTable<0x100> slidingPairViews{TableId::SlidingPairViews}; // exe 0x88c668
const ViewTable<0x100> walkOffViews{TableId::WalkOffViews};         // exe 0x88c868
// the door leaves' offsets (the exit's high nibble of byte 2 picks them)
struct LeafOffset : DataRecord {
    int16_t x = 0, z = 0;
    static LeafOffset load(const TableReader& r) { return {{}, r.s16(0), r.s16(2)}; }
};
using LeafOffsets = DataTable<RecordTable<LeafOffset, 0x10, 4>>;
const LeafOffsets passageLeafOffsets{TableId::PassageLeafOffsets};         // exe 0x88c7d4
const LeafOffsets doubleLeafOffsets{TableId::DoubleDoorLeafOffsets};       // exe 0x88c634
const LeafOffsets slidingPairLeafOffsets{TableId::SlidingPairLeafOffsets}; // exe 0x88c694
// the kinds' state tables (the named routines, engine/routines.csv)
const DataTable<RoutineTable<3>> passageStates{TableId::PassageStates};         // exe 0x88c7c8
const DataTable<RoutineTable<3>> walkOffStates{TableId::WalkOffStates};         // exe 0x88c890
const DataTable<RoutineTable<5>> doorActorStates{TableId::DoorActorStates};     // exe 0x88c968
const DataTable<RoutineTable<3>> hingedStates{TableId::HingedDoorStates};       // exe 0x88c5b8
const DataTable<RoutineTable<3>> doubleStates{TableId::DoubleDoorStates};       // exe 0x88c620
const DataTable<RoutineTable<3>> slidingPairStates{TableId::SlidingPairStates}; // exe 0x88c680
const DataTable<RoutineTable<3>> pitStates{TableId::PitStates};                 // exe 0x88c7f8
// the door actor's camera scripts by its yaw (0, a quarter, a half, other; exe 0x88c948): the scripts' addresses, as
// the script state stores them (the address is the record's identity)
const DataTable<ScalarTable<uint32_t, 4>> doorActorCameras{TableId::DoorActorCameras};
// the frozen transition runs its steps with no camera script
constexpr uint32_t kNoCamera = 0;

// ---- sounds, distances, timings -------------------------------------------------------------------------------------
constexpr uint16_t kLeavesOpenSound = 0xba;  // the sliding leaves start to open
constexpr uint16_t kLeavesHoldSound = 0xbd;  // they stop open (the walk starts)
constexpr uint16_t kLeavesCloseSound = 0xbb; // they start to close
constexpr uint16_t kLeavesStopSound = 0xbc;  // they stop (the room switches)
constexpr uint16_t kDoorSinkSound = 0x13f;   // the door actor carrying the player
constexpr uint16_t kTeleportSound = 0x2e9;
constexpr uint32_t kNoSoundPosition = 0;
constexpr int kQuadrantShift = 10;
constexpr int16_t kPassageLeaveBack = 0x90, kPassageArriveBack = 0x110; // the player placed behind the point
constexpr int kPassageFarCell = 0x100;     // the segment beyond the threshold
constexpr int16_t kHingedSide = -0x24, kDoorBack = 0x7c, kDoubleSide = 0x28, kSlidingPairBack = 0x90;
constexpr int kCameraHeight = 0xa0;        // the transition camera's point above the player's feet
constexpr int kWalkOffInset = 4;           // the walk-off's start inside the trigger (<< 4)
constexpr int kWalkOffCameraBack = 9;      // its camera behind the start (<< 9)
constexpr int16_t kWalkOffStep = -0x200;
constexpr int kDoorReach = 0x200;          // the door actor within this of the player
constexpr int16_t kDoorSinkDrop = 0x40;    // the player stands this far into the door
constexpr int16_t kDoorSinkMaxSpeed = 0x30, kDoorSinkAcceleration = 4;
constexpr int32_t kDoorSinkFadeTick = 0x3c;
constexpr uint8_t kDoorSinkFade = kFadeOutRequest | 2;
constexpr uint8_t kDoorActorFadeOut = kFadeOutRequest, kDoorActorFadeIn = kFadeInRequest | 2;
constexpr int32_t kDoorSinkSoundTicks = 0x40;
constexpr uint8_t kFastPace = 3;           // at 20 / s the sink's sound every 3 ticks, else every 9
constexpr int kSinkSoundFast = 3, kSinkSoundSlow = 9;
constexpr int16_t kWalkThroughTurn = 0xe3;
constexpr uint8_t kWalkThroughAnimStep = 8;
constexpr int kLeafSwingDivisor = 17;      // the leaf's swing speed: its opening / this
constexpr int16_t kLeafSwingAcceleration = -2;
constexpr int kLadderCameraHeight = 0x80;
constexpr int16_t kLadderClimbSpeed = 0x37;
constexpr uint32_t kLadderTicks = 0x50;
constexpr int kLadderGrip = 6;             // the player placed this far up the trigger
constexpr int16_t kLadderFloor = -0x800;   // the climb stops above this height
constexpr int32_t kPitYaw = 0x1400000, kPitPitch = 0xf00000, kPitDistance = 0x4000000;
constexpr int kPitCameraHeight = 0x280;
constexpr int32_t kPitTicks = 9;
constexpr int kPushedCameraHeight = 0x40;
constexpr int kPushDistance = 0x500;
constexpr uint32_t kPushedYawOffset = 0x250;
constexpr int32_t kPushedPitch = 0xf00000, kPushedDistance = 0x1800000;
constexpr uint32_t kPushedTicks = 0x10;
constexpr uint32_t kTeleportRngSeed = 0x873ca9e6; // the RNG while the sparks fly
constexpr int kTeleportCameraHeight = 0x100;
constexpr uint32_t kTeleportYawBase = 0x6000000;
constexpr int32_t kTeleportPitch = int32_t(0xffd00000u), kTeleportDistance = 0x4000000;
constexpr int16_t kTeleportStep = 0x80, kSparkOffset = 0x20, kSparkHeight = -0xc0;
constexpr int16_t kTeleportWalk = -0x50;
constexpr uint32_t kTeleportFlashStart = 0x10, kTeleportEnd = 0x38;
constexpr uint16_t kBoneFlash = 0x220;
constexpr uint8_t kTeleportSparkType = 0x1e; // a stage table type (per class: game/global_class_types.hpp)
constexpr uint8_t kTeleportSparkVariant = 3;
constexpr int kFirstSparks = 10, kSparksPerTick = 2, kSparksWaiting = 1;
constexpr uint8_t kRoomPadActivated = 1;

// the script bytes / the leaves / the request
StaticActor& leaf(size_t i) { return asStatic(*scriptState().actors[i]); }

// the cell's segment under a 16.16 point (the exe reads address 0 outside the grid: a port gap)
uint16_t cellSegment(int32_t x, int32_t z) {
    const uint32_t c = gridCellOf(wholePart(x), wholePart(z)); // exe 0x40f030
    if (!c) notPorted(kExitArea, "a door segment outside the grid (the exe reads address 0)");
    return uint16_t(GridCell(c).word & kCellSegmentMask);
}
int32_t toFixed(int v) { return int32_t(uint32_t(v) << 16); }

// exe 0x42c2e0(x | y << 16, z | yaw << 16, side, back): the player at the place moved by (side, back) in the yaw's axis
// frame, dropped onto the ground, facing the yaw (the door kinds inline it with their copies of the signs)
void placePlayer(const PlayerEnv& env, const RoomPlace& place, int16_t side, int16_t back, const AxisSigns& signs) {
    Player& p = player();
    const int16_t yaw = place.yaw;
    const int i = yaw >> kQuadrantShift;
    const int c0 = signs[size_t(i)], c1 = signs[size_t(i + 1)];
    const int32_t x = toFixed(uint16_t(int16_t(place.x) + c0 * side + c1 * back));
    const int32_t z = toFixed(uint16_t(int16_t(place.z) + (c1 * side - c0 * back)));
    const int32_t y = toFixed(int16_t(place.y));
    p.pos.x = x;
    p.pos.y = y;
    p.pos.z = z;
    p.prevPos.x = x;
    p.prevPos.y = y;
    p.prevPos.z = z;
    if (actorTestMove(env, p) != 0) actorPushOut(env, p);
    const int32_t ground = toFixed(uint16_t(actorGround(env, p))) | (y & 0xffff);
    p.pos.y = ground;
    p.prevPos.y = ground;
    p.pos.z = z;
    p.prevPos.z = z;
    p.pos.x = x;
    p.prevPos.x = x;
    p.rotation.z = 0;
    p.rotation.x = 0;
    p.rotation.y = yaw;
    p.prevRotation = p.rotation;
}

// exe 0x42c410(x, z, view): the transition camera between the player and the point, above the player's feet, at the
// view (the door kinds inline it with their tables)
template <size_t N> void doorCamera(int16_t x, int16_t z, int16_t view, const ViewTable<N>& views) {
    ScriptCamera& sc = scriptCamera();
    const Player& p = player();
    sc.setAnchor(ScriptCameraAnchor::Absolute);
    sc.point[0] = int32_t(uint32_t((int32_t(p.pos.x.whole) + x) << 15) & 0xffff0000u);
    sc.point[2] = int32_t(uint32_t((int32_t(p.pos.z.whole) + z) << 15) & 0xffff0000u);
    sc.point[1] = toFixed(int32_t(p.pos.y.whole) - kCameraHeight);
    const TransitionView v = views[size_t(int32_t(view))];
    sc.angles[kScriptPitch] = toFixed(v.pitch);
    sc.angles[kScriptYaw] = int32_t(uint32_t((int32_t(v.yawOffset) + p.rotation.y) << 16) & 0xfff0000u);
    sc.angles[kScriptDistance] = toFixed(v.distance);
}

// the transition's end once the room switched: flag 0x700 off, the player's pad back, the script ended
void endTransition() {
    clearStoryFlag(kFlagTransitionArmed);
    scriptReplayPadOff();
    scriptEndBlock();
}

// the request's handshake: the field mode's acceptance answered (2 -> 3); true when answered
bool answerRequest() {
    TransitionRequest& r = transitionRequest();
    if (r.state != kTransitionAccepted) return false;
    r.state = kTransitionScriptDone;
    return true;
}

// ---- the steps (word 1 of each step record: the exe routine run each tick of the step) ------------------------------
// both sliding leaves to `sub`, the sound
void slidingLeavesTo(SlidingLeafSub sub, uint16_t sound) {
    StaticActor& first = leaf(kFirstLeaf);
    first.subState = uint8_t(sub);
    first.step = 0;
    StaticActor& second = leaf(kSecondLeaf);
    second.subState = uint8_t(sub);
    second.step = 0;
    soundPlayAtPos(sound, kNoSoundPosition);
}
// exe 0x42b4e0: the leaves open, the pad released
void stepLeavesOpen() {
    if (scriptState().stepTick != 0) return;
    replayPad().held = 0;
    slidingLeavesTo(SlidingLeafSub::Open, kLeavesOpenSound);
}
// exe 0x42b8b0: the leaves stop open, the replay pad walks forward
void stepLeavesHoldWalkOut() {
    if (scriptState().stepTick != 0) return;
    replayPad().held = player().button(PlayerButton::Forward);
    slidingLeavesTo(SlidingLeafSub::Stop, kLeavesHoldSound);
}
// exe 0x42b8f0: the leaves close, the pad released
void stepLeavesClose() {
    if (scriptState().stepTick != 0) return;
    replayPad().held = 0;
    slidingLeavesTo(SlidingLeafSub::Close, kLeavesCloseSound);
}
// exe 0x42d550: the walk-off's walk: forward in the movement frame, pushed out, on the ground
void stepWalkOff(const PlayerEnv& env) {
    Player& p = player();
    moveInFacing(p, 0, 0, kWalkOffStep);
    if (actorTestMove(env, p) != 0) actorPushOut(env, p);
    p.pos.y.whole = int16_t(actorGround(env, p));
}
// exe 0x42d7d0: the player stands in the door actor and both sink (or rise); the fade after 60 ticks, then the next
// step
void stepDoorActorSink() {
    ScriptState& s = scriptState();
    DoorActorScriptBytes& bytes = scriptState().part<DoorActorScriptBytes>();
    StaticActor& door = staticActorAt(actorBySlot(bytes.doorSlot));
    const DoorFields doorFields(door);
    Player& p = player();
    if (s.stepTick == 0) {
        p.pos.x = door.pos.x;
        p.pos.y = door.pos.y;
        p.pos.y.whole = int16_t(p.pos.y.whole - kDoorSinkDrop);
        p.pos.z = door.pos.z;
        p.rotation.y = int16_t((door.rotation.y - kHalfTurn) & kAngleMask);
        bytes.sinkSpeed = 0;
        s.clearStepPhase();
    }
    if (s.stepPhase == 0) {
        if (s.stepTick == kDoorSinkFadeTick) {
            scriptFade(kDoorSinkFade, 0);
            ++s.stepPhase;
        }
    } else if (s.stepPhase == 1 && fadeState().busy == 0) {
        s.clearStepPhase();
        scriptSequenceSkip(1);
    }
    if (s.stepTick != 0) {
        if (bytes.sinkSpeed < kDoorSinkMaxSpeed) bytes.sinkSpeed = int16_t(bytes.sinkSpeed + kDoorSinkAcceleration);
        const int v = doorFields.descends == 0 ? -bytes.sinkSpeed : bytes.sinkSpeed;
        integrateVelocity(p, 0, v, 0);
        integrateVelocity(door, 0, v, 0);
    }
    const int32_t t = s.stepTick;
    if (t >= 1 && t <= kDoorSinkSoundTicks) {
        const int n = t - 1;
        if ((display().pace == kFastPace ? n % kSinkSoundFast : n % kSinkSoundSlow) == 0)
            soundPlayAtPos(kDoorSinkSound, door.pos);
    }
}
// exe 0x42abc0: kinds 0 / 1: the player script driven, pushing the door
void stepDoorPush() {
    if (scriptState().stepTick != 0) return;
    playerToScriptIfFree();
    playerScriptAnim(PlayerAnim::DoorPush, 0, true);
}
// exe 0x42abe0: the hinged leaf opens with the player's hand
void stepLeafFollowsHand() {
    if (scriptState().stepTick == 0) leaf(kFirstLeaf).subState = uint8_t(SwingLeafSub::FollowHand);
}
// exe 0x42ac00: the hinged leaf swings on at its opening / 17 a tick, slowing by 2
void stepLeafSwingsOn() {
    if (scriptState().stepTick != 0) return;
    StaticActor& l = leaf(kFirstLeaf);
    SwingLeafFields& f = classState<SwingLeafFields>(l);
    l.subState = uint8_t(SwingLeafSub::SwingOn);
    l.spin.y = int16_t(f.opening / kLeafSwingDivisor);
    f.swingAcceleration = kLeafSwingAcceleration;
}
// The walk through a door of the hinged kinds: the step each tick, the extra push from tick `fastTick` on
struct WalkThrough {
    int16_t step, push;
    int32_t fastTick;
};
constexpr WalkThrough kHingedWalk{-0xc, -0x28, 0x11}; // kind 0
constexpr WalkThrough kDoubleWalk{-0xe, -0x30, 0xf};  // kind 1
// exe 0x53d9f0 (kind 0) / 0x42b020 (kind 1): the player walks through the door: tick 0 the replay pad forward and
// walking, the walk animation, the yaw + 0xe3; tick 1 back to its play; from its fast tick an extra push; every tick
// its step
void stepWalkThrough(const WalkThrough& walk) {
    Player& p = player();
    const int32_t t = scriptState().stepTick;
    if (t == 0) {
        replayPad().held = uint16_t(p.button(PlayerButton::Forward) | p.button(PlayerButton::Walk));
        playerScriptAnim(PlayerAnim::Walk, kWalkThroughAnimStep, true);
        p.rotation.y = int16_t(uint16_t(p.rotation.y) + kWalkThroughTurn);
        p.prevRotation = p.rotation;
    } else if (t == 1) {
        playerBackToPlay();
    } else if (t >= walk.fastTick) {
        moveInFacing(p, 0, 0, walk.push);
    }
    moveInFacing(p, 0, 0, walk.step);
}

// the step's routine of this tick (word 1 of the step record, named by the source)
void runStep(const PlayerEnv& env, const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::ExitLeavesOpenStep: stepLeavesOpen(); break;
    case RoutineId::ExitLeavesHoldWalkOutStep: stepLeavesHoldWalkOut(); break;
    case RoutineId::ExitLeavesCloseStep: stepLeavesClose(); break;
    case RoutineId::ExitWalkOffStep: stepWalkOff(env); break;
    case RoutineId::ExitDoorActorSinkStep: stepDoorActorSink(); break;
    case RoutineId::ExitDoorPushStep: stepDoorPush(); break;
    case RoutineId::ExitLeafFollowsHandStep: stepLeafFollowsHand(); break;
    case RoutineId::ExitLeafSwingsOnStep: stepLeafSwingsOn(); break;
    case RoutineId::ExitHingedWalkThroughStep: stepWalkThrough(kHingedWalk); break;
    case RoutineId::ExitDoubleWalkThroughStep: stepWalkThrough(kDoubleWalk); break;
    default: notPortedAt(kExitArea, "sequence step", routine.pointerValue());
    }
}

// exe 0x42d4c0 (state 1 of most kinds): the step's ids and tick copied, its routine (none at the list's end: the end
// state), the sequence's tick
void sequenceState(const PlayerEnv& env) {
    ScriptState& s = scriptState();
    s.part<CastTimelineStep>().stepId = s.stepId;
    s.part<CastTimelineTick>().stepTick = s.stepTick;
    s.part<CastTimelineStep>().stepSub = s.stepSub;
    if (s.steps->id != kScriptStepEnd) runStep(env, stateRoutine(s.steps->routine));
    else s.state = uint8_t(KindState::End);
    scriptSequenceTick();
}

// ---- kind 6 (exit type 8: a sliding door / passage), states 0x88c7c8 ------------------------------------------------
// Runs twice: in the room left (flag 0x701 clear: the walk-out through the trigger, request 2 -> 3, flag 0x701 set) and
// again in the room entered (flag 0x701 set: placed 0x110 behind the destination, the walk-in, the flags cleared).
// exe 0x42bd60
void passageStart(const PlayerEnv& env) {
    ScriptState& s = scriptState();
    TransitionRequest& r = transitionRequest();
    DoorScriptBytes& bytes = scriptState().part<DoorScriptBytes>();
    if (!storyFlag(kFlagTransitionArriving)) actorsSweep(kSweepTransition, kSweepFree);
    windowCloseAll(WindowClose::Force);
    const uint32_t first = allocActor60(0);
    pointAtWord(s.actors[kFirstLeaf], first);
    if (first == 0) return;
    const uint32_t second = allocActor60(0);
    pointAtWord(s.actors[kSecondLeaf], second);
    if (second == 0) {
        freeActor(leaf(kFirstLeaf));
        return;
    }
    StaticActor& l0 = staticActorAt(first);
    StaticActor& l1 = staticActorAt(second);
    for (StaticActor* l : {&l0, &l1}) {
        l->flags = kActorLiveDrawn;
        l->loopFlags |= kActorSweepGroup1;
        l->type = kSlidingLeafType;
    }
    l0.variant = uint8_t(r.exitByte1 << 1);
    l1.variant = uint8_t((r.exitByte1 << 1) + 1);
    const uint8_t byte2 = r.exitByte2;
    const int16_t leafIndex = int16_t(byte2 >> 4), view = int16_t(byte2 & 0xf);
    const LeafOffset offset = passageLeafOffsets[size_t(int32_t(leafIndex))];
    const bool arriving = storyFlag(kFlagTransitionArriving);
    const RoomPlace& place = arriving ? r.destination : r.trigger;
    if (!arriving) bytes.otherStage = gameState().stage != r.destStage ? 1 : 0;
    for (StaticActor* l : {&l0, &l1}) {
        DoorLeafParams params(*l);
        params.offsetX = arriving ? int16_t(-offset.x) : int16_t(offset.x);
        params.offsetZ = arriving ? int16_t(-offset.z) : int16_t(offset.z);
    }
    for (StaticActor* l : {&l0, &l1}) {
        l->pos.x = toFixed(place.x);
        l->pos.y = toFixed(place.y);
        l->pos.z = toFixed(place.z);
    }
    scriptSequenceStart(stateAddress(TableId::ExitPassageCamera), arriving ? stateAddress(TableId::ExitPassageInSteps) : stateAddress(TableId::ExitPassageOutSteps));
    scriptReplayPadOn();
    const int32_t x = toFixed(place.x), z = toFixed(place.z);
    bytes.doorSegment = cellSegment(x, z);
    const int i = int16_t(place.yaw) >> kQuadrantShift;
    const int c0 = passageSigns[size_t(i)], c1 = passageSigns[size_t(i + 1)];
    const int32_t farX = toFixed(uint16_t(int16_t(x >> 16) + (arriving ? c1 : -c1) * kPassageFarCell));
    const int32_t farZ = toFixed(uint16_t(int16_t(z >> 16) + (arriving ? -c0 : c0) * kPassageFarCell));
    bytes.farSegment = cellSegment(farX, farZ);
    segmentSetAltVolumes(bytes.doorSegment, true);
    segmentSetAltVolumes(bytes.farSegment, true);
    segmentSetDoorsHidden(bytes.doorSegment, true);
    placePlayer(env, place, 0, arriving ? kPassageArriveBack : kPassageLeaveBack, passageSigns);
    doorCamera(place.x, place.z, view, passageViews);
    s.state = uint8_t(KindState::Sequence);
    s.subState = 0;
}
// exe 0x42c190: arriving: the leaves freed, the flags cleared, the segments back, the end; leaving: the leaves stop,
// the handshake, then (the room switched) the leaves freed, the segments back, flag 0x701 set (the second run)
void passageEnd() {
    ScriptState& s = scriptState();
    DoorScriptBytes& bytes = scriptState().part<DoorScriptBytes>();
    if (storyFlag(kFlagTransitionArriving)) {
        soundPlayAtPos(kLeavesStopSound, kNoSoundPosition);
        replayPad().held = 0;
        leaf(kFirstLeaf).state = uint8_t(DoorLeafState::Free);
        leaf(kSecondLeaf).state = uint8_t(DoorLeafState::Free);
        clearStoryFlag(kFlagTransitionArmed);
        clearStoryFlag(kFlagTransitionArriving);
        segmentSetDoorsHidden(bytes.doorSegment, false);
        segmentSetAltVolumes(bytes.doorSegment, false);
        segmentSetAltVolumes(bytes.farSegment, false);
        scriptReplayPadOff();
        scriptEndBlock();
        return;
    }
    switch (LeavesEndSub(s.subState)) {
    case LeavesEndSub::StopLeaves:
        replayPad().held = 0;
        slidingLeavesTo(SlidingLeafSub::Stop, kLeavesStopSound);
        ++s.subState;
        [[fallthrough]];
    case LeavesEndSub::BeforeSwitch:
        if (answerRequest()) ++s.subState;
        return;
    case LeavesEndSub::AfterSwitch:
        if (transitionRequest().state != kTransitionNone) return;
        leaf(kFirstLeaf).state = uint8_t(DoorLeafState::Free);
        leaf(kSecondLeaf).state = uint8_t(DoorLeafState::Free);
        segmentSetDoorsHidden(bytes.doorSegment, false);
        segmentSetAltVolumes(bytes.doorSegment, false);
        segmentSetAltVolumes(bytes.farSegment, false);
        setStoryFlag(kFlagTransitionArriving);
        scriptReplayPadOff();
        scriptEndBlock();
        return;
    default: return;
    }
}

// ---- kind 13 (exit type 15: the walk-off), states 0x88c890 ----------------------------------------------------------
// exe 0x42d280: the player script driven, still, placed inside the trigger on its yaw and on the ground, running; the
// camera behind it
void walkOffStart(const PlayerEnv& env) {
    ScriptState& s = scriptState();
    const TransitionRequest& r = transitionRequest();
    Player& p = player();
    ScriptCamera& sc = scriptCamera();
    actorsSweep(kSweepTransition, kSweepFree);
    windowCloseAll(WindowClose::Force);
    scriptSequenceStart(stateAddress(TableId::ExitWalkOffCamera), stateAddress(TableId::ExitWalkOffSteps));
    scriptReplayPadOn();
    p.airborne = 0;
    p.onPlatform = 0;
    p.setPrevMoveDir(MoveDir::Forward);
    p.setMoveDir(MoveDir::Forward);
    p.velocity.clearWords();
    p.clearDirectionOffsetWords();
    p.interactionTarget.clear();
    p.carryFlags = 0;
    playerToScriptIfFree();
    const int16_t yaw = r.trigger.yaw;
    p.rotation.z = 0;
    p.rotation.y = yaw;
    p.rotation.x = 0;
    p.prevRotation = p.rotation;
    const int i = yaw >> kQuadrantShift;
    const int c0 = walkOffSigns[size_t(i)], c1 = walkOffSigns[size_t(i + 1)];
    const int32_t x = toFixed(uint16_t(r.trigger.x + int16_t(c1 << kWalkOffInset)));
    const int32_t z = toFixed(uint16_t(r.trigger.z + int16_t(-c0 << kWalkOffInset)));
    const int32_t y = toFixed(r.trigger.y);
    p.pos.x = x;
    p.pos.y = y;
    p.pos.z = z;
    p.prevPos.x = x;
    p.prevPos.y = y;
    p.prevPos.z = z;
    if (actorTestMove(env, p) != 0) actorPushOut(env, p);
    const int16_t ground = int16_t(actorGround(env, p));
    const int32_t groundY = toFixed(uint16_t(ground));
    p.pos.x = x;
    p.pos.y = groundY;
    p.pos.z = z;
    p.prevPos.x = x;
    p.prevPos.y = groundY;
    p.prevPos.z = z;
    playerScriptAnim(PlayerAnim::Run, 0, true);
    sc.point[0] = toFixed(int32_t(int16_t(-c1 << kWalkOffCameraBack)) + int16_t(uint32_t(x) >> 16));
    sc.point[1] = toFixed(int32_t(ground) - kCameraHeight);
    sc.setAnchor(ScriptCameraAnchor::Absolute);
    const TransitionView v = walkOffViews[r.exitByte2];
    sc.point[2] = toFixed(int32_t(int16_t(c0 << kWalkOffCameraBack)) + int16_t(uint32_t(z) >> 16));
    sc.angles[kScriptPitch] = toFixed(v.pitch);
    sc.angles[kScriptYaw] = int32_t(uint32_t((int32_t(v.yawOffset) + p.rotation.y) << 16) & 0xfff0000u);
    sc.angles[kScriptDistance] = toFixed(v.distance);
    s.subState = 0;
    s.state = uint8_t(KindState::Sequence);
}
// exe 0x42d500: the handshake, then (the room switched) the player back to its play, the end
void walkOffEnd() {
    ScriptState& s = scriptState();
    if (EndSub(s.subState) == EndSub::BeforeSwitch) {
        if (answerRequest()) ++s.subState;
        return;
    }
    if (transitionRequest().state != kTransitionNone) return;
    clearStoryFlag(kFlagTransitionArmed);
    playerBackToPlay();
    scriptReplayPadOff();
    scriptEndBlock();
}

// ---- kind 14 (the door actor's "yes": game/door_actor), states 0x88c968 --------------------------------------------
// exe 0x42d5c0: the door actor within reach of the player standing on an exit's trigger found (none: the end), then,
// once the player is script driven, the camera by the door's yaw and the request armed from that exit
void doorActorStart() {
    ScriptState& s = scriptState();
    DoorActorScriptBytes& bytes = scriptState().part<DoorActorScriptBytes>();
    Player& p = player();
    if (DoorActorStartSub(s.subState) == DoorActorStartSub::FindDoor) {
        scriptReplayPadOn();
        bytes.doorSlot = 0;
        for (size_t i = 0; i < StaticPool::size(); ++i) {
            const StaticActor& a = staticPool()[i];
            if (!(a.flags & kActorLive) || a.type != kDoorActorType) continue;
            if (std::abs(int(p.pos.x.whole) - int(a.pos.x.whole)) >= kDoorReach) continue;
            if (std::abs(int(p.pos.z.whole) - int(a.pos.z.whole)) >= kDoorReach) continue;
            bytes.exitIndex = 0;
            for (uint32_t e = 0; exitEntry(e).id != kExitEnd; ++e) {
                const ExitEntry exit = exitEntry(e);
                if (uint16_t(a.pos.x.whole) == uint16_t(exit.trigger.x) &&
                    uint16_t(a.pos.y.whole) == uint16_t(exit.trigger.y) &&
                    uint16_t(a.pos.z.whole) == uint16_t(exit.trigger.z)) {
                    bytes.doorSlot = a.slot;
                    s.flags |= kScriptUnskippable;
                    ++s.subState;
                    return;
                }
                ++bytes.exitIndex;
            }
        }
        endTransition();
        return;
    }
    if (DoorActorStartSub(s.subState) != DoorActorStartSub::WaitPlayer || playerToScriptIfFree() != 0)
        return;
    actorsSweep(kSweepTransition, kSweepFree);
    windowCloseAll(WindowClose::Force);
    s.enterState(uint8_t(KindState::Sequence));
    const int16_t doorYaw = staticActorAt(actorBySlot(bytes.doorSlot)).rotation.y;
    const int k = doorYaw == 0 ? 0 : doorYaw == kQuarterTurn ? 1 : (doorYaw != kHalfTurn) + 2;
    scriptSequenceStart(doorActorCameras[size_t(k)], stateAddress(TableId::ExitDoorActorSteps));
    p.clearDirectionOffsetWords();
    p.idleFaces[0] = 0;
    p.idleFaces[1] = 0;
    const ExitEntry exit = exitEntry(bytes.exitIndex);
    TransitionRequest& r = transitionRequest();
    r.state = kTransitionAsked;
    r.keepPosition = 0;
    r.exitId = exit.id;
    r.exitByte1 = exit.byte1;
    r.exitByte2 = exit.byte2;
    r.destStage = exit.destStage;
    r.destRoom = exit.destRoom;
    r.trigger = exit.trigger;
    r.destination = exit.destination;
    r.fadeOut = kDoorActorFadeOut;
    r.fadeIn = kDoorActorFadeIn;
}
// exe 0x53f090: the step's routine (none at the list's end: the end state), the sequence's tick
void doorActorSequence(const PlayerEnv& env) {
    ScriptState& s = scriptState();
    if (s.steps->id == kScriptStepEnd) s.enterState(uint8_t(KindState::End));
    else if (s.steps->routine != 0) runStep(env, stateRoutine(s.steps->routine));
    scriptSequenceTick();
}
// exe 0x42c5f0 (also kind 8's end): the handshake, then (the room switched) the end
void handshakeEnd() {
    ScriptState& s = scriptState();
    const EndSub sub = EndSub(s.subState);
    if (sub == EndSub::BeforeSwitch) {
        if (transitionRequest().state == kTransitionAccepted) {
            s.subState = uint8_t(EndSub::AfterSwitch);
            transitionRequest().state = kTransitionScriptDone;
        }
    } else if (sub == EndSub::AfterSwitch && transitionRequest().state == kTransitionNone) {
        endTransition();
    }
}

// ---- kinds 0 / 1 / 2 (exit types 2 / 3 / 4: a hinged door, a double hinged door, a sliding door pair) ---------------
// One run in the room left (no arrival walk-in): the door leaves spawned (game/door_leaves), the door segment's door
// list hidden (the renderer then draws the open doorway), the player placed in front of the trigger, a camera script,
// a step list; the end waits for the room switch.
// a leaf at the trigger: shown, its type, variant and offsets
void spawnLeaf(StaticActor& l, uint8_t type, uint8_t variant, int16_t offsetX, int16_t offsetZ) {
    const TransitionRequest& r = transitionRequest();
    DoorLeafParams params(l);
    l.flags = kActorLiveDrawn;
    l.type = type;
    l.variant = variant;
    params.offsetX = offsetX;
    params.offsetZ = offsetZ;
    l.pos.x = toFixed(r.trigger.x);
    l.pos.y = toFixed(r.trigger.y);
    l.pos.z = toFixed(r.trigger.z);
}
// the door segment under the trigger, its door list hidden (exe 0x600f50(segment, 1))
void hideDoorSegment() {
    const TransitionRequest& r = transitionRequest();
    DoorScriptBytes& bytes = scriptState().part<DoorScriptBytes>();
    bytes.doorSegment = cellSegment(toFixed(r.trigger.x), toFixed(r.trigger.z));
    segmentSetDoorsHidden(bytes.doorSegment, true);
}
// exe 0x42a8c0: kind 0's start (one hinged leaf without offsets, its variant the exit's byte 1)
void hingedDoorStart(const PlayerEnv& env) {
    ScriptState& s = scriptState();
    const TransitionRequest& r = transitionRequest();
    actorsSweep(kSweepTransition, kSweepFree);
    windowCloseAll(WindowClose::Force);
    const uint32_t address = allocActor60(0);
    if (address == 0) return;
    StaticActor& l = staticActorAt(address);
    l.flags = kActorLiveDrawn;
    l.type = kHingedLeafType;
    DoorLeafParams(l).clear();
    l.variant = r.exitByte1;
    l.pos.x = toFixed(r.trigger.x);
    l.pos.y = toFixed(r.trigger.y);
    l.pos.z = toFixed(r.trigger.z);
    pointAtWord(s.actors[kFirstLeaf], address);
    scriptSequenceStart(stateAddress(TableId::ExitHingedDoorCamera), stateAddress(TableId::ExitHingedDoorSteps));
    scriptReplayPadOn();
    hideDoorSegment();
    placePlayer(env, r.trigger, kHingedSide, kDoorBack, hingedSigns);
    doorCamera(r.trigger.x, r.trigger.z, int16_t(r.exitByte2), hingedViews);
    s.state = uint8_t(KindState::Sequence);
    s.subState = 0;
}
// a two-leaf door kind's start
struct DoorPairKind {
    uint8_t leafType;
    const LeafOffsets& offsets;
    TableId camera, steps; // the camera script and the steps
    int16_t side, back;
    const AxisSigns& signs;
    const ViewTable<0x100>& views;
};
// exe 0x42ac60 (kind 1: double hinged leaves) / 0x42b0f0 (kind 2: a sliding pair): two leaves with the offsets of the
// exit's high nibble, mirrored by their variants
void doorPairStart(const PlayerEnv& env, const DoorPairKind& kind) {
    ScriptState& s = scriptState();
    const TransitionRequest& r = transitionRequest();
    actorsSweep(kSweepTransition, kSweepFree);
    windowCloseAll(WindowClose::Force);
    const uint32_t first = allocActor60(0);
    if (first == 0) return;
    pointAtWord(s.actors[kFirstLeaf], first);
    const uint32_t second = allocActor60(0);
    if (second == 0) {
        freeActor(leaf(kFirstLeaf));
        return;
    }
    pointAtWord(s.actors[kSecondLeaf], second);
    const LeafOffset offset = kind.offsets[size_t(r.exitByte2 >> 4)];
    spawnLeaf(staticActorAt(first), kind.leafType, uint8_t(r.exitByte1 << 1), offset.x, offset.z);
    spawnLeaf(staticActorAt(second), kind.leafType, uint8_t((r.exitByte1 << 1) + 1), offset.x, offset.z);
    scriptSequenceStart(stateAddress(kind.camera), stateAddress(kind.steps));
    scriptReplayPadOn();
    hideDoorSegment();
    placePlayer(env, r.trigger, kind.side, kind.back, kind.signs);
    doorCamera(r.trigger.x, r.trigger.z, int16_t(r.exitByte2 & 0xf), kind.views);
    s.state = uint8_t(KindState::Sequence);
    s.subState = 0;
}
const DoorPairKind kDoubleDoor{kDoubleLeafType, doubleLeafOffsets, TableId::ExitDoubleDoorCamera, TableId::ExitDoubleDoorSteps, kDoubleSide, kDoorBack,
                               doubleSigns, doubleViews};
const DoorPairKind kSlidingPair{kSlidingLeafType, slidingPairLeafOffsets, TableId::ExitSlidingPairCamera, TableId::ExitSlidingPairSteps, 0,
                                kSlidingPairBack, slidingPairSigns, slidingPairViews};
// exe 0x42ab50 (kind 0) / 0x42afb0 (kind 1): the handshake (the pad released), then (the room switched) the leaves
// freed, the door segment shown, the end
void hingedDoorEnd(bool pair) {
    ScriptState& s = scriptState();
    if (EndSub(s.subState) == EndSub::BeforeSwitch) {
        replayPad().held = 0;
        if (answerRequest()) ++s.subState;
        return;
    }
    if (transitionRequest().state != kTransitionNone) return;
    leaf(kFirstLeaf).state = uint8_t(DoorLeafState::Free);
    if (pair) leaf(kSecondLeaf).state = uint8_t(DoorLeafState::Free);
    clearStoryFlag(kFlagTransitionArmed);
    segmentSetDoorsHidden(scriptState().part<DoorScriptBytes>().doorSegment, false);
    scriptReplayPadOff();
    scriptEndBlock();
}
// exe 0x42b430 (kind 2): the leaves stop, the handshake, then the same end
void slidingPairEnd() {
    ScriptState& s = scriptState();
    switch (LeavesEndSub(s.subState)) {
    case LeavesEndSub::StopLeaves:
        replayPad().held = 0;
        slidingLeavesTo(SlidingLeafSub::Stop, kLeavesStopSound);
        ++s.subState;
        [[fallthrough]];
    case LeavesEndSub::BeforeSwitch:
        if (answerRequest()) ++s.subState;
        [[fallthrough]];
    case LeavesEndSub::AfterSwitch:
        if (transitionRequest().state != kTransitionNone) return;
        leaf(kFirstLeaf).state = uint8_t(DoorLeafState::Free);
        leaf(kSecondLeaf).state = uint8_t(DoorLeafState::Free);
        clearStoryFlag(kFlagTransitionArmed);
        segmentSetDoorsHidden(scriptState().part<DoorScriptBytes>().doorSegment, false);
        scriptReplayPadOff();
        scriptEndBlock();
        return;
    default: return;
    }
}

// ---- kind 4 (exit type 6): exe 0x42b930, no camera script, one endless step, the player frozen until the load -------
void frozenTransition() {
    ScriptState& s = scriptState();
    Player& p = player();
    switch (FrozenState(s.state)) {
    case FrozenState::Start:
        actorsSweep(kSweepTransition, kSweepFree);
        windowCloseAll(WindowClose::Force);
        scriptSequenceStart(kNoCamera, stateAddress(TableId::ExitFrozenSteps));
        s.flags |= kScriptNoCameraScript | kScriptUnskippable;
        scriptReplayPadOn();
        p.clearDirectionOffsetWords();
        p.idleFaces[0] = 0;
        p.idleFaces[1] = 0;
        ++s.state;
        break;
    case FrozenState::BeforeSwitch:
        if (transitionRequest().state == kTransitionAccepted) {
            s.state = uint8_t(FrozenState::AfterSwitch);
            transitionRequest().state = kTransitionScriptDone;
        }
        break;
    case FrozenState::AfterSwitch:
        if (transitionRequest().state == kTransitionNone) {
            endTransition();
            return;
        }
        break;
    default: break;
    }
    scriptSequenceTick();
}

// ---- kind 9 (exit type 11: a ladder / hole): exe 0x42c650 -----------------------------------------------------------
// The player on the trigger (y - 6) facing its yaw, climbing up / down (+-0x37 a tick) for 0x50 ticks while above
// -0x800, the camera by the exit's view (byte 2 & 0xf), then the handshake.
void ladderTransition() {
    ScriptState& s = scriptState();
    const TransitionRequest& r = transitionRequest();
    ClimbScriptBytes& bytes = scriptState().part<ClimbScriptBytes>();
    Player& p = player();
    switch (TimedState(s.state)) {
    case TimedState::PadOn:
        scriptReplayPadOn();
        ++s.state;
        [[fallthrough]];
    case TimedState::Start: {
        if (playerToScriptIfFree() != 0) return;
        actorsSweep(kSweepTransition, kSweepFree);
        windowCloseAll(WindowClose::Force);
        scriptSequenceStart(stateAddress(TableId::ExitLadderCamera), stateAddress(TableId::ExitLadderSteps));
        static constexpr uint32_t kYaw[4] = {0x700, 0x500, 0x800, 0x800};
        static constexpr uint32_t kPitch[4] = {0xf500000, 0xb00000, 0xf800000, 0x800000};
        static constexpr uint32_t kDistance[4] = {0x1800000, 0x1800000, 0x2000000, 0x2000000};
        const uint32_t view = r.exitByte2 & 0xf;
        const uint32_t v = view < 3 ? view : 3;
        ScriptCamera& sc = scriptCamera();
        sc.point[0] = toFixed(r.trigger.x);
        sc.point[1] = toFixed(int32_t(r.trigger.y) - kLadderCameraHeight);
        sc.point[2] = toFixed(r.trigger.z);
        sc.angles[kScriptYaw] = int32_t(((uint32_t(int32_t(r.trigger.yaw)) - kYaw[v]) & kAngleMask) << 16);
        sc.angles[kScriptPitch] = int32_t(kPitch[v]);
        sc.angles[kScriptDistance] = int32_t(kDistance[v]);
        const bool descending = v == 1 || v == 3;
        bytes.climbSpeed = descending ? kLadderClimbSpeed : int16_t(-kLadderClimbSpeed);
        bytes.ticksLeft = kLadderTicks;
        p.pos.x = toFixed(r.trigger.x);
        p.pos.y = toFixed(int32_t(r.trigger.y) - kLadderGrip);
        p.pos.z = toFixed(r.trigger.z);
        p.rotation.y = r.trigger.yaw;
        p.clearDirectionOffsetWords();
        p.idleFaces[0] = 0;
        p.idleFaces[1] = 0;
        p.airborne = kAirborneHanging;
        playerScriptAnim(descending ? PlayerAnim::ClimbDown : PlayerAnim::ClimbUp, 0, true);
        ++s.state;
        break;
    }
    case TimedState::Timed:
        --bytes.ticksLeft;
        if (bytes.ticksLeft == 0) ++s.state;
        break;
    case TimedState::BeforeSwitch:
        if (answerRequest()) ++s.state;
        break;
    case TimedState::AfterSwitch:
        if (transitionRequest().state == kTransitionNone) {
            clearStoryFlag(kFlagTransitionArmed);
            playerBackToPlay();
            p.airborne = 0;
            scriptReplayPadOff();
            scriptEndBlock();
            return;
        }
        break;
    default: break;
    }
    scriptSequenceTick();
    if (p.pos.y.whole >= kLadderFloor) integrateVelocity(p, 0, bytes.climbSpeed, 0);
}

// ---- kind 8 (exit type 10: a pit), states 0x88c7f8 (exe 0x42c4d0 / 0x42c5c0 / 0x42c5f0 = kind 14's end) ------------
// A fixed camera above the trigger, the player's pending turn applied and frozen, 10 ticks, then the load.
void pitStart() {
    ScriptState& s = scriptState();
    const TransitionRequest& r = transitionRequest();
    Player& p = player();
    ScriptCamera& sc = scriptCamera();
    actorsSweep(kSweepTransition, kSweepFree);
    windowCloseAll(WindowClose::Force);
    scriptSequenceStart(stateAddress(TableId::ExitPitCamera), stateAddress(TableId::ExitPitSteps));
    s.flags |= kScriptUnskippable;
    sc.angles[kScriptYaw] = kPitYaw;
    sc.angles[kScriptPitch] = kPitPitch;
    sc.point[0] = toFixed(r.trigger.x);
    sc.point[1] = toFixed(int32_t(r.trigger.y) - kPitCameraHeight);
    sc.point[2] = toFixed(r.trigger.z);
    sc.angles[kScriptDistance] = kPitDistance;
    scriptReplayPadOn();
    p.rotation.x = int16_t(uint16_t(p.rotation.x) + uint16_t(p.directionOffset.x));
    p.rotation.y = int16_t(uint16_t(p.rotation.y) + uint16_t(p.directionOffset.y));
    p.rotation.z = int16_t(uint16_t(p.rotation.z) + uint16_t(p.directionOffset.z));
    p.clearDirectionOffsetWords();
    p.idleFaces[0] = 0;
    p.idleFaces[1] = 0;
    if (p.airborne == kAirborneHanging) p.airborne = kAirborneFalling;
    s.enterState(uint8_t(KindState::Sequence));
}
// exe 0x42c5c0: the pit's sequence for 10 ticks (the player kept from hanging), then the end state
void pitWait() {
    ScriptState& s = scriptState();
    Player& p = player();
    if (s.stepTick > kPitTicks) s.enterState(uint8_t(KindState::End));
    if (p.airborne == kAirborneHanging) p.airborne = kAirborneFalling;
    scriptSequenceTick();
}

// ---- kind 10 (exit type 12): exe 0x42c990 ---------------------------------------------------------------------------
// The player on the trigger, pushed 0x500 forward (view 0), posing, 16 ticks, then the load.
void pushedOutTransition() {
    ScriptState& s = scriptState();
    const TransitionRequest& r = transitionRequest();
    ClimbScriptBytes& bytes = scriptState().part<ClimbScriptBytes>();
    Player& p = player();
    switch (TimedState(s.state)) {
    case TimedState::PadOn:
        scriptReplayPadOn();
        ++s.state;
        [[fallthrough]];
    case TimedState::Start: {
        if (playerToScriptIfFree() != 0) return;
        actorsSweep(kSweepTransition, kSweepFree);
        windowCloseAll(WindowClose::Force);
        p.pos.x = toFixed(r.trigger.x);
        p.pos.y = toFixed(r.trigger.y);
        p.pos.z = toFixed(r.trigger.z);
        integrateVelocity(p, 0, 0, (r.exitByte2 & 0xf) != 0 ? 0 : kPushDistance);
        p.clearDirectionOffsetWords();
        p.idleFaces[0] = 0;
        p.idleFaces[1] = 0;
        playerScriptAnim(PlayerAnim::EventPose, 0, true);
        scriptSequenceStart(stateAddress(TableId::ExitPushedCamera), stateAddress(TableId::ExitPushedSteps));
        ScriptCamera& sc = scriptCamera();
        sc.point[0] = toFixed(r.trigger.x);
        sc.angles[kScriptPitch] = kPushedPitch;
        sc.point[1] = toFixed(int32_t(r.trigger.y) - kPushedCameraHeight);
        sc.point[2] = toFixed(r.trigger.z);
        sc.angles[kScriptYaw] = int32_t(((uint32_t(uint16_t(p.rotation.y)) - kPushedYawOffset) & kAngleMask) << 16);
        sc.angles[kScriptDistance] = kPushedDistance;
        bytes.ticksLeft = kPushedTicks;
        ++s.state;
        break;
    }
    case TimedState::Timed:
        --bytes.ticksLeft;
        if (bytes.ticksLeft == 0) {
            ++s.state;
            scriptSequenceTick();
            return;
        }
        break;
    case TimedState::BeforeSwitch:
        if (answerRequest()) {
            ++s.state;
            scriptSequenceTick();
            return;
        }
        break;
    case TimedState::AfterSwitch:
        if (transitionRequest().state == kTransitionNone) {
            clearStoryFlag(kFlagTransitionArmed);
            playerBackToPlay();
            scriptReplayPadOff();
            scriptEndBlock();
            return;
        }
        break;
    default: break;
    }
    scriptSequenceTick();
}

// the teleporter pad of an exit (its byte 1: the room actor of the pad, the way the player is moved to it)
enum class PadSide : uint8_t { TowardZPlus = 0, TowardXPlus = 1, TowardZMinus = 2, TowardXMinus = 3 };
// the sparks' random spread (centred: 0xc0 - 0..0x17f), speed, tail, life and colour (the RNG's low bits)
constexpr uint32_t kSparkSpreadRange = 0x180, kSparkSpreadHalf = 0xc0;
constexpr uint32_t kSparkSpeedMask = 0x1f, kSparkSpeedBase = 0x20;
constexpr uint32_t kSparkTailMask = 0xf, kSparkTailBase = 0xe0;
constexpr uint32_t kSparkLifeMask = 0xf, kSparkLifeBase = 8;
constexpr uint32_t kSparkColourBase = 0x20;
// a spark (effect type 0x1e) around the pad's point, spread across the travel axis
void teleportSpark(const TeleportScriptBytes& bytes) {
    const uint32_t address = allocActorA0(kActorStageClass);
    if (address == 0) return;
    EffectActor& spark = effectActorAt(address);
    TeleporterSparkFields& f = classState<TeleporterSparkFields>(spark);
    spark.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
    spark.flags = kActorLiveDrawn;
    spark.type = kTeleportSparkType;
    spark.variant = kTeleportSparkVariant;
    spark.setPositionWords(bytes.sparkPoint);
    // the velocity dword (+0xc..+0xf) and the tail / life dword (+0x18..+0x1b) cleared
    for (size_t i = 0; i < 3; ++i) {
        f.velocity[i] = 0;
        f.tail[i] = 0;
    }
    f.pad = 0;
    f.life = 0;
    const bool odd = (bytes.padSide & 1) != 0; // an x side (PadSide): the spread on z, the motion on x
    const size_t along = odd ? 0 : 2;          // the velocity / tail lane of the travel axis
    int16_t& spread = odd ? spark.pos.z : spark.pos.x;
    spread = int16_t(uint16_t(spread) + (kSparkSpreadHalf - rngStep() % kSparkSpreadRange));
    uint8_t speed = uint8_t((rngStep() & kSparkSpeedMask) + kSparkSpeedBase);
    uint8_t tail = uint8_t(kSparkTailBase - (rngStep() & kSparkTailMask));
    if (bytes.padSide > uint8_t(PadSide::TowardXPlus)) { // a minus side: the spread mirrored
        speed = uint8_t(-int(speed));
        tail = uint8_t(-int(tail));
    }
    f.velocity[along] = int8_t(speed);
    f.tail[along] = int8_t(tail);
    spark.pos.y = int16_t(uint16_t(spark.pos.y) + (kSparkSpreadHalf - rngStep() % kSparkSpreadRange));
    f.life = uint8_t((rngStep() & kSparkLifeMask) + kSparkLifeBase);
    f.colour[0] = uint8_t(rngStep() + kSparkColourBase);
    f.colour[1] = uint8_t(rngStep() + kSparkColourBase);
    f.colour[2] = uint8_t(rngStep() + kSparkColourBase);
}
// the bones the teleport flashes at its ticks (kNoBone: none)
constexpr int kNoBone = -1;
struct BoneFlash {
    uint32_t tick;
    int bone, secondBone;
};
constexpr BoneFlash kTeleportFlashes[] = {
    {0xb, kRightFootBone, kNoBone},           {0xc, kRightShinBone, kNoBone},
    {0xe, kRightThighBone, kLeftHandBone},    {0xf, kLeftUpperArmBone, kLeftForearmBone},
    {0x11, kLeftThighBone, kNoBone},          {0x13, kLeftShinBone, kNoBone},
    {0x16, kLeftFootBone, kNoBone},           {0x17, kRightUpperArmBone, kRightForearmBone},
    {0x18, kRightHandBone, kNoBone}};
// the bones flashed first (at kTeleportFlashStart)
constexpr int kTeleportFirstFlash[] = {kBodyBone, kHeadBone, kHipsBone};

// exe 0x42cb50: the teleporter's tick by its state: the RNG saved and reseeded, the player on the pad's trigger and
// moved toward the pad, the camera above it, the sparks and the bones' flashes for 0x38 ticks, the handshake, then (the
// room switched) the RNG back, the flashes off, the end

void teleporterTransition() {
    ScriptState& s = scriptState();
    const TransitionRequest& r = transitionRequest();
    TeleportScriptBytes& bytes = scriptState().part<TeleportScriptBytes>();
    Player& p = player();
    int sparks = 0;
    switch (TimedState(s.state)) {
    case TimedState::PadOn:
        scriptReplayPadOn();
        bytes.savedRng = uint16_t(rngStep()); // the RNG saved and reseeded while the sparks fly
        rngSetState(kTeleportRngSeed);
        s.enterState(uint8_t(TimedState::Start));
        [[fallthrough]];
    case TimedState::Start: {
        if (playerToScriptIfFree() != 0) return;
        actorsSweep(kSweepTransition, kSweepFree);
        windowCloseAll(WindowClose::Force);
        scriptSequenceStart(stateAddress(TableId::ExitTeleportCamera), stateAddress(TableId::ExitTeleportSteps));
        p.pos.x = toFixed(r.trigger.x);
        p.pos.y = toFixed(r.trigger.y);
        p.pos.z = toFixed(r.trigger.z);
        p.rotation.y = r.trigger.yaw;
        p.clearDirectionOffsetWords();
        p.idleFaces[0] = 0;
        p.idleFaces[1] = 0;
        p.airborne = 0;
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        ScriptCamera& sc = scriptCamera();
        sc.point[0] = toFixed(r.trigger.x);
        sc.point[1] = toFixed(int32_t(r.trigger.y) - kTeleportCameraHeight);
        sc.point[2] = toFixed(r.trigger.z);
        const int side = int8_t(r.exitByte1);
        sc.angles[kScriptYaw] = int32_t(((uint32_t(side) << 26) + kTeleportYawBase) & 0xfff0000u);
        sc.angles[kScriptPitch] = kTeleportPitch;
        sc.angles[kScriptDistance] = kTeleportDistance;
        RoomScriptState& room = roomScriptState();
        if (room.actors[0] != 0) actorHeaderAt(room.actors[0]).params[3] = kRoomPadActivated;
        // the pad of this exit (the room's actors 0..3: the four pads): the sparks from its point, the player moved
        // 0x80 toward it
        const PadSide sel = side == 0   ? PadSide::TowardZPlus
                            : side == 1 ? PadSide::TowardXPlus
                            : side == 2 ? PadSide::TowardZMinus
                                        : PadSide::TowardXMinus;
        bytes.padSide = uint8_t(sel);
        const uint32_t pad = room.actors[uint8_t(sel)];
        if (pad != 0) { // the pad's position words (+0x10 / +0x14: a pool-0xa0 type 0x1e effect, its s16 point)
            const EffectActor& padActor = effectActorAt(pad);
            bytes.sparkPoint.setXY(padActor.pos.x, padActor.pos.y);
            bytes.sparkPoint.setZSpare(padActor.pos.z, padActor.posSpare);
        } else { // OpenDash without the room handler's pads (the exe reads address 0x10): the trigger
            bytes.sparkPoint.x = r.trigger.x;
            bytes.sparkPoint.y = r.trigger.y;
            bytes.sparkPoint.setZSpare(r.trigger.z, 0);
        }
        switch (sel) {
        case PadSide::TowardZPlus:
            bytes.sparkPoint.z = int16_t(bytes.sparkPoint.z + kSparkOffset);
            p.pos.z.whole = int16_t(p.pos.z.whole + kTeleportStep);
            sc.point[2] = p.pos.z;
            break;
        case PadSide::TowardXPlus:
            bytes.sparkPoint.x = int16_t(bytes.sparkPoint.x + kSparkOffset);
            p.pos.x.whole = int16_t(p.pos.x.whole + kTeleportStep);
            sc.point[0] = p.pos.x;
            break;
        case PadSide::TowardZMinus:
            bytes.sparkPoint.z = int16_t(bytes.sparkPoint.z - kSparkOffset);
            p.pos.z.whole = int16_t(p.pos.z.whole - kTeleportStep);
            sc.point[2] = p.pos.z;
            break;
        default: // PadSide::TowardXMinus (its spark point moves +x, as the exe's)
            bytes.sparkPoint.x = int16_t(bytes.sparkPoint.x + kSparkOffset);
            p.pos.x.whole = int16_t(p.pos.x.whole - kTeleportStep);
            sc.point[0] = p.pos.x;
            break;
        }
        bytes.sparkPoint.y = int16_t(bytes.sparkPoint.y + kSparkHeight);
        soundPlayFlatForced(kTeleportSound);
        bytes.ticks = 0;
        s.enterState(uint8_t(TimedState::Timed));
        break;
    }
    case TimedState::Timed: {
        // 0x38 ticks of sparks (10 on the first, then 2 a tick) while the player's bones flash
        sparks = kSparksPerTick;
        const uint32_t t = bytes.ticks;
        if (t == 0) {
            playerScriptAnim(PlayerAnim::Walk, 0, true);
            sparks = kFirstSparks;
        } else if (t == kTeleportFlashStart) {
            for (const int bone : kTeleportFirstFlash) p.bonePoints[size_t(bone)].colour = kBoneFlash;
        } else if (t == kTeleportEnd) {
            s.enterState(uint8_t(TimedState::BeforeSwitch));
        } else {
            for (const BoneFlash& f : kTeleportFlashes)
                if (f.tick == t) {
                    p.bonePoints[size_t(f.bone)].colour = kBoneFlash;
                    if (f.secondBone != kNoBone) p.bonePoints[size_t(f.secondBone)].colour = kBoneFlash;
                }
        }
        bytes.ticks = t + 1;
        break;
    }
    case TimedState::BeforeSwitch:
        sparks = kSparksWaiting;
        if (answerRequest()) s.enterState(uint8_t(TimedState::AfterSwitch));
        break;
    case TimedState::AfterSwitch:
        if (transitionRequest().state != kTransitionNone) break;
        rngSetState(rngNext(bytes.savedRng));
        clearStoryFlag(kFlagTransitionArmed);
        for (size_t i = 0; i < kPlayerBones; ++i) p.bonePoints[i].colour = 0;
        playerBackToPlay();
        scriptReplayPadOff();
        scriptEndBlock();
        return;
    default: break;
    }
    scriptSequenceTick();
    if (s.state >= uint8_t(TimedState::Timed)) integrateVelocity(p, 0, 0, kTeleportWalk);
    for (; sparks > 0; --sparks) teleportSpark(bytes);
}

// ---- the dispatch ---------------------------------------------------------------------------------------------------
// the kind's state (+4) by its state table (its named routines)
template <size_t N> void runKindState(const PlayerEnv& env, const DataTable<RoutineTable<N>>& states) {
    const uint8_t state = scriptState().state;
    if (state >= N) notPortedAt(kExitArea, "transition script state", state);
    const RoutineRef& routine = states[state];
    switch (routine.id()) {
    case RoutineId::PassageStart: passageStart(env); break;
    case RoutineId::TransitionSequence: sequenceState(env); break;
    case RoutineId::PassageEnd: passageEnd(); break;
    case RoutineId::WalkOffStart: walkOffStart(env); break;
    case RoutineId::WalkOffEnd: walkOffEnd(); break;
    case RoutineId::DoorActorStart: doorActorStart(); break;
    case RoutineId::ScriptSequenceState: doorActorSequence(env); break;
    case RoutineId::TransitionHandshakeEnd: handshakeEnd(); break;
    case RoutineId::HingedDoorStart: hingedDoorStart(env); break;
    case RoutineId::HingedDoorEnd: hingedDoorEnd(false); break;
    case RoutineId::DoubleDoorStart: doorPairStart(env, kDoubleDoor); break;
    case RoutineId::DoubleDoorEnd: hingedDoorEnd(true); break;
    case RoutineId::SlidingPairStart: doorPairStart(env, kSlidingPair); break;
    case RoutineId::SlidingPairEnd: slidingPairEnd(); break;
    case RoutineId::PitStart: pitStart(); break;
    case RoutineId::PitWait: pitWait(); break;
    default: notPortedAt(kExitArea, "transition script state function", routine.pointerValue());
    }
}

} // namespace

void exitTransitionTick(const PlayerEnv& env) {
    const uint8_t kind = transitionRequest().kind;
    const RoutineRef handler = stateRoutine(dataScalar<uint32_t>(stage().transitionHandlers.value() + uint32_t(kind) * 4));
    switch (handler.id()) {
    case RoutineId::PassageTransition: runKindState(env, passageStates); break;     // TransitionKind::Passage
    case RoutineId::WalkOffTransition: runKindState(env, walkOffStates); break;     // TransitionKind::WalkOff
    case RoutineId::DoorActorTransition: runKindState(env, doorActorStates); break;   // TransitionKind::DoorActor
    case RoutineId::HingedDoorTransition: runKindState(env, hingedStates); break;      // TransitionKind::HingedDoor
    case RoutineId::DoubleDoorTransition: runKindState(env, doubleStates); break;      // TransitionKind::DoubleDoor
    case RoutineId::SlidingPairTransition: runKindState(env, slidingPairStates); break; // TransitionKind::SlidingPair
    case RoutineId::FrozenTransition: frozenTransition(); break;                   // TransitionKind::Frozen
    case RoutineId::LadderTransition: ladderTransition(); break;                   // TransitionKind::Ladder
    case RoutineId::PitTransition: runKindState(env, pitStates); break;         // TransitionKind::Pit
    case RoutineId::PushedOutTransition: pushedOutTransition(); break;                // TransitionKind::PushedOut
    case RoutineId::TeleporterTransition: teleporterTransition(); break;               // TransitionKind::Teleporter
    default: notPortedAt(kExitArea, "transition kind handler", handler.pointerValue());
    }
}

} // namespace od::port
