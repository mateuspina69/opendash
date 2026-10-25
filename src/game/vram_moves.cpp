// The VRAM image moves and the texture animators (see vram_moves.hpp).
#include "game/actor_state.hpp"
#include "game/vram_moves.hpp"

#include <utility>

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/game_state.hpp"
#include "game/player/player_input.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

std::vector<VramMove> g_moves;

constexpr int kVramWidthMask = 0x3ff, kVramHeightMask = 0x1ff; // exe 0x40b710's masks

// ---- the sand flow -------------------------------------------------------------------------------------------------
// its frame count (the header's state byte, counted down every tick)
struct SandFlowFields {
    explicit SandFlowFields(ActorHeader& actor) : count(actor.state) {}
    uint8_t& count; // the header's state byte
};
// the sand strip: a 16 x 64 window at (kSandX, kSandTop + (count >> 1 & 0x3f)) to (kSandToX, kSandTop) on even counts,
// an 8 x 32 window at (kSandX2, kSandTop + (count >> 2 & 0x1f)) to (kSandToX2, kSandTop) on counts divisible by 4
constexpr int kSandX = 0x540, kSandToX = 0x560, kSandX2 = 0x550, kSandToX2 = 0x570, kSandTop = 0x100;

// ---- the strip scrollers -------------------------------------------------------------------------------------------
// The scroller's fields (the header's bytes): started, the scroll's offset (0..0x7f, the move's by its half), the speed
// (kScrollSpeed; kStoppingSpeed once stopping), the stop's ticks
struct StripScrollFields {
    explicit StripScrollFields(const ControllerActor& actor) : StripScrollFields(const_cast<ControllerActor&>(actor)) {}
    explicit StripScrollFields(ControllerActor& actor) : started(actor.state), offset(actor.subState), speed(actor.step), stopTicks(actor.stepCounter) {}
    uint8_t& started;
    uint8_t& offset;
    uint8_t& speed;
    uint8_t& stopTicks;
};
constexpr uint8_t kScrollSpeed = 2, kStoppingSpeed = 1, kStopTicks = 0x20, kOffsetMask = 0x7f;
constexpr int kStripWidth = 0x10, kStripHeight = 0x40;
// the lava (ST27 sub 8) and its stop: the lava turned off
constexpr int kLavaX = 0x6b0, kLavaFrom = 0x180, kLavaTo = 0x140;
constexpr uint32_t kFlagLavaStopped = 0x683;
// the ST4C strip
constexpr int kStripX = 0x5b0, kStripFrom = 0x1c0, kStripTo = 0x180;

// the shared body of the strip scrollers exe 0x425eb0 / 0x426180: started once; the strip at (x, from) scrolled into
// (x, to) by the offset >> 1 (two moves around the wrap), the offset advanced by the speed; stopping (`stops` and the
// lava's flag: the speed kStoppingSpeed) -> freed after kStopTicks
void scrollStrip(ControllerActor& actor, int x, int from, int to, bool stops) {
    StripScrollFields f(actor);
    if (f.started == 0) {
        f.started = 1;
        f.offset = 0;
        f.speed = kScrollSpeed;
        f.stopTicks = kStopTicks;
    }
    if (stops && storyFlag(kFlagLavaStopped)) f.speed = kStoppingSpeed;
    const int o = f.offset >> 1;
    if (o == 0) {
        vramMove(x, from, kStripWidth, kStripHeight, x, to);
    } else {
        vramMove(x, from, kStripWidth, kStripHeight - o, x, to + o);
        vramMove(x, from + kStripHeight - o, kStripWidth, o, x, to);
    }
    if (f.speed == kStoppingSpeed) {
        --f.stopTicks;
        if (f.stopTicks == 0) {
            freeActor(actor); // exe 0x5e15c0
            return;
        }
    }
    f.offset = uint8_t((f.offset + f.speed) & kOffsetMask);
}

// ---- the tile rows -------------------------------------------------------------------------------------------------
// A tile row (exe data, 8 bytes): the VRAM page (its x by the low nibble, its half by bit 4), the tile's size (its height;
// its width / 4 in VRAM words), the flip book's frames and their step's shift, the destination x / 4 and y, the source
// x / 4 and y
struct TileRow : DataRecord {
    static constexpr uint32_t kBytes = 8;
    uint8_t page = 0, size = 0, frames = 0, frameShift = 0, destX = 0, destY = 0, srcX = 0, srcY = 0;

    int pageX() const { return (page & 0xf) << 6; }
    int pageY() const { return (page & 0x10) << 4; }
    static TileRow load(const TableReader& r) {
        return {{}, r.u8(0), r.u8(1), r.u8(2), r.u8(3), r.u8(4), r.u8(5), r.u8(6), r.u8(7)};
    }
};
constexpr uint32_t kFlipBookTiles = 6;
const DataTable<RecordTable<TileRow, kFlipBookTiles, TileRow::kBytes>> flipBookRows{TableId::FlipBookRows}; // exe 0x88be88
const DataTable<TileRow> waveTileRow{TableId::WaveTileRow};                                                  // exe 0x88beb8

// The flip book's fields (the header's bytes): its state, its frame
struct FlipBookFields {
    explicit FlipBookFields(ActorHeader& actor) : state(actor.state), frame(actor.subState) {}
    uint8_t& state;
    uint8_t& frame; // the header's sub-state byte
};
enum class FlipBookState : uint8_t { Start = 0, Wait = 1, Run = 2 };
constexpr uint8_t kFlipBookFrames = 0x80;
// its cue: flipBookCue() at kFlipBookCueValue and the second pad's circle (kPadCircle) pressed
constexpr uint8_t kFlipBookCueValue = 6;

// the wave tile's sine: the frame's 16th step of the exe table, capped below one
constexpr int16_t kSineOne = 0x1000, kSineCap = 0xfff;

} // namespace

uint8_t& flipBookCue() {
    static uint8_t cue = 0;
    return cue;
}

void vramMove(int sx, int sy, int w, int h, int dx, int dy) {
    g_moves.push_back({sx & kVramWidthMask, sy & kVramHeightMask, w, h, dx & kVramWidthMask, dy & kVramHeightMask});
}

std::vector<VramMove> takeVramMoves() { return std::exchange(g_moves, {}); }

void sandFlowUpdate(const PlayerEnv&, uint32_t address) {
    SandFlowFields f(actorHeaderAt(address));
    const uint8_t c = f.count;
    if (!(c & 1)) vramMove(kSandX, kSandTop + ((c >> 1) & 0x3f), kStripWidth, kStripHeight, kSandToX, kSandTop);
    if (!(c & 3)) vramMove(kSandX2, kSandTop + ((c >> 2) & 0x1f), 8, 0x20, kSandToX2, kSandTop);
    f.count = uint8_t(c - 1);
}

void lavaFlowUpdate(const PlayerEnv&, uint32_t address) {
    scrollStrip(controllerActorAt(address), kLavaX, kLavaFrom, kLavaTo, true);
}

void stripScrollUpdate(const PlayerEnv&, uint32_t address) {
    scrollStrip(controllerActorAt(address), kStripX, kStripFrom, kStripTo, false);
}

void flipBookTilesUpdate(const PlayerEnv&, uint32_t address) {
    FlipBookFields f(actorHeaderAt(address));
    switch (FlipBookState(f.state)) {
    case FlipBookState::Start:
        f.state = uint8_t(FlipBookState::Wait);
        f.frame = 0;
        return;
    case FlipBookState::Wait:
        if (flipBookCue() == kFlipBookCueValue && (pad1().pressed & kPadCircle)) f.state = uint8_t(FlipBookState::Run);
        return;
    case FlipBookState::Run: {
        const int c = f.frame;
        for (uint32_t i = 0; i < kFlipBookTiles; ++i) {
            const TileRow& r = flipBookRows[i];
            const int frames = r.frames;
            const int frame = frames ? (c << r.frameShift) % frames : 0;
            vramMove((r.srcX >> 2) + r.pageX(), r.srcY + r.pageY() + frame, r.size >> 2, r.size, (r.destX >> 2) + r.pageX(),
                     r.destY + r.pageY());
        }
        f.frame = uint8_t(c + 1);
        if (f.frame == kFlipBookFrames) f.frame = 0;
        return;
    }
    default: return;
    }
}

void waveTileUpdate(const PlayerEnv&, uint32_t) {
    const TileRow& r = *waveTileRow;
    const int size = r.size;
    int16_t s = fixedSin(int(uint32_t(uint8_t(gameState().frame)) << 4));
    if (s == kSineOne) s = kSineCap;
    const int o = int16_t((size >> 1) + (s >> 9));
    vramMove((r.srcX >> 2) + r.pageX(), r.srcY + r.pageY(), size >> 2, o, (r.destX >> 2) + r.pageX(),
             size + r.pageY() + r.destY - o);
    vramMove((r.srcX >> 2) + r.pageX(), r.srcY + o + r.pageY(), size >> 2, size - o, (r.destX >> 2) + r.pageX(),
             r.destY + r.pageY());
}

} // namespace od::port
