// ST59 / ST5A / ST5B: the time-trial course minigame:
//  * the room-script stage case 0x58ca30 (inline in the room scripts' tick, its state the room handler's step byte);
//  * the pool-e0 type 0x29 class 0x427b90: dispatch [[0x88c0a8 + kind * 4] + state * 4](actor) (the kind in the
//    sub-type byte) after the globals 0xa59de0 = the race block and 0xa59dd4 / 0xa59dd8 / 0xa59ddc = actor + 0x18.
//
// The race block (the stage's block 0xa6d298, zeroed by the stage case with exe 0x40dc40(0xa6d298, 0xa6d388)): the race
// time (16ths; +0x20 a tick, the low nibble a display jitter from 0x88c150), shown through the message word 0xa7aea4;
// the countdown actor, the speed gauge, the floor-cell watcher; the wait before the countdown, a time freeze (ticking
// sound 0x30d every 9 ticks); the checkpoint progress (-1 at the start; 4 = lap done, -2 = wrong way), the course
// (0..2); the jitter row / column.
// Best times: the game's best-time rows, three per course sorted by their time (exe 0x4287f0).
// Render output: the gauge (0x4288b0) and the countdown digits (0x428c70) are screen-space SPRT GPU packets linked
// into the OT [0xa67840] + 0x78 (packet cursor 0x6d004c) -- not draw-command records: st59HudDraw rebuilds them from
// the state the class left (od::hud::Prim, like content/calbania's escapeTimerDraw). The controller and the floor-cell
// watcher write no packets (their windows are the message system's).
#include "content/late_game/st59.hpp"
#include "content/late_game/st59_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/actors.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/hud.hpp"
#include "game/message.hpp"
#include "game/player/event_loadout.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/rewards.hpp"
#include "game/room_grid.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/spawn.hpp"
#include "game/stage_state.hpp"
#include "game/vram_moves.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/minimap.hpp"
#include "game/transition_state.hpp"

using namespace od::port::st59_state;

namespace od::port {

using namespace st59_state;

namespace {

constexpr RoutineId kRaceClass = RoutineId::RaceActorUpdate;
constexpr uint8_t kRaceActorType = 0x29;
// the class's kinds (the sub-type byte)
enum class RaceActor : uint8_t { Controller = 0, Gauge = 1, Countdown = 2, FloorWatcher = 3 };
enum class RaceActorState : uint8_t { Start = 0, Run = 1, Free = 2 };
// the controller's steps (its sub-state; table 0x88c064)
enum class RaceStep : uint8_t { Menu = 0, Countdown = 1, Race = 2, Finish = 3, Failed = 4, Record = 5, Reset = 6, Quit = 7 };
// the stage case's steps (the room handler's step byte)
enum class CourseCaseStep : uint8_t { SetUp = 0, StartRace = 1 };
constexpr int8_t kProgressStart = -1, kLapDone = 4, kWrongWay = -2;
constexpr uint32_t kTimeOver = 0x46500;
constexpr uint8_t kNoRank = 3;
// the course menu's answers and the minigame's flags
constexpr uint32_t kFlagStart = 0x680, kFlagQuit = 0x681, kFlagOtherCourse = 0x682, kFlagCourseA = 0x683,
                   kFlagCourseB = 0x684, kFlagMenuLast = 0x685;
constexpr uint32_t kFlagMinigame = 0x24d, kFlagRaceFrozen = 0x6f2, kFlagMinigameB = 0x24e;
constexpr uint32_t kCourseMessages = 0x73c000;
constexpr uint16_t kTickSound = 0x30d, kEndJingle = 0x30f, kPrizeSound = 0x30e, kCoinSound = 0x85, kRecordSound = 0xc0,
                   kCountSound = 0x309, kGoSound = 0x30a;
constexpr uint8_t kCourseFade = 0x12, kResetFadeIn = 2;

constexpr uint32_t kRaceBlock = 0xa6d298;
RaceBlock& race() { return stageBlock().part<RaceBlock>(); }
// exe 0x40dc40 over the stage block and the bytes after it, to 0xa6d38b (the stage case), by what they are: the stage
// block, the flip-book's cue byte after it, the transition request, the sound listener's position, the animated pool's
// class table, the minimap, the sound queue's flags, the music channels' playing bits (the bytes between them and
// after: never read)
void raceStageClear() {
    race(); // (the race's part made: its members in the cleared block)
    stageBlock().clearAll();
    flipBookCue() = 0;
    transitionRequest().clearBytes();
    soundListener().position = ListenerRef::none();
    stageClassTables().animated = nullptr;
    minimap() = MinimapState{};
    soundQueue().flags = 0;
    musicState().playing = 0;
}


// ---- the courses' data (exe) ----------------------------------------------------------------------------------------
// the course starts {x, y, z, yaw} (8 bytes per course)
const DataTable<RecordTable<RoomPlace, 3, 8>> courseStarts{TableId::St59CourseStarts};
RoomPlace courseStart(uint32_t course) { return courseStarts[course]; }
// a checkpoint {mode, edge} (four per course): passed when (mode 0) x > edge, (1) z > edge, (2) x < edge, (3) z < edge
struct Checkpoint : DataRecord {
    uint32_t mode = 0;
    int32_t edge = 0;
    static Checkpoint load(const TableReader& r) { return {{}, r.u32(0), r.s32(4)}; }
};
const DataTable<RecordTable<Checkpoint, 3 * 4, 8>> checkpointTable{TableId::St59Checkpoints};
Checkpoint checkpoint(uint32_t course, uint32_t k) { return checkpointTable[course * 4 + k]; }
const DataTable<ScalarTable<uint8_t, 6>> speedCellCodes{TableId::St59SpeedCellCodes}; // the speed floor's cell codes (first, last) by course
const DataTable<ScalarTable<uint32_t, 3>> coursePars{TableId::St59CoursePars};
const DataTable<ScalarTable<uint32_t, 3>> coursePrizes{TableId::St59CoursePrizes};
const DataTable<ScalarTable<uint8_t, 0x30>> timeJitter{TableId::St59TimeJitter}; // the time's low nibble by jitter row * 3 + column
const DataTable<ScalarTable<uint8_t, 3>> courseRecordCounts{TableId::St59CourseRecordCounts};
constexpr uint32_t kCourseRecordsSize = 0xa0;

void nextStep(ControllerActor& actor) { ++actor.step; }
void enterRaceStep(ControllerActor& actor, RaceStep s) {
    actor.subState = uint8_t(s);
    actor.restartStep();
}

// exe 0x428790(stage, sub, place): fade 0x12 (exe 0x53f6b0) and the request {2, stage, sub}, the destination = the
// place's two words, the fades in 2 / out none
void goStage(uint8_t stage, uint8_t sub, const RoomPlace& place) {
    TransitionRequest& r = transitionRequest();
    scriptFade(kCourseFade, 0); // the exe pushes only the type (param = a stale stack word; 0xa7226a is never read)
    r.keepPosition = 0;
    r.exitId = 0;
    r.exitByte1 = 0;
    r.destStage = stage;
    r.destRoom = sub;
    r.state = kTransitionAccepted;
    r.destination = place;
    r.fadeOut = kNoFade;
    r.fadeIn = 2;
}
// the player at the course start, still
void playerToStart(const RaceBlock& block) {
    const RoomPlace start = courseStart(block.course);
    Player& p = player();
    p.clearDirectionOffsetWords();
    p.pos.x = int32_t(uint32_t(int32_t(start.x)) << 16);
    p.pos.y = int32_t(uint32_t(int32_t(start.y)) << 16);
    p.pos.z = int32_t(uint32_t(int32_t(start.z)) << 16);
    p.rotation.y = start.yaw;
}

// exe 0x428670: the checkpoint progress: the next checkpoint passed -> + 1; then back over the last one (tested
// with the mode of the one before it) -> - 1
void checkpoints() {
    RaceBlock& block = race();
    const uint32_t course = block.course;
    const int32_t x = player().pos.x.whole, z = player().pos.z.whole;
    auto pass = [&](uint32_t mode, int32_t edge) {
        switch (mode) {
        case 0: return x > edge;
        case 1: return z > edge;
        case 2: return x < edge;
        default: return z < edge;
        }
    };
    {
        const Checkpoint next = checkpoint(course, uint8_t(block.progress) & 3u);
        const uint32_t mode = next.mode;
        if (mode <= 3 && pass(mode, next.edge)) block.progress = int8_t(uint8_t(block.progress) + 1u);
    }
    const int32_t p = block.progress;
    const Checkpoint last = checkpoint(course, uint32_t(p - 1) & 3u);
    if (pass(uint32_t(p - 2) & 3u, last.edge)) block.progress = int8_t(p - 1);
}
// exe 0x4287f0: the race time into the course's best-time rows (the rows below shifted down); the rank, 3 = none
uint8_t bestTimeInsert() {
    const RaceBlock& block = race();
    const uint32_t t = block.time;
    const uint32_t first = uint32_t(block.course) * 3;
    BestTimes& times = bestTimes();
    uint32_t i = 0;
    while (i < 3 && uint32_t(times.records[first + i].time) <= t) ++i;
    if (i == 3) return kNoRank;
    for (uint32_t r = 2; r > i; --r)
        for (uint32_t k = 0; k < 3; ++k) times.setWord((first + r) * 3 + k, times.word((first + r - 1) * 3 + k));
    times.records[first + i].time = int32_t(block.time);
    return uint8_t(i);
}
// the finish / give-up jingle: sound 0x30f, the music faded out
void raceEndSound() {
    soundPlayPanned(kEndJingle, 0, 0); // exe 0x5d5340
    soundFadeOut(0xf, 0x333, 0);       // 0x5d5780
}
// one of the class's actors of the kind (a controller slot, exe 0x5e1440); 0 when the pool is full
uint32_t spawnRaceActor(RaceActor kind) {
    const uint32_t slot = allocActorE0(); // exe 0x5e1440
    if (slot == 0) return 0;
    ControllerActor& actor = controllerActorAt(slot);
    actor.flags = kActorLive;
    actor.type = kRaceActorType;
    actor.subtype = uint8_t(kind);
    actor.variant = 0;
    actor.spawnByte = 0;
    return slot;
}

// ---- the race controller ---------------------------------------------------------------------------------------
// exe 0x427bd0
void controllerStart(ControllerActor& actor, RaceActorFields& fields) {
    if (actor.step != 0) return;
    windowCloseAll(WindowClose::Force); // exe 0x5577d0
    setStoryFlag(kFlagMinigame);
    setStoryFlag(kFlagRaceFrozen);
    setStoryFlag(kFlagMinigameB);
    fields.clearScratchBytes();
    actor.setDrawn(false);
    for (size_t i = 0; i < kHudElements; ++i) hudMemory().elements[i].flags &= uint8_t(~kHudElementDrawn);
    playerToStart(race());
    actor.enterState(uint8_t(RaceActorState::Run));
    // falls into state 1 (0x427cc0) below
}
// exe 0x427d00: the course menu (message 0 of the stage bank)
void courseMenu(ControllerActor& actor) {
    switch (actor.step) {
    case 0: {
        soundFadeOut(0xf, 0x333, 0);
        for (uint32_t f = kFlagStart; f <= kFlagMenuLast; ++f) clearStoryFlag(f);
        RaceBlock& block = race();
        block.time = 0;
        block.progress = kProgressStart;
        block.freeze = 0;
        block.tickSoundTimer = 0;
        block.jitterRow = 0;
        block.jitterColumn = 0;
        const uint32_t c = block.course;
        spawnRecords(stateAddress(TableId::St59CourseRecords) + c * kCourseRecordsSize, courseRecordCounts[c]); // exe 0x511090
        nextStep(actor);
        return;
    }
    case 1:
        if (fadeState().busy != 0) return;
        windowClose(0, WindowClose::Force);     // exe 0x5576b0
        windowOpen(0, kCourseMessages, 0);      // exe 0x557390
        nextStep(actor);
        return;
    case 2:
        if (windowStatus(0) != 0) return; // exe 0x557810
        if (storyFlag(kFlagStart)) {
            enterRaceStep(actor, RaceStep::Countdown);
            return;
        }
        if (storyFlag(kFlagOtherCourse)) {
            loadoutRestoreParts(); // exe 0x5429c0
            nextStep(actor);
            loadoutGiveTimeTrial(); // exe 0x542960
            if (storyFlag(kFlagCourseA)) goStage(0x59, 0, courseStart(0));
            else if (storyFlag(kFlagCourseB)) goStage(0x5a, 0, courseStart(1));
            else goStage(0x5b, 0, courseStart(2));
            return;
        }
        if (storyFlag(kFlagQuit)) enterRaceStep(actor, RaceStep::Quit);
        return;
    default: return;
    }
}
// exe 0x427ed0: the wait, the countdown and the floor watcher spawned, GO -> the race
void countdown(ControllerActor& actor) {
    RaceBlock& block = race();
    switch (actor.step) {
    case 0:
        block.wait = 0x1e;
        nextStep(actor);
        windowClose(1, WindowClose::Force);
        windowOpen(1, kCourseMessages, 0x14);
        messageWindow(1).timeValue = int32_t(block.time); // the time the course's message prints (16ths)
        [[fallthrough]]; // 0x427f2c
    case 1:
        block.wait = uint16_t(block.wait - 1u);
        if (block.wait == 0) nextStep(actor);
        return;
    case 2: {
        const uint32_t digits = spawnRaceActor(RaceActor::Countdown);
        pointAtWord(block.countdown, digits);
        if (digits != 0) controllerActorAt(digits).loopFlags |= kActorLateLoop;
        pointAtWord(block.watcher, spawnRaceActor(RaceActor::FloorWatcher));
        nextStep(actor);
        return;
    }
    case 3: {
        if (!block.countdown) notPorted(kStoryPort, "ST59 0x428002: no countdown actor (the exe reads [0 + 0xc])");
        if (classState<RaceActorFields>(*block.countdown).go == 0) return; // GO
        playerBackToPlay();
        scriptReplayPadOff(); // exe 0x511890
        Player& p = player();
        p.airborne = 0;
        p.flags |= kActorLiveDrawn;
        soundPlayFlatForced(7); // 0x5d5260
        clearStoryFlag(kFlagRaceFrozen);
        enterRaceStep(actor, RaceStep::Race);
        return;
    }
    default: return;
    }
}
// exe 0x428060: the time (frozen: a tick every 9 ticks), the checkpoints; the lap -> the finish, the wrong way or the
// time over -> failed
void raceTick(ControllerActor& actor) {
    RaceBlock& block = race();
    if (block.freeze != 0) {
        block.freeze = uint16_t(block.freeze - 1u);
        if (block.tickSoundTimer == 0) {
            block.tickSoundTimer = 8;
            soundPlayPanned(kTickSound, 0, 0);
        } else {
            block.tickSoundTimer = uint16_t(block.tickSoundTimer - 1u);
        }
    } else if (block.time < 0xffffff00u) {
        block.time += 0x20u;
        const uint32_t j = timeJitter[uint32_t(block.jitterRow) * 3 + block.jitterColumn];
        block.time = (block.time & 0xfffffff0u) | j;
        ++block.jitterColumn;
        if (block.jitterColumn >= 3) {
            const uint32_t r = rngStep();
            block.jitterRow = uint8_t(r & 0xf);
            block.jitterColumn = 0;
        }
    }
    if (actor.step != 0) return;
    checkpoints();
    const int8_t p = block.progress;
    if (p == kLapDone) {
        setStoryFlag(kFlagRaceFrozen);
        raceEndSound();
        if (block.freeze == 0) { // the time down to a multiple of 6 (in 16ths), the jitter kept
            const uint32_t t = block.time;
            block.time = (((t >> 4) / 6) * 0x60) | (t & 0xf);
        }
        enterRaceStep(actor, RaceStep::Finish);
        return;
    }
    if (p == kWrongWay) {
        setStoryFlag(kFlagRaceFrozen);
    } else {
        if (block.time < kTimeOver) return;
        setStoryFlag(kFlagRaceFrozen); // time over
        block.time = kTimeOver;
    }
    raceEndSound();
    enterRaceStep(actor, RaceStep::Failed);
}
// exe 0x428210: the finish message, the prize under the course's par, the best times
void finish(ControllerActor& actor, RaceActorFields& fields) {
    RaceBlock& block = race();
    switch (actor.step) {
    case 0:
        scriptReplayPadOn(); // exe 0x5117f0
        windowClose(0, WindowClose::Force);
        windowOpen(0, kCourseMessages, 0x1e);
        block.freeze = 0;
        block.tickSoundTimer = 0;
        nextStep(actor);
        return;
    case 1:
        if (playerToScriptWhenStanding() == 0) nextStep(actor);
        return;
    case 2: {
        if (windowStatus(0) != 0) return;
        const uint32_t c = block.course;
        const uint32_t par = coursePars[c];
        if (block.time <= par) { // under the course's par: the prize base + 10 per 6 units under
            const uint32_t prize = coursePrizes[c] + (((par - block.time) >> 4) / 6) * 10;
            messageWindow(0).number = int32_t(prize); // the prize the message prints
            zennyAdd(int32_t(prize)); // exe 0x5e51a0
            soundPlayPanned(kPrizeSound, 0, 0);
            soundPlayPanned(kCoinSound, 0, 0);
            windowClose(0, WindowClose::Force);
            windowOpen(0, kCourseMessages, 0x21);
        }
        nextStep(actor);
        return;
    }
    case 3: {
        if (windowStatus(0) != 0) return;
        fields.rank = bestTimeInsert(); // [0xa59ddc] + 2
        enterRaceStep(actor, fields.rank < kNoRank ? RaceStep::Record : RaceStep::Reset);
        return;
    }
    default: return;
    }
}
// exe 0x428370: time over (message 0x1f) / wrong way (0x20)
void failed(ControllerActor& actor) {
    switch (actor.step) {
    case 0: {
        scriptReplayPadOn();
        windowClose(0, WindowClose::Force);
        RaceBlock& block = race();
        if (block.time == kTimeOver) {
            windowOpen(0, kCourseMessages, 0x1f);
            nextStep(actor);
            return;
        }
        windowOpen(0, kCourseMessages, 0x20);
        if (block.freeze == 0) {
            const uint32_t r = rngStep();
            block.time = (block.time & 0xfffffff0u) | (r % 10);
        }
        nextStep(actor);
        return;
    }
    case 1:
        if (playerToScriptWhenStanding() == 0) nextStep(actor);
        return;
    case 2:
        if (windowStatus(0) != 0) return;
        enterRaceStep(actor, RaceStep::Reset);
        return;
    default: return;
    }
}
// exe 0x428450: a new best time (message 0x18 + rank)
void record(ControllerActor& actor, const RaceActorFields& fields) {
    switch (actor.step) {
    case 0:
        soundPlayPanned(kRecordSound, 0, 0);
        windowClose(0, WindowClose::Force);
        windowOpen(0, kCourseMessages, int16_t(fields.rank + 0x18));
        nextStep(actor);
        return;
    case 1:
        if (windowStatus(0) != 0) return;
        enterRaceStep(actor, RaceStep::Reset);
        return;
    default: return;
    }
}
// exe 0x4284c0: back to the start line and the menu
void resetRace(ControllerActor& actor) {
    switch (actor.step) {
    case 0:
        scriptFade(kCourseFade, 0); // 0x53f6b0(0x12)
        nextStep(actor);
        return;
    case 1:
        if (fadeState().busy != 0) return;
        actorsSweep(0x80, 0x80); // exe 0x5e05d0
        playerToStart(race());
        player().status &= uint16_t(~kStatusSlowedFloor);
        windowClose(1, WindowClose::Force);
        scriptFade(kResetFadeIn, 0); // 0x53f6b0(2)
        enterRaceStep(actor, RaceStep::Menu);
        return;
    default: return;
    }
}
// exe 0x4285a0: back to ST3C sub 1
void quit(ControllerActor& actor) {
    switch (actor.step) {
    case 0: {
        loadoutRestoreParts(); // exe 0x5429c0
        LaneVec4& place = projectionScratch().point;
        place.x = 0x100;
        place.y = int16_t(0xfec0);
        place.z = int16_t(0xfe18);
        projectionScratch().pointYaw = 0;
        goStage(0x3c, 1, RoomPlace{{}, place.x, place.y, place.z, place.spare});
        nextStep(actor);
        return;
    }
    case 1:
        clearStoryFlag(kFlagMinigame);
        clearStoryFlag(kFlagRaceFrozen);
        clearStoryFlag(kFlagMinigameB);
        nextStep(actor);
        return;
    case 2:
        if (transitionRequest().state != kTransitionNone) return;
        clearStoryFlag(kFlagMinigame);
        clearStoryFlag(kFlagRaceFrozen);
        player().animPaused = 0;
        playerBackToPlay();
        scriptReplayPadOff();
        player().flags |= kActorLiveDrawn;
        return;
    default: return;
    }
}
// exe 0x427cc0: [0x88c064 + step * 4], then the time shown
void controllerRun(ControllerActor& actor, RaceActorFields& fields) {
    switch (RaceStep(actor.subState)) {
    case RaceStep::Menu: courseMenu(actor); break;
    case RaceStep::Countdown: countdown(actor); break;
    case RaceStep::Race: raceTick(actor); break;
    case RaceStep::Finish: finish(actor, fields); break;
    case RaceStep::Failed: failed(actor); break;
    case RaceStep::Record: record(actor, fields); break;
    case RaceStep::Reset: resetRace(actor); break;
    case RaceStep::Quit: quit(actor); break;
    default: notPortedAt(kStoryPort, "ST59 controller 0x427cc0 sub", actor.subState);
    }
    messageWindow(1).timeValue = int32_t(race().time);
}

// ---- the speed gauge ---------------------------------------------------------------------------------------------
// exe 0x4288b0 (while the frame draws): the gauge value follows the player's speed -velocity z - 0x40 (on the speed
// floor: -(velocity z + 0x40) >> 2), at most 0x32 a tick, not below 0. Its picture (frame SPRT 0x70 x 0x28 at its
// position, CLUT 0x7dc8 uv (0, 0x58); bar 0x20 high, value / 10 wide at +8 / +2, CLUT 0x7d48 uv (0, 0x80)): GPU packets
// in the OT, rebuilt by st59HudDraw.
void gaugeFollow(RaceActorFields& gauge) {
    const Player& p = player();
    int32_t v;
    if (p.status & kStatusSlowedFloor) v = -((int32_t(p.velocity.z) + 0x40) >> 2);
    else v = -0x40 - int32_t(p.velocity.z);
    const int32_t cur = gauge.gaugeValue;
    if (cur >= v + 0x32) v = cur - 0x32;
    else if (cur + 0x32 <= v) v = cur + 0x32;
    if (v <= 0) v = 0;
    gauge.gaugeValue = int16_t(v);
}
// exe 0x428890
void gaugeRun(RaceActorFields& gauge) {
    if (display().drawingOff == 0) gaugeFollow(gauge);
}
// exe 0x428860
void gaugeStart(ControllerActor& actor, RaceActorFields& gauge) {
    gauge.clearScratchBytes();
    actor.enterState(uint8_t(RaceActorState::Run));
    actor.setDrawn(false);
    gaugeRun(gauge);
}

// ---- the "3, 2, 1, GO" digits ------------------------------------------------------------------------------------
// The digit slides in from y -0x280 (16ths; screen y = slide >> 4) by cosine steps, shows 3 / 2 / 1 / GO (its uv and
// CLUT) 0x1e ticks each (sounds 0x309 / 0x30a), sets GO at GO (the race starts), slides back out and frees itself.
// Picture: SPRT 0x60 x 0x28 at its position (exe 0x428c70), rebuilt by st59HudDraw.
enum class DigitStep : uint8_t { SlideIn = 0, Three = 1, Two = 2, One = 3, Go = 4, SlideOut = 5 };
constexpr uint16_t kDigitTicks = 0x1e;
// exe 0x428a50
void digitsRun(ControllerActor& actor, RaceActorFields& digit) {
    switch (DigitStep(actor.step)) {
    case DigitStep::SlideIn: {
        const int32_t k = fixedCos(int((digit.timerLow & 0x3fu) << 6));
        digit.slide = int16_t(uint16_t(digit.slide) + uint16_t(((k * 10) >> 8) & ~0xf));
        digit.timer = uint16_t(digit.timer + 1u);
        if (int16_t(uint16_t(digit.slide) & 0xfff0u) >= 0x280) {
            digit.timer = kDigitTicks;
            nextStep(actor);
        }
        break;
    }
    case DigitStep::Three:
    case DigitStep::Two:
        digit.timer = uint16_t(digit.timer - 1u);
        if (digit.timer == 0) {
            soundPlayPanned(kCountSound, 0, 0);
            digit.timer = kDigitTicks;
            digit.u = DigitStep(actor.step) == DigitStep::Three ? 0 : 0x60;
            digit.v = 0xa0;
            nextStep(actor);
        }
        break;
    case DigitStep::One:
        digit.timer = uint16_t(digit.timer - 1u);
        if (digit.timer == 0) {
            digit.timer = kDigitTicks;
            soundPlayPanned(kCountSound, 0, 0);
            digit.u = 0;
            digit.v = 0xd0;
            nextStep(actor);
        }
        break;
    case DigitStep::Go:
        digit.timer = uint16_t(digit.timer - 1u);
        if (digit.timer == 0) {
            soundPlayPanned(kGoSound, 0, 0);
            digit.timer = 0;
            digit.clut = 0x7d80;
            nextStep(actor);
            digit.go = 1;
        }
        break;
    case DigitStep::SlideOut: {
        const int32_t k = fixedSin(int((digit.timerLow & 0x3fu) << 6));
        digit.slide = int16_t(uint16_t(digit.slide) - uint16_t(((k * 5) >> 8) & ~0xf));
        digit.timer = uint16_t(digit.timer + 1u);
        if (int16_t(uint16_t(digit.slide) & 0xfff0u) <= -0x280) actor.enterState(uint8_t(RaceActorState::Free));
        break;
    }
    default: break;
    }
    digit.y = int16_t(digit.slide >> 4);
    // drawing on: exe 0x428c70 (the digit's GPU packets) -- presentation, st59HudDraw
}
// exe 0x4289f0
void digitsStart(ControllerActor& actor, RaceActorFields& digit) {
    digit.clearScratchBytes();
    digit.u = 0x20;
    digit.v = 0;
    digit.clut = 0x7dc0;
    digit.x = 0x70;
    actor.setDrawn(false);
    digit.slide = int16_t(0xfd80);
    actor.enterState(uint8_t(RaceActorState::Run));
    digitsRun(actor, digit);
}

// ---- the floor-cell watcher --------------------------------------------------------------------------------------
// exe 0x428d50: the player's speed-floor status set while it stands (the ground probe at its y, on the ground) on a
// grid cell (collision + floor) whose code is within the course's speed-cell range, the lap not done; else cleared (the
// gauge's alternate speed scale)
void watcherRun(const PlayerEnv& env) {
    Player& p = player();
    const uint32_t cell = gridCellUnder(p.pos); // exe 0x40f030
    if (cell == 0) notPorted(kStoryPort, "ST59 0x428d50: the player outside the collision grid (the exe reads [0])");
    const int32_t w = int16_t(GridCell(cell).word);
    const RaceBlock& block = race();
    if ((w & (kCellCollides | kCellFloor)) == (kCellCollides | kCellFloor) && block.progress != kLapDone) {
        const uint32_t code = uint32_t(w) & kCellSegmentNumberMask;
        const uint32_t course = block.course;
        if (code >= speedCellCodes[course * 2] && code <= speedCellCodes[course * 2 + 1] && p.airborne == 0) {
            const int16_t at[3] = {p.pos.x.whole, p.pos.y.whole, p.pos.z.whole};
            if (probeGroundAt(env, at, CollisionBoxRef::record(0), 0, 1) == int32_t(p.pos.y.whole)) { // exe 0x40f1c0
                p.setStatusBits(kStatusSlowedFloor);
                return;
            }
        }
    }
    p.status &= uint16_t(~kStatusSlowedFloor);
}
// exe 0x428d20
void watcherStart(const PlayerEnv& env, ControllerActor& actor, RaceActorFields& fields) {
    fields.clearScratchBytes();
    actor.enterState(uint8_t(RaceActorState::Run));
    actor.setDrawn(false);
    watcherRun(env);
}

// exe 0x427b90
void raceActor(const PlayerEnv& env, ControllerActor& actor) {
    RaceClassGlobals& g = raceGlobals();
    g.raceBlock = kRaceBlock;
    g.classBytesB = slotIdentityWord(actor) + 0x18;
    g.classBytesA = slotIdentityWord(actor) + 0x18;
    g.classBytesC = slotIdentityWord(actor) + 0x18;
    RaceActorFields& fields = classState<RaceActorFields>(actor);
    const uint8_t kind = actor.subtype, s = actor.state;
    if (kind > 3 || s > 2) notPortedAt(kStoryPort, "ST59 class 0x427b90 variant/state", uint32_t(kind) << 8 | s);
    if (RaceActorState(s) == RaceActorState::Free) { // 0x427cf0
        freeActor(actor);
        return;
    }
    const bool start = RaceActorState(s) == RaceActorState::Start;
    switch (RaceActor(kind)) {
    case RaceActor::Controller:
        if (start) {
            if (actor.step != 0) return;
            controllerStart(actor, fields);
        }
        controllerRun(actor, fields);
        return;
    case RaceActor::Gauge: start ? gaugeStart(actor, fields) : gaugeRun(fields); return;
    case RaceActor::Countdown: start ? digitsStart(actor, fields) : digitsRun(actor, fields); return;
    default: start ? watcherStart(env, actor, fields) : watcherRun(env); return;
    }
}

} // namespace

// ---- public ------------------------------------------------------------------------------------------------------

void st59StageCase() { // 0x58ca30
    RoomScriptState& room = roomScriptState();
    switch (CourseCaseStep(room.handlerStep)) {
    case CourseCaseStep::SetUp: {
        raceStageClear(); // exe 0x40dc40
        RaceBlock& block = race();
        block.course = uint8_t(gameState().stage - 0x59u);
        scriptReplayPadOn(); // exe 0x5117f0
        const uint32_t slot = allocActorE0(); // exe 0x5e1440: the speed gauge
        pointAtWord(block.gauge, slot);
        if (slot != 0) {
            ControllerActor& gauge = controllerActorAt(slot);
            RaceActorFields& fields = classState<RaceActorFields>(gauge);
            gauge.flags = kActorLive;
            gauge.type = kRaceActorType;
            gauge.subtype = uint8_t(RaceActor::Gauge);
            gauge.variant = 0;
            gauge.spawnByte = 0;
            gauge.loopFlags |= kActorLateLoop;
            fields.x = 0xc0;
            fields.y = 0xb8;
        }
        ++room.handlerStep;
        return;
    }
    case CourseCaseStep::StartRace:
        if (playerToScriptWhenStanding() != 0) return; // exe 0x572320
        spawnRaceActor(RaceActor::Controller);          // the race controller (not kept)
        ++room.handlerStep;
        return;
    default: return;
    }
}

void raceActorUpdate(const PlayerEnv& env, uint32_t actor) { raceActor(env, controllerActorAt(actor)); }

// The packets of 0x4288b0 (the gauge) and 0x428c70 (the digits), both run at the end of the class's state 1 (state 0
// falls into it) unless the frame draws nothing, from the state the tick left (the gauge's value is the one the packet
// used). Each: draw mode 0x40b6d0(p, 1, 1, tpage 0x36) + SPRT 0x64808080 (opaque, colour 0x80 = texels as is):
//  * gauge: the frame at its position, uv (0, 0x58) CLUT 0x7dc8, 0x70 x 0x28; when the value v > 0, the bar at (x + 8,
//    y + 2), uv (0, 0x80) CLUT 0x7d48, v / 10 x 0x20 (chain: mode, frame, bar = drawn in that order);
//  * countdown: at its position, its uv and CLUT, 0x60 x 0x28.
// Each actor's chain goes to the head of OT [0xa67840] + 0x78 (the actor run last is drawn first): pool e0 (exe
// 0x55bec0) then its late loop (exe 0x55bf20, the gauge: kActorLateLoop), slot order. Alive after the tick in state 1
// or 2 (state 2, set during the tick, still drew; the tick that runs state 2 frees the actor).
std::vector<hud::Prim> st59HudDraw() {
    std::vector<hud::Prim> out;
    if (display().drawingOff != 0) return out;
    auto sprt = [](int16_t x, int16_t y, int w, int h, uint8_t u, uint8_t v, uint16_t clut) {
        hud::Prim q;
        q.kind = hud::Prim::Textured;
        q.tpage = 0x36;
        q.clut = clut;
        q.colour = 0x808080;
        q.x[0] = q.x[2] = float(x);
        q.x[1] = q.x[3] = float(x + w);
        q.y[0] = q.y[1] = float(y);
        q.y[2] = q.y[3] = float(y + h);
        q.u[0] = q.u[2] = float(u);
        q.u[1] = q.u[3] = float(u + w);
        q.v[0] = q.v[1] = float(v);
        q.v[2] = q.v[3] = float(v + h);
        return q;
    };
    std::vector<std::vector<hud::Prim>> chains; // in the exe's insertion order
    for (const ActorPool pool : {ActorPool::Controllers, ActorPool::ControllersLate}) {
        for (uint32_t i = 0; i < 32; ++i) {
            ControllerActor& actor = controllerPool()[i];

            const RaceActorState s = RaceActorState(actor.state);
            if (!actorHandler(actor, pool).is(kRaceClass) || (s != RaceActorState::Run && s != RaceActorState::Free))
                continue;
            const RaceActorFields& fields = classState<RaceActorFields>(actor);
            const int16_t x = fields.x, y = fields.y;
            std::vector<hud::Prim> c;
            if (RaceActor(actor.subtype) == RaceActor::Gauge) {
                c.push_back(sprt(x, y, 0x70, 0x28, 0, 0x58, 0x7dc8));
                const int32_t v = fields.gaugeValue;
                if (v > 0) c.push_back(sprt(int16_t(x + 8), int16_t(y + 2), v / 10, 0x20, 0, 0x80, 0x7d48));
            } else if (RaceActor(actor.subtype) == RaceActor::Countdown) {
                c.push_back(sprt(x, y, 0x60, 0x28, fields.u, fields.v, fields.clut));
            } else {
                continue;
            }
            chains.push_back(std::move(c));
        }
    }
    for (auto it = chains.rbegin(); it != chains.rend(); ++it) out.insert(out.end(), it->begin(), it->end());
    return out;
}

} // namespace od::port
