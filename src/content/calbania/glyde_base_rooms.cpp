// The room handlers of Glyde's base (ST30 / ST31, chapter 3):
//   ST30 sub 3 (exe 0x585df0): script 0x21 once (step 1) when no script runs.
//   ST30 sub 0 (exe 0x5854c0): the door test (the music faded, script 0). 0 -> without flag 0x3e5: flag 0x711; without
//   0x20 the wave (encounter group of script slot 4), sound 0x25 and the opening (step 1); with 0x20 the gate static
//   (exe 0x585ec0(0); step 5, sound 0x28); with 0x3e5 only sound 0x28 (step 0xff). 1: the opening (exe 0x585e10, event
//   message 2) -> 2; 2: flag 0x20 (the wave beaten) -> 0x3c ticks (byte 3), the music faded -> 3 -> script 0x22 (4);
//   4: the gate static, sound 0x28, a message trigger (message 3, shown at once) -> 5; 5: flag 0x3e5 -> flag 0x711
//   cleared (6).
//   ST30 sub 1 (exe 0x585670): the door test as sub 0. 0 (no script running) -> without flag 0x3e6: flag 0x712; without
//   0x21 the wave (slot 5), sound 0x26 and the opening (step 1); with 0x21 the gate static 1 (step 9, sound 0x2a); with
//   0x3e6 sound 0x28 (0xff). 1: the opening (event message 4) -> 2: 6 enemies left, group 0, the message trigger
//   (exe 0x586450(0, 5)); 3 / 4: none left -> 6 more of group 0; 5: none left -> 1 of group 1; 6: none left -> 0x3c
//   ticks, the escape's ticks cleared, flag 0x1f, the music faded; 7: script 0x22; 8: the gate static, sound 0x28; 9:
//   flag 0x3e6 -> flag 0x712 cleared.
//   ST30 sub 2 (exe 0x585890, the main gate): the door test as sub 0. Without flag 0x23 (the wave): 0 (no script): the
//   rescue / hint latches cleared; without 0x3e7: flag 0x713, group 2 (sound 0x27, step 1) or with 0x22 the gate
//   static 2 (step 6); 1: the opening (message 6) unless 0x22; 2: the message trigger (0xe, 7) with its countdown
//   0x708, 3 enemies left (unless 0x3e7); 3: 0x22 -> 0x3c ticks, the music faded; 4: the place of the last gate static
//   (type 0x42 in the statics' first half) kept, script 0x22; 5: the gate static there, sound 0x28; 6: 0x3e7 -> 0x713
//   cleared. With flag 0x23 (the escape): 0: cells (0x3e, 0x46) / (0x42, 0x40) to 3; 0 / 1: script 0x25 once; 2: sound
//   0x29 (unless music channel 0 plays), the escape's ticks = 0xe2e shown by the escape timer (pool e0 type 0x22), group
//   3, the one to carry out (pool 0x20 type 0x71 variant 0x81 at (-7, 0, 0x8b8) yaw 0x260), then the opening (message
//   0x12); 4: the timer (while alive): beeps (sound 0x1c6) every 30 ticks below 0x384, triggers 9 at 0x1e0 / 0xd at
//   0xa8c; the player and the carried one (put down) both in the gate box x (0xf600, 0xf640) z (0x160, 0x260) -> flag
//   0x3e5 (else trigger 8 once); success or time out (0x3e5 cleared): all sound faded, the voice faded, the windows
//   closed, event message 1 (window 1, flags 0xb).
//   ST31 sub 0 (exe 0x585f80, the box room): the door test. 0 (no script): sound 0x2a, the 7 x 7 walk grid from
//   0x8cacd0, 4 boxes (static type 0x25, variants 0x8cad1c, places 0x8cad20), 3 walkers (pool 0x20 type 0x38 sub i,
//   variants 0x8cad30, places / yaws 0x8cad34) -> 1. 1: the opening (exe 0x5863a0, event message 1) -> the message
//   trigger (0xe, 4) with its countdown 0x708 -> 2. 2 (every tick): the 8 cells of 0x8cad04 {x, z, grid index} not at
//   variant 0 -> their grid byte's bit 0 cleared; once a box in state 2 -> trigger (0, 3); once no box of variant 2 ->
//   trigger (0xe, 6) with its countdown 0x96; at tick 0xa8c a walker still alive -> trigger (0, 5).
//   ST31 sub 1 (exe 0x586220, Shu's brother's cell): 0: script 0x23 once -> 1; 1 (no script): 3 Birdbots (pool 0x20
//   type 0x2d sub 1, places / yaws 0x8cad48) and the brother (type 0x71 variant 0x80 at (0x2d0, 0, 0x2d0) yaw 0x200)
//   -> 2; 2: no Birdbot left -> the music faded (0x199), 0x28 ticks -> 3; 3 (the player alive): flags 0x23 / 0x670, the
//   story + 1, script 0x24 -> 4.
#include "content/calbania/ch3_rooms.hpp"
#include "content/calbania/calbania_rooms.hpp"
#include "content/calbania/escape_timer_state.hpp"

#include "content/nino/glyde_box_walkers_state.hpp"
#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/encounter.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/game_state.hpp"
#include "game/hud.hpp"
#include "game/message.hpp"
#include "game/message_triggers.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_grid.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/table_records.hpp"

namespace od::port {


using od::content::GlydeBaseRoomState;
using od::content::glydeBaseRoomState;

namespace {

constexpr RoutineId kFnEntrance = RoutineId::GlydeBaseRoomsEntrance, kFnSub0 = RoutineId::GlydeBaseRoomsSub0,
                    kFnSub1 = RoutineId::GlydeBaseRoomsSub1, kFnGate = RoutineId::GlydeBaseRoomsGate,
                   kFnBoxRoom = RoutineId::GlydeBaseRoomsBoxRoom, kFnCell = RoutineId::GlydeBaseRoomsCell;

constexpr uint32_t kSweepAll = 0xffe, kSweepFreeze = 1, kSweepThaw = 2, kSweepClass8 = 8, kSweepKeep = 0x80;
constexpr uint32_t kOpeningEventFlags = 8;
constexpr uint16_t kBannerX = 0x54, kBannerY = 0x30;
constexpr uint8_t kNoStep = 0xff;
constexpr uint8_t kWaveDoneTicks = 0x3c;
constexpr int16_t kMusicFade = 0x111;
constexpr uint8_t kEntranceScript = 0x21, kOverviewScript = 0x22, kRescueScript = 0x25, kBrotherHeldScript = 0x23,
                  kBrotherFreedScript = 0x24;
constexpr uint16_t kSoundBase = 0x28;
constexpr uint8_t kTriggerLastStory = 9;
constexpr uint8_t kTriggerCountdown = 0xe; // the message trigger's countdown variant (its first parameter: the ticks)

// a script or an event runs
bool scriptRuns() { return scriptState().inUse(); }
bool eventRuns() { return eventState().active != 0; }

// the door test of ST30's rooms: the armed transition fades the music and starts script 0
bool baseDoorScript() {
    if (!storyFlag(kFlagTransitionArmed)) return false;
    soundFadeOut(kMusicFades, kMusicFade, 0);
    scriptRun(0);
    return true;
}

enum class OpeningStep : uint8_t { Banner = 0, Freeze = 1, WaitEvent = 2, Done = 3 };
// exe 0x585e10(state, message) / exe 0x5863a0: the base's mission opening -- the banner and the event message, the
// actors 0xffe frozen, then (every tick) the actors of class 8 swept until no event runs: the banner hidden, the player's input
// back, the actors thawed -> 1 (done); the handler's byte 3 is its step
int baseOpening(uint16_t message) {
    uint8_t& step = roomScriptState().handlerByte3;
    switch (OpeningStep(step)) {
    case OpeningStep::Banner:
        hudBannerShow(kBannerX, kBannerY, HudBannerStyle::MissionStart);
        eventStartAt(0, nullptr, kOpeningEventFlags, 0, message);
        ++step;
        return 0;
    case OpeningStep::Freeze:
        actorsSweep(kSweepAll, kSweepFreeze);
        ++step;
        return 0;
    case OpeningStep::WaitEvent:
        actorsSweep(kSweepClass8, kSweepKeep);
        if (eventRuns()) return 0;
        ++step;
        hudBanner().hide(); // exe 0x5f2680
        player().inputMode = 0;
        actorsSweep(kSweepAll, kSweepThaw);
        return 1;
    case OpeningStep::Done: return 1;
    default: return 0;
    }
}

// the gate statics' places (exe data, 8 bytes each)
struct GatePlace : DataRecord {
    ShortPoint point;
    static GatePlace load(const TableReader& r) {
        GatePlace rec;
        rec.point = ShortPoint::load(r.sub(0x0, 6));
        return rec;
    }
};
const DataTable<RecordTable<GatePlace, 3, 8>> gatePlaces{TableId::GlydeBaseRoomsGatePlaces};
constexpr uint8_t kGateStaticType = 0x42;
// exe 0x585ec0(k): the gate static k (static stage type 0x42 variant 1) at its place; nullptr when the pool is full
StaticActor* gateStatic(uint32_t k) {
    const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (address == 0) return nullptr;
    StaticActor& a = staticActorAt(address);
    const GatePlace place = gatePlaces[k];
    a.flags = kActorLive;
    a.type = kGateStaticType;
    a.subtype = 0;
    a.variant = 1;
    a.loopFlags |= kActorSweepGroup2;
    a.pos.x.whole = place.point.x;
    a.pos.y.whole = place.point.y;
    a.pos.z.whole = place.point.z;
    a.params[0] = uint8_t(k);
    return &a;
}
// exe 0x586450(variant, message): a message trigger of the variant unless the player is down; nullptr when none
ControllerActor* messageTrigger(uint8_t variant, int16_t message) {
    if (player().hitPoints < 0) return nullptr;
    const uint32_t address = allocActorE0(); // exe 0x5e1440
    if (address == 0) return nullptr;
    ControllerActor& c = controllerActorAt(address);
    MessageTriggerFields& t = classState<MessageTriggerFields>(c);
    c.flags = kActorLive;
    c.type = kMessageTriggerType;
    c.variant = variant;
    t.flag = kNoMessageFlag;
    t.message = message;
    t.lastStory = int8_t(kTriggerLastStory);
    return &c;
}
// ... with its countdown
void countdownTrigger(int16_t message, int16_t ticks) {
    if (ControllerActor* c = messageTrigger(kTriggerCountdown, message)) classState<MessageTriggerFields>(*c).params[0] = ticks;
}

// ---- ST30 sub 0 ---------------------------------------------------------------------------------------------------
enum class Sub0Step : uint8_t { Start = 0, Opening = 1, Wave = 2, Wait = 3, Static = 4, WaitEscape = 5, Done = 6 };
constexpr uint32_t kFlagSub0Busy = 0x711, kFlagEscaped = 0x3e5, kFlagSub0Wave = 0x20;
constexpr uint16_t kSub0Message = 2, kSub0Sound = 0x25;
constexpr int kSub0Group = 4;
constexpr int16_t kStaticTriggerMessage = 3;

void sub0Room() { // exe 0x5854c0
    if (baseDoorScript()) return;
    RoomScriptState& room = roomScriptState();
    const uint8_t s0 = room.handlerStep;
    switch (Sub0Step(s0)) {
    case Sub0Step::Start: {
        room.handlerStep = kNoStep;
        uint16_t sound = kSoundBase;
        if (!storyFlag(kFlagEscaped)) {
            setStoryFlag(kFlagSub0Busy);
            if (!storyFlag(kFlagSub0Wave)) {
                room.handlerStep = uint8_t(Sub0Step::Opening);
                sound = kSub0Sound;
                encounterSpawnGroupScriptSlot(kSub0Group);
            } else {
                room.handlerStep = uint8_t(Sub0Step::WaitEscape);
                gateStatic(0);
                sound = kSoundBase;
            }
        }
        soundPlayFlatForced(sound);
        if (Sub0Step(room.handlerStep) != Sub0Step::Opening) return;
        [[fallthrough]];
    }
    case Sub0Step::Opening:
        if (!storyFlag(kFlagSub0Wave) && baseOpening(kSub0Message)) ++room.handlerStep;
        return;
    case Sub0Step::Wave:
        if (storyFlag(kFlagSub0Wave)) {
            ++room.handlerStep;
            room.handlerByte3 = kWaveDoneTicks;
            soundFadeOut(kMusicFades, kMusicFade, 0);
        }
        return;
    case Sub0Step::Wait:
        --room.handlerByte3;
        if (room.handlerByte3 == 0) {
            room.handlerStep = uint8_t(s0 + 1u);
            scriptRun(kOverviewScript);
        }
        return;
    case Sub0Step::Static: {
        room.handlerStep = uint8_t(s0 + 1u);
        gateStatic(0);
        soundPlayFlatForced(kSoundBase);
        if (const uint32_t address = allocActorE0()) { // exe 0x5e1440: a message trigger shown at once
            ControllerActor& c = controllerActorAt(address);
            MessageTriggerFields& t = classState<MessageTriggerFields>(c);
            c.flags = kActorLive;
            c.type = kMessageTriggerType;
            c.variant = kMessageTriggerAtOnce;
            t.flag = kNoMessageFlag;
            t.message = kStaticTriggerMessage;
            t.lastStory = int8_t(kTriggerLastStory);
        }
        return;
    }
    case Sub0Step::WaitEscape:
        if (storyFlag(kFlagEscaped)) {
            ++room.handlerStep;
            clearStoryFlag(kFlagSub0Busy);
        }
        return;
    default: return;
    }
}

// ---- ST30 sub 1 ---------------------------------------------------------------------------------------------------
enum class Sub1Step : uint8_t {
    Start = 0, Opening = 1, FirstWave = 2, SecondWave = 3, ThirdWave = 4, LastWave = 5, Won = 6, Wait = 7,
    Static = 8, WaitEscape = 9, Done = 10
};
constexpr uint32_t kFlagSub1Busy = 0x712, kFlagSub1Escaped = 0x3e6, kFlagSub1Wave = 0x21, kFlagWallBroken = 0x1f;
constexpr uint16_t kSub1Message = 4, kSub1Sound = 0x26, kSub1StaticSound = 0x2a;
constexpr int kSub1Group = 5, kWaveGroup = 0, kLastGroup = 1;
constexpr uint32_t kWaveSize = 6, kLastWaveSize = 1;
constexpr int16_t kSub1TriggerMessage = 5;

void sub1Room() { // exe 0x585670
    if (baseDoorScript()) return;
    RoomScriptState& room = roomScriptState();
    EncounterState& enc = encounterState();
    const uint8_t s0 = room.handlerStep;
    switch (Sub1Step(s0)) {
    case Sub1Step::Start: {
        if (scriptRuns()) return;
        room.handlerStep = kNoStep;
        uint16_t sound = kSoundBase;
        if (!storyFlag(kFlagSub1Escaped)) {
            room.handlerStep = uint8_t(Sub1Step::Opening);
            setStoryFlag(kFlagSub1Busy);
            sound = kSub1StaticSound;
            if (!storyFlag(kFlagSub1Wave)) {
                encounterSpawnGroupScriptSlot(kSub1Group);
                sound = kSub1Sound;
            } else {
                room.handlerStep = uint8_t(Sub1Step::WaitEscape);
                gateStatic(1);
            }
        }
        soundPlayFlatForced(sound);
        if (Sub1Step(room.handlerStep) != Sub1Step::Opening) return;
        [[fallthrough]];
    }
    case Sub1Step::Opening:
        if (!storyFlag(kFlagSub1Wave) && baseOpening(kSub1Message)) ++room.handlerStep;
        return;
    case Sub1Step::FirstWave:
        room.handlerStep = uint8_t(s0 + 1u);
        enc.waveEnemies = kWaveSize;
        encounterSpawnGroupScriptSlot(kWaveGroup);
        messageTrigger(kMessageTriggerAtOnce, kSub1TriggerMessage);
        return;
    case Sub1Step::SecondWave:
    case Sub1Step::ThirdWave:
        if (enc.waveEnemies == 0) {
            room.handlerStep = uint8_t(s0 + 1u);
            enc.waveEnemies = kWaveSize;
            encounterSpawnGroupScriptSlot(kWaveGroup);
        }
        return;
    case Sub1Step::LastWave:
        if (enc.waveEnemies == 0) {
            room.handlerStep = uint8_t(s0 + 1u);
            enc.waveEnemies = kLastWaveSize;
            encounterSpawnGroupScriptSlot(kLastGroup);
        }
        return;
    case Sub1Step::Won:
        if (enc.waveEnemies == 0) {
            room.handlerByte3 = kWaveDoneTicks;
            room.handlerStep = uint8_t(s0 + 1u);
            glydeBaseRoomState().escapeTicks = 0;
            setStoryFlag(kFlagWallBroken);
            soundFadeOut(kMusicFades, kMusicFade, 0);
        }
        return;
    case Sub1Step::Wait:
        --room.handlerByte3;
        if (room.handlerByte3 == 0) {
            room.handlerStep = uint8_t(s0 + 1u);
            scriptRun(kOverviewScript);
        }
        return;
    case Sub1Step::Static:
        room.handlerStep = uint8_t(s0 + 1u);
        gateStatic(1);
        soundPlayFlatForced(kSoundBase);
        return;
    case Sub1Step::WaitEscape:
        if (storyFlag(kFlagSub1Escaped)) {
            ++room.handlerStep;
            clearStoryFlag(kFlagSub1Busy);
        }
        return;
    default: return;
    }
}

// ---- ST30 sub 2 ---------------------------------------------------------------------------------------------------
enum class GateStep : uint8_t { Start = 0, Opening = 1, Trigger = 2, Wave = 3, Wait = 4, Static = 5, WaitEscape = 6 };
enum class EscapeStep : uint8_t { Cells = 0, Rescue = 1, Start = 2, Opening = 3, Run = 4, Over = 5 };
constexpr uint32_t kFlagGateBusy = 0x713, kFlagGateEscaped = 0x3e7, kFlagGateWave = 0x22, kFlagEscape = 0x23;
constexpr uint16_t kGateMessage = 6, kGateSound = 0x27, kEscapeMessage = 0x12, kEscapeSound = 0x29;
constexpr int kGateGroup = 2, kEscapeGroup = 3;
constexpr uint16_t kGateWaveSize = 3;
constexpr int16_t kGateTriggerMessage = 7, kGateTriggerTicks = 0x708;
constexpr int16_t kNoGateX = int16_t(0xfc00);
constexpr uint32_t kGateStaticSlotBit = 0x20; // the statics whose slot id has this bit (the pool's first half)
constexpr uint32_t kEscapeTicks = 0xe2e;
constexpr uint8_t kEscapeTimerType = 0x22, kEscapeTimerSlotBits = 0xe0;
constexpr uint8_t kCarriedType = 0x71, kCarriedVariant = 0x81;
constexpr uint32_t kTicksPerSecond = 0x1e, kBeepFrom = 0x384;
constexpr uint16_t kBeepSound = 0x1c6;
constexpr uint32_t kHurryTick = 0x1e0, kHintTick = 0xa8c;
constexpr int16_t kHurryMessage = 9, kHintMessage = 0xd, kCarryMessage = 8;
constexpr uint32_t kEscapeEndFlags = 0xb;
constexpr uint32_t kVoiceFadeFast = 0x10;
constexpr int16_t kAllSoundFade = 0x3ff;

// the gate's box (x / z, unsigned integer parts)
bool inGate(uint32_t x, uint32_t z) { return x > 0xf600 && x < 0xf640 && z > 0x160 && z < 0x260; }
// the escape is over (made or timed out): all sound faded, the voice faded, the windows closed, the event message 1
void escapeOver() { // exe 0x585c47..
    soundFadeOut(kAllFades, kAllSoundFade, 0);
    if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFadeFast);
    windowCloseAll(WindowClose::Force);
    eventStartAt(0, nullptr, kEscapeEndFlags, 1, 1);
}
void gateWave(uint8_t s0) {
    RoomScriptState& room = roomScriptState();
    GlydeBaseRoomState& base = glydeBaseRoomState();
    switch (GateStep(s0)) {
    case GateStep::Start: {
        if (scriptRuns()) return;
        base.rescueLatch = 0;
        base.gateHintLatch = 0;
        room.handlerStep = kNoStep;
        uint16_t sound = kSoundBase;
        if (!storyFlag(kFlagGateEscaped)) {
            room.handlerStep = uint8_t(GateStep::Opening);
            setStoryFlag(kFlagGateBusy);
            if (storyFlag(kFlagGateWave)) {
                room.handlerStep = uint8_t(GateStep::WaitEscape);
                gateStatic(2);
            } else {
                encounterSpawnGroupScriptSlot(kGateGroup);
                sound = kGateSound;
            }
        }
        soundPlayFlatForced(sound);
        if (GateStep(room.handlerStep) != GateStep::Opening) return;
        [[fallthrough]];
    }
    case GateStep::Opening:
        if (!storyFlag(kFlagGateWave) && baseOpening(kGateMessage)) ++room.handlerStep;
        return;
    case GateStep::Trigger:
        room.handlerStep = uint8_t(s0 + 1u);
        if (storyFlag(kFlagGateEscaped)) return;
        countdownTrigger(kGateTriggerMessage, kGateTriggerTicks);
        encounterState().setWaveEnemiesWord(kGateWaveSize);
        return;
    case GateStep::Wave:
        if (storyFlag(kFlagGateWave)) {
            room.handlerStep = uint8_t(s0 + 1u);
            room.handlerByte3 = kWaveDoneTicks;
            soundFadeOut(kMusicFades, kMusicFade, 0);
        }
        return;
    case GateStep::Wait: {
        --room.handlerByte3;
        if (room.handlerByte3 != 0) return;
        room.handlerStep = uint8_t(s0 + 1u);
        base.gateStaticX = kNoGateX;
        base.gateStaticZ = 0;
        StaticPool& statics = staticPool();
        for (size_t i = 0; i < statics.size(); ++i) {
            StaticActor& a = statics[i];
            if ((a.slot & kGateStaticSlotBit) && a.type == kGateStaticType) {
                base.gateStaticX = a.pos.x.whole;
                base.gateStaticZ = a.pos.z.whole;
            }
        }
        scriptRun(kOverviewScript);
        return;
    }
    case GateStep::Static: {
        room.handlerStep = uint8_t(s0 + 1u);
        StaticActor* a = gateStatic(2);
        if (a == nullptr)
            notPorted(kStoryPort, "ST30 sub 2: static 0x42 pool full (the exe writes through a null pointer)");
        a->pos.x.whole = base.gateStaticX;
        a->pos.z.whole = base.gateStaticZ;
        soundPlayFlatForced(kSoundBase);
        return;
    }
    case GateStep::WaitEscape:
        if (storyFlag(kFlagGateEscaped)) {
            room.handlerStep = uint8_t(s0 + 1u);
            clearStoryFlag(kFlagGateBusy);
        }
        return;
    default: return;
    }
}
void gateEscape(uint8_t s0) {
    RoomScriptState& room = roomScriptState();
    GlydeBaseRoomState& base = glydeBaseRoomState();
    switch (EscapeStep(s0)) {
    case EscapeStep::Cells:
        room.handlerStep = uint8_t(s0 + 1u);
        switchGridCellVariant(0x3e, 0x46, 3);
        switchGridCellVariant(0x42, 0x40, 3);
        [[fallthrough]];
    case EscapeStep::Rescue:
        ++room.handlerStep;
        if (base.rescueLatch != 0) return;
        base.rescueLatch = 1;
        scriptRun(kRescueScript);
        return;
    case EscapeStep::Start:
        if (!musicChannelPlaying(0)) soundPlayFlatForced(kEscapeSound);
        ++room.handlerStep;
        base.escapeTicks = kEscapeTicks;
        if (const uint32_t address = allocActorE0()) { // exe 0x5e1440: the escape timer on screen
            ControllerActor& t = controllerActorAt(address);
            t.flags |= kActorLive;
            t.slot |= kEscapeTimerSlotBits;
            t.type = kEscapeTimerType;
            t.subtype = 0;
            classState<escape_timer_state::EscapeTimerFields>(t).timer = &base.escapeTicks; // the ticks it shows
        }
        encounterSpawnGroupScriptSlot(kEscapeGroup);
        if (const uint32_t address = allocActor20()) { // exe 0x5e1160: the one to carry out
            AnimatedActor& a = animatedActorAt(address);
            a.flags = kActorLiveDrawn;
            a.loopFlags |= kActorSweepGroup2;
            a.type = kCarriedType;
            a.subtype = 0;
            a.variant = kCarriedVariant;
            a.pos.x.whole = int16_t(0xfff9);
            a.pos.y.whole = 0;
            a.pos.z.whole = 0x8b8;
            a.rotation.y = 0x260;
            base.carriedActor = address;
        }
        if (baseOpening(kEscapeMessage)) ++room.handlerStep;
        return;
    case EscapeStep::Opening:
        if (baseOpening(kEscapeMessage)) ++room.handlerStep;
        return;
    case EscapeStep::Run: {
        Player& p = player();
        if (p.hitPoints < 0) return;
        const uint32_t c = base.escapeTicks - 1u;
        base.escapeTicks = c;
        if (c == 0) {
            room.handlerStep = uint8_t(EscapeStep::Over);
            clearStoryFlag(kFlagEscaped);
            escapeOver();
            return;
        }
        if (int32_t(c) % int32_t(kTicksPerSecond) == 0 && int32_t(c) <= int32_t(kBeepFrom)) {
            soundPlayFlat(kBeepSound); // exe 0x5d51f0
        }
        if (c == kHurryTick) messageTrigger(kMessageTriggerAtOnce, kHurryMessage);
        if (c == kHintTick && base.gateHintLatch == 0) messageTrigger(kMessageTriggerAtOnce, kHintMessage);
        if (p.airborne != 0) return;
        const Actor& carried = actorAt(base.carriedActor);
        if (carried.airborne != 0) return;
        if (!inGate(uint16_t(p.pos.x.whole), uint16_t(p.pos.z.whole))) return;
        if (inGate(uint16_t(carried.pos.x.whole), uint16_t(carried.pos.z.whole))) {
            ++room.handlerStep;
            setStoryFlag(kFlagEscaped);
            escapeOver();
            return;
        }
        if (base.gateHintLatch == 0) {
            base.gateHintLatch = 1;
            messageTrigger(kMessageTriggerAtOnce, kCarryMessage);
        }
        return;
    }
    default: return;
    }
}
void gateRoom() { // exe 0x585890
    if (baseDoorScript()) return;
    const uint8_t s0 = roomScriptState().handlerStep;
    if (!storyFlag(kFlagEscape)) gateWave(s0);
    else gateEscape(s0);
}

// ---- ST31 sub 0 ---------------------------------------------------------------------------------------------------
enum class BoxRoomStep : uint8_t { Spawn = 0, Opening = 1, Run = 2 };
constexpr uint16_t kBoxRoomSound = 0x2a, kBoxRoomMessage = 1;
const DataTable<ScalarTable<uint8_t, 0x31>> walkGridStart{TableId::GlydeBaseRoomsWalkGridStart};
const DataTable<ScalarTable<uint8_t, 4>> boxVariants{TableId::GlydeBaseRoomsBoxVariants};
struct BoxPlace : DataRecord {
    int16_t x = 0;
    int16_t z = 0;
    static BoxPlace load(const TableReader& r) {
        BoxPlace rec;
        rec.x = r.s16(0x0);
        rec.z = r.s16(0x2);
        return rec;
    }
};
const DataTable<RecordTable<BoxPlace, 4, 4>> boxPlaces{TableId::GlydeBaseRoomsBoxPlaces};
const DataTable<ScalarTable<uint8_t, 3>> walkerVariants{TableId::GlydeBaseRoomsWalkerVariants};
struct WalkerPlace : DataRecord {
    int16_t x = 0;
    int16_t z = 0;
    int16_t yaw = 0;
    static WalkerPlace load(const TableReader& r) {
        WalkerPlace rec;
        rec.x = r.s16(0x0);
        rec.z = r.s16(0x2);
        rec.yaw = r.s16(0x4);
        return rec;
    }
};
const DataTable<RecordTable<WalkerPlace, 3, 6>> walkerPlaces{TableId::GlydeBaseRoomsWalkerPlaces};
// the cells a box can stand on: {x, z, walk grid index}
struct GridCellLink : DataRecord {
    uint8_t x = 0;
    uint8_t z = 0;
    uint8_t gridIndex = 0;
    static GridCellLink load(const TableReader& r) {
        GridCellLink rec;
        rec.x = r.u8(0x0);
        rec.z = r.u8(0x1);
        rec.gridIndex = r.u8(0x2);
        return rec;
    }
};
const DataTable<RecordTable<GridCellLink, 8, 3>> boxCells{TableId::GlydeBaseRoomsBoxCells};
constexpr uint8_t kBoxType = 0x25, kWalkerType = 0x38;
constexpr size_t kBoxes = 4, kWalkers = 3;
constexpr uint8_t kGridOpenBit = 0x01;
constexpr uint8_t kBoxMoved = 2;      // a box's state 2 (thrown) / its variant 2
constexpr int16_t kBoxThrownMessage = 3, kBoxesGoneMessage = 6, kWalkersLeftMessage = 5, kBoxRoomTriggerMessage = 4;
constexpr int16_t kBoxRoomTriggerTicks = 0x708, kBoxesGoneTicks = 0x96;
constexpr uint32_t kWalkersHintTick = 0xa8c;

// the first live box whose state / variant is kBoxMoved (kBoxes: none)
template <class Pick> size_t boxWith(Pick pick) {
    GlydeBaseRoomState& base = glydeBaseRoomState();
    size_t i = 0;
    for (; i < kBoxes; ++i) {
        const uint32_t a = base.boxes[i];
        if (a != 0) {
            const ActorHeader& box = actorHeaderAt(a);
            if ((box.flags & kActorLive) && box.type == kBoxType && pick(box) == kBoxMoved) break;
        }
    }
    return i;
}
void boxRoom() { // exe 0x585f80
    if (roomDoorScript()) return;
    RoomScriptState& room = roomScriptState();
    GlydeBaseRoomState& base = glydeBaseRoomState();
    switch (BoxRoomStep(room.handlerStep)) {
    case BoxRoomStep::Spawn:
        if (scriptRuns()) return;
        ++room.handlerStep;
        soundPlayFlatForced(kBoxRoomSound);
        for (size_t k = 0; k < base.walkGrid.size(); ++k) base.walkGrid[k] = walkGridStart[k];
        for (size_t i = 0; i < kBoxes; ++i) {
            const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
            if (address != 0) {
                StaticActor& a = staticActorAt(address);
                const BoxPlace place = boxPlaces[i];
                a.flags = kActorLiveDrawn;
                a.type = kBoxType;
                a.subtype = 0;
                a.variant = boxVariants[i];
                a.spawnByte = 0;
                a.pos.y.whole = 0;
                a.pos.x.whole = place.x;
                a.pos.z.whole = place.z;
                a.rotation.y = 0;
                a.params[2] = 0;
            }
            base.boxes[i] = address;
        }
        for (size_t i = 0; i < kWalkers; ++i) {
            const uint32_t address = allocActor20(); // exe 0x5e1160
            if (address != 0) {
                AnimatedActor& a = animatedActorAt(address);
                const WalkerPlace place = walkerPlaces[i];
                a.flags = kActorLiveDrawn;
                a.type = kWalkerType;
                a.subtype = uint8_t(i);
                a.variant = walkerVariants[i];
                a.params[3] = uint8_t(i);
                a.pos.y.whole = 0;
                a.pos.x.whole = place.x;
                a.pos.z.whole = place.z;
                a.rotation.y = place.yaw;
                classState<glyde_box_walkers_state::WalkerFields>(a).target = 0;
            }
            base.walkers[i] = address;
        }
        [[fallthrough]];
    case BoxRoomStep::Opening:
        if (!baseOpening(kBoxRoomMessage)) return; // exe 0x5863a0
        ++room.handlerStep;
        countdownTrigger(kBoxRoomTriggerMessage, kBoxRoomTriggerTicks); // exe 0x586450
        [[fallthrough]];
    case BoxRoomStep::Run: {
        for (size_t k = 0; k < boxCells.size(); ++k) {
            const GridCellLink cell = boxCells[k];
            if (cellSegmentVariant(GridCell(gridCellAtIndex(cell.x, cell.z)).head) != 0)
                base.walkGrid[cell.gridIndex] &= uint8_t(~kGridOpenBit);
        }
        if (base.boxLatchA == 0 && boxWith([](const ActorHeader& b) { return b.state; }) != kBoxes) {
            base.boxLatchA = 1;
            messageTrigger(kMessageTriggerAtOnce, kBoxThrownMessage);
        }
        if (base.boxLatchB == 0 && boxWith([](const ActorHeader& b) { return b.variant; }) == kBoxes) {
            base.boxLatchB = 1;
            countdownTrigger(kBoxesGoneMessage, kBoxesGoneTicks);
        }
        ++base.ticks;
        if (base.ticks != kWalkersHintTick) return;
        size_t i = 0;
        for (; i < kWalkers; ++i) {
            const uint32_t a = base.walkers[i];
            if (a == 0) continue;
            const ActorHeader& walker = actorHeaderAt(a);
            if ((walker.flags & kActorLive) && walker.type == kWalkerType) break;
        }
        if (i != kWalkers) messageTrigger(kMessageTriggerAtOnce, kWalkersLeftMessage);
        return;
    }
    default: return;
    }
}

// ---- ST31 sub 1 ---------------------------------------------------------------------------------------------------
enum class CellStep : uint8_t { Script = 0, Spawn = 1, Fight = 2, Wait = 3, Done = 4 };
struct BirdbotPlace : DataRecord {
    int16_t x = 0;
    int16_t z = 0;
    int16_t yaw = 0;
    static BirdbotPlace load(const TableReader& r) {
        BirdbotPlace rec;
        rec.x = r.s16(0x0);
        rec.z = r.s16(0x2);
        rec.yaw = r.s16(0x4);
        return rec;
    }
};
const DataTable<RecordTable<BirdbotPlace, 3, 6>> cellBirdbotPlaces{TableId::GlydeBaseRoomsCellBirdbotPlaces};
constexpr uint8_t kBirdbotType = 0x2d, kBirdbotSubtype = 1, kBrotherType = 0x71, kBrotherVariant = 0x80;
constexpr size_t kCellBirdbots = 3;
constexpr uint8_t kFreedTicks = 0x28;
constexpr int16_t kFreedMusicFade = 0x199;
constexpr uint32_t kFlagBrotherFreed = 0x670;

void cellRoom() { // exe 0x586220
    RoomScriptState& room = roomScriptState();
    GlydeBaseRoomState& base = glydeBaseRoomState();
    switch (CellStep(room.handlerStep)) {
    case CellStep::Script:
        if (base.scriptLatch != 0) return;
        base.scriptLatch = 1;
        room.handlerStep = uint8_t(CellStep::Spawn);
        scriptRun(kBrotherHeldScript);
        return;
    case CellStep::Spawn:
        if (scriptRuns()) return;
        room.handlerStep = uint8_t(CellStep::Fight);
        for (size_t i = 0; i < kCellBirdbots; ++i) {
            const uint32_t address = allocActor20(); // exe 0x5e1160
            if (address != 0) {
                AnimatedActor& a = animatedActorAt(address);
                const BirdbotPlace place = cellBirdbotPlaces[i];
                a.flags = kActorLiveDrawn;
                a.type = kBirdbotType;
                a.subtype = kBirdbotSubtype;
                a.variant = 0;
                a.pos.y.whole = 0;
                a.pos.x.whole = place.x;
                a.pos.z.whole = place.z;
                a.rotation.y = place.yaw;
            }
            base.cellBirdbots[i] = address;
        }
        if (const uint32_t address = allocActor20()) {
            AnimatedActor& a = animatedActorAt(address);
            a.flags = kActorLiveDrawn;
            a.type = kBrotherType;
            a.subtype = 0;
            a.variant = kBrotherVariant;
            a.pos.x.whole = 0x2d0;
            a.pos.y.whole = 0;
            a.pos.z.whole = 0x2d0;
            a.rotation.y = 0x200;
        }
        [[fallthrough]];
    case CellStep::Fight: {
        size_t i = 0;
        for (; i < kCellBirdbots; ++i) {
            const uint32_t a = base.cellBirdbots[i];
            if (a == 0) continue;
            const ActorHeader& bird = actorHeaderAt(a);
            if ((bird.flags & kActorLive) && bird.type == kBirdbotType && bird.subtype == kBirdbotSubtype) break;
        }
        if (i != kCellBirdbots) return;
        ++room.handlerStep;
        room.handlerByte1 = kFreedTicks;
        soundFadeOut(kMusicFades, kFreedMusicFade, 0);
        return;
    }
    case CellStep::Wait:
        if (player().hitPoints < 0) return;
        --room.handlerByte1;
        if (room.handlerByte1 != 0) return;
        room.handlerStep = uint8_t(CellStep::Done);
        setStoryFlag(kFlagEscape);
        setStoryFlag(kFlagBrotherFreed);
        ++gameState().nextStory; // exe 0x512800
        scriptRun(kBrotherFreedScript);
        return;
    default: return;
    }
}

} // namespace

bool glydeBaseRoomHandler(RoutineId fn, bool run) {
    switch (fn) {
    case kFnEntrance:
        if (run && roomScriptState().handlerStep == 0 && !scriptRuns()) {
            roomScriptState().handlerStep = 1;
            scriptRun(kEntranceScript);
        }
        return true;
    case kFnSub0:
        if (run) sub0Room();
        return true;
    case kFnSub1:
        if (run) sub1Room();
        return true;
    case kFnGate:
        if (run) gateRoom();
        return true;
    case kFnBoxRoom:
        if (run) boxRoom();
        return true;
    case kFnCell:
        if (run) cellRoom();
        return true;
    default: return false;
    }
}

} // namespace od::port
