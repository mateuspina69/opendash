// The attract intro's script 4 (ST03, rooms 0 .. 4; see attract.hpp): the airship's story told over its rooms, its
// cast keyed on the script's step sub and tick (content/intro/st03_attract_cast).
//   state 0 (exe 0x513760): the camera script and its steps, the player hidden, the script's bytes, state 1
//   state 1 (exe 0x5137b0): the script's own step runner (chapter 1 also runs the engine's loop sound, exe 0x5150f0)
//   state 2 (exe 0x513810): the actors swept, the sounds faded, then the main task back to the title
// Steps (exe 0x89dd80; chapter:step):
//   0:0 0x5138d0 the engine sound, to room 1          0:1 0x5139b0 ST0301.DAT, voice clip 9    0:2 0x513a70 the room's
//   texture flips                                     0:6 0x513ae0 to room 2
//   1:0 .. 1:16 the chapter in rooms 2 .. 4: the scenery kinds, the timed rings, the files ST0302 .. ST0305, the voice
//   clips 0xb .. 0x10, the sparkles and the chips, the loop sound's dips
//   2:0 0x514bb0 the other bank, the texture flip 2   2:1 0x514bf0 to room 0              2:2 0x514c60 the fade out
#include "content/intro/st03_attract_script_state.hpp"
#include "content/intro/attract.hpp"

#include "content/intro/attract_effects.hpp"
#include "content/intro/opening_effects.hpp"
#include "content/intro/st03_attract_cast.hpp"
#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/display.hpp"
#include "game/fade.hpp"
#include "game/fade_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_uv_animation.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/stage_files.hpp"
#include "game/story.hpp"
#include "game/transition.hpp"
#include "game/dat_names.hpp"

using namespace od::port::st03_attract_script_state;

namespace od::port {

namespace {

// the states (exe table 0x89dea0: a routine per state; the named routines of engine/routines.csv)
const DataTable<RoutineTable<3>> script4States{TableId::St03AttractScriptScript4States};
constexpr uint8_t kST03 = 3;

// the running script's bytes as script 4's
Script4Bytes& script4Bytes() { return scriptState().part<Script4Bytes>(); }
// the loop sound's request (exe 0x5150f0, chapter 1)
enum class LoopRequest : uint8_t { None = 0, Start = 1, Keep = 2, Dip = 3, Restore = 4 };

constexpr uint8_t kFadeIn = kFadeInRequest | 2, kFadeOut = kFadeOutRequest | 2;
constexpr uint32_t kVoiceFade = 8;
constexpr uint8_t kGamePace = 2, kSlowPace = 3;
// the script's DAT files (ST0301 .. ST0305.DAT) and the narration's voice clips, by the step they play in (their
// words are not transcribed)
constexpr DatFile kFileST0301{"ST0301.DAT"}, kFileST0302{"ST0302.DAT"}, kFileST0303{"ST0303.DAT"}, kFileST0304{"ST0304.DAT"}, kFileST0305{"ST0305.DAT"};
constexpr uint32_t kVoiceRoom1 = 9, kVoiceChapter1 = 0xb, kVoiceDip = 0xe, kVoiceRoom4 = 0xf, kVoiceLast = 0x10;
constexpr uint16_t kEngineSound = 0xe1, kEngineStopSound = 0xe2, kLoopSound = 0xd7, kChipsSound = 0x50;
constexpr uint32_t kLoopFade = 0x20;      // the fade entry of the loop sounds (game/audio_logic)
constexpr int16_t kLoopVolume = 0x3fff;
constexpr uint8_t kChapterWithLoop = 1;
// the sweeps of the steps: the effects of group 0 (the sparkles), every actor but them
constexpr uint32_t kSweepAllButEffects0 = kSweepActors & ~kSweepEffectsGroup0;
// the script actor whose drawing the steps of chapter 1 switch (the actor of the camera script's last record)
constexpr size_t kShownActor = 31;
constexpr size_t kMovedActor = 5;

// the script block
ScriptState& block() { return scriptState(); }
// the step's end: the next step of the list
void nextStep() { scriptSequenceSkip(1); }
// the sparkles' clock counts while the block is not paused (the exe's tail of most steps)
void sparkleClockTick() {
    if (!(block().flags & kScriptCameraPaused)) script4Bytes().sparkleClock += 1u;
}
// the step's end with its phases cleared byte by byte
void nextStepClearingPhases() {
    ScriptState& s = block();
    s.clearStepPhaseBytes();
    nextStep();
}

// ---- the spawns
// --------------------------------------------------------------------------------------------------------

// exe 0x514cc0(variant, param): the script actor of the camera script's record 0x89e018 (an effect of the room) with
// its variant and the word of its first two parameters (the pool full: nothing)
void cueOfRecord(uint8_t variant, uint16_t param) {
    if (const uint32_t at = scriptActorSpawnRecord(stateAddress(TableId::St03AttractScriptCueRecord))) {
        ActorHeader& a = actorHeaderAt(at);
        a.variant = variant;
        a.setParamsLowWord(param);
    }
}
// exe 0x514ff0(k): the script actor of record 0x89e030 + k * 0x14
constexpr uint32_t kCastRecordSize = 0x14;
// (the record k of the cast's table)
void castOfRecord(uint32_t k) { scriptActorSpawnRecord(stateAddress(TableId::St03AttractScriptCastRecords) + k * kCastRecordSize); }

// n pieces of scenery (static type 0x16, sweep group 2) of `kind` (exe 0x59b690 reads its place from the kind's record
// `index`), yawed by `yaw`; their pose cleared (stops when the pool is full)
void sceneryOfKind(int n, uint8_t kind, uint8_t yaw) {
    for (int i = 0; i < n; ++i) {
        const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (slot == 0) return;
        StaticActor& a = staticActorAt(slot);
        AttractSceneryFields f(a);
        a.setParamsWord(0);
        a.flags = kActorLiveDrawn;
        a.loopFlags |= kActorSweepGroup2;
        f.index = int8_t(i);
        a.type = kAttractSceneryType;
        a.variant = 0;
        f.kind = int8_t(kind);
        f.yaw = int8_t(yaw);
        a.prevPos.x = 0;
        a.pos.x = 0;
        a.prevPos.y = 0;
        a.pos.y = 0;
        a.prevPos.z = 0;
        a.pos.z = 0;
        a.prevRotation.x = 0;
        a.rotation.x = 0;
        a.prevRotation.y = 0;
        a.rotation.y = 0;
        a.prevRotation.z = 0;
        a.rotation.z = 0;
    }
}

// ---- the timed rings (exe 0x515010 / 0x515030): the table {s16 x, y, z, s16 code = tick << 2 | variant} (0x7fff
// ends) at the cursor; every entry whose tick is the step tick spawns a ring / flash at its point (variant 0: the
// script's ring radius)
struct RingCue : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    ShortVec3 point; // +0x0
    int16_t code = 0; // +0x6
    template <class R> static RingCue load(const R& r) {
        RingCue v;
        v.point = {r.template read<int16_t>(0x0), r.template read<int16_t>(0x2), r.template read<int16_t>(0x4)};
        v.code = r.template read<int16_t>(0x6);
        return v;
    }
};
constexpr uint32_t kRingCueSize = 8;
constexpr int16_t kRingCuesEnd = 0x7fff;
constexpr int kCueTickShift = 2;
constexpr uint8_t kCueVariantMask = 3;

// exe 0x515010(table, radius): the rings' table and the radius of its variant-0 rings
void ringsFrom(uint32_t table, uint8_t radius) {
    Script4Bytes& bytes = script4Bytes();
    bytes.ringCursor = table;
    bytes.ringRadius = radius;
}
// exe 0x515030
void ringsTick() {
    Script4Bytes& bytes = script4Bytes();
    int32_t t = block().stepTick;
    if ((int32_t(dataRecord<RingCue>(bytes.ringCursor, RingCue::kBytes).code) >> kCueTickShift) > t) return;
    for (;;) {
        const RingCue cue = dataRecord<RingCue>(bytes.ringCursor, RingCue::kBytes);
        const int16_t code = cue.code;
        if (code == kRingCuesEnd) return;
        if (t == (int32_t(code) >> kCueTickShift)) {
            if (const uint32_t at = allocActorE0()) {
                ControllerActor& c = controllerActorAt(at);
                c.type = kRingFlashType;
                c.flags |= kActorLive;
                c.variant = uint8_t(uint8_t(code) & kCueVariantMask);
                c.loopFlags |= kActorLateLoop;
                c.setRecordPointShorts(cue.point.x, cue.point.y, cue.point.z);
                classState<RingFlashFields>(c).radius =
                    RingFlashVariant(c.variant) == RingFlashVariant::Ring ? uint16_t(bytes.ringRadius) : uint16_t(0);
            }
        }
        bytes.ringCursor += kRingCueSize;
        t = block().stepTick;
        if ((int32_t(dataRecord<RingCue>(bytes.ringCursor, RingCue::kBytes).code) >> kCueTickShift) > t) return;
    }
}

// ---- the sparkles (exe 0x514cf0): n sparkles (the opening's effect class, its variant Sparkle) along the closed path
// of 4 points {s16 x, y, z, u8 colour} at the sparkles' clock, while fewer than 60 effects live
struct SparklePoint : DataRecord {
    static constexpr uint32_t kBytes = 0x7;
    int16_t x = 0; // +0x0
    int16_t y = 0; // +0x2
    int16_t z = 0; // +0x4
    uint8_t colour = 0; // +0x6
    template <class R> static SparklePoint load(const R& r) {
        SparklePoint v;
        v.x = r.template read<int16_t>(0x0);
        v.y = r.template read<int16_t>(0x2);
        v.z = r.template read<int16_t>(0x4);
        v.colour = r.template read<uint8_t>(0x6);
        return v;
    }
};
constexpr uint32_t kSparklePointSize = 8;
constexpr int kMaxLiveEffects = 0x3c;

// exe 0x514cf0(path, n)
void pathSparkles(uint32_t path, int n) {
    int live = 0;
    for (uint32_t i = 0; i < EffectPool::size(); ++i)
        if (actorSystem().effectsLive[i >> 5] & (0x80000000u >> (i & 0x1f))) ++live;
    for (int i = 0; i < n; ++i) {
        if (live >= kMaxLiveEffects) return;
        const uint32_t at = allocActorA0(kActorStageClass); // exe 0x5e13a0
        if (at == 0) continue;
        EffectActor& e = effectActorAt(at);
        SparkleFields s(e);
        e.flags = kActorLiveDrawn;
        e.type = kOpeningEffectType;
        e.loopFlags |= kActorLateLoop;
        e.variant = uint8_t(OpeningEffect::Sparkle);
        const int32_t t = script4Bytes().sparkleClock;
        const uint32_t ph = uint32_t(i * 16 + t) & 0x3f, mph = uint32_t(t - i * 16) & 0x3f;
        const SparklePoint p0 = dataRecord<SparklePoint>(path + (uint32_t(t + i) & 3) * kSparklePointSize, SparklePoint::kBytes);
        const SparklePoint p1 = dataRecord<SparklePoint>(path + (uint32_t(t + i + 1) & 3) * kSparklePointSize, SparklePoint::kBytes);
        const int32_t w = int32_t(sin64(ph)) >> 4, iw = 0x100 - w;
        e.pos.x = int16_t((p0.x * w + p1.x * iw) >> 8);
        e.pos.y = int16_t((p1.y * iw + p0.y * w) >> 8);
        e.pos.z = int16_t((p1.z * iw + p0.z * w) >> 8);
        s.spriteWord = int16_t((cos64(ph) >> 8) + 0x180);
        s.direction = int8_t((sin64(mph) >> 9) + p0.colour);
        s.spriteShade = 0;
        s.spriteSize = uint8_t((uint16_t(sin64(ph)) >> 8) + 0x40u);
        s.lifeParam = int8_t((cos64(mph) >> 10) + 0x10);
        ++live;
    }
}

// ---- the steps
// -----------------------------------------------------------------------------------------------------------

// the engine step's phases: the sound started, then the dip and the warp on their ticks
enum class EnginePhase : uint8_t { Start = 0, Run = 1 };
void engineStep() { // exe 0x5138d0 (0:0): the engine sound; 0x70 the music dipped; 0x78 to room 1
    ScriptState& s = block();
    if (EnginePhase(s.stepPhase) == EnginePhase::Start) {
        player().flags &= uint8_t(~kActorLive);
        soundPlayFlat(kEngineSound);
        script4Bytes().engineSound = 1;
        s.flags &= uint8_t(~kScriptUnskippable);
        ++s.stepPhase;
    } else if (EnginePhase(s.stepPhase) == EnginePhase::Run) {
        if (s.stepTick == 0x70) soundFadeDip(kLoopFade, 0x7ff, 0, 0);
        if (s.stepTick == 0x78) {
            attractWarp(kST03, 1);
            s.clearStepPhaseBytes();
            s.flags |= kScriptUnskippable;
            nextStep();
        }
    }
}

// the file steps' phases: a phase that waits ticks the sparkles' clock and stays; the others move on after their work
// and tick the clock too
enum class FirstFilePhase : uint8_t { StopEngine = 0, LoadFile = 1, WaitFile = 2, WaitStream = 3, WaitVoice = 4 };
void firstFileStep() { // exe 0x5139b0 (0:1): ST0301.DAT (file 0x4d), voice clip 9 after 300 ticks
    ScriptState& s = block();
    Script4Bytes& bytes = script4Bytes();
    switch (FirstFilePhase(s.stepPhase)) {
    case FirstFilePhase::StopEngine:
        soundPlayFlat(kEngineStopSound);
        bytes.engineSound = 0;
        break;
    case FirstFilePhase::LoadFile:
        datLoadRequest(kFileST0301);
        bytes.sparkleClock = 0;
        break;
    case FirstFilePhase::WaitFile:
        if (fileLoads().loading & kFileLoading) return sparkleClockTick();
        break;
    case FirstFilePhase::WaitStream:
        if ((fileLoads().loading & kStreamBusy) || bytes.sparkleClock < 300) return sparkleClockTick();
        voiceClipPlay(kVoiceRoom1);
        break;
    case FirstFilePhase::WaitVoice:
        if (voiceClipPlaying()) {
            s.flags &= uint8_t(~kScriptUnskippable);
            bytes.sparkleClock = 0;
            nextStepClearingPhases();
            return;
        }
        return sparkleClockTick();
    default: return sparkleClockTick();
    }
    ++s.stepPhase;
    sparkleClockTick();
}

// the textures step's phases: the actor moved, the room's flips 1 and 3
enum class TexturesPhase : uint8_t { MoveActor = 0, FirstFlip = 1, SecondFlip = 2 };
void texturesStep() { // exe 0x513a70 (0:2): 0xa0 the script actor 5 moved; the room's texture flips 1 / 3
    ScriptState& s = block();
    switch (TexturesPhase(s.stepPhase)) {
    case TexturesPhase::MoveActor:
        if (s.stepTick == 0xa0) {
            Actor& a = actorLayoutOf(*s.actors[kMovedActor]);
            a.pos.x.whole = int16_t(uint16_t(a.pos.x.whole) - 0x30u);
            ++s.stepPhase;
        }
        return;
    case TexturesPhase::FirstFlip:
        if (s.stepTick == 0x16d) {
            showRoomUvFirstFrame(1); // exe 0x514ea0
            ++s.stepPhase;
        }
        return;
    case TexturesPhase::SecondFlip:
        if (s.stepTick == 0x203) {
            showRoomUvFirstFrame(3);
            ++s.stepPhase;
        }
        return;
    default: return;
    }
}

// exe 0x513ae0 (0:6): past tick `after`, on a frame of buffer 0, to room `room`
void warpAfterStep(int32_t after, uint8_t room) {
    ScriptState& s = block();
    if (s.stepTick > after && display().drawBuffer == 0) {
        attractWarp(kST03, room);
        s.clearStepPhase();
        nextStep();
    }
}

// exe 0x513b50 (1:0): the other bank, the cast's records, a flash, the scenery kind 0, the rings 0x89deb0; the paces
constexpr TableId kChapterCast[7] = {TableId::St03AttractScriptChapterCastRecord0, TableId::St03AttractScriptChapterCastRecord1, TableId::St03AttractScriptChapterCastRecord2, TableId::St03AttractScriptChapterCastRecord3, TableId::St03AttractScriptChapterCastRecord4, TableId::St03AttractScriptChapterCastRecord5, TableId::St03AttractScriptChapterCastRecord6};
// (tick 0, then the paces; the rings every tick)
void chapterStep() {
    const int32_t t = block().stepTick;
    if (t == 0) {
        actorsSweep(kSweepActors, kSweepFree);
        switchStageBank();
        for (TableId r : kChapterCast) scriptActorSpawnRecord(stateAddress(r));
        cueOfRecord(1, 0xfff);
        castOfRecord(0);
        sceneryOfKind(0xf, 0, 0);
        ringsFrom(stateAddress(TableId::St03AttractScriptChapterRings), 0x40);
    } else if (t == 0x140) {
        display().pace = kSlowPace;
    } else if (t == 0x1d6) {
        display().pace = kGamePace;
    }
    ringsTick();
}

// exe 0x513ca0 (1:1): the scenery kind 1, the cast 0x89ce90 / 0x89cea4, ST0302.DAT (file 0x4e), voice clip 0xb
enum class SecondFilePhase : uint8_t { Start = 0, Cast = 1, LoadFile = 2, WaitFile = 3, WaitStream = 4, WaitVoice = 5 };
// (its phases: SecondFilePhase)
void secondFileStep() {
    ScriptState& s = block();
    Script4Bytes& bytes = script4Bytes();
    switch (SecondFilePhase(s.stepPhase)) {
    case SecondFilePhase::Start:
        actorsSweep(kSweepStaticsGroup2, kSweepFree);
        sceneryOfKind(4, 1, 0x10);
        scriptActorSpawnRecord(stateAddress(TableId::St03AttractScriptSecondFileFirstCastRecord));
        bytes.sparkleClock = 0;
        break;
    case SecondFilePhase::Cast:
        if (s.stepTick != 0x5a) return sparkleClockTick();
        scriptActorSpawnRecord(stateAddress(TableId::St03AttractScriptSecondFileSecondCastRecord));
        voiceClipFadeOut(kVoiceFade);
        s.flags |= kScriptUnskippable;
        break;
    case SecondFilePhase::LoadFile:
        if (!voiceClipIdle()) return sparkleClockTick();
        datLoadRequest(kFileST0302);
        break;
    case SecondFilePhase::WaitFile:
        if (fileLoads().loading & kFileLoading) return sparkleClockTick();
        break;
    case SecondFilePhase::WaitStream:
        if ((fileLoads().loading & kStreamBusy) || display().drawBuffer != 0 || bytes.sparkleClock < 0x96)
            return sparkleClockTick();
        voiceClipPlay(kVoiceChapter1);
        break;
    case SecondFilePhase::WaitVoice:
        if (voiceClipPlaying()) {
            s.flags &= uint8_t(~kScriptUnskippable);
            bytes.sparkleClock = 0;
            nextStepClearingPhases();
            return;
        }
        return sparkleClockTick();
    default: return sparkleClockTick();
    }
    ++s.stepPhase;
    sparkleClockTick();
}

// the scenery step's phases: kind 2, kind 3 at 0xbe, the shown actor at 0x280, kind 4 at 0x2a8
enum class SceneryKindsPhase : uint8_t { Kind2 = 0, Kind3 = 1, Show = 2, Kind4 = 3 };
// exe 0x513e10 (1:2): the scenery kinds 2 / 3 / 4, the rings 0x89df28, the shown actor hidden / shown
void sceneryKindsStep() {
    ScriptState& s = block();
    const uint32_t shown = slotIdentityWord(s.actors[kShownActor]);
    switch (SceneryKindsPhase(s.stepPhase)) {
    case SceneryKindsPhase::Kind2:
        actorsSweep(kSweepStaticsGroup2, kSweepFree);
        scriptActorFreeRecord(stateAddress(TableId::St03AttractScriptSecondFileSecondCastRecord)); // exe 0x511760
        sceneryOfKind(0xc, 2, 0xe9);
        actorHeaderAt(shown).setDrawn(false);
        ringsFrom(stateAddress(TableId::St03AttractScriptSceneryKindsRings), 0x80);
        s.flags &= uint8_t(~kScriptUnskippable);
        ++s.stepPhase;
        break;
    case SceneryKindsPhase::Kind3:
        if (s.stepTick == 0xbe) {
            actorsSweep(kSweepStaticsGroup2, kSweepFree);
            sceneryOfKind(0xf, 3, 0xf8);
            ++s.stepPhase;
        }
        break;
    case SceneryKindsPhase::Show:
        if (s.stepTick == 0x280) {
            actorHeaderAt(shown).setDrawn(true);
            ++s.stepPhase;
        }
        break;
    case SceneryKindsPhase::Kind4:
        if (s.stepTick == 0x2a8) {
            actorsSweep(kSweepStaticsGroup2, kSweepFree);
            sceneryOfKind(9, 4, 0xf8);
            actorHeaderAt(shown).setDrawn(false);
            ++s.stepPhase;
        }
        break;
    default: break;
    }
    ringsTick();
}

void showActorStep() { // exe 0x514060 (1:3)
    ScriptState& s = block();
    if (s.stepTick == 0) (*s.actors[kShownActor]).setDrawn(true);
    if (s.stepTick > 399 && display().drawBuffer == 0) {
        s.clearStepPhase();
        nextStep();
    }
}

// the slow step's phases: the bank, the scenery and the slow pace; the pace back at 0x14; done
enum class SlowSceneryPhase : uint8_t { Start = 0, SlowPace = 1, Done = 2 };
void slowSceneryStep() { // exe 0x5140a0 (1:4): the other bank, a flash, the scenery kind 5, the slow pace for 0x14 ticks
    ScriptState& s = block();
    if (SlowSceneryPhase(s.stepPhase) == SlowSceneryPhase::Start) {
        actorsSweep(kSweepActors, kSweepFree);
        switchStageBank();
        cueOfRecord(1, 0xfff);
        castOfRecord(0);
        sceneryOfKind(10, 5, 10);
        display().pace = kSlowPace;
        ++s.stepPhase;
    } else if (SlowSceneryPhase(s.stepPhase) == SlowSceneryPhase::SlowPace && s.stepTick == 0x14) {
        display().pace = kGamePace;
        (*s.actors[kShownActor]).setDrawn(false);
        ++s.stepPhase;
    }
}

void sweepShowStep() { // exe 0x514190 (1:5)
    ScriptState& s = block();
    if (s.stepTick == 0) {
        actorsSweep(kSweepStaticsGroup2, kSweepFree);
        s.clearStepPhase();
    } else if (s.stepTick == 0x3c) {
        (*s.actors[kShownActor]).setDrawn(true);
    }
}

// exe 0x5141d0 (1:7): 0x28a a flash (0x962); 0x294 the walls' cells 0x46 / 0x48 switched; 0x29e back, the shake, the
// loop sound, 20 chips (the opening's effect class, its variant Chip) at the points 0x89df78, then 8 sparkles a tick
constexpr uint32_t kChipCount = 20;
constexpr uint32_t kChipWallRow = 0x42, kChipWallA = 0x46, kChipWallB = 0x48;
constexpr uint8_t kChipLife = 0x10;
// (the walls, the chips, the sparkles)
void chipsStep() {
    ScriptState& s = block();
    Script4Bytes& bytes = script4Bytes();
    const int32_t t = s.stepTick;
    if (t < 0x295) {
        if (t == 0x294) {
            switchGridCellVariant(kChipWallA, kChipWallRow, 1);
            switchGridCellVariant(kChipWallB, kChipWallRow, 1);
        } else if (t == 0) {
            bytes.sparkleClock = 0;
        } else if (t == 0x28a) {
            actorsSweep(kSweepEffectsGroup0, kSweepFree);
            cueOfRecord(1, 0x962);
        }
    } else if (t == 0x29e) {
        bytes.loop = uint8_t(LoopRequest::Start);
        cameraShake(false, 0x800, 0x100);
        switchGridCellVariant(kChipWallA, kChipWallRow, 0);
        switchGridCellVariant(kChipWallB, kChipWallRow, 0);
        const DataArray<ShortVec4> points(stateAddress(TableId::St03AttractScriptChipPoints));
        for (uint32_t i = 0; i < kChipCount; ++i) {
            const uint32_t at = allocActorA0(kActorStageClass);
            if (at == 0) continue;
            EffectActor& e = effectActorAt(at);
            ChipFields& c = classState<ChipFields>(e);
            e.flags = kActorLiveDrawn;
            e.type = kOpeningEffectType;
            e.loopFlags |= kActorLateLoop;
            e.variant = uint8_t(OpeningEffect::Chip);
            const uint32_t k = (i & 3) * 0x10; // the 64-step angle
            e.setPositionWords(points[i]);
            c.direction = uint8_t((sin64(k) >> 10) + 0x4a);
            c.reachParam = int8_t(0x28u - (uint16_t(cos64(k)) >> 8));
            c.lifeParam = int8_t(kChipLife);
            c.liftParam = int8_t((uint16_t(cos64(k)) >> 8) + 0x1eu);
        }
    }
    if (s.stepTick > 0x29e) {
        pathSparkles(stateAddress(TableId::St03AttractScriptSparklePathA), 8);
        bytes.sparkleClock += 1u;
    }
}

void sparklesStep() { // exe 0x5143e0 (1:8): a flash, sparkles every 4 ticks
    Script4Bytes& bytes = script4Bytes();
    if (block().stepTick == 0) {
        actorsSweep(kSweepEffectsGroup0, kSweepFree);
        cueOfRecord(1, 0xfff);
        bytes.sparkleClock = 0;
    }
    if (!(uint8_t(bytes.sparkleClock) & 3)) pathSparkles(stateAddress(TableId::St03AttractScriptSparklePathB), 4);
    bytes.sparkleClock += 1u;
}

// exe 0x514430 (1:9): a flash (0x4d6), the cast record 0x89cf20 out / 0x89cffc in, ST0303.DAT (file 0x4f), to room 3
enum class ThirdFilePhase : uint8_t { Cast = 0, LoadFile = 1, WaitFile = 2, WaitStream = 3 };
// (its phases: ThirdFilePhase; the sparkles every other tick past tick 4)
void thirdFileStep() {
    ScriptState& s = block();
    Script4Bytes& bytes = script4Bytes();
    switch (ThirdFilePhase(s.stepPhase)) {
    case ThirdFilePhase::Cast:
        if (s.stepTick == 5) {
            actorsSweep(kSweepEffectsGroup0, kSweepFree);
            cueOfRecord(1, 0x4d6);
            bytes.sparkleClock = 0;
        }
        if (s.stepTick != 0x5a) break;
        scriptActorFreeRecord(stateAddress(TableId::St03AttractScriptThirdFileFreedCastRecord));
        scriptActorSpawnRecord(stateAddress(TableId::St03AttractScriptThirdFileCastRecord));
        soundPlayFlatForced(kChipsSound);
        voiceClipFadeOut(kVoiceFade);
        s.flags |= kScriptUnskippable;
        ++s.stepPhase;
        break;
    case ThirdFilePhase::LoadFile:
        if (!voiceClipIdle()) break;
        datLoadRequest(kFileST0303);
        ++s.stepPhase;
        break;
    case ThirdFilePhase::WaitFile:
        if (!(fileLoads().loading & kFileLoading)) {
            s.flags &= uint8_t(~kScriptUnskippable);
            ++s.stepPhase;
        }
        break;
    case ThirdFilePhase::WaitStream:
        if (!(fileLoads().loading & kStreamBusy) && s.stepTick > 0x95) {
            attractWarp(kST03, 3);
            s.flags &= uint8_t(~kScriptUnskippable);
            nextStepClearingPhases();
            return;
        }
        break;
    default: break;
    }
    if (s.stepTick > 4) {
        if (!(uint8_t(bytes.sparkleClock) & 1)) pathSparkles(stateAddress(TableId::St03AttractScriptSparklePathA), 4);
        bytes.sparkleClock += 1u;
    }
}

// the dip step's phases: the ring and the dip; the bank and the voice; the voice waited
enum class DipVoicePhase : uint8_t { Start = 0, Switch = 1, WaitVoice = 2 };
// exe 0x514580 (1:10): a ring, the cast record, the loop sound dipped; on a frame of buffer 1 the other bank, voice 0xe
void dipVoiceStep() {
    ScriptState& s = block();
    Script4Bytes& bytes = script4Bytes();
    switch (DipVoicePhase(s.stepPhase)) {
    case DipVoicePhase::Start:
        actorsSweep(kSweepEffectsGroup0, kSweepFree);
        cueOfRecord(0, 1);
        castOfRecord(0);
        bytes.loop = uint8_t(LoopRequest::Dip);
        bytes.setLoopDipBytes(0, 0x28);
        ++s.stepPhase;
        return;
    case DipVoicePhase::Switch:
        if (s.stepTick > 0x27 && display().drawBuffer == 1) {
            actorsSweep(kSweepAllButEffects0, kSweepFree);
            switchStageBank();
            voiceClipPlay(kVoiceDip);
            s.flags |= kScriptUnskippable;
            ++s.stepPhase;
        }
        return;
    case DipVoicePhase::WaitVoice:
        if (voiceClipPlaying()) {
            s.clearStepPhaseBytes();
            s.flags &= uint8_t(~kScriptUnskippable);
            nextStep();
        }
        return;
    default: return;
    }
}

// exe 0x5147e0 (1:13) / 0x514bf0 (2:1): at tick `at`, to room `room`
void warpAtStep(int32_t at, uint8_t room) {
    ScriptState& s = block();
    if (s.stepTick == at) {
        attractWarp(kST03, room);
        s.clearStepPhaseBytes();
        s.flags &= uint8_t(~kScriptUnskippable);
        nextStep();
    }
}

// the ring step's phases: the ring; the warp at tick 300
enum class RingWarpPhase : uint8_t { Start = 0, Wait = 1 };
void ringWarpStep() { // exe 0x514640 (1:11): a ring; 300: to room 4
    ScriptState& s = block();
    if (RingWarpPhase(s.stepPhase) == RingWarpPhase::Start) {
        actorsSweep(kSweepEffectsGroup0, kSweepFree);
        cueOfRecord(0, 1);
        ++s.stepPhase;
    } else if (RingWarpPhase(s.stepPhase) == RingWarpPhase::Wait && s.stepTick == 300) {
        attractWarp(kST03, 4);
        s.clearStepPhase();
        nextStep();
    }
}

// exe 0x5146f0 (1:12): ST0304.DAT (file 0x50), voice clip 0xf
enum class FourthFilePhase : uint8_t { Start = 0, LoadFile = 1, WaitFile = 2, WaitStream = 3, WaitVoice = 4 };
// (its phases: FourthFilePhase)
void fourthFileStep() {
    ScriptState& s = block();
    switch (FourthFilePhase(s.stepPhase)) {
    case FourthFilePhase::Start:
        actorsSweep(kSweepEffectsGroup0, kSweepFree);
        cueOfRecord(1, 1);
        castOfRecord(0);
        voiceClipFadeOut(kVoiceFade);
        s.flags |= kScriptUnskippable;
        ++s.stepPhase;
        return;
    case FourthFilePhase::LoadFile:
        if (voiceClipIdle()) {
            datLoadRequest(kFileST0304);
            ++s.stepPhase;
        }
        return;
    case FourthFilePhase::WaitFile:
        if (!(fileLoads().loading & kFileLoading)) ++s.stepPhase;
        return;
    case FourthFilePhase::WaitStream:
        if (!(fileLoads().loading & kStreamBusy) && s.stepTick > 0x95) {
            voiceClipPlay(kVoiceRoom4);
            ++s.stepPhase;
        }
        return;
    case FourthFilePhase::WaitVoice:
        if (voiceClipPlaying()) {
            s.clearStepPhaseBytes();
            s.flags &= uint8_t(~kScriptUnskippable);
            nextStep();
        }
        return;
    default: return;
    }
}

// the loop steps' phases: the flash and the loop's request; the warp at the tick
enum class LoopWarpPhase : uint8_t { Start = 0, Wait = 1 };
// exe 0x514850 (1:14) / 0x514920 (1:15): a flash, the cast record, the loop sound's request; at tick `at` to room `room`
void loopWarpStep(LoopRequest loop, bool dips, int32_t at, uint8_t room) {
    ScriptState& s = block();
    if (LoopWarpPhase(s.stepPhase) == LoopWarpPhase::Start) {
        Script4Bytes& bytes = script4Bytes();
        actorsSweep(kSweepEffectsGroup0, kSweepFree);
        cueOfRecord(1, dips ? 0xfff : 1);
        castOfRecord(0);
        bytes.loop = uint8_t(loop);
        if (dips) bytes.setLoopDipBytes(0xff, 0x1f);
        ++s.stepPhase;
    } else if (LoopWarpPhase(s.stepPhase) == LoopWarpPhase::Wait && s.stepTick == at) {
        attractWarp(kST03, room);
        s.flags &= uint8_t(~kScriptUnskippable);
        s.clearStepPhaseBytes();
        nextStep();
    }
}

// exe 0x5149e0 (1:16): the fades, ST0305.DAT (file 0x51), voice clip 0x10, to room 1
enum class LastFilePhase : uint8_t { Start = 0, Fade = 1, LoadFile = 2, WaitFile = 3, WaitStream = 4, WaitVoice = 5 };
// (its phases: LastFilePhase)
void lastFileStep() {
    ScriptState& s = block();
    switch (LastFilePhase(s.stepPhase)) {
    case LastFilePhase::Start:
        actorsSweep(kSweepEffectsGroup0, kSweepFree);
        cueOfRecord(0, 0xfff);
        castOfRecord(0);
        script4Bytes().loop = uint8_t(LoopRequest::Dip);
        ++s.stepPhase;
        return;
    case LastFilePhase::Fade:
        if (s.stepTick == 0xe6) {
            voiceClipFadeOut(kVoiceFade);
            soundFadeOut(kMusicFades, 0x1ff, 0);
            soundFadeDip(kLoopFade, 0x1ff, 0, 0);
            s.flags |= kScriptUnskippable;
            ++s.stepPhase;
        }
        return;
    case LastFilePhase::LoadFile:
        if (voiceClipIdle() && soundDone(kMusicFades)) {
            datLoadRequest(kFileST0305);
            ++s.stepPhase;
        }
        return;
    case LastFilePhase::WaitFile:
        if (!(fileLoads().loading & kFileLoading)) {
            s.flags &= uint8_t(~kScriptUnskippable);
            ++s.stepPhase;
        }
        return;
    case LastFilePhase::WaitStream:
        if (!(fileLoads().loading & kStreamBusy) && s.stepTick > 0x105 && display().drawBuffer == 0) {
            voiceClipPlay(kVoiceLast);
            s.flags |= kScriptUnskippable;
            ++s.stepPhase;
        }
        return;
    case LastFilePhase::WaitVoice:
        if (voiceClipPlaying() && display().drawBuffer == 0) {
            attractWarp(kST03, 1);
            s.clearStepPhaseBytes();
            s.flags &= uint8_t(~kScriptUnskippable);
            nextStep();
        }
        return;
    default: return;
    }
}

// the last bank step's phases: the bank and the flip; done
enum class LastBankPhase : uint8_t { Start = 0, Done = 1 };
void lastBankStep() { // exe 0x514bb0 (2:0): the other bank, the room's texture flip 2
    ScriptState& s = block();
    if (LastBankPhase(s.stepPhase) == LastBankPhase::Start) {
        actorsSweep(kSweepActors, kSweepFree);
        switchStageBank();
        showRoomUvFirstFrame(2);
        ++s.stepPhase;
    }
}

// the fade-out step's phases: until tick 0x82; the fade waited
enum class FadeOutPhase : uint8_t { Wait = 0, Fading = 1 };
void fadeOutStep() { // exe 0x514c60 (2:2): 0x82 the fade out, then the steps' end
    ScriptState& s = block();
    if (FadeOutPhase(s.stepPhase) == FadeOutPhase::Wait) {
        if (s.stepTick == 0x82) {
            s.stepPhase = uint8_t(FadeOutPhase::Fading);
            s.flags |= kScriptUnskippable;
            scriptFade(kFadeOut, 0);
        }
    } else if (FadeOutPhase(s.stepPhase) == FadeOutPhase::Fading && fadeState().idle()) {
        s.clearStepPhaseBytes();
        nextStep();
    }
}

// the steps' routines (by the named routine)
void script4Step(const RoutineRef& routine) {
    if (routine.null()) return; // no routine: an empty step
    switch (routine.id()) {
    case RoutineId::EmptyRoutine: return; // an empty step
    case RoutineId::AttractScript4EngineStep: engineStep(); return;
    case RoutineId::AttractScript4FirstFileStep: firstFileStep(); return;
    case RoutineId::AttractScript4TexturesStep: texturesStep(); return;
    case RoutineId::AttractScript4WarpAfterStep: warpAfterStep(0xef, 2); return;
    case RoutineId::AttractScript4ChapterStep: chapterStep(); return;
    case RoutineId::AttractScript4SecondFileStep: secondFileStep(); return;
    case RoutineId::AttractScript4SceneryKindsStep: sceneryKindsStep(); return;
    case RoutineId::AttractScript4ShowActorStep: showActorStep(); return;
    case RoutineId::AttractScript4SlowSceneryStep: slowSceneryStep(); return;
    case RoutineId::AttractScript4SweepShowStep: sweepShowStep(); return;
    case RoutineId::AttractScript4ChipsStep: chipsStep(); return;
    case RoutineId::AttractScript4SparklesStep: sparklesStep(); return;
    case RoutineId::AttractScript4ThirdFileStep: thirdFileStep(); return;
    case RoutineId::AttractScript4DipVoiceStep: dipVoiceStep(); return;
    case RoutineId::AttractScript4RingWarpStep: ringWarpStep(); return;
    case RoutineId::AttractScript4FourthFileStep: fourthFileStep(); return;
    case RoutineId::AttractScript4Step17: warpAtStep(100, 2); return;
    case RoutineId::AttractScript4Step18: loopWarpStep(LoopRequest::Dip, true, 0x8c, 4); return;
    case RoutineId::AttractScript4Step19: loopWarpStep(LoopRequest::Restore, false, 0xe6, 2); return;
    case RoutineId::AttractScript4LastFileStep: lastFileStep(); return;
    case RoutineId::AttractScript4LastBankStep: lastBankStep(); return;
    case RoutineId::AttractScript4Step22: warpAtStep(0xe6, 0); return;
    case RoutineId::AttractScript4FadeOutStep: fadeOutStep(); return;
    default: notPortedAt(kStoryPort, "script 4 step function", routine.pointerValue());
    }
}

// ---- the states
// ----------------------------------------------------------------------------------------------------------

void script4SetUp() { // exe 0x513760
    ScriptState& s = block();
    Script4Bytes& bytes = script4Bytes();
    scriptSequenceStart(stateAddress(TableId::St03AttractScriptCameraScript), scriptStepList<TableId::St03AttractScriptSteps>);
    player().setDrawn(false);
    bytes.engineSound = 0;
    bytes.loop = 0;
    bytes.loopHandle = 0;
    bytes.loopPan = 0;
    bytes.setLoopDipBytes(0xff, 0x3f);
    s.flags |= kScriptUnskippable;
    s.enterStateBytes(uint8_t(CutsceneState::Steps));
    scriptSequenceTick();
}

// exe 0x5150f0: chapter 1's loop sound 0xd7 (no position, its pan the script's byte)
void engineLoop() {
    Script4Bytes& bytes = script4Bytes();
    const uint16_t pan = uint16_t(int16_t(bytes.loopPan));
    switch (LoopRequest(bytes.loop)) {
    case LoopRequest::Start:
        bytes.loopHandle = uint8_t(soundStartLoop(kLoopSound, 0, pan));
        soundFadeRestore(kLoopFade, kLoopVolume, 0);
        if (bytes.loopHandle != 0) bytes.loop = uint8_t(LoopRequest::Keep);
        return;
    case LoopRequest::Keep: soundUpdateLoop(bytes.loopHandle, 0, pan); return;
    case LoopRequest::Dip:
        soundFadeDip(kLoopFade, kLoopVolume, bytes.loopDip, 0);
        soundUpdateLoop(bytes.loopHandle, 0, pan);
        bytes.loop = uint8_t(LoopRequest::Keep);
        return;
    case LoopRequest::Restore:
        soundFadeRestore(kLoopFade, kLoopVolume, 0);
        soundUpdateLoop(bytes.loopHandle, 0, pan);
        bytes.loop = uint8_t(LoopRequest::Keep);
        return;
    default: return;
    }
}

void script4Steps() { // exe 0x5137b0
    ScriptState& s = block();
    const ScriptStep step = scriptStepAt(s.steps.value());
    bool end = step.id == kScriptStepEnd;
    if (!end) {
        const RoutineRef routine = stateRoutine(step.routine);
        if (routine) script4Step(routine);
        if (scriptSkipRequested() != 0 && display().drawBuffer != 0) {
            scriptFade(kScriptSkipWipe, 0);
            end = true;
        }
    }
    if (end) s.enterState(CutsceneState::WrapUp);
    if (s.stepId == kChapterWithLoop) engineLoop();
    scriptSequenceTick();
}

enum class WrapUpSub : uint8_t { Stop = 0, WaitSounds = 1, ToTitle = 2 };
void script4WrapUp() { // exe 0x513810
    ScriptState& s = block();
    switch (WrapUpSub(s.subState)) {
    case WrapUpSub::Stop:
        actorsSweep(kSweepActors, kSweepFree);
        display().pace = kGamePace;
        if (script4Bytes().engineSound != 0) soundPlayFlat(kEngineStopSound);
        if (!soundDone(kMusicFades)) soundFadeOut(kMusicFades, 0x3ff, 0);
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
        ++s.subState;
        [[fallthrough]];
    case WrapUpSub::WaitSounds:
        if (voiceClipIdle() && soundDone(kMusicFades)) ++s.subState;
        return;
    case WrapUpSub::ToTitle: attractBackToTitle(); return;
    default: return;
    }
}

} // namespace

void st03AttractScript() { // exe 0x513740
    ScriptState& s = block();
    switch (script4States->idAt(s.state)) {
    case RoutineId::St03Script4SetUpState: script4SetUp(); return;
    case RoutineId::St03Script4StepsState: script4Steps(); return;
    case RoutineId::St03Script4WrapUpState: script4WrapUp(); return;
    default: notPortedAt(kStoryPort, "script 4 state", s.state);
    }
}

} // namespace od::port
