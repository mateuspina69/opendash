// The minimap (see minimap.hpp).
#include "game/minimap.hpp"

#include <cstdio>
#include <set>

#include "game/audio_logic.hpp"
#include "game/content_registry.hpp"
#include "game/display.hpp"
#include "game/event_state.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/geometry_scratch.hpp"
#include "game/loaded_data.hpp"
#include "game/matrix.hpp"
#include "game/menu_draw.hpp"
#include "game/player/player_state.hpp"
#include "game/projection.hpp"
#include "game/room_grid.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr uint16_t kEntryYawQuadrant = 0xc00;
constexpr int16_t kSlideStep = 0x10;
constexpr uint8_t kRadarAlert = 2;        // the radar beeped (game/hud shows the alert clut)
constexpr uint8_t kRadarOff = 0xff;       // no weapon: no radar
constexpr uint16_t kRadarBeepSound = 0x88;
constexpr uint32_t kLayerEntries = 8;     // the room block's map layers: its entries from +8 on (2 + layer)
// the entrance marker's red: 0x7f .. 0xff and back over 32 frames
constexpr uint32_t kPulsePeriodMask = 0x1f, kPulseHalf = 0x10;
// the player's heading: its corners (x, y, z) before the turn, its colours, the scale to the panel (>> 5)
constexpr int16_t kHeadingCorners[3][3] = {{0, 0, 0xc0}, {-0x60, 0, -0xa0}, {0x60, 0, -0xa0}};
constexpr uint32_t kHeadingTip = 0xffff3f, kHeadingBack = 0xff3f00, kHeadingOutline = 0x804040;
constexpr int kHeadingShift = 5;
// the towns' icons (tpage 0xd, clut 0x7fd3)
constexpr uint16_t kIconMode = 0x0d, kIconClut = 0x7fd3;
constexpr uint32_t kIconSemi = 8, kIconSmall = 4, kIconTurn = 3;

// an area's cells and panel: the collision cells (0x200 units, biased by 0x8000) two to a map cell; the panel's
// pixels 64 units, the far edges' bias, the half sizes' trim and pad, the panel's centre, the tiles (16 x 16; the
// bytes 0x5c..0x5f and 0xff: no tile by default)
constexpr int32_t kPanelCellBias = 0x8000;
constexpr int kCellShift = 9, kPanelShift = 6;
constexpr int32_t kPanelEdgeBias = 0x7dc1, kPanelHalfTrim = 0x47, kPanelHalfPad = 0x20;
constexpr int16_t kPanelCentreX = 0x24, kPanelCentreY = 0x3c;
constexpr int16_t kTileSize = 0x10;
constexpr uint8_t kNoTileFirst = 0x5c, kNoTileLast = 0x5f, kNoTile = 0xff;
// the ruins' dots: 4 x 4, their list's 8-byte entries ended by flag 0xffff, a set flag's colour; the pace that halves
// their pulse where a callback asks
constexpr int16_t kDotSide = 4;
constexpr uint32_t kDotSize = 8;
constexpr uint16_t kDotListEnd = 0xffff;
constexpr uint32_t kDotDone = 0x008080;
constexpr uint8_t kDotsHalvedPace = 3;
// a town's scroll within +-0x5c; its picture 256 x 256 at 0xa0a0a0
constexpr int16_t kPictureScroll = 0x5c;
constexpr int16_t kPictureSize = 0x100;
constexpr uint32_t kPictureColour = 0xa0a0a0;

std::vector<ScreenPrim>* gDraw = nullptr; // setMinimapDraw

// a slide is held while a script, a fade or an event runs
bool slideHeld() { return scriptState().flags != 0 || fadeState().busy != 0 || eventState().active != 0; }

// exe 0x4155d0: the panel's slide
void minimapSlide() {
    MinimapState& m = minimap();
    if (!(m.flags & kMinimapOn)) return;
    if (gameOptions().minimapShown == 0 || !(m.flags & kMinimapAllowed)) {
        // switched off / put away: out
        if (m.phase == uint8_t(MinimapPhase::Hidden)) return;
        m.phase = uint8_t(MinimapPhase::SlideOut);
    }
    switch (MinimapPhase(m.phase)) {
    case MinimapPhase::Hidden:
        if (!slideHeld()) m.phase = uint8_t(MinimapPhase::SlideIn);
        return;
    case MinimapPhase::SlideIn:
        if (slideHeld()) {
            m.phase = uint8_t(MinimapPhase::SlideOut);
            return;
        }
        m.slideX = int16_t(m.slideX + kSlideStep);
        if (m.slideX < kMinimapShownX) return;
        m.slideX = kMinimapShownX;
        m.phase = uint8_t(MinimapPhase::Shown);
        return;
    case MinimapPhase::Shown:
        if (slideHeld()) m.phase = uint8_t(MinimapPhase::SlideOut);
        return;
    case MinimapPhase::SlideOut:
        if (!slideHeld()) m.phase = uint8_t(MinimapPhase::SlideIn); // (the slide out still runs this frame)
        m.slideX = int16_t(m.slideX - kSlideStep);
        if (m.slideX > kMinimapHiddenX) return;
        m.slideX = kMinimapHiddenX;
        m.phase = uint8_t(MinimapPhase::Hidden);
        return;
    }
}

// a point of the panel as the packets carry it
struct PanelPoint {
    int16_t x, y;
};

void tri(size_t c, const PanelPoint (&p)[3], const uint32_t (&colour)[3]) {
    ScreenPrim& t = menu_draw::otPut(c, ScreenPrim::Tri);
    for (int k = 0; k < 3; ++k) {
        t.qx[k] = p[k].x;
        t.qy[k] = p[k].y;
        t.qc[k] = colour[k];
    }
}

void line(size_t c, PanelPoint a, PanelPoint b, uint32_t colour) {
    ScreenPrim& l = menu_draw::otPut(c, ScreenPrim::Line);
    l.qx[0] = a.x;
    l.qy[0] = a.y;
    l.qx[1] = b.x;
    l.qy[1] = b.y;
    l.colour = colour;
}

} // namespace

void minimapStageReset() { minimap() = MinimapState{}; }

void minimapRoomReset(const Player& player) {
    MinimapState& m = minimap();
    m.clearRoomWords();
    m.slideX = kMinimapHiddenX;
    m.entryYaw = uint16_t(uint16_t(player.rotation.y) & kEntryYawQuadrant);
    m.entryX = player.pos.x.whole;
    m.entryZ = player.pos.z.whole;
}

void minimapSwitchOn() {
    MinimapState& m = minimap();
    m.flags = kMinimapOn | kMinimapAllowed;
    m.clearSecondWord();
}

void fieldHudFrame() {
    GameState& g = gameState();
    if (g.enemyNear != 0) {
        if (player().weaponMode == 0) {
            g.enemyNear = int8_t(kRadarOff);
        } else if (g.enemyNear == 1) {
            soundPlayFlatForced(kRadarBeepSound);
            g.enemyNear = int8_t(kRadarAlert);
        }
    }
    minimapSlide();
}

void setMinimapDraw(std::vector<ScreenPrim>* draw) { gDraw = draw; }

void minimapDrawPass() {
    const RoutineId cb = stage().roomCallback;
    if (cb == RoutineId::None) return;
    const auto it = content().roomCallbacks.find(cb);
    if (it == content().roomCallbacks.end()) {
        static std::set<RoutineId> reported;
        if (reported.insert(cb).second)
            std::fprintf(stderr, "minimap: stage callback 0x%x not ported\n", routineStateValue(cb));
        return;
    }
    menu_draw::begin(gDraw);
    it->second();
    menu_draw::end();
}

void minimapMarkCell(uint32_t base, int32_t cell) {
    uint8_t& b = extraFlags().bytes[size_t(int32_t(base) + (cell >> 3))];
    b = uint8_t(b | 1u << (cell & 7));
}

bool minimapCellExplored(uint32_t base, int32_t cell) {
    return (extraFlags().bytes[size_t(int32_t(base) + (cell >> 3))] & 1u << (cell & 7)) != 0;
}

uint32_t minimapLayer(uint8_t layer) {
    const uint32_t block = roomGrid().block;
    return block + dataScalar<uint32_t>(block + kLayerEntries + uint32_t(layer) * 4) * 4;
}

int32_t minimapPlayerCell(const MinimapArea& a) {
    const Player& p = player();
    const int32_t row = ((((int32_t(p.pos.z.whole) + kPanelCellBias) >> kCellShift) - a.cellZ - 1) >> 1) * a.columns;
    const int32_t column = (((int32_t(p.pos.x.whole) + kPanelCellBias) >> kCellShift) - a.cellX - 1) >> 1;
    return row + column;
}

bool minimapNoTile(uint8_t t) { return (t >= kNoTileFirst && t <= kNoTileLast) || t == kNoTile; }

void minimapAreaPanel(const MinimapArea& a, const MinimapTileStyle& s) {
    using namespace menu_draw;
    const MinimapState& m = minimap();
    const Player& p = player();
    MinimapLanes l = minimapLanes(); // x / y: the scroll, then the tiles' origin; corner: a tile's
    const size_t c = minimapPanelOpen(s.mode);
    minimapPanelBackdrop(c);
    // the area's far edges in panel pixels (1 / 64 of the units), the player's point from them, kept within the half
    // sizes of the area round them
    const int32_t edgeX = (((a.cellX + a.columns) << kCellShift) - kPanelEdgeBias) >> kPanelShift;
    const int32_t edgeY = (((a.cellZ + a.rows) << kCellShift) - kPanelEdgeBias) >> kPanelShift;
    l.x = int16_t((p.pos.x.whole >> kPanelShift) - edgeX);
    l.y = int16_t(edgeY - (p.pos.z.whole >> kPanelShift));
    const int32_t halfX = (((a.columns << 4) - kPanelHalfTrim) >> 1) + kPanelHalfPad;
    const int32_t halfY = (((a.rows << 4) - kPanelHalfTrim) >> 1) + kPanelHalfPad;
    if (l.x.get() > edgeX + halfX) l.x = int16_t(edgeX + halfX);
    else if (l.x.get() < edgeX - halfX) l.x = int16_t(edgeX - halfX);
    if (l.y.get() > edgeY + halfY) l.y = int16_t(edgeY + halfY);
    else if (l.y.get() < edgeY - halfY) l.y = int16_t(edgeY - halfY);
    const int16_t baseX = int16_t(m.slideX - l.x.get() + kPanelCentreX);
    const int16_t baseY = int16_t(kPanelCentreY - l.y.get());
    l.clut = int16_t(s.clut);
    // the tiles: row 0 at the bottom
    const uint32_t tiles = minimapLayer(s.layer) + uint16_t((m.visitedBase - s.firstBase) * 8);
    l.x = int16_t(edgeX - a.columns * 8 + s.originShiftX + baseX);
    l.y = int16_t(edgeY + a.rows * 8 - kTileSize + baseY);
    for (int32_t row = 0; row < a.rows; ++row)
        for (int32_t column = 0; column < a.columns; ++column) {
            const int32_t cell = row * a.columns + column;
            const uint8_t t = dataScalar<uint8_t>(tiles + uint32_t(cell));
            if (s.noTile(t)) continue;
            const uint32_t colour = minimapCellExplored(m.visitedBase, cell) ? kNeutral : s.unexplored;
            if (colour == 0) continue;
            l.cornerX = int16_t(column * kTileSize + l.x.get());
            l.cornerY = int16_t(l.y.get() - row * kTileSize);
            const uint8_t r = s.texel ? s.texel(t) : t;
            const uint8_t u = uint8_t(r << 4), v = uint8_t((r & 0xf0) + s.texelRowOffset);
            l.texel = int16_t(uint16_t(v << 8 | u));
            otSprite(c, l.cornerX.get(), l.cornerY.get(), spriteUv(s.clut, u, v), kTileSize, kTileSize, colour,
                     uint8_t(kSprite16 | 2));
        }
    if (s.dots) {
        const uint8_t frame = uint8_t(gameState().frame);
        const uint32_t t = s.dotsPaceHalved && display().pace == kDotsHalvedPace ? (frame & 0xf) << 1 : frame & 0x1f;
        const uint32_t pulse = minimapPulse(t);
        const uint32_t pulsing = pulse | pulse << 8;
        for (uint32_t e = s.dots; dataScalar<uint16_t>(e) != kDotListEnd; e += kDotSize) {
            const int16_t x = dataScalar<int16_t>(e + 4), z = dataScalar<int16_t>(e + 6);
            const uint32_t colour = storyFlag(dataScalar<uint16_t>(e)) ? kDotDone : pulsing;
            otTile(c, int16_t((x >> kPanelShift) - edgeX + baseX - 2), int16_t(edgeY - (z >> kPanelShift) + baseY - 2),
                   kDotSide, kDotSide, colour, kTileSemi);
        }
    }
    minimapEntranceMarker(c, int16_t((m.entryX >> kPanelShift) - edgeX + baseX),
                          int16_t(edgeY - (m.entryZ >> kPanelShift) + baseY));
    minimapPlayerHeading(c, int16_t((p.pos.x.whole >> kPanelShift) + baseX - edgeX),
                         int16_t(baseY - (p.pos.z.whole >> kPanelShift) + edgeY));
    minimapPanelClose(c);
}

void minimapEntranceMarker(size_t c, int16_t x, int16_t y) {
    PanelPoint p[3];
    switch (minimap().entryYaw) {
    case 0: p[0] = {x, int16_t(y + 6)}, p[1] = {int16_t(x - 3), int16_t(y - 5)}, p[2] = {int16_t(x + 3), int16_t(y - 5)}; break;
    case 0x400: p[0] = {int16_t(x - 6), y}, p[1] = {int16_t(x + 5), int16_t(y - 3)}, p[2] = {int16_t(x + 5), int16_t(y + 3)}; break;
    case 0x800: p[0] = {x, int16_t(y - 6)}, p[1] = {int16_t(x + 3), int16_t(y + 5)}, p[2] = {int16_t(x - 3), int16_t(y + 5)}; break;
    default: p[0] = {int16_t(x + 6), y}, p[1] = {int16_t(x - 5), int16_t(y + 3)}, p[2] = {int16_t(x - 5), int16_t(y - 3)}; break;
    }
    uint32_t t = uint8_t(gameState().frame);
    if (display().pace == 1) t >>= 1;
    t &= kPulsePeriodMask;
    const uint32_t red = minimapPulse(t);
    const uint32_t colour = (((red & 0x1fe) << 9 | (red & 0x3fc)) << 6 | red) & 0xffffff;
    tri(c, p, {colour, colour, colour});
}

void minimapPlayerHeading(size_t c, int16_t x, int16_t y) {
    const Player& pl = player();
    // the turn: Ry(the stick's yaw offset - the player's yaw) * the identity, its padding short 0 (exe 0x40bea0 on the
    // stack's identity image); no translation
    const Matrix3 m = mulMatrix3x3(rotationY(int32_t(pl.directionOffset.y) - int32_t(pl.rotation.y)), kIdentityMatrix);
    const int32_t origin[3] = {0, 0, 0};
    loadProjectionMatrix(m, 0);
    loadProjectionTranslation(origin);
    PanelPoint p[3];
    for (int k = 0; k < 3; ++k) {
        // (the point's spare lane is the exe's stack garbage: 0 here)
        loadProjectionPoint(kHeadingCorners[k][0], kHeadingCorners[k][1], kHeadingCorners[k][2], 0);
        transformProjectionPoint();
        int32_t view[3];
        storeProjectionPoint(view);
        p[k] = {int16_t((view[0] >> kHeadingShift) + x), int16_t((view[2] >> kHeadingShift) + y)};
    }
    tri(c, p, {kHeadingTip, kHeadingBack, kHeadingBack});
    // the outline: a 0x4c polyline round the three corners
    line(c, p[0], p[1], kHeadingOutline);
    line(c, p[1], p[2], kHeadingOutline);
    line(c, p[2], p[0], kHeadingOutline);
}

void minimapIcon(size_t c, int16_t x, int16_t y, uint32_t flags) {
    using namespace menu_draw;
    const uint8_t semi = (flags & kIconSemi) ? 2 : 0;
    if (flags & kIconSmall) {
        otDrawMode(c, kIconMode);
        otSprite(c, int16_t(x - 4), int16_t(y - 4), spriteUv(kIconClut, 0xb0 + (flags & kIconTurn) * 8, 0xa0), 8, 8,
                 kNeutral, uint8_t(kSprite8 | semi));
        return;
    }
    // the corners TL TR BL BR of the 24 x 24 quad; its texels (0xb8 .. 0xcf, 0x88 .. 0x9f) turned a quarter per step
    constexpr int kU0 = 0xb8, kU1 = 0xcf, kV0 = 0x88, kV1 = 0x9f;
    static constexpr Q4 kTurnU[4] = {{{kU0, kU1, kU0, kU1}}, {{kU0, kU0, kU1, kU1}}, {{kU1, kU0, kU1, kU0}}, {{kU1, kU1, kU0, kU0}}};
    static constexpr Q4 kTurnV[4] = {{{kV0, kV0, kV1, kV1}}, {{kV1, kV0, kV1, kV0}}, {{kV1, kV1, kV0, kV0}}, {{kV0, kV1, kV0, kV1}}};
    const uint32_t k = flags & kIconTurn;
    otQuad(c, {{x - 12, x + 12, x - 12, x + 12}}, {{y - 12, y - 12, y + 12, y + 12}}, kNeutral,
           uint8_t(kPolyFT4 | semi), kTurnU[k], kTurnV[k], kIconClut, kIconMode);
}

MinimapLanes minimapLanes() {
    GeometryScratch& g = geometryScratch();
    return {g.lane(0x50), g.lane(0x52), g.lane(0x54), g.lane(0x56), g.lane(0xa0), g.lane(0xa2)};
}

size_t minimapPanelOpen(uint16_t mode) {
    using namespace menu_draw;
    const size_t c = otOpen(kOtOverlay);
    otClip(c, minimap().slideX, displayDrawArea().y + kMinimapY, kMinimapSize, kMinimapSize);
    otDrawMode(c, mode);
    return c;
}

void minimapPanelBackdrop(size_t c) {
    using namespace menu_draw;
    otTile(c, minimap().slideX, kMinimapY, kMinimapSize, kMinimapSize, kNeutral, kTileSemi);
}

void minimapPanelClose(size_t c) { menu_draw::otClipDrawArea(c); }

int16_t minimapScrollClamp(int16_t v) {
    if (v > kPictureScroll) return kPictureScroll;
    if (v < -kPictureScroll) return int16_t(-kPictureScroll);
    return v;
}

void minimapPictureSprite(size_t c, int16_t x, int16_t y, uint16_t clut) {
    using namespace menu_draw;
    otSprite(c, x, y, spriteUv(clut, 0, 0), kPictureSize, kPictureSize, kPictureColour, uint8_t(kSpriteFree | 2));
}

uint32_t minimapPulse(uint32_t t) { return t <= kPulseHalf ? t * 8 + 0x7f : 0x17f - t * 8; }

} // namespace od::port
