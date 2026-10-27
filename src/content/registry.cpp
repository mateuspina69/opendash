// The composition of the content: every area's actor classes, and the chains the game systems dispatch through
// (game/content_registry). The chain orders are the ones the hand-written dispatch had in game/story and the stage
// setup before the registry: the first module that handles a call wins.
#include "content/registry.hpp"

#include "content/calbania/calbania_rooms.hpp"
#include "content/calbania/ch3_story.hpp"
#include "content/calbania/st24_script56.hpp"
#include "content/calbania/st32_quiz.hpp"
#include "content/calbania/st32_rooms.hpp"
#include "content/forbidden_island/elephant_boss.hpp"
#include "content/forbidden_island/island_rooms.hpp"
#include "content/forbidden_island/island_visit.hpp"
#include "content/forbidden_island/st11_script2a.hpp"
#include "content/forbidden_island/st1c_script13.hpp"
#include "content/forbidden_island/st3e_reports.hpp"
#include "content/forbidden_island/st49_landing.hpp"
#include "content/intro/crash_landing.hpp"
#include "content/intro/fire_mission.hpp"
#include "content/intro/intro_rooms.hpp"
#include "content/intro/opening.hpp"
#include "content/intro/st39_script68.hpp"
#include "content/intro/st4a_rooms.hpp"
#include "content/intro/attract.hpp"
#include "content/late_game/ch4_story.hpp"
#include "content/late_game/end_scripts.hpp"
#include "content/late_game/exam_loadout.hpp"
#include "content/late_game/late_rooms.hpp"
#include "content/late_game/st4b_scripts.hpp"
#include "content/late_game/st4b_st4c_rooms.hpp"
#include "content/late_game/st4c_story.hpp"
#include "content/late_game/st53_story.hpp"
#include "content/late_game/st54_clear_screen.hpp"
#include "content/late_game/st59.hpp"
#include "content/manda/manda_rooms.hpp"
#include "content/manda/manda_ruin_scripts.hpp"
#include "content/manda/pokte_rooms.hpp"
#include "content/manda/st24_ruin_gate.hpp"
#include "content/manda/st25_tron_scenes.hpp"
#include "content/nino/ch2_rooms.hpp"
#include "content/nino/ch2_scripts.hpp"
#include "content/nino/nino_rooms.hpp"
#include "content/nino/st35_rooms.hpp"
#include "content/nino/st35_scripts.hpp"
#include "content/nino/st36_rooms.hpp"
#include "content/nino/st37_rooms.hpp"
#include "content/nino/st37_scripts.hpp"
#include "content/nino/st38_rooms.hpp"
#include "content/nino/st15_st16_rooms.hpp"
#include "content/nino/st16_script33.hpp"
#include "content/saul_kada/saul_kada_rooms.hpp"
#include "content/saul_kada/fire_ruin_depths_scripts.hpp"
#include "content/saul_kada/fire_ruin_entrance_scripts.hpp"
#include "content/saul_kada/fire_ruin_rooms.hpp"
#include "content/saul_kada/fire_ruin_visit_scripts.hpp"
#include "content/saul_kada/pirate_base_rooms.hpp"
#include "content/saul_kada/pirate_base_scripts.hpp"
#include "content/saul_kada/saul_kada_town_rooms.hpp"
#include "content/saul_kada/st33_key_script.hpp"
#include "content/saul_kada/st48_sand_script.hpp"
#include "content/saul_kada/st41_st44_rooms.hpp"
#include "content/subruins/st51_rooms.hpp"
#include "content/subruins/st58_rooms.hpp"
#include "content/subruins/subruin_rooms.hpp"
#include "content/yosyonke/arrival.hpp"
#include "content/yosyonke/dev_room.hpp"
#include "content/yosyonke/minimaps.hpp"
#include "content/calbania/minimaps.hpp"
#include "content/forbidden_island/minimaps.hpp"
#include "content/late_game/minimaps.hpp"
#include "content/manda/minimaps.hpp"
#include "content/nino/minimaps.hpp"
#include "content/saul_kada/minimaps.hpp"
#include "content/subruins/minimaps.hpp"
#include "content/yosyonke/ice_ruin_room_helpers.hpp"
#include "content/yosyonke/ice_ruin_rooms.hpp"
#include "content/yosyonke/ice_ruin_scripts.hpp"
#include "content/yosyonke/abandoned_mine_exit.hpp"
#include "content/yosyonke/abandoned_mine_rooms.hpp"
#include "content/yosyonke/abandoned_mine_scripts.hpp"
#include "content/yosyonke/st05_entry_list.hpp"
#include "content/yosyonke/st47_rooms.hpp"
#include "content/yosyonke/town_room_cases.hpp"
#include "content/yosyonke/town_rooms.hpp"
#include "content/yosyonke/town_side_scripts.hpp"
#include "content/yosyonke/town_story.hpp"

#include "game/encounter.hpp"
#include "game/story.hpp"

namespace od::content {

using namespace od::port;

namespace {

// a script module keyed by the script's handler (game/story storyScriptHandler) instead of its id
template <bool (*Ported)(RoutineId), bool (*Tick)(const PlayerEnv&, RoutineId)>
ScriptModule byHandler() {
    return {[](uint8_t id) { return Ported(storyScriptHandler(id).id()); },
            [](const PlayerEnv& env, uint8_t id) { return Tick(env, storyScriptHandler(id).id()); }};
}

void registerStory(ContentRegistry& r) {
    r.scripts = {
        byHandler<&openingScriptPorted, &openingScriptTick>(),             // script 5 (intro/opening_script)
        byHandler<&missionResultScriptPorted, &missionResultScriptTick>(), // script 0xd (intro/mission_result)
        byHandler<&crashLandingScriptPorted, &crashLandingScriptTick>(),   // script 6 (intro/crash_landing_script)
        byHandler<&arrivalScriptPorted, &arrivalScriptTick>(),             // scripts 0x55 / 0x4c (yosyonke/arrival)
        byHandler<&abandonedMineExitScriptPorted, &abandonedMineExitScriptTick>(),     // script 0x12 (yosyonke/abandoned_mine_exit)
        byHandler<&attractScriptPorted, &attractScriptTick>(),   // scripts 3 / 4 (intro/attract)
        byHandler<&elephantScriptPorted, &elephantScriptTick>(), // forbidden_island/elephant_boss
        byHandler<&st1cScriptPorted, &st1cScriptTick>(),         // script 0x13 (forbidden_island/st1c_script13)
        byHandler<&st3eReportScriptPorted, &st3eReportScriptTick>(), // 0x64..0x67 (forbidden_island/st3e_reports)
        byHandler<&ch2ScriptPorted, &ch2ScriptTick>(),           // nino/ch2_scripts
        byHandler<&ch3ScriptPorted, &ch3ScriptTick>(),           // calbania/ch3_story
        byHandler<&ch4ScriptPorted, &ch4ScriptTick>(),           // late_game/ch4_story
        {&st53ScriptPorted, &st53ScriptTick},                    // late_game/st53_story
        {&st4cScriptPorted, &st4cScriptTick},                    // late_game/st4c_story
        // script 0x76 before the town's side scripts: its tick clears its screen sprites on every call that reaches
        // it (whichever script runs), as the one module of these five scripts did
        {&clearScreenScriptPorted, &clearScreenScriptTick},      // script 0x76 (late_game/st54_clear_screen)
        {&townSideScriptPorted, &townSideScriptTick},            // 0x27 / 0x70 / 0x72 / 0x73 (yosyonke/town_side_scripts)
        {&st39Script68Ported, &st39Script68Tick},                // 0x68 (intro/st39_script68)
        {&st4bScriptPorted, &st4bScriptTick},                    // 0x5a / 0x5b (late_game/st4b_scripts)
        {&st24Script56Ported, &st24Script56Tick},                // 0x56 (calbania/st24_script56)
        byHandler<&iceRuinScriptPorted, &iceRuinScriptTick>(),   // 0x48 / 0x49 / 0x4a / 0x4b (yosyonke/ice_ruin_scripts)
        {&st16Script33Ported, &st16Script33Tick},                // 0x33 (nino/st16_script33)
        {&st11Script2aPorted, &st11Script2aTick},                // 0x2a (forbidden_island/st11_script2a)
        byHandler<&st32QuizScriptPorted, &st32QuizScriptTick>(), // 0x2b (calbania/st32_quiz)
        byHandler<&st05EntryListScriptPorted, &st05EntryListScriptTick>(), // 0x74 (yosyonke/st05_entry_list)
        byHandler<&st37ScriptPorted, &st37ScriptTick>(),         // nino/st37_scripts
        byHandler<&ninoRuinSt35ScriptPorted, &ninoRuinSt35ScriptTick>(), // nino/st35_scripts
        byHandler<&st48SandScriptPorted, &st48SandScriptTick>(), // 0x58 (saul_kada/st48_sand_script)
        byHandler<&keyDropScriptPorted, &keyDropScriptTick>(),   // 0x2f (saul_kada/st33_key_script)
        byHandler<&pirateBaseScriptsPorted, &pirateBaseScriptsTick>(), // 0x5c / 0x5d / 0x5e (saul_kada/pirate_base_scripts)
        byHandler<&fireRuinVisitScriptsPorted, &fireRuinVisitScriptsTick>(), // 0x2e / 0x63 / 0x2d (saul_kada/fire_ruin_visit_scripts)
        byHandler<&fireRuinEntranceScriptsPorted, &fireRuinEntranceScriptsTick>(), // saul_kada/fire_ruin_entrance_scripts
        byHandler<&fireRuinDepthsScriptsPorted, &fireRuinDepthsScriptsTick>(),     // saul_kada/fire_ruin_depths_scripts
        byHandler<&st4bScript59Ported, &st4bScript59Tick>(),     // script 0x59 (late_game/st4b_st4c_rooms)
        byHandler<&endScriptPorted, &endScriptTick>(),           // 0x75 / 0x6d / 0x3f / 0x6f / 0x6e (late_game/end_scripts)
        {&townStoryScriptPorted, &townStoryScriptTick},          // yosyonke/town_story
        byHandler<&abandonedMineScriptPorted, &abandonedMineScriptTick>(),   // scripts 0x50..0x54 (yosyonke/abandoned_mine_scripts)
        byHandler<&st49ScriptPorted, &st49ScriptTick>(),         // script 0x18 (forbidden_island/st49_landing)
        byHandler<&st25TronScriptPorted, &st25TronScriptTick>(), // 0x1b / 0x1c (manda/st25_tron_scenes)
        byHandler<&st24GateScriptPorted, &st24GateScriptTick>(), // 0x14 (manda/st24_ruin_gate)
        byHandler<&mandaRuinScriptPorted, &mandaRuinScriptTick>(), // 9 / 0xa / 0xb / 0xc / 0xe / 0xf / 0x1a (manda/manda_ruin_scripts)
    };
    r.roomCases = {
        &attractRoomCase,      // ST02 / ST03 (intro/attract)
        &openingRoomCase,      // ST39 (intro/opening_script)
        &crashLandingRoomCase, // ST3A (intro/crash_landing_script)
        &fireMissionRoomCase,  // ST1E (intro/fire_mission_rooms)
        &st1cRoomCase,         // ST1C (forbidden_island/st1c_script13)
        &ch2RoomCase,          // ST15 / ST16
        &st49RoomCase,         // ST49 (forbidden_island/st49_landing)
    };
    r.roomCasesKnown = {&openingRoomCaseKnown, &crashLandingRoomCaseKnown, &fireMissionRoomCaseKnown,
                        &st49RoomCaseKnown,    &st1cRoomCaseKnown,         &ch2RoomCaseKnown};
}

// the story chapters' room handlers / tails and stage cases; it adds to the chains and maps the areas' room
// registrations add to too, so the order of the two does not matter
void registerStageSetup(ContentRegistry& r) {
    r.roomHandlers.insert(r.roomHandlers.end(), {
        &townRoomHandler,         // yosyonke/town_story
        &pokteRoomHandler,        // ST24 / ST25 (manda/pokte_rooms)
        &mandaRuinRoomHandler,    // ST12 / ST13 / ST14 (manda/manda_rooms)
        &st51RoomHandler,         // ST51 (subruins/st51_rooms)
        &subruinLockRoomHandler,  // the sub-ruins' lock handler, ST14's too (subruins/subruin_rooms)
        &islandVisitRoomHandler,  // forbidden_island/island_visit
        &ch2RoomHandler,          // nino/ch2_rooms
        &ch3RoomHandler,          // calbania/ch3_story
        &ch4RoomHandler,          // late_game/ch4_story
        &st53RoomHandler,         // late_game/st53_story
        &ninoRuinSt35RoomHandler, // nino/st35_rooms
        &ninoSt36RoomHandler,     // nino/st36_rooms
        &st37RoomHandler,         // nino/st37_rooms
        &nino38RoomHandler,       // nino/st38_rooms
        &saulKadaTownRoomHandler, // ST29 / 2A / 2B / 2C / 48 (saul_kada/saul_kada_town_rooms)
        &fireRuinRoomHandler,     // ST26 / 27 / 28 / 33 / 34 (saul_kada/fire_ruin_rooms)
        &pirateBaseRoomHandler,   // ST3B / ST3C (saul_kada/pirate_base_rooms)
        &townRoomCaseHandler,     // ST05 / ST06 / ST0A / ST0B / ST0C / ST47 (yosyonke/town_room_cases)
        &st15St16RoomHandler,     // ST15 / ST16 (nino/st15_st16_rooms)
        &iceRuinRoomHandler,      // ST2D / ST2E / ST2F (yosyonke/ice_ruin_rooms)
        &st32RoomHandler,         // ST32 (calbania/st32_rooms)
        &st41St44RoomHandler,     // ST41 / ST42 / ST44 (saul_kada/st41_st44_rooms)
        &st4aRoomHandler,         // ST4A (intro/st4a_rooms)
        &st4bSt4cRoomHandler,     // ST4B / ST4C (late_game/st4b_st4c_rooms)
        &st58RoomHandler,         // ST58 (subruins/st58_rooms)
    });
    r.roomHelpers.push_back(&subruinRoomHelper); // the sub-ruins ST4D / 4E / 4F / 50 (subruins/subruin_rooms)
    r.roomCaseTails[0x35] = &ninoRuinSt35RoomTail; // FUN_00586950's pace part (nino/st35_rooms)
    r.roomCaseTails[0x36] = &ninoSt36RoomCaseTail; // FUN_00586d80's flag-0x90 part (nino/st36_rooms)
    r.roomCaseTails[0x37] = &st37RoomCaseTail;     // FUN_00587120 after the handler (nino/st37_rooms)
    r.roomCaseTails[0x38] = &nino38RoomCaseTail;   // FUN_00587800's flag-0x92 part (nino/st38_rooms)
    r.roomCaseTails[0x29] = &saulKadaTownRoomCaseTail; // 0x583730's script-8 tail (saul_kada/saul_kada_town_rooms)
    // the stage cases with more than "handler + door test" (yosyonke/town_room_cases: ST05 0x57c0d0, ST06 0x57c390, ST0B
    // 0x57d050; nino/st15_st16_rooms: ST15 0x57eeb0, ST16 0x57f040: their own handlers, door test and tails), ST5C
    // 0x58cb10 (late_game/end_scripts) and the time-trial courses 0x58ca30 (late_game/st59)
    r.stageCases[0x05] = []() { return townRoomCaseHandler(RoutineId::St05RoomHandler, true); };
    r.stageCases[0x06] = []() { return townRoomCaseHandler(RoutineId::St06RoomHandler, true); };
    r.stageCases[0x0b] = []() { return townRoomCaseHandler(RoutineId::St0BRoomHandler, true); };
    r.stageCases[0x15] = []() { return st15St16RoomHandler(RoutineId::St15RoomHandler, true); };
    r.stageCases[0x16] = []() { return st15St16RoomHandler(RoutineId::St16RoomHandler, true); };
    r.stageCases[0x5c] = []() { st5cStageCase(); return true; };
    r.stageCases[0x59] = []() { st59StageCase(); return true; };
    r.stageCases[0x5a] = []() { st59StageCase(); return true; };
    r.stageCases[0x5b] = []() { st59StageCase(); return true; };
    r.subruinRoomScripts.known = &subruinRoomScriptsKnown;
    r.subruinRoomScripts.run = &subruinRoomScripts;
}

void registerCalls(ContentRegistry& r) {
    r.scriptWhiteScreens = {
        &st4bScriptWhiteScreen,   // script 0x5b (late_game/st4b_scripts)
        &baseBlownWhiteScreen,    // script 0x26 (calbania/ch3_story)
        &islandBlownWhiteScreen,  // script 0x3c (calbania/ch3_story)
        &trainDefenceWhiteScreen, // script 0x71 (yosyonke/town_story)
    };
    r.gameOverRescuePlace = &examRescuePlace; // the licence exam's (late_game/exam_loadout)
}

} // namespace

void registerAll() {
    ContentRegistry& r = contentRegistry();
    if (r.ready) return;
    registerIntroClasses(r);
    registerYosyonkeClasses(r);
    registerForbiddenIslandClasses(r);
    registerMandaClasses(r);
    registerNinoClasses(r);
    registerCalbaniaClasses(r);
    registerSaulKadaClasses(r);
    registerLateGameClasses(r);
    registerSubruinsClasses(r);
    registerStory(r);
    registerStageSetup(r);
    // the areas' rooms in the stage / room setup and the room scripts (their room helpers / handlers join the chains;
    // no address or stage of theirs is another module's, so no registration replaces another)
    registerIntroRooms(r);
    registerYosyonkeRooms(r);
    registerAbandonedMineRooms(r);
    registerSt47Rooms(r);
    registerIslandRooms(r);
    registerMandaRooms(r);
    registerNinoRooms(r);
    registerCalbaniaRooms(r);
    registerSaulKadaRooms(r);
    registerIceRuinRooms(r);
    registerLateGameRooms(r);
    registerDevRoom(r); // the Flutter's / Nino's development room (script 0x30's stage callbacks)
    registerYosyonkeMinimaps(r); // the minimap's stage callbacks (game/minimap), by area
    registerSubruinsMinimaps(r);
    registerCalbaniaMinimaps(r);
    registerForbiddenIslandMinimaps(r);
    registerMandaMinimaps(r);
    registerNinoMinimaps(r);
    registerSaulKadaMinimaps(r);
    registerLateGameMinimaps(r);
    registerCalls(r);
    r.ready = true;
}

} // namespace od::content
