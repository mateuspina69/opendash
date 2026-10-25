// The licence exam's spawner (ST55, pool 0xe0 type 0xa, exe 0x422d60) and its clock's picture (see exam_spawner.hpp).
#include "content/manda/exam_spawner.hpp"
#include "content/manda/exam_spawner_state.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/actors.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/display.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/event.hpp"
#include "game/game_state.hpp"
#include "game/hud.hpp"
#include "game/message.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace exam_spawner_state;

namespace {

constexpr const char* kEnemySpawnerPort = "enemy spawner port"; // the area of its port-gap errors

enum class ExamSpawnerState : uint8_t {
    Init = 0,    // exe 0x422cd0
    Running = 1, // exe 0x422d80: the clock is drawn (examTimerDraw)
};


// exe 0x422d60: the licence exam's spawner -- states [0x88bcf8]: 0 = 0x422cd0 (+0x18..+0x1f cleared, state 1), 1 =
// 0x422d80 -> exe 0x422d90: draw only (the exam timer 0xa6d298 as minutes / seconds / hundredths in GPU packets at
// *0x6d004c, ordering table [0xa67840] + 0x78, unless 0x6d000a) -- drawn from this state by od::hud::examTimerDraw
// (game/hud)
void examSpawnerUpdate(ControllerActor& actor) {
    switch (ExamSpawnerState(actor.state)) {
    case ExamSpawnerState::Init:
        classState<ExamSpawnerFields>(actor).unread = {};
        actor.enterState(uint8_t(ExamSpawnerState::Running));
        return;
    case ExamSpawnerState::Running: return;
    default: notPortedAt(kEnemySpawnerPort, "ST55 spawner 0x422d60 state", actor.state);
    }
}

} // namespace

void spawnerSt55Update(const PlayerEnv&, uint32_t actor) { examSpawnerUpdate(controllerActorAt(actor)); }

// ---- the licence exam clock's picture (the host draws it from the state the spawner left this tick; into the OT
// [0xa67840] + 0x78: the last added drawn first, emitted here in that order) ------------------------------------------
namespace {

constexpr RoutineId kSt55Spawner = RoutineId::SpawnerSt55Update;
const DataTable<RecordTable<hud::PictureQuad, 3, 0x10>> examPlates{TableId::ExamSpawnerPlates};
const DataTable<ScalarTable<uint16_t, 6>> examDigitX{TableId::ExamSpawnerDigitX};
constexpr float kExamDigitY = 0x12;
constexpr uint32_t kExamBlinkTicks = 15; // the plates are left out on the ticks with (t + 1) % 15 == 0 outside room 0

} // namespace

std::vector<hud::Prim> examTimerDraw() {
    std::vector<hud::Prim> out;
    // the ST55 exam's clock (ticks at 30 / s): the stage block's first word
    const StageBlockHead* clock = stageBlock().parts.held<StageBlockHead>();
    if (!clock || !examPlates.available() || display().drawingOff != 0) return out;
    bool on = false;
    for (ControllerActor* c : controllersRunning(kSt55Spawner))
        if (ExamSpawnerState(c->state) == ExamSpawnerState::Running) on = true;
    if (!on) return out;
    std::vector<hud::Prim> added; // the exe's insertion order
    const uint32_t t = clock->word;
    if ((t + 1) % kExamBlinkTicks != 0 || gameState().room == 0)
        for (size_t k = 0; k < examPlates.size(); ++k) added.push_back(hud::pictureQuad(examPlates[k], 0x808080));
    const uint32_t secs = t / 30;
    const uint32_t vals[3] = {secs / 60, secs % 60, (t % 30) * 100 / 30};
    size_t xi = 0;
    for (const uint32_t v : vals) {
        uint32_t val = v, div = 10;
        for (int d = 0; d < 2; ++d, ++xi) {
            const uint32_t dg = val / div;
            val %= div;
            div /= 10;
            added.push_back(hud::pictureDigit(float(examDigitX[xi]), kExamDigitY, dg, 0x808080));
        }
    }
    out.assign(added.rbegin(), added.rend());
    return out;
}

} // namespace od::port
