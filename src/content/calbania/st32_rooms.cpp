// The room scripts of ST32: the handlers of the room scripts' stage case exe 0x5864c0 (story-indexed [[0x8cadb8 +
// story * 4] + sub * 4]) (their state: the room handler's step byte). One case per exe function (addresses in the
// comments), transcribed from the disassembly.
#include "content/calbania/st32_rooms.hpp"

#include "content/calbania/st32_quiz.hpp"
#include "game/actor_pools.hpp"
#include "game/game_state.hpp"
#include "game/room_scripts.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

enum class QuizRoomStep : uint8_t { Spawn = 0, Done = 1 };

constexpr uint8_t kStoryAfterQuiz = 4;
constexpr uint32_t kFlagQuizOpen = 0x581;            // the quiz is open at story 4
constexpr uint32_t kContestantRecordSize = 0x14;

} // namespace

bool st32RoomHandler(RoutineId fn, bool run) {
    switch (fn) {
    // ---- ST32 (case 0x5864c0, table 0x8cadb8 by story: stories 0..3 [0x8cadb0] (sub 1), from 4 [0x8cadb4] (sub 0))
    case RoutineId::St32RoomHandler: {
        // step 0: unless story 4 without flag 0x581, the three contestants tracked into the stage block
        // and the plate; step 1
        RoomScriptState& room = roomScriptState();
        if (!run || QuizRoomStep(room.handlerStep) != QuizRoomStep::Spawn) return true;
        if (gameState().story != kStoryAfterQuiz || storyFlag(kFlagQuizOpen)) {
            QuizShowBlock& show = stageBlock().part<QuizShowBlock>();
            for (uint32_t i = 0; i < 3; ++i)
                pointAtWord(show.contestants[i], spawnRecordTracked(stateAddress(TableId::St32RoomsContestantRecords) + i * kContestantRecordSize));
            spawnRecordTracked(stateAddress(TableId::St32RoomsQuizPlateRecord));
        }
        ++room.handlerStep;
        return true;
    }
    default: return false;
    }
}

} // namespace od::port
