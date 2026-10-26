// The stage and room setup's template (see stage_setup.hpp).
#include "game/stage_setup.hpp"

#include <cstdio>

#include "game/actors.hpp"
#include "game/audio_logic.hpp"
#include "game/content_registry.hpp"
#include "game/encounter.hpp"
#include "game/game_state.hpp"
#include "game/stage_globals.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/room_scripts.hpp"
#include "game/spawn.hpp"
#include "game/sky_layers.hpp"
#include "game/stage_state.hpp"
#include "game/territory.hpp"
#include "game/transition_state.hpp"
#include "game/visited_rooms.hpp"

namespace od::port {

namespace {

// ---- the generated tables ---------------------------------------------------------
struct StageWrite {
    uint8_t stage;
    StageGlobal global;
    RoutineId routine = RoutineId::None; // a callback written: its routine (None: 0)
};
struct StageSetupEntry {
    RoutineId fn;
    uint32_t first, count;
    int32_t kinds; // exe 0x50f280's argument (-1: none)
    uint32_t branches, opaque, flags, late;
};
struct RoomSetupEntry {
    RoutineId fn;
    bool exits; // the room function sets the room's exit table (TableId::RoomExits)
    int32_t weapon;
    bool plain;
    uint32_t helperCount;
    RoutineId helper0, helper1;
};
#include "game/stage_setup_tables.inc"
constexpr int kStages = int(sizeof kStageSetup / sizeof kStageSetup[0]);
static_assert(sizeof kRoomSetup / sizeof kRoomSetup[0] == 93, "93 stages");
constexpr uint32_t kTableHelpers = 2; // the helper columns of the table (a function calling more: its stage's parts)

constexpr const char* kStageSetupPort = "stage setup"; // the area of its port-gap errors

// the stage's table a global the stage setup points at a table names (the data source's per-stage tables)
TableId stageTableOf(StageGlobal global) {
    switch (global) {
    case StageGlobal::AnimatedClasses: return TableId::StageAnimatedClasses;
    case StageGlobal::StaticClasses: return TableId::StageStaticClasses;
    case StageGlobal::ProjectileClasses: return TableId::StageProjectileClasses;
    case StageGlobal::EffectClasses: return TableId::StageEffectClasses;
    case StageGlobal::ControllerClasses: return TableId::StageControllerClasses;
    case StageGlobal::EncounterZoneTables: return TableId::StageEncounterZones;
    case StageGlobal::EncounterGroupTables: return TableId::StageEncounterGroups;
    case StageGlobal::EncounterFieldTable: return TableId::StageEncounterField;
    case StageGlobal::EncounterEnemyGroups: return TableId::StageEncounterEnemyGroups;
    case StageGlobal::ClutAnimations: return TableId::StageClutAnimations;
    case StageGlobal::GroundKinds: return TableId::StageGroundKinds;
    case StageGlobal::TownRoutes: return TableId::StageTownRoutes;
    case StageGlobal::TransitionHandlers: return TableId::StageTransitionHandlers;
    case StageGlobal::StageVoices: return TableId::StageVoices;
    case StageGlobal::SecondVoices: return TableId::StageSecondVoices;
    default: notPortedAt(kStageSetupPort, "a stage setup write of a global that names no table", uint32_t(global));
    }
}
// The setups also write nulls to clear optional globals. The source distinguishes a stage with no table from a
// required table with no placement (an error), so only a genuine absence becomes zero here.
uint32_t stageTableValue(StageGlobal global, int stage) {
    const TableKey key{stageTableOf(global), uint32_t(stage)};
    return dataSource().hasTable(key) ? dataSource().stateAddress(key) : 0;
}
// a global the stage setups write: its field written (a table as the value the state keeps for it, a callback as its
// routine, the stage block's head 0)
void writeStageGlobal(StageGlobal global, int stageIndex, RoutineId routine) {
    StageClassTables& classes = stageClassTables();
    StageState& st = stage();
    EncounterState& e = encounterState();
    VoiceTables& voices = voiceTables();
    const auto value = [&] { return stageTableValue(global, stageIndex); };
    switch (global) {
    case StageGlobal::None: notPortedAt(kStageSetupPort, "a stage setup write of no global", uint32_t(stageIndex));
    case StageGlobal::AnimatedClasses: classes.animated.pointAt(value()); break;
    case StageGlobal::StaticClasses: classes.statics.pointAt(value()); break;
    case StageGlobal::ProjectileClasses: classes.projectiles.pointAt(value()); break;
    case StageGlobal::EffectClasses: classes.effects.pointAt(value()); break;
    case StageGlobal::ControllerClasses: classes.controllers.pointAt(value()); break;
    case StageGlobal::EncounterZoneTables: e.zoneTables = value(); break;
    case StageGlobal::EncounterGroupTables: e.groupTables = value(); break;
    case StageGlobal::EncounterFieldTable: e.fieldTable = value(); break;
    case StageGlobal::EncounterEnemyGroups: e.enemyGroups = value(); break;
    case StageGlobal::ClutAnimations: st.clutAnimations = value(); break;
    case StageGlobal::GroundKinds: st.groundKinds = value(); break;
    case StageGlobal::TownRoutes: st.townRoutes = value(); break;
    case StageGlobal::TransitionHandlers: st.transitionHandlers.pointAt(value()); break;
    case StageGlobal::StageVoices: voices.stage = value(); break;
    case StageGlobal::SecondVoices: voices.second = value(); break;
    case StageGlobal::Script8Callback: st.script8Callback = routine; break;
    case StageGlobal::Script30Callback: st.script30Callback = routine; break;
    case StageGlobal::RoomCallback: st.roomCallback = routine; break;
    case StageGlobal::SecondRoomCallback: st.secondRoomCallback = routine; break;
    case StageGlobal::StageBlockHead: stageBlock().part<StageBlockHead>().word = 0; break; // the setups write 0
    }
}
// the setup's writes of a stage, in the exe's order, `kinds` (exe 0x50f280) called right after the ground kinds' write
// (-1: none)
void stageWrites(const StageSetupEntry& s, int32_t (*kinds)()) {
    for (uint32_t i = 0; i < s.count; ++i) {
        const StageWrite& w = kStageWrites[s.first + i];
        writeStageGlobal(w.global, w.stage, w.routine);
        if (kinds && w.global == StageGlobal::GroundKinds) {
            const int32_t k = kinds();
            if (k >= 0) encounterSlotsSetup(uint32_t(k));
        }
    }
}

// ---- the room helpers ----------------------------------------------------------------------------------------------

// the template helpers: the island has no room list (exe 0x57bb70: the room-script handler's word kept; 0x57eca0:
// cleared first)
constexpr RoutineId kNoListHelper = RoutineId::NoRoomListHelper, kNoListClearHelper = RoutineId::NoRoomListClearHelper;
// what the table's helper columns also hold besides helpers: the room's UV-animation list (exe 0x5f5bd0: run by the
// stage's part) and the inline story-flag calls (exe 0x510e50 / 0x510e30: inside the stage's part)
constexpr RoutineId kUvAnimationListRoutine = RoutineId::UvAnimationListSetup,
                    kFlagTestRoutine = RoutineId::StoryFlagTestCall, kFlagClearRoutine = RoutineId::StoryFlagClearCall;
// a helper column's entry that is one of those routines (not a room helper: skipped by the helper loop)
bool notAHelper(RoutineId fn) {
    return fn == kUvAnimationListRoutine || fn == kFlagTestRoutine || fn == kFlagClearRoutine;
}
// a room helper run (run = false: whether it is transcribed): the template's, then the stages' (the content's)
bool roomHelper(RoutineId fn, bool run) {
    if (fn == kNoListHelper || fn == kNoListClearHelper) {
        if (!run) return true;
        if (fn == kNoListClearHelper) roomScriptState().clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        return true;
    }
    for (RoomHandlerFn helper : content().roomHelpers)
        if (helper(fn, run)) return true;
    return false;
}
// the stage's room function beyond the template (null: none)
const RoomFunctionParts* roomFunction(int stage) {
    const auto& parts = content().roomFunctions;
    const auto it = parts.find(uint8_t(stage));
    return it == parts.end() ? nullptr : &it->second;
}
// the stage's setup entry's helper column i (0 or 1)
RoutineId tableHelper(const RoomSetupEntry& r, uint32_t i) { return i == 0 ? r.helper0 : r.helper1; }

} // namespace

SpawnCoverage spawnCoverage() {
    SpawnCoverage c;
    const int st = gameState().stage;
    if (st >= kStages) {
        c.why = "stage out of range";
        return c;
    }
    const StageSetupEntry& s = kStageSetup[st];
    c.stage = (s.late == 0 && s.opaque == 0 && s.kinds >= 0 && s.flags == 0) || !encounterStageRest(st).empty() ||
              content().stageEncounterKinds.count(uint8_t(st)) != 0;
    const RoomSetupEntry& r = kRoomSetup[st];
    c.room = true;
    char b[160];
    for (uint32_t i = 0; i < r.helperCount && i < kTableHelpers; ++i) {
        const RoutineId h = tableHelper(r, i);
        if (!roomHelper(h, false) && !notAHelper(h)) {
            std::snprintf(b, sizeof b, "room helper %06x ", routineStateValue(h));
            c.why += b;
            c.room = false;
        }
    }
    const RoomFunctionParts* parts = roomFunction(st);
    if (r.helperCount > kTableHelpers && !parts) {
        c.why += "room function calls more helpers ";
        c.room = false;
    }
    if (!r.plain && !parts) {
        c.why += "room function has code beyond the template ";
        c.room = false;
    }
    c.roomScript = roomScriptCovered(c.why);
    if (!c.stage) c.why += "stage setup has branches / computed writes ";
    return c;
}

void stageSetup() {
    const int st = gameState().stage;
    stageBlock().newStage();
    if (st >= kStages) notPortedAt(kStageSetupPort, "stage", uint32_t(st));
    const StageSetupEntry& s = kStageSetup[st];
    if (const auto rest = encounterStageRest(st); !rest.empty()) {
        // the four sub-ruin setups (ST4D / 4E / 4F / 50) that draw their encounter layout with the RNG
        // (game/encounter): the dword writes, the layout (flags, RNG, zone / group tables), then exe 0x50f280; their
        // music calls are game/stage_music's (as for the other stages)
        for (const EncounterRest& r : rest)
            if (r.kind == EncounterRestKind::Write)
                writeStageGlobal(r.global, st, r.routine);
        encounterStageLayout(st);
        for (const EncounterRest& r : rest)
            if (r.kind == EncounterRestKind::Call && r.routine == kEncounterSlotsSetupRoutine)
                encounterSlotsSetup(r.a0);
        return;
    }
    if (const auto it = content().stageEncounterKinds.find(uint8_t(st)); it != content().stageEncounterKinds.end()) {
        stageWrites(s, it->second); // the stage's own branch around exe 0x50f280 (ST0E, ST48)
        return;
    }
    if (s.late || s.opaque || s.kinds < 0 || s.flags)
        notPortedAt(kStageSetupPort, "stage setup with branches", routineStateValue(s.fn));
    stageWrites(s, nullptr);
    encounterSlotsSetup(uint32_t(s.kinds));
}

void roomSetupActors(int32_t param) {
    (void)param; // param >= 0: the room camera's set up too (the host's)
    const GameState& g = gameState();
    const int st = g.stage;
    if (st >= kStages) notPortedAt(kStageSetupPort, "stage", uint32_t(st));
    const RoomSetupEntry& r = kRoomSetup[st];
    skyLayersSetup();
    const uint32_t room = roomIndex(st, g.room);
    spawnList(dataScalar<uint32_t>(dataSource().stateAddress({TableId::RoomSpawnList, room})),
              int32_t(int8_t(transitionRequest().keepPosition)));
    for (uint32_t i = 0; i < r.helperCount && i < kTableHelpers; ++i) {
        const RoutineId h = tableHelper(r, i);
        if (notAHelper(h)) continue;
        if (!roomHelper(h, true)) notPortedAt(kStageSetupPort, "room helper", routineStateValue(h));
    }
    encounterSlotsEnterRoom(g.room);
    const RoomFunctionParts* parts = roomFunction(st);
    if (parts && parts->afterEncounterSlots) parts->afterEncounterSlots();
    if (!r.plain && !parts) notPortedAt(kStageSetupPort, "room function beyond the template", routineStateValue(r.fn));
    territory().table = 0;
    if (r.exits) stage().exitTable = dataScalar<uint32_t>(dataSource().stateAddress({TableId::RoomExits, room})); // the sub's exits
    if (parts && parts->weaponMode) parts->weaponMode();
    else if (r.weapon >= 0) weaponMode(int8_t(r.weapon)); // the constant argument of the other room functions
    else notPortedAt(kStageSetupPort, "room weapon mode (variable)", routineStateValue(r.fn));
    if (parts && parts->tail) parts->tail();
}

} // namespace od::port
