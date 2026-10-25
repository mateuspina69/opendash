// The attract intro's script 3 (ST02, rooms 0 .. 2; see attract.hpp): the narration over the airship, its cast keyed on
// the cast's copy of the timeline (content/intro/st02_attract_cast).
//   state 0 (exe 0x512a00): the RNG seeded, the camera script and its steps, the replay pad, the player taken over
//   state 1 (exe 0x537c50): the step runner with the cast's timeline (game/script_engine scriptStepsStateForCast)
//   state 2 (exe 0x512a80): played to its end, the request to ST03 room 0 (script 4 goes on); skipped with start, the
//           main task back to the title
// Steps (exe 0x89c790; chapter:step, the step tick: ScriptState::stepTick):
//   0:0 0x512bb0 the screen covered, voice clip 0      0:1 0x512c70 the fade in       0:2 0x512c90 to room 1
//   1:6 0x512cf0 two of the cast switched              1:13 0x512d30 to room 2, ST0201.DAT, voice clip 3
//   2:0 .. 2:5 the scenery and the timed rings          2:6 0x512fb0 ST0202.DAT, voice clip 4
//   2:7 0x5130b0 the other bank, four script actors     2:9 / 2:10 the screen quads' counters (their quads: render)
//   2:11 0x513150 ST0203.DAT, voice clip 7              2:12 / 2:13 the timed rings (the VRAM copy: render)
//   2:14 0x513270 the image scroll, the walls' cells switched, the fade out
// story.md, "Attract intro").
#include "content/intro/st02_attract_script_state.hpp"
#include "content/intro/attract.hpp"

#include "content/intro/attract_effects.hpp"
#include "content/intro/st02_attract_cast.hpp"
#include "content/intro/st03_attract_cast.hpp"
#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/display.hpp"
#include "game/fade.hpp"
#include "game/fade_state.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/stage_files.hpp"
#include "game/status_screen.hpp"
#include "game/vram_moves.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::st02_attract_script_state;

namespace od::port {

namespace {

// the states (exe table 0x89ca5c: a routine per state; the named routines of engine/routines.csv)
const DataTable<RoutineTable<3>> script3States{TableId::St02AttractScriptScript3States};
constexpr uint32_t kRngSeed = 0x873ca9e6;
constexpr uint8_t kST02 = 2, kST03 = 3;

// the running script's bytes as script 3's
Script3Bytes& script3Bytes() { return scriptState().part<Script3Bytes>(); }

enum class SetUpSub : uint8_t { Start = 0, TakeOver = 1 };
enum class WrapUpSub : uint8_t { Stop = 0, WaitVoice = 1, Next = 2, WaitRequest = 3 };

constexpr uint8_t kCoverAtOnce = kFadeOutRequest;      // fade 0x10: covered
constexpr uint8_t kFadeIn = kFadeInRequest | 2, kFadeOut = kFadeOutRequest | 2;
constexpr uint32_t kVoiceFade = 8;
constexpr uint32_t kVibrationStop = 0x1000000;
constexpr uint8_t kGamePace = 2, kSlowPace = 3;
// the script's DAT files (the scenes of rooms 2 .. 2's last part: ST0201 / ST0202 / ST0203.DAT) and the narration's
// voice clips, by the scene they play over (their words are not transcribed)
constexpr uint16_t kFileST0201 = 0x48, kFileST0202 = 0x49, kFileST0203 = 0x4a;
constexpr uint32_t kVoiceOpening = 0, kVoiceRoom2 = 3, kVoiceSecondScene = 4, kVoiceThirdScene = 7;

// the script block
ScriptState& block() { return scriptState(); }
// the step's end: the next step of the list
void nextStep() { scriptSequenceSkip(1); }

// ---- the timed rings (exe 0x513610 / 0x513620): the cursor 0xa59f48 into a table of entries {s16 tick (< 0 ends),
// s16 variant, the point (x, y, z and the radius in the 4th lane)}: each entry's tick that comes spawns a ring / flash
// (controller type 0xe) at its point
struct TimedRing : DataRecord {
    static constexpr uint32_t kBytes = 12;
    int16_t tick = 0;    // +0x0
    uint8_t variant = 0; // +0x2
    ShortVec4 point;     // +0x4: the ring's point (its spare lane: the radius)
    uint16_t radius() const { return uint16_t(point.spare); }
    template <class R> static TimedRing load(const R& r) {
        return {{}, r.s16(0), r.u8(2), ShortVec4(r.s16(4), r.s16(6), r.s16(8), r.s16(0xa))};
    }
};
TimedRing timedRingAt(uint32_t at) { return dataRecord<TimedRing>(at, TimedRing::kBytes); }
constexpr uint32_t kTimedRingSize = 12;
// the timed rings' cursor (one for the program)
// exe 0x513610(table)
void timedRingsFrom(uint32_t table) { timedRings().cursor = table; }
// exe 0x513620: the entries whose tick is the step tick spawned, the cursor past every entry up to it
void timedRingsTick() {
    TimedRings& r = timedRings();
    int32_t tick = timedRingAt(r.cursor).tick;
    int32_t now = block().stepTick;
    while (tick <= now) {
        if (tick < 0) return;
        if (now == tick) {
            if (const uint32_t at = allocActorE0()) { // exe 0x5e1440
                const TimedRing e = timedRingAt(r.cursor);
                ControllerActor& c = controllerActorAt(at);
                c.flags = kActorLive;
                c.type = kRingFlashType;
                c.variant = e.variant;
                c.loopFlags |= kActorLateLoop;
                c.setRecordPointWords(e.point);
                classState<RingFlashFields>(c).radius = e.radius();
            }
        }
        r.cursor += kTimedRingSize;
        now = block().stepTick;
        tick = timedRingAt(r.cursor).tick;
    }
}

// ---- the scenery lists (exe 0x5135a0): records {s32 variant (< 0 ends), s16 x, y, z, s16 yaw}, each the scenery
// (static type 0x16, sweep group 2) at its point
struct SceneryPlacement : DataRecord {
    static constexpr uint32_t kBytes = 0xc;
    int32_t variant = 0; // +0x0: the scenery's model variant (its low byte)
    int16_t x = 0; // +0x4
    int16_t y = 0; // +0x6
    int16_t z = 0; // +0x8
    int16_t yaw = 0; // +0xa
    template <class R> static SceneryPlacement load(const R& r) {
        SceneryPlacement v;
        v.variant = r.template read<int32_t>(0x0);
        v.x = r.template read<int16_t>(0x4);
        v.y = r.template read<int16_t>(0x6);
        v.z = r.template read<int16_t>(0x8);
        v.yaw = r.template read<int16_t>(0xa);
        return v;
    }
};
constexpr uint32_t kSceneryPlacementSize = 12;

// exe 0x5135a0(list): the list's scenery placed
void placeScenery(uint32_t list) {
    for (uint32_t at = list; dataRecord<SceneryPlacement>(at, SceneryPlacement::kBytes).variant >= 0; at += kSceneryPlacementSize) {
        const SceneryPlacement p = dataRecord<SceneryPlacement>(at, SceneryPlacement::kBytes);
        const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (slot == 0) continue;
        StaticActor& a = staticActorAt(slot);
        a.flags = kActorLiveDrawn;
        a.type = kAttractSceneryType;
        a.variant = uint8_t(p.variant);
        a.loopFlags |= kActorSweepGroup2;
        a.pos.x = int32_t(uint32_t(int32_t(p.x)) << 16);
        a.pos.y = int32_t(uint32_t(int32_t(p.y)) << 16);
        a.pos.z = int32_t(uint32_t(int32_t(p.z)) << 16);
        a.rotation.x = 0;
        a.rotation.z = 0;
        a.rotation.y = p.yaw;
        a.setParamsWord(0);
    }
}

// ---- the depth of script actor 12 (exe 0x513400): the view depth / 4 of the point (0, 0, 0xce4) (0 off screen or out
// of the depth range 0x30 .. the effects' far range * 4), less 1, as the word of the actor's first two parameters (a
// static of room 2; the reader of the word is its class's draw, not traced)
constexpr size_t kDepthActor = 12;
constexpr int16_t kDepthPointZ = 0xce4;
constexpr int32_t kNearDepth = 0x30;

// exe 0x513400
void actorDepth() {
    ActorHeader& a = (*block().actors[kDepthActor]);
    if (a.flags == 0) return;
    int16_t d = 0;
    const int sh = stage().coordinateShift & 0x1f;
    const int16_t v[3] = {0, 0, int16_t(uint16_t(uint32_t(kDepthPointZ) << sh))};
    const CameraPoint c = cameraPoint(v); // exe 0x5f2f20
    if (c.view[2] <= int32_t(uint32_t(stage().effectsFar) * 4) && c.view[2] >= kNearDepth &&
        uint16_t(c.sx + 0x160) <= 0x400 && uint16_t(c.sy + 0x88) <= 0x400)
        d = int16_t(uint16_t(uint32_t(c.view[2] >> 2)));
    if (d > 0) --d;
    a.setParamsLowWord(uint16_t(d));
}

// ---- the steps
// -----------------------------------------------------------------------------------------------------------

// the cover step's phases: the fade asked with the voice clip, the fade waited, the clip waited
enum class CoverPhase : uint8_t { Cover = 0, WaitFade = 1, WaitVoice = 2 };
void coverStep() { // exe 0x512bb0 (0:0): covered, voice clip 0; its black full-screen tile is render output
    ScriptState& s = block();
    switch (CoverPhase(s.stepPhase)) {
    case CoverPhase::Cover:
        if (fadeState().idle()) {
            scriptFade(kCoverAtOnce, 0);
            voiceClipPlay(kVoiceOpening);
            ++s.stepPhase;
        }
        break;
    case CoverPhase::WaitFade:
        if (!fadeState().idle()) break;
        s.stepPhase = uint8_t(CoverPhase::WaitVoice);
        [[fallthrough]];
    case CoverPhase::WaitVoice:
        if (voiceClipPlaying()) {
            s.stepPhase = uint8_t(CoverPhase::Cover);
            s.flags &= uint8_t(~kScriptUnskippable);
            nextStep();
        }
        break;
    default: break;
    }
}

void fadeInStep() { // exe 0x512c70 (0:1)
    if (block().stepTick == 0) scriptFade(kFadeIn, 0);
}

void toRoom1Step() { // exe 0x512c90 (0:2)
    if (block().stepTick == 0x15e) {
        attractWarp(kST02, 1);
        nextStep();
    }
}

// exe 0x512cf0 (1:6): script actors 3 and 5 switched to their second variant, the first placed
constexpr size_t kSwitchedActor = 3, kSecondSwitchedActor = 5;
// (tick 0 only)
void switchCastStep() {
    ScriptState& s = block();
    if (s.stepTick != 0) return;
    Actor& a = actorLayoutOf(*s.actors[kSwitchedActor]);
    if (a.flags != 0) {
        AttractCastVariant(a).index = 1;
        a.pos.x.whole = -0x18;
        a.pos.y.whole = 0xf58;
        a.pos.z.whole = 0x170c;
    }
    ActorHeader& c = (*s.actors[kSecondSwitchedActor]);
    if (c.flags != 0) AttractCastVariant(c).index = 1;
}

// exe 0x512d30 (1:13): the fade out, to room 2, ST0201.DAT (file 0x48) into the other bank, voice clip 3
enum class Room2Phase : uint8_t { Enter = 0, Wait = 1, FadeOut = 2, LoadFile = 3, WaitFile = 4, WaitVoice = 5 };
// (its phases: Room2Phase)
void toRoom2Step() {
    ScriptState& s = block();
    switch (Room2Phase(s.stepPhase)) {
    case Room2Phase::Enter:
        ++s.stepPhase;
        [[fallthrough]];
    case Room2Phase::Wait:
        if (s.stepTick == 100) {
            scriptFade(kFadeOut, 0);
            voiceClipFadeOut(kVoiceFade);
            s.flags |= kScriptUnskippable;
            ++s.stepPhase;
        }
        return;
    case Room2Phase::FadeOut:
        if (fadeState().idle()) {
            actorsSweep(kSweepActors, kSweepFree);
            attractWarp(kST02, 2);
            ++s.stepPhase;
        }
        return;
    case Room2Phase::LoadFile:
        if (voiceClipIdle()) {
            datLoadRequest(kFileST0201);
            ++s.stepPhase;
        }
        return;
    case Room2Phase::WaitFile:
        if (!(fileLoads().loading & kFileLoading)) {
            switchStageBank(); // exe 0x5f3200
            voiceClipPlay(kVoiceRoom2);
            ++s.stepPhase;
        }
        return;
    case Room2Phase::WaitVoice:
        if (voiceClipPlaying()) {
            s.flags &= uint8_t(~kScriptUnskippable);
            s.stepPhase = uint8_t(Room2Phase::Enter);
            scriptFade(kFadeIn, 0);
            nextStep();
        }
        return;
    default: return;
    }
}

void sceneryStep() { // exe 0x512e90 (2:0)
    if (block().stepTick == 0) placeScenery(stateAddress(TableId::St02AttractScriptSceneryA));
}

void sweepSceneryStep() { // exe 0x512eb0 (2:1)
    if (block().stepTick == 0) actorsSweep(kSweepStaticsGroup2, kSweepFree);
}

// exe 0x512ed0 / 0x512f40 (2:2 / 2:4): the scenery list and the timed rings' table at tick 0, then the rings
void sceneryAndRingsStep(uint32_t list, uint32_t table) {
    if (block().stepTick == 0) {
        placeScenery(list);
        timedRingsFrom(table);
    }
    timedRingsTick();
}

// exe 0x512f00 / 0x512f70 (2:3 / 2:5): the scenery swept and the timed rings' table at tick 0, then the rings
void sweepAndRingsStep(uint32_t table) {
    if (block().stepTick == 0) {
        actorsSweep(kSweepStaticsGroup2, kSweepFree);
        timedRingsFrom(table);
    }
    timedRingsTick();
}

// exe 0x512fb0 (2:6): the scenery, the rings' table 0x89cbc8, ST0202.DAT (file 0x49), voice clip 4
enum class FilePhase : uint8_t { Start = 0, Wait = 1, LoadFile = 2, WaitFile = 3, WaitVoice = 4 };
// (its phases: FilePhase; the rings every tick)
void secondFileStep() {
    ScriptState& s = block();
    switch (FilePhase(s.stepPhase)) {
    case FilePhase::Start:
        placeScenery(stateAddress(TableId::St02AttractScriptSceneryC));
        timedRingsFrom(stateAddress(TableId::St02AttractScriptSecondFileRings));
        ++s.stepPhase;
        break;
    case FilePhase::Wait:
        if (s.stepTick == 0x28) {
            voiceClipFadeOut(kVoiceFade);
            s.flags |= kScriptUnskippable;
            ++s.stepPhase;
        }
        break;
    case FilePhase::LoadFile:
        if (voiceClipIdle()) {
            datLoadRequest(kFileST0202);
            ++s.stepPhase;
        }
        break;
    case FilePhase::WaitFile:
        if (!(fileLoads().loading & kFileLoading)) {
            voiceClipPlay(kVoiceSecondScene);
            ++s.stepPhase;
        }
        break;
    case FilePhase::WaitVoice:
        if (voiceClipPlaying()) {
            s.stepPhase = uint8_t(FilePhase::Start);
            nextStep();
        }
        break;
    default: break;
    }
    timedRingsTick();
}

// exe 0x5130b0 (2:7): the other bank, the script actors of four records
constexpr TableId kSecondCast[4] = {TableId::St02AttractScriptSecondCastRecord0, TableId::St02AttractScriptSecondCastRecord1, TableId::St02AttractScriptSecondCastRecord2, TableId::St02AttractScriptSecondCastRecord3};
// (tick 0 only)
void secondCastStep() {
    ScriptState& s = block();
    if (s.stepTick != 0) return;
    actorsSweep(kSweepStaticsGroup2, kSweepFree);
    switchStageBank();
    s.flags &= uint8_t(~kScriptUnskippable);
    for (TableId r : kSecondCast) scriptActorSpawnRecord(stateAddress(r));
}

// exe 0x513110 (2:9): at 0x7d the fading quad's 4 ticks (exe 0x513460: its counter; the quad is render output)
void fadingQuadStep() {
    Script3Bytes& bytes = script3Bytes();
    if (block().stepTick == 0x7d) bytes.fadeQuadTicks = 4;
    if (bytes.fadeQuadTicks != 0) --bytes.fadeQuadTicks;
}

// exe 0x513130 (2:10): ticks 0xd .. 0x2a the flipping quad's counter (exe 0x513500; the quad is render output)
void flippingQuadStep() {
    const int32_t t = block().stepTick;
    if (t > 0xc && t < 0x2b) ++script3Bytes().flipQuadTicks;
}

// exe 0x513150 (2:11): ST0203.DAT (file 0x4a) into the other bank, voice clip 7
enum class ThirdFilePhase : uint8_t { FadeVoice = 0, LoadFile = 1, WaitFile = 2, WaitVoice = 3 };
// (its phases: ThirdFilePhase)
void thirdFileStep() {
    ScriptState& s = block();
    switch (ThirdFilePhase(s.stepPhase)) {
    case ThirdFilePhase::FadeVoice:
        voiceClipFadeOut(kVoiceFade);
        s.flags |= kScriptUnskippable;
        ++s.stepPhase;
        [[fallthrough]];
    case ThirdFilePhase::LoadFile:
        if (voiceClipIdle()) {
            datLoadRequest(kFileST0203);
            ++s.stepPhase;
        }
        return;
    case ThirdFilePhase::WaitFile:
        if (!(fileLoads().loading & kFileLoading)) {
            switchStageBank();
            voiceClipPlay(kVoiceThirdScene);
            ++s.stepPhase;
        }
        return;
    case ThirdFilePhase::WaitVoice:
        if (voiceClipPlaying()) {
            s.stepPhase = uint8_t(ThirdFilePhase::FadeVoice);
            nextStep();
        }
        return;
    default: return;
    }
}

// The airship window's shutter image in VRAM (exe 0x89cdc0): 2:12 keeps a copy of it just below, 2:14 slides that
// copy down over the window a row a tick, so the shutters open slowly
VramRect shutterImage() { return dataRecord<VramRect>(stateAddress(TableId::St02AttractScriptShutterImage), VramRect::kBytes); }

// exe 0x5136b0: the shutter image copied just below itself (exe 0x401330, the VRAM move)
void keepShutterImage() {
    const VramRect r = shutterImage();
    vramMove(r.x, r.y, r.w, r.h, r.x, r.y + r.h);
}

// exe 0x5136e0: the scroll's tick -- the kept copy, less the rows already slid, moved down to the window's row `ticks`
void slideShutterImage(int16_t ticks) {
    const VramRect r = shutterImage();
    if (ticks > r.h) return;
    const int rows = r.h - ticks;
    vramMove(r.x, r.y + r.h, r.w, rows, r.x, r.y + r.h - rows);
}

// exe 0x513200 (2:12): the rings' table 0x89cc38 and the shutter image kept (exe 0x5136b0)
void lastRingsStep() {
    ScriptState& s = block();
    if (s.stepTick == 0) {
        timedRingsFrom(stateAddress(TableId::St02AttractScriptLastRings));
        s.flags &= uint8_t(~kScriptUnskippable);
        keepShutterImage();
    }
    timedRingsTick();
}

// exe 0x513240 (2:13)
void finalRingsStep() {
    if (block().stepTick == 0) timedRingsFrom(stateAddress(TableId::St02AttractScriptFinalRings));
    timedRingsTick();
}

// exe 0x513270 (2:14): the slow pace, the rings' table 0x89cd10, the shutters' slide (exe 0x5136e0) until 0x80, then
// the walls' cells of row 0x42 (columns 0x36 .. 0x48) to variant 1 and the scenery; at 300 the fade out
enum class ScrollPhase : uint8_t { Start = 0, Restart = 1, Scroll = 2, FadeOut = 3, End = 4 };
constexpr uint32_t kWallRow = 0x42, kFirstWallColumn = 0x36, kEndWallColumn = 0x4a;
// (its phases: ScrollPhase; then the depth of script actor 12 and the rings, every tick)
void scrollStep() {
    ScriptState& s = block();
    Script3Bytes& bytes = script3Bytes();
    switch (ScrollPhase(s.stepPhase)) {
    case ScrollPhase::Start:
        display().pace = kSlowPace;
        timedRingsFrom(stateAddress(TableId::St02AttractScriptScrollRings));
        bytes.clearScrollTicks();
        s.stepPhase = uint8_t(ScrollPhase::Scroll); // (the exe adds 2 to phase 0)
        break;
    case ScrollPhase::Restart:
        if (s.stepTick > 0) {
            bytes.clearScrollTicks();
            ++s.stepPhase;
        }
        break;
    case ScrollPhase::Scroll:
        if (bytes.scrollTicks > 0x7f) {
            for (uint32_t k = kFirstWallColumn; k < kEndWallColumn; k += 2) switchGridCellVariant(k, kWallRow, 1);
            placeScenery(stateAddress(TableId::St02AttractScriptSceneryC));
            ++s.stepPhase;
        } else {
            ++bytes.scrollTicks;
            slideShutterImage(bytes.scrollTicks);
        }
        break;
    case ScrollPhase::FadeOut:
        if (s.stepTick == 300) {
            scriptFade(kFadeOut, 0);
            s.flags |= kScriptUnskippable;
            ++s.stepPhase;
        }
        break;
    case ScrollPhase::End:
        if (fadeState().idle()) {
            s.clearStepPhaseBytes();
            nextStep();
        }
        break;
    default: break;
    }
    actorDepth();
    timedRingsTick();
}

// the steps' routines (by the named routine)
void script3Step(const RoutineRef& routine) {
    if (routine.null()) return; // no routine: an empty step
    switch (routine.id()) {
    case RoutineId::EmptyRoutine: return; // an empty step
    case RoutineId::AttractScript3CoverStep: coverStep(); return;
    case RoutineId::AttractScript3FadeInStep: fadeInStep(); return;
    case RoutineId::AttractScript3ToRoom1Step: toRoom1Step(); return;
    case RoutineId::AttractScript3SwitchCastStep: switchCastStep(); return;
    case RoutineId::AttractScript3ToRoom2Step: toRoom2Step(); return;
    case RoutineId::AttractScript3SceneryStep: sceneryStep(); return;
    case RoutineId::AttractScript3SweepSceneryStep: sweepSceneryStep(); return;
    case RoutineId::AttractScript3Step8: sceneryAndRingsStep(stateAddress(TableId::St02AttractScriptSceneryB), stateAddress(TableId::St02AttractScriptStep8Rings)); return;
    case RoutineId::AttractScript3Step9: sweepAndRingsStep(stateAddress(TableId::St02AttractScriptStep9Rings)); return;
    case RoutineId::AttractScript3Step10: sceneryAndRingsStep(stateAddress(TableId::St02AttractScriptSceneryC), stateAddress(TableId::St02AttractScriptStep10Rings)); return;
    case RoutineId::AttractScript3Step11: sweepAndRingsStep(stateAddress(TableId::St02AttractScriptStep11Rings)); return;
    case RoutineId::AttractScript3SecondFileStep: secondFileStep(); return;
    case RoutineId::AttractScript3SecondCastStep: secondCastStep(); return;
    case RoutineId::AttractScript3FadingQuadStep: fadingQuadStep(); return;
    case RoutineId::AttractScript3FlippingQuadStep: flippingQuadStep(); return;
    case RoutineId::AttractScript3ThirdFileStep: thirdFileStep(); return;
    case RoutineId::AttractScript3LastRingsStep: lastRingsStep(); return;
    case RoutineId::AttractScript3FinalRingsStep: finalRingsStep(); return;
    case RoutineId::AttractScript3ScrollStep: scrollStep(); return;
    default: notPortedAt(kStoryPort, "script 3 step function", routine.pointerValue());
    }
}

// ---- the states
// ----------------------------------------------------------------------------------------------------------

void script3SetUp() { // exe 0x512a00
    ScriptState& s = block();
    switch (SetUpSub(s.subState)) {
    case SetUpSub::Start:
        rngSetState(kRngSeed);
        scriptSequenceStart(stateAddress(TableId::St02AttractScriptCameraScript), stateAddress(TableId::St02AttractScriptSteps));
        scriptReplayPadOn(); // exe 0x5117f0
        s.flags |= kScriptUnskippable;
        ++s.subState;
        scriptSequenceTick();
        return;
    case SetUpSub::TakeOver:
        if (playerToScriptIfFree() == 0) {
            player().flags &= uint8_t(~kActorLiveDrawn);
            playerScriptAnim(PlayerAnim::Idle, 0, true);
            s.enterStateBytes(uint8_t(CutsceneState::Steps));
        }
        return;
    default: return;
    }
}

void script3WrapUp() { // exe 0x512a80
    ScriptState& s = block();
    switch (WrapUpSub(s.subState)) {
    case WrapUpSub::Stop:
        display().pace = kGamePace;
        padVibrate(0, kVibrationStop);
        actorsSweep(kSweepActors, kSweepFree);
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
        ++s.subState;
        [[fallthrough]];
    case WrapUpSub::WaitVoice:
        if (voiceClipIdle()) ++s.subState;
        return;
    case WrapUpSub::Next:
        if (!(s.flags & kScriptSkipTaken)) { // played to the end: ST03 room 0 (the attract goes on)
            requestTransition(kTransitionAccepted, kST03, 0, 0, -0x340, 0, 0, kFadeIn, kNoFade);
            ++s.subState;
            return;
        }
        if (fadeState().idle()) attractBackToTitle(); // skipped with start
        return;
    case WrapUpSub::WaitRequest:
        if (transitionRequest().state == kTransitionNone) {
            playerBackToPlay(); // exe 0x5723c0
            scriptReplayPadOff();
            player().flags |= kActorLiveDrawn;
            scriptEndBlock();
        }
        return;
    default: return;
    }
}

} // namespace

void st02AttractScript() { // exe 0x5129e0
    ScriptState& s = block();
    switch (script3States->idAt(s.state)) {
    case RoutineId::St02Script3SetUpState: script3SetUp(); return;
    case RoutineId::ScriptCastStepsState: scriptStepsStateForCast(&script3Step); return;
    case RoutineId::St02Script3WrapUpState: script3WrapUp(); return;
    default: notPortedAt(kStoryPort, "script 3 state", s.state);
    }
}

} // namespace od::port
