// The town's room scripts: the handlers of the room scripts' stage cases (exe 0x512890: [[table + story * 4] + sub *
// 4] or [table + sub * 4]) for ST05 / ST06 / ST0A / ST0B / ST0C / ST47, and the whole cases of ST05 / ST06 / ST0B (more
// than "handler + door test"). Their state: the room scripts' handler bytes (+0 the handler's step, +1 a once-per-room
// byte, +3 the licence office's state). One routine per exe routine (addresses in the comments); the dispatch is by
// the handlers' named routines.
#include "content/yosyonke/town_room_cases.hpp"
#include "content/yosyonke/town_room_cases_state.hpp"

#include "content/late_game/exam_loadout.hpp"
#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/breakable_walls.hpp"
#include "game/draw_commands.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/game_state.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/rewards.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/spawn.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::town_room_cases_state;

namespace od::port {

namespace {

uint8_t story() { return gameState().story; }
uint8_t sub() { return gameState().room; }
RoomScriptState& rooms() { return roomScriptState(); }

// a handler's spawns once per room (the handler's step 0 -> 1)
bool firstRun() { return rooms().handlerStep == 0; }
void markRun() { ++rooms().handlerStep; }

// the inline placement of the ST05 records (exe 0x57c2d0's body): the flags, the type word, the parameters, the 16.16
// position from the record's point, the yaw, the roll and the pitch 0
void placeFromRecord(Actor& actor, const SpawnRecord& r) {
    actor.flags = r.flags;
    actor.setTypeWord(r.typeWord);
    actor.setParamsWord(r.paramsWord);
    actor.pos.x = int32_t(uint32_t(int32_t(r.point.x)) << 16);
    actor.pos.y = int32_t(uint32_t(int32_t(r.point.y)) << 16);
    actor.pos.z = int32_t(uint32_t(int32_t(r.point.z)) << 16);
    actor.rotation.y = r.yaw;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
}
// exe 0x57c2d0(record): a static actor (exe 0x5e12f0: pool 0x60 from the stage's table) placed from the record
void staticFromRecord(uint32_t record) {
    if (const uint32_t at = allocActor60(kActorStageClass)) placeFromRecord(staticActorAt(at), dataRecord<SpawnRecord>(record, SpawnRecord::kBytes));
}

// ---- the digger's licence exam office (ST0B; exe 0x57d220 on the handler's byte 3) -----------------------------------
// Arrive: flag 0x246 (back from the exam) -> the life to max + 1 (exe 0x5e5320), the event message 0x5b, 0x246 cleared,
//         Result; else WaitExam
// WaitExam: flag 0x246 and no event -> the exam's loadout (exe 0x542730), 0x24b cleared, the event message 0x6b (param
//         0xb)
// Result: no event -> 0x247 cleared; flag 0x249 (passed) -> the licence class (the difficulty) + 1, its tables
//         (exe 0x50f260); class 2: flag 0x3d6 (0x3d5 / 0x3d7 cleared) + 3000 to the balance, class 3: flag 0x3d5
//         (0x3d6 / 0x3d7 cleared) + 8000; -> WaitExam
enum class OfficeState : uint8_t { Arrive = 0, WaitExam = 1, Result = 2 };
constexpr uint32_t kFlagBackFromExam = 0x246, kFlagExamStarted = 0x247, kFlagExamPassed = 0x249,
                   kFlagExamLoadout = 0x24b;
constexpr uint32_t kFlagClass3Licence = 0x3d5, kFlagClass2Licence = 0x3d6, kFlagLicenceOther = 0x3d7;
constexpr uint16_t kWelcomeBackMessage = 0x5b, kExamMessage = 0x6b;
constexpr uint32_t kExamEventParam = 0xb;
void examOffice() {
    uint8_t& s = rooms().handlerByte3;
    switch (OfficeState(s)) {
    case OfficeState::Arrive:
        if (storyFlag(kFlagBackFromExam)) {
            hitPointsChange(player(), player().maxHitPoints + 1);
            eventStartAt(0, nullptr, 0, 0, kWelcomeBackMessage);
            clearStoryFlag(kFlagBackFromExam);
            s = uint8_t(OfficeState::Result);
        } else {
            ++s;
        }
        return;
    case OfficeState::WaitExam:
        if (!storyFlag(kFlagBackFromExam) || eventState().active != 0) return;
        examLoadoutGive();
        clearStoryFlag(kFlagExamLoadout);
        eventStartAt(0, nullptr, kExamEventParam, 0, kExamMessage);
        return;
    case OfficeState::Result:
        if (eventState().active != 0) return;
        clearStoryFlag(kFlagExamStarted);
        if (storyFlag(kFlagExamPassed)) {
            ++gameState().difficulty;
            selectDifficultyStats(gameState().difficulty);
            if (gameState().difficulty == 2) {
                clearStoryFlag(kFlagLicenceOther);
                setStoryFlag(kFlagClass2Licence);
                clearStoryFlag(kFlagClass3Licence);
                balanceAdd(3000); // exe 0x5e51d0
            } else if (gameState().difficulty == 3) {
                clearStoryFlag(kFlagLicenceOther);
                clearStoryFlag(kFlagClass2Licence);
                setStoryFlag(kFlagClass3Licence);
                balanceAdd(8000);
            }
        }
        s = uint8_t(OfficeState::WaitExam);
        return;
    default: return;
    }
}

// a case's call of a table handler: ours, the rets (exe 0x5d4f30 / 0x52ef00) or the door-only exe 0x582490
void callHandler(const RoutineRef& fn) {
    if (fn.is(RoutineId::EmptyRoomHandler) || fn.is(RoutineId::EmptyRoutine)) return;
    if (fn.is(RoutineId::RoomDoorScript)) {
        roomDoorScript();
        return;
    }
    if (!townRoomCaseHandler(fn.id(), true))
        notPortedAt(kStoryPort, "town room cases: room handler", fn.pointerValue());
}

// ---- ST05 -----------------------------------------------------------------------------------------------------------
// by sub-room (exe table 0x8c51c0: the handlers, named by the source)
const DataTable<RoutineTable<3>> st05Handlers{TableId::TownRoomCasesSt05Handlers};
constexpr uint32_t kFlagEntryList = 0x690;
constexpr uint8_t kEntryListScript = 0x74;
// exe 0x57c0d0 (the case): the first tick in the room the sub-room's handler, after it sub 1's handler (exe 0x57c180,
// which returns at once); then the door (flag 0x700 -> script 0) or flag 0x690 -> script 0x74
void st05Case() {
    if (rooms().handlerByte1 != 0) {
        if (sub() == 1) callHandler(st05Handlers[1]);
    } else {
        callHandler(st05Handlers[sub()]);
        ++rooms().handlerByte1;
    }
    if (roomDoorScript()) return;
    if (storyFlag(kFlagEntryList)) scriptStart(kEntryListScript); // exe 0x5112b0
}
// exe 0x57c140 (sub 0): the statics 0x8c5148 (flag 0x12d) / 0x8c5160 (flag 0x130)
void st05Sub0() {
    if (storyFlag(0x12d)) staticFromRecord(stateAddress(TableId::TownRoomCasesSt05Sub0FirstStatic));
    if (storyFlag(0x130)) staticFromRecord(stateAddress(TableId::TownRoomCasesSt05Sub0SecondStatic));
}
// exe 0x57c180 (sub 1, the first tick only): the statics 0x8c50e8 / 0x8c5100 / 0x8c5118 / 0x8c5130 by the flags
// 0xd8 / 0xd9 / 0xda / 0x12c; with 0x127 and without 0x128 a pool-0x20 actor from the record 0x8c51a8
void st05Sub1() {
    if (rooms().handlerByte1 != 0) return;
    if (storyFlag(0xd8)) staticFromRecord(stateAddress(TableId::TownRoomCasesSt05Sub1FirstStatic));
    if (storyFlag(0xd9)) staticFromRecord(stateAddress(TableId::TownRoomCasesSt05Sub1SecondStatic));
    if (storyFlag(0xda)) staticFromRecord(stateAddress(TableId::TownRoomCasesSt05Sub1ThirdStatic));
    if (storyFlag(0x12c)) staticFromRecord(stateAddress(TableId::TownRoomCasesSt05Sub1FourthStatic));
    if (storyFlag(0x128) || !storyFlag(0x127)) return;
    if (const uint32_t at = allocActor20()) placeFromRecord(animatedActorAt(at), dataRecord<SpawnRecord>(stateAddress(TableId::TownRoomCasesSt05Sub1ActorRecord), SpawnRecord::kBytes)); // exe 0x5e1160
}
// exe 0x57c290 (sub 2): the statics 0x8c5178 (flag 0x12e) / 0x8c5190 (flag 0x12b)
void st05Sub2() {
    if (storyFlag(0x12e)) staticFromRecord(stateAddress(TableId::TownRoomCasesSt05Sub2FirstStatic));
    if (storyFlag(0x12b)) staticFromRecord(stateAddress(TableId::TownRoomCasesSt05Sub2SecondStatic));
}

// ---- ST06 -----------------------------------------------------------------------------------------------------------
// by sub-room (exe table 0x8c51cc: likewise)
const DataTable<RoutineTable<6>> st06Handlers{TableId::TownRoomCasesSt06Handlers};
// exe 0x57c390 (the case): the doors 0x717 / 0x719 locked as flag 0xd1, then the sub-room's handler
void st06Case() {
    if (storyFlag(0xd1)) {
        setStoryFlag(kFlagLocked + 7);
        setStoryFlag(kFlagLocked + 9);
    } else {
        clearStoryFlag(kFlagLocked + 7);
        clearStoryFlag(kFlagLocked + 9);
    }
    callHandler(st06Handlers[sub()]);
}
constexpr uint32_t kFlagSt06Pickup = 0xd3; // its event: the item at the player (with 0xde)
// exe 0x57c3e0 (sub 3): once a room: with flag 0xd5 a static type 1 at (-0xd8, -0x30, -0x88); without 0xd6 the cell
// (0x3f, 0x40) variant 1. Flags 0xd3 and 0xde -> 0xd3 cleared, the item event at the player (message -1). The door:
// back to sub 0xb with flag 0xd3 -> the fade 0x14 first
void st06Sub3() {
    if (rooms().handlerByte1 == 0) {
        if (storyFlag(0xd5)) {
            if (const uint32_t at = allocActor60(kActorStageClass)) { // exe 0x5e12f0
                StaticActor& a = staticActorAt(at);
                a.flags = kActorLive;
                a.setTypeWord(1);
                a.setParamsWord(0);
                a.pos.x = int32_t(0xff280000u);
                a.pos.y = int32_t(0xffd00000u);
                a.pos.z = int32_t(0xff780000u);
                a.rotation.y = 0;
                a.rotation.z = 0;
                a.rotation.x = 0;
            }
        }
        if (!storyFlag(0xd6)) switchGridCellVariant(0x3f, 0x40, 1); // exe 0x58d5f0
        rooms().handlerByte1 = 1;
    }
    if (storyFlag(kFlagSt06Pickup) && storyFlag(0xde)) {
        clearStoryFlag(kFlagSt06Pickup);
        eventStartAtActor(player(), uint32_t(EventMode::ItemGet) << 16, 0, 0xffff);
    }
    if (storyFlag(kFlagTransitionArmed)) {
        if (transitionRequest().exitId == 0xb && storyFlag(kFlagSt06Pickup)) scriptFade(0x14, 0); // exe 0x53f6b0
        scriptRun(0);
    }
}
// exe 0x57c520 (sub 4): once a room without flag 0xd4 the cell (0x40, 0x40) variant 1; the door
void st06Sub4() {
    if (rooms().handlerByte1 == 0) {
        if (!storyFlag(0xd4)) switchGridCellVariant(0x40, 0x40, 1); // exe 0x58d5f0
        ++rooms().handlerByte1;
    }
    roomDoorScript();
}
// exe 0x57c5a0 (sub 5): flag 0xd3 -> script 0x70; else the door
void st06Sub5() {
    if (storyFlag(kFlagSt06Pickup)) {
        scriptRun(0x70);
        return;
    }
    roomDoorScript();
}

// ---- the once-a-room spawns of ST0A / ST0B / ST0C / ST47 -------------------------------------------------------------
void spawnOnce(uint32_t list, uint32_t n) {
    if (!firstRun()) return;
    spawnRecords(list, n);
    markRun();
}
void spawnOnce(uint32_t listA, uint32_t nA, uint32_t listB, uint32_t nB) {
    if (!firstRun()) return;
    spawnRecords(listA, nA);
    spawnRecords(listB, nB);
    markRun();
}

// ---- ST0B: the licence exam office ------------------------------------------------------------------------------------
// by story: the sub-rooms' handlers (exe table 0x8c5b78: a pointer per story 0..0x14 to its two sub-rooms' handlers,
// windows into one list 0x8c5b58; the exe reads [[table + story * 4] + sub * 4] only; named by the source)
struct StoryHandlerTables : DataRecord {
    static constexpr size_t kStories = 0x15, kSubs = 2;
    static constexpr uint32_t kBytes = kStories * 4;
    std::array<std::array<RoutineRef, kSubs>, kStories> handlers{};
    const RoutineRef& handler(size_t story, size_t sub) const {
        if (story >= kStories) tableIndexError(story, kStories);
        if (sub >= kSubs) tableIndexError(sub, kSubs);
        return handlers[story][sub];
    }
    static StoryHandlerTables load(const TableReader& r) {
        StoryHandlerTables t;
        for (size_t s = 0; s < kStories; ++s) {
            const TableReader subs = r.follow(s * 4, kSubs * 4);
            for (size_t k = 0; k * 4 + 4 <= subs.size() && k < kSubs; ++k) t.handlers[s][k] = subs.routine(k * 4);
        }
        return t;
    }
};
const DataTable<StoryHandlerTables> st0bHandlerTables{TableId::TownRoomCasesSt0bHandlerTables};
constexpr uint32_t kFlagOfficeScript = 0x80, kFlagTimeUp = 0x680;
constexpr uint32_t kTimeUpFlashTicks = 0x10;
// exe 0x57d050 (the case): the handler; the door; no event and flag 0x80 -> 0x80 cleared, script 0x27; else the office
// (exe 0x57d220) and the time-up flash: flag 0x680 -> the flash's 0x10 ticks, 0x680 cleared; the flash counts down
// with a fading additive full-screen tint (exe 0x5f3150)
void st0bCase() {
    callHandler(st0bHandlerTables->handler(story(), sub()));
    if (roomDoorScript()) return;
    if (eventState().active == 0 && storyFlag(kFlagOfficeScript)) {
        clearStoryFlag(kFlagOfficeScript);
        scriptRun(0x27);
        return;
    }
    examOffice();
    LicenceOfficeBlock& block = stageBlock().part<LicenceOfficeBlock>();
    if (storyFlag(kFlagTimeUp)) {
        block.timeUpFlash = kTimeUpFlashTicks;
        clearStoryFlag(kFlagTimeUp);
    }
    if (const uint32_t c = block.timeUpFlash) {
        const uint32_t v = c - 1u, x = v * 0xff0u;
        block.timeUpFlash = v;
        dc::screenTint((((x >> 8) | (x & 0xfe00u) | 0x44000000u) >> 1) | ((x & 0xffff00u) << 8)); // exe 0x5f3150
    }
}
// the office's rooms: their spawns once, then the office
void officeRoom(uint32_t listB, uint32_t nB) {
    if (firstRun()) {
        spawnRecords(stateAddress(TableId::TownRoomCasesSt0BOfficeSpawns), 3);
        spawnRecords(listB, nB);
        markRun();
    }
    examOffice();
}

// ---- ST47 -------------------------------------------------------------------------------------------------------------
constexpr uint32_t kFlagSt47Script = 0x680;
// exe 0x58ac10 (stories 2..0xe sub 5 / 0xf.. sub 2): the records 0x8ccd78 x 2 once; then (no event) flag 0x680 ->
// script 0x73, 0x680 cleared
void st47LastRoom() {
    if (rooms().handlerStep == 0) {
        spawnRecords(stateAddress(TableId::St47RoomsSub2Record), 2);
        markRun();
    } else if (rooms().handlerStep == 1 && eventState().active == 0 && storyFlag(kFlagSt47Script)) {
        scriptRun(0x73);
        clearStoryFlag(kFlagSt47Script);
    }
}

} // namespace

bool townRoomCaseHandler(RoutineId fn, bool run) {
    switch (fn) {
    // ---- ST05 (the case 0x57c0d0, the table 0x8c51c0 by sub-room) ----
    case RoutineId::St05RoomHandler:
        if (run) st05Case();
        return true;
    case RoutineId::St05RoomHandlerA:
        if (run) st05Sub0();
        return true;
    case RoutineId::St05RoomHandlerB:
        if (run) st05Sub1();
        return true;
    case RoutineId::St05RoomHandlerC:
        if (run) st05Sub2();
        return true;
    // ---- ST06 (the case 0x57c390, the table 0x8c51cc by sub-room) ----
    case RoutineId::St06RoomHandler:
        if (run) st06Case();
        return true;
    case RoutineId::St06RoomHandlerA:
        if (run) st06Sub3();
        return true;
    case RoutineId::St06RoomHandlerB:
        if (run) st06Sub4();
        return true;
    case RoutineId::St06RoomHandlerC:
        if (run) st06Sub5();
        return true;
    // ---- ST0A (the case 0x57cce0, the table 0x8c5818 by story) ----
    case RoutineId::St0AStory0Sub3RoomHandler: // story 0 sub 3: the record 0x8c58d8, once
    case RoutineId::St0ARoomHandlerE: // story 1 sub 8 / later sub 3: the same
        if (run) spawnOnce(stateAddress(TableId::TownRoomCasesSt0ASub3Spawns), 1);
        return true;
    case RoutineId::St0ARoomHandlerF: // story 1 sub 9 / later sub 4: the records 0x8c58f0 x 1 + 0x8c5948 x 3, once
        if (run) spawnOnce(stateAddress(TableId::TownRoomCasesSt0ASub4Spawns), 1, stateAddress(TableId::TownRoomCasesSt0ASub4MoreSpawns), 3);
        return true;
    // ---- ST0B (the case 0x57d050, the table 0x8c5b78 by story): the licence exam office ----
    case RoutineId::St0BRoomHandler:
        if (run) st0bCase();
        return true;
    case RoutineId::St0BStory0To1Sub0RoomHandler: // story 0..1 sub 0: the record 0x8c5bd0, once
        if (run) spawnOnce(stateAddress(TableId::TownRoomCasesSt0BEntranceSpawns), 1);
        return true;
    case RoutineId::St0BStory0To1Sub1RoomHandler:
        // story 0..1 sub 1: the records 0x8c5bf8 x 3 + 0x8c5c38 x 6 once; the office (exe 0x57d220)
    case RoutineId::St0BRoomHandlerA: // story 0..1 sub 3 / 2..0xf sub 1: the same
        if (run) officeRoom(stateAddress(TableId::TownRoomCasesSt0BOfficeEarlySpawns), 6);
        return true;
    case RoutineId::St0BRoomHandlerB: // story 0..1 sub 2 / 2..0xf sub 0: the records 0x8c5bd0 x 2, once
    case RoutineId::St0BRoomHandlerC: // sub 4 / 2..0xf sub 2 / 0x10.. sub 0: the same
        if (run) spawnOnce(stateAddress(TableId::TownRoomCasesSt0BEntranceSpawns), 2);
        return true;
    case RoutineId::St0BRoomHandlerD:
        // sub 5 / 2..0xf sub 3 / 0x10.. sub 1: the records 0x8c5bf8 x 3 + 0x8c5cb0 x 7 once; the office
        if (run) officeRoom(stateAddress(TableId::TownRoomCasesSt0BOfficeLaterSpawns), 7);
        return true;
    // ---- ST0C (the case 0x57d410, the table 0x8c5f34 by story) ----
    case RoutineId::St0CStory0To1Sub0RoomHandler: // story 0..1 sub 0: the record 0x8c5f88, once
    case RoutineId::St0CRoomHandler: // story 0..1 sub 3 / later sub 0: the same
        if (run) spawnOnce(stateAddress(TableId::TownRoomCasesSt0CSub0Spawns), 1);
        return true;
    case RoutineId::St0CStory0To1Sub1RoomHandler: // story 0..1 sub 1: the records 0x8c5fa0 x 2 + 0x8c5ff0 x 3, once
    case RoutineId::St0CRoomHandlerA: // story 0..1 sub 4 / later sub 1: the same
        if (run) spawnOnce(stateAddress(TableId::TownRoomCasesSt0CSub1Spawns), 2, stateAddress(TableId::TownRoomCasesSt0CSub1MoreSpawns), 3);
        return true;
    case RoutineId::St0CStory0To1Sub2RoomHandler: // story 0..1 sub 2: the records 0x8c5fc8 x 1 + 0x8c6030 x 3, once
        if (run) spawnOnce(stateAddress(TableId::TownRoomCasesSt0CSub2Spawns), 1, stateAddress(TableId::TownRoomCasesSt0CSub2MoreSpawns), 3);
        return true;
    case RoutineId::St0CRoomHandlerB: // story 0..1 sub 5 / later sub 2: the records 0x8c5fc8 x 2 + 0x8c6030 x 3, once
        if (run) spawnOnce(stateAddress(TableId::TownRoomCasesSt0CSub2Spawns), 2, stateAddress(TableId::TownRoomCasesSt0CSub2MoreSpawns), 3);
        return true;
    // ---- ST47 (the case 0x58a9e0, the table 0x8cccf4 by story) ----
    case RoutineId::St47RoomHandlerD: // story 0 sub 7 / 1 sub 4 / 2..0xe sub 1: the record 0x8ccd60, once
        if (run) spawnOnce(stateAddress(TableId::St47RoomsSub1Record), 1);
        return true;
    case RoutineId::St47RoomHandlerE: // story 0 sub 8 / 1 sub 5 / 2..0xe sub 2: the records 0x8ccd78 x 3, once
        if (run) spawnOnce(stateAddress(TableId::St47RoomsSub2Record), 3);
        return true;
    case RoutineId::St47RoomHandlerF:
        // story 1 sub 4 / 2..0xe sub 3 / 0xf.. sub 0: the record 0x8ccd48 + the records 0x8ccdb8 x 3, once
        if (run) spawnOnce(stateAddress(TableId::St47RoomsLightRecord), 1, stateAddress(TableId::St47RoomsCastRecords), 3);
        return true;
    case RoutineId::St47RoomHandlerG: // story 1 sub 5 / 2..0xe sub 4 / 0xf.. sub 1: the record 0x8ccd60, once
        if (run) spawnOnce(stateAddress(TableId::St47RoomsSub1Record), 1);
        return true;
    case RoutineId::St47RoomHandlerH:
        if (run) st47LastRoom();
        return true;
    default: return false;
    }
}

// the handler the ST0B case calls directly kHandlersTranscribed: 0x57d220

} // namespace od::port
