// The attract intro's entry points (see attract.hpp): the room cases of ST02 / ST03 and the scripts' dispatch, and the
// warp and the end its two scripts share.
#include "content/intro/attract.hpp"

#include "engine/data_source.hpp"
#include "engine/routine_ids.hpp"
#include "game/audio_logic.hpp"
#include "game/game_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/story.hpp"
#include "game/title.hpp"
#include "game/transition_state.hpp"

namespace od::port {

namespace {

constexpr uint8_t kScript3 = 3, kScript4 = 4;
constexpr uint8_t kST02 = 2, kST03 = 3;
// ST02's room handlers (exe table 0x8c5024 by room; the named routines of engine/routines.csv): room 0 starts script 3,
// rooms 1 / 2 have none (a bare return)
const DataTable<RoutineTable<3>> st02RoomHandlers{TableId::AttractSt02RoomHandlers};
constexpr uint8_t kScriptStarted = 1;

} // namespace

// whether the handler is one of the attract's scripts
bool attractScriptPorted(RoutineId handler) { return handler == RoutineId::St02AttractScript || handler == RoutineId::St03AttractScript; }

// the attract script of the handler ticked (false: another handler)
bool attractScriptTick(const PlayerEnv&, RoutineId handler) {
    switch (handler) {
    case RoutineId::St02AttractScript: st02AttractScript(); return true;
    case RoutineId::St03AttractScript: st03AttractScript(); return true;
    default: return false;
    }
}

// exe 0x57bb30 / 0x57bb90: ST02's / ST03's room case (false: another stage)
bool attractRoomCase() {
    const GameState& g = gameState();
    RoomScriptState& room = roomScriptState();
    switch (g.stage) {
    case kST02: // exe 0x57bb30
        switch (st02RoomHandlers->idAt(g.room)) {
        case RoutineId::AttractStartsScript3:
            if (room.handlerStep == 0) {
                scriptRun(kScript3); // exe 0x511270
                ++room.handlerStep;
            }
            return true;
        case RoutineId::EmptyRoutine: return true;
        default: notPortedAt(kStoryPort, "ST02 room handler", st02RoomHandlers->pointerAt(g.room));
        }
    case kST03: // exe 0x57bb90: room 0, the first time: script 4
        if (g.room == 0 && room.handlerStep == 0) {
            scriptRun(kScript4);
            room.handlerStep = kScriptStarted;
        }
        return true;
    default: return false;
    }
}

// the in-stage warp of the attract's steps (no fade)
void attractWarp(uint8_t stage, uint8_t room) {
    requestTransition(kTransitionWarp, stage, room, 0, 0, 0, 0, kNoFade, kNoFade);
}

// the scripts' end: the block ended, the sounds stopped, the main task's mode back to the title
void attractBackToTitle() {
    scriptEndBlock();
    soundStopAll();
    mainTask().enterMode(MainMode::ToTitle);
}

} // namespace od::port
