// The base's alarm (pool e0 type 0x2a, exe 0x428de0; ST31 sub 1: spawned by script 0x24's step 0x523490 at 0x18):
// state 0 (the flash's phase and the siren's countdown cleared) -> 1; every tick the siren's countdown: at 0 back to
// 0x2f and the siren (sound 0x1c7 at (0, -0x200, 0)); the phase += 0x80. Its packet (a full-screen red semi-transparent
// flat quad, OT [0xa67840] + 0x78, red = 0x50 + (sin(phase) * 64 >> 12)) is drawn from this state by alarmFlashDraw.
#include "content/calbania/ch3_story.hpp"
#include "content/calbania/base_alarm_state.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actors.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/display.hpp"

namespace od::port {

using namespace base_alarm_state;

namespace {

enum class AlarmState : uint8_t { Start = 0, Running = 1 };
constexpr RoutineId kAlarmClass = RoutineId::AlarmFlashUpdate;
constexpr int kAlarmRedBase = 0x50;
constexpr uint16_t kSirenEvery = 0x2f, kSiren = 0x1c7, kPhaseStep = 0x80;
constexpr int16_t kSirenY = -0x200;

} // namespace

void alarmFlashUpdate(const PlayerEnv&, uint32_t actor) {
    ControllerActor& a = controllerActorAt(actor);
    AlarmFields& f = classState<AlarmFields>(a);
    if (AlarmState(a.state) == AlarmState::Start) {
        a.state = uint8_t(AlarmState::Running);
        f.phase = 0;
        f.sirenTicks = 0;
    }
    const uint16_t t = f.sirenTicks;
    f.sirenTicks = uint16_t(t - 1);
    if (t == 0) {
        f.sirenTicks = kSirenEvery;
        soundPlayAtXyz(kSiren, 0, kSirenY, 0); // exe 0x5d5380
    }
    f.phase = uint16_t(f.phase + kPhaseStep);
}

std::vector<hud::Prim> alarmFlashDraw() {
    std::vector<hud::Prim> out;
    for (ControllerActor* c : controllersRunning(kAlarmClass)) {
        if (AlarmState(c->state) != AlarmState::Running) continue;
        const AlarmFields& f = classState<AlarmFields>(*c);
        if (!fixedSinMapped(f.phase)) continue;
        const int sine = fixedSin(f.phase);
        hud::Prim q;
        q.kind = hud::Prim::Flat;
        q.colour = uint32_t(((sine << 6) >> 12) + kAlarmRedBase) & 0xff;
        q.semi = true;
        q.x[0] = q.x[2] = 0.0f;
        q.x[1] = q.x[3] = float(kScreenWidth);
        q.y[0] = q.y[1] = 0.0f;
        q.y[2] = q.y[3] = float(kScreenHeight);
        out.push_back(q);
    }
    return out;
}

} // namespace od::port
