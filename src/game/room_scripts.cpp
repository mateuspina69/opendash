// The room scripts (see room_scripts.hpp): exe 0x512890's dispatch of the stage cases.
#include "game/room_scripts.hpp"

#include <cstdio>

#include "game/content_registry.hpp"
#include "game/game_state.hpp"
#include "game/script_engine.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/travel.hpp"

namespace od::port {

namespace {

// What a stage's case of exe 0x512890 is made of
enum class RoomScriptCaseKind : uint8_t {
    StoryTable, // the handler [[table + story * 4] + sub * 4] (+ the case's head / tail: the content's)
    SubTable,   // the handler [table + sub * 4] (likewise)
    InlineCase, // the case's own code (its routine: the content's room handlers answer it)
    OwnRoutine, // the stage's own room routine (likewise; always transcribed for the coverage)
    Quiet,      // no handler and nothing that spawns
};
struct RoomScriptCase {
    uint8_t stage;
    RoomScriptCaseKind kind;
    RoutineId routine = RoutineId::None; // the case's code / the routine (InlineCase / OwnRoutine)
};
constexpr RoomScriptCaseKind S = RoomScriptCaseKind::StoryTable, R = RoomScriptCaseKind::SubTable;
constexpr RoomScriptCaseKind I = RoomScriptCaseKind::InlineCase, O = RoomScriptCaseKind::OwnRoutine;
// the stage cases of exe 0x512890 (the stages not listed: the story's room cases, the travel map, the sub-ruins and
// the content's whole stage cases -- ContentRegistry::roomCases / stageCases)
constexpr RoomScriptCase kRoomScriptCases[] = {
    {0x00, RoomScriptCaseKind::Quiet},
    {0x02, R}, {0x04, S}, {0x07, R}, {0x08, S}, {0x09, S},
    {0x0a, S}, {0x0c, S}, {0x0d, S}, {0x0e, S}, {0x0f, S},
    {0x10, S}, {0x11, S}, {0x12, S}, {0x13, S}, {0x14, S},
    {0x17, S}, {0x18, S}, {0x19, S}, {0x1a, RoomScriptCaseKind::Quiet},
    {0x1b, S}, {0x1d, S}, {0x1f, S}, {0x20, S},
    {0x21, I, RoutineId::St21RoomCase}, {0x22, I, RoutineId::St22RoomCase},
    {0x23, S}, {0x24, S}, {0x25, S}, {0x26, S}, {0x27, S},
    {0x28, S}, {0x29, S}, {0x2a, S}, {0x2b, S}, {0x2c, S},
    {0x2d, R}, {0x2e, R}, {0x2f, R}, {0x30, R}, {0x31, R},
    {0x32, S}, {0x33, S}, {0x34, S}, {0x35, S}, {0x36, S},
    {0x37, S}, {0x38, S}, {0x3b, S}, {0x3c, S}, {0x3d, S},
    {0x3e, R}, {0x3f, S}, {0x40, R}, {0x41, R}, {0x42, R},
    {0x43, R}, {0x44, R}, {0x45, R}, {0x46, S}, {0x47, S},
    {0x48, S}, {0x49, I, RoutineId::St49RoomCase}, {0x4a, R}, {0x4b, R},
    {0x4c, R}, {0x4e, R}, {0x4f, R}, {0x51, R},
    {0x52, O, RoutineId::St52RoomRoutine}, {0x53, S}, {0x54, I, RoutineId::St54RoomCase},
    {0x55, O, RoutineId::St55RoomRoutine}, {0x56, I, RoutineId::St56RoomCase},
    {0x57, I, RoutineId::St57RoomCase},
    {0x58, R},
};
// the stages whose case is not in the table
constexpr uint8_t kTravelMapStage = 0x01;                     // the travel map (game/travel)
constexpr uint8_t kAttractStageA = 0x02, kAttractStageB = 0x03; // the attract's (the story's room cases)
constexpr uint8_t kFireMissionStage = 0x1e, kOpeningStage = 0x39, kCrashLandingStage = 0x3a; // likewise
constexpr uint8_t kFirstSubruinStage = 0x4d, kLastSubruinStage = 0x50; // the sub-ruins (by the RNG's layout)
// the handlers that are a bare ret in the exe
constexpr RoutineId kEmptyHandlerA = RoutineId::EmptyRoomHandler, kEmptyHandlerB = RoutineId::EmptyRoutine;

// the stage's case of exe 0x512890's dispatch (null: the stage has none)
const RoomScriptCase* roomScriptCase(uint8_t stage) {
    for (const RoomScriptCase& c : kRoomScriptCases)
        if (c.stage == stage) return &c;
    return nullptr;
}
// the case picks its handler from a table (by story then sub-room, or by sub-room)
bool tableCase(const RoomScriptCase& c) {
    return c.kind == RoomScriptCaseKind::StoryTable || c.kind == RoomScriptCaseKind::SubTable;
}
// the handler a table case picks for the running story and sub-room from the stage's handler table (the table's
// pointer: the source names its routine)
RoutineRef handlerOf(const RoomScriptCase& c) {
    const GameState& g = gameState();
    const uint32_t table = dataSource().stateAddress({TableId::RoomScriptHandlers, c.stage});
    uint32_t subTable = table;
    if (c.kind == RoomScriptCaseKind::StoryTable) subTable = dataScalar<uint32_t>(table + uint32_t(g.story) * 4);
    return stateRoutine(dataScalar<uint32_t>(subTable + uint32_t(g.room) * 4));
}
// a room handler run (run = false: whether it is transcribed): the empty ones, then the content's
bool roomHandler(RoutineId fn, bool run) {
    if (fn == RoutineId::None) return false;
    if (fn == kEmptyHandlerA || fn == kEmptyHandlerB) return true;
    for (RoomHandlerFn handler : content().roomHandlers)
        if (handler(fn, run)) return true;
    return false;
}
// one of the sub-ruin stages (their handlers are content/subruins/subruin's)
bool subruinStage(uint8_t stage) { return stage >= kFirstSubruinStage && stage <= kLastSubruinStage; }
// a stage whose case is the story's (ContentRegistry::roomCasesKnown)
bool contentRoomCaseKnown(uint8_t stage) {
    for (auto known : content().roomCasesKnown)
        if (known(stage)) return true;
    return false;
}

} // namespace

bool roomDoorScript() {
    if (!storyFlag(kFlagTransitionArmed)) return false;
    scriptRun(0);
    return true;
}

bool roomScriptHandler() {
    if (storyRoomCase()) return true; // the story's stage cases (ContentRegistry::roomCases)
    const uint8_t stage = gameState().stage;
    if (stage == kTravelMapStage) { // case 1: exe 0x57bb10
        travelRoomCase();
        return true;
    }
    if (subruinStage(stage)) { // content/subruins/subruin
        if (!content().subruinRoomScripts.known()) return false;
        content().subruinRoomScripts.run();
        return true;
    }
    const RoomScriptCase* c = roomScriptCase(stage);
    if (c && (c->kind == RoomScriptCaseKind::InlineCase || c->kind == RoomScriptCaseKind::OwnRoutine)) {
        roomHandler(c->routine, true);
        return true;
    }
    // the cases with more than "handler + door test" (their own handlers, door test and tails)
    if (const auto it = content().stageCases.find(stage); it != content().stageCases.end()) return it->second();
    if (!c || !tableCase(*c)) return c != nullptr;
    if (const auto it = content().roomCaseHeads.find(stage); it != content().roomCaseHeads.end()) it->second();
    const RoutineId h = handlerOf(*c).id();
    if (!roomHandler(h, false)) return false;
    roomHandler(h, true);
    if (const auto it = content().roomCaseTails.find(stage); it != content().roomCaseTails.end()) it->second();
    return true;
}

bool roomScriptHandlerMissing() {
    const uint8_t stage = gameState().stage;
    if (stage == kOpeningStage || stage == kFireMissionStage || stage == kCrashLandingStage ||
        stage == kTravelMapStage || stage == kAttractStageA || stage == kAttractStageB || contentRoomCaseKnown(stage))
        return false;
    if (subruinStage(stage)) return false; // game/transition's door test covers them
    const RoomScriptCase* c = roomScriptCase(stage);
    if (!c || c->kind == RoomScriptCaseKind::Quiet) return false;
    if (!tableCase(*c)) return !roomHandler(c->routine, false);
    const RoutineRef h = handlerOf(*c);
    return h && !roomHandler(h.id(), false);
}

bool roomScriptCovered(std::string& why) {
    const GameState& g = gameState();
    const uint8_t stage = g.stage;
    if (stage == kTravelMapStage || stage == kAttractStageA || stage == kAttractStageB ||
        contentRoomCaseKnown(stage))
        return true;
    if (subruinStage(stage)) {
        const bool known = content().subruinRoomScripts.known();
        if (!known) why += "sub-ruin room-script handler ";
        return known;
    }
    const RoomScriptCase* c = roomScriptCase(stage);
    if (!c) {
        if (content().stageCases.count(stage)) return true;
        why += "room-script stage case not transcribed ";
        return false;
    }
    switch (c->kind) {
    case RoomScriptCaseKind::InlineCase: return roomHandler(c->routine, false);
    case RoomScriptCaseKind::OwnRoutine:
    case RoomScriptCaseKind::Quiet: return true;
    default: {
        const RoutineRef h = handlerOf(*c);
        const bool known = roomHandler(h.id(), false);
        if (!known) {
            char b[160];
            std::snprintf(b, sizeof b, "room-script handler %06x (story %u sub %u) ", h.pointerValue(),
                          unsigned(g.story), unsigned(g.room));
            why += b;
        }
        return known;
    }
    }
}

} // namespace od::port
