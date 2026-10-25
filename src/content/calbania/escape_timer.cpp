// The escape timer (pool e0 type 0x22, exe 0x4270a0; ST30 sub 2, the escape: spawned by the room handler 0x585890
// pointing at the escape's ticks): state 0 (the stopwatch's countdown and frame cleared) -> 1; every tick exe 0x4270e0
// (its logic part): the stopwatch's frame (0..2, one step every 2 ticks of its countdown). Its packets (exe 0x4270e0,
// OT [0x6d0048] + 0x14) are drawn from this state by escapeTimerDraw.
#include "content/calbania/ch3_story.hpp"
#include "content/calbania/escape_timer_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actors.hpp"

namespace od::port {

using namespace escape_timer_state;

namespace {

enum class TimerState : uint8_t { Start = 0, Running = 1 };
constexpr uint8_t kStopwatchFrames = 3;

// A sprite of the escape timer's layout (exe 0x88bff8, 8 bytes): its place and size
struct EscapeTimerSprite : DataRecord {
    int16_t x = 0;
    int16_t y = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    static EscapeTimerSprite load(const TableReader& r) {
        EscapeTimerSprite rec;
        rec.x = r.s16(0x0);
        rec.y = r.s16(0x2);
        rec.w = r.u16(0x4);
        rec.h = r.u16(0x6);
        return rec;
    }
};
constexpr size_t kEscapeTimerSprites = 5; // three digits, the stopwatch, the plate
const DataTable<RecordTable<EscapeTimerSprite, kEscapeTimerSprites, 8>> escapeTimerSprites{TableId::EscapeTimerSprites};
constexpr RoutineId kEscapeTimerClass = RoutineId::EscapeTimerUpdate;
constexpr uint32_t kTicksPerSecond = 30;
constexpr uint32_t kHurrySeconds = 0x1e; // at this and below the digits turn to the other clut
// the sprites' words as the exe's packets hold them: u | v << 8 | clut << 16
constexpr uint32_t kDigitsClut = 0x3c780000u, kHurryDigitsClut = 0x3c380000u;
constexpr uint32_t kDigitTexel = 0x1800u; // (digit | this) << 3: the digit's u / v
constexpr uint32_t kBlankDigit = 0xc000u; // a tens digit 0 after hundreds
constexpr uint32_t kStopwatchFirst = 5;   // (frame + this) << 4: the stopwatch's u / v
constexpr uint32_t kStopwatchWord = 0x3c78c000u, kPlateWord = 0x3c78d000u;

} // namespace

void escapeTimerUpdate(const PlayerEnv&, uint32_t actor) {
    ControllerActor& a = controllerActorAt(actor);
    EscapeTimerFields& f = classState<EscapeTimerFields>(a);
    if (TimerState(a.state) == TimerState::Start) { // exe 0x4270b7
        a.state = uint8_t(TimerState::Running);
        f.frameCountdown = 0;
        f.stopwatchFrame = 0;
    } else if (TimerState(a.state) != TimerState::Running) {
        return;
    }
    const uint8_t c = f.frameCountdown; // exe 0x4270e0 (its logic part)
    f.frameCountdown = uint8_t(c - 1);
    if (c == 0) {
        f.frameCountdown = 1;
        const uint8_t k = uint8_t(f.stopwatchFrame + 1);
        f.stopwatchFrame = k;
        if (k == kStopwatchFrames) f.stopwatchFrame = 0;
    }
}

std::vector<hud::Prim> escapeTimerDraw() {
    std::vector<hud::Prim> out;
    for (ControllerActor* c : controllersRunning(kEscapeTimerClass)) {
        if (TimerState(c->state) != TimerState::Running) continue;
        const EscapeTimerFields& f = classState<EscapeTimerFields>(*c);
        if (!f.timer || !escapeTimerSprites.available()) continue;
        uint32_t s = *f.timer / kTicksPerSecond;
        const uint32_t clut = s > kHurrySeconds ? kDigitsClut : kHurryDigitsClut;
        uint32_t w[kEscapeTimerSprites] = {};
        int first = 0;
        const uint32_t h100 = s / 100;
        s -= h100 * 100;
        if (h100 != 0) w[0] = ((h100 | kDigitTexel) << 3) | clut;
        else first = 1;
        const uint32_t t10 = s / 10;
        s -= t10 * 10;
        if (t10 != 0) w[1] = ((t10 | kDigitTexel) << 3) | clut;
        else if (w[0] == 0) ++first;
        else w[1] = clut | kBlankDigit;
        w[2] = ((s | kDigitTexel) << 3) | clut;
        w[3] = ((uint32_t(f.stopwatchFrame) + kStopwatchFirst) << 4) | kStopwatchWord;
        w[4] = kPlateWord;
        for (int k = int(kEscapeTimerSprites) - 1; k >= first; --k) { // the plate first
            const EscapeTimerSprite sp = escapeTimerSprites[size_t(k)];
            const float x = float(sp.x), y = float(sp.y);
            const float wd = float(sp.w), ht = float(sp.h);
            const float u = float(w[k] & 0xff), v = float((w[k] >> 8) & 0xff);
            out.push_back(hud::plateSprite(x, y, x + wd, y + ht, u, v, u + wd, v + ht, uint16_t(w[k] >> 16)));
        }
    }
    return out;
}

} // namespace od::port
