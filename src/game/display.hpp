// The display: the buffer the frame draws into, the game's pace, the display-list bytes the wipes close and reopen,
// the display mode (the title's 640 x 480 / the game's), the two draw environments' flags, the display mask a fade
// of type 0 sets, and the routines that set them (exe 0x549860 / 0x549880 / 0x549a20, the mask count of 0x53f500).
// The renderer is the host's: these are the bytes the game logic reads back.
#pragma once

#include <cstdint>

#include "engine/data_source.hpp"
#include "game/state_array.hpp"

namespace od::port {

struct DisplayState {
    uint8_t drawBuffer = 0;   // the buffer the frame draws into (0 / 1); the wipes and the stage switches act on the
                              // frames of buffer 1
    uint8_t pace = 0;         // the frames per game tick (1: 60 / s, 2: 30 / s, 3: 20 / s)
    uint8_t displayHeld = 0;  // the shown image held while a wipe closes the display list
    uint8_t drawingOff = 0;   // the frame draws nothing (the draw functions skip their packets)
    uint8_t bufferMode = 0;   // how the frame's buffer is presented (exe 0x549860 sets it)
    uint8_t mode = 0;         // the display mode (0: the game's; the title's 640 x 480: 2)
    uint8_t maskTicks = 0;    // the display kept off while counting (0: not masked)
    uint8_t frontEndLayer = 1; // set by the title (and at the start), cleared by the field mode each tick (exe
                               // 0x549a70; its reader is not traced)
};
inline DisplayState& display() {
    static DisplayState s;
    return s;
}
constexpr uint8_t kGameDisplayMode = 0;  // the game's display mode (pace 2)
constexpr uint8_t kTitleDisplayMode = 2; // the title's 640 x 480 (pace 1)
// the paces of the display modes (frames per game tick); the status sub-screen runs at the title's
constexpr uint8_t kTitleDisplayPace = 1;
constexpr uint8_t kGameDisplayPace = 2;
// the buffer modes (exe 0x549860)
constexpr uint8_t kBufferModeNormal = 0;
constexpr uint8_t kBufferModeStage = 1;    // a stage load, the status sub-screen
constexpr uint8_t kBufferModeFrontEnd = 2; // the front end's DAT loads
// the screen of the game's display mode (the PS1 frame the HUD and the 2D pictures are laid out on)
constexpr int kScreenWidth = 320, kScreenHeight = 240;

// A drawing area of the display buffers (exe table 0x88c3c8: 8 bytes, by the draw buffer + the display mode * 2): the
// rect the 2D builders clip to
struct DrawArea : DataRecord {
    int16_t x = 0, y = 0, w = 0, h = 0;
    static DrawArea load(const TableReader& r) { return {{}, r.s16(0), r.s16(2), r.s16(4), r.s16(6)}; }
};
inline const DataTable<RecordTable<DrawArea, 8, 8>> displayDrawAreas{TableId::DisplayDrawAreas};
// the drawing area of the buffer the frame draws into (exe 0x40b650's table lookup)
inline const DrawArea& displayDrawArea() {
    return displayDrawAreas[uint32_t(display().drawBuffer) + uint32_t(display().mode) * 2];
}

// One of the two draw environments: the vertical placements the wipes zero / set and the four flag bytes exe
// 0x549a40 copies in (hypothesis on the placements' roles: the first's clip, the second's origin / screen)
struct DrawEnvironment {
    int16_t clipY = 0;
    int16_t originY = 0;
    int16_t screenY = 0;
    StateArray<uint8_t, 4> flags{};
};
struct DrawEnvironments {
    StateArray<DrawEnvironment, 2> envs{};
    uint8_t clearBackground = 0; // the frame is cleared to the environment's colour (flags[0])
    uint8_t flagB = 0;           // the three other bytes copied into flags[1..3] (their meaning is not traced)
    uint8_t flagC = 0;
    uint8_t flagD = 0;
};
inline DrawEnvironments& drawEnvironments() {
    static DrawEnvironments s;
    return s;
}

// exe 0x549860(mode): the frame buffer's presentation mode; while a fade runs, also the mode its wipe restores
void setBufferMode(uint8_t mode);
// exe 0x549880(mode): the display mode (draw / display environments: the renderer's) and the pace that goes with it
// (the game's mode 0: 30 / s; the title's: 60 / s); the display masked for a few frames
void setDisplayMode(uint8_t mode);
// exe 0x549a20: the four flag bytes copied into both draw environments (exe 0x549a40 on each)
void applyDrawEnvironmentFlags();
// exe 0x53f500's display mask: while the count runs (a fade of type 0 or a display mode change set it to 1) it counts
// up each frame and keeps the display off until it passes (mode >> 1) * 4 + 4, then 0 and the display back on. Render
// side: the app runs it after the fade's tick; the fade's level reports the display off as black.
void displayMaskTick();

} // namespace od::port
