// The status sub-screen's shared controls (see status_controls.hpp), one function per exe function.
#include "game/status_controls.hpp"
#include "game/patched_data.hpp"

#include "engine/data_source.hpp"
#include "engine/screen_prim.hpp"
#include "game/audio_logic.hpp"
#include "game/game_state.hpp"
#include "game/items.hpp"
#include "game/menu_banks.hpp"
#include "game/menu_draw.hpp"
#include "game/menu_pointer.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"

namespace od::port {

using namespace menu_draw;

namespace {

// A piece of a frame style (exe table 0x91a9d8: 9 pieces of 8 bytes per style, 0x48 a style): its sprite's texture
// word, its size
struct FramePiece : DataRecord {
    static constexpr uint32_t kBytes = 8;
    uint32_t texture = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    template <class R> static FramePiece load(const R& r) { return {{}, r.u32(0), r.u16(4), r.u16(6)}; }
};
// piece k of a style's table
FramePiece framePiece(uint32_t style, size_t k) {
    return dataRecord<FramePiece>(style + uint32_t(k) * FramePiece::kBytes, FramePiece::kBytes);
}
constexpr uint32_t kFrameStyleSize = 0x48;
constexpr uint16_t kFrameMode = 7;
constexpr uint32_t kFrameArrowArt = 0x7f122030;

constexpr uint32_t kPageTitleArt = 0x7f101090;
constexpr uint16_t kRowMode = 7;
// the rows' cluts: the cursor's, a row chosen on a deeper level, another on a deeper level
constexpr uint16_t kCursorClut = 0x7f13, kChosenClut = 0x7f50, kOtherClut = 0x7f51;

// the scroll bar's art (tpage 7, clut 0x7f53)
constexpr uint32_t kScrollTopArt = 0x7f5310f0, kScrollBottomArt = 0x7f5328f0, kScrollUpArt = 0x7f5310f8,
                   kScrollDownArt = 0x7f5318f8;
constexpr uint16_t kScrollClut = 0x7f53;

// the items' icons (tpage 0xd) and the item tables
const DataTable<ScalarTable<uint32_t, 0x10>> itemIconArts{TableId::StatusControlsItemIconArts};
const DataTable<RecordTable<ItemEntry, 0x30, 4>> keyItemEntries{TableId::StatusKeyItemEntries};
const DataTable<RecordTable<ItemEntry, 0x80, 4>> usableItemEntries{TableId::StatusUsableItemEntries};
constexpr uint16_t kIconMode = 0x0d;
constexpr uint32_t kIconDim = 0x606060;

// the list cursor's animations: a row / a column moved (ticks, by the direction), the scroll (2 a step, 3 past 2)
constexpr uint8_t kMoveSteps = 4;

// the pad
uint16_t repeated() { return pad0().repeated; }
uint16_t held() { return pad0().held; }
constexpr uint16_t kPadPage = kPadL1 | kPadR1;

// a move's animation start: kMoveSteps signed by the direction (up: -kMoveSteps)
uint32_t moveSteps(bool back) { return ((uint32_t(back) - 1u) & 0xf8u) + kMoveSteps; }
// the scroll animation by a scroll of d rows
uint32_t scrollSteps(int8_t d) { return uint32_t((d < 0 ? -d : d) < 2 ? d * 2 : d * 3); }
// a scroll animation's tick (the scroll row moved when it reaches 0, or past 2: at once)
void scrollAnimationTick(StatusScreen& s, int8_t c) {
    const int a = c < 0 ? -c : c;
    if (a < 3) {
        if (c < 1) {
            s.scrollStep = int8_t(c + 1);
            if (int8_t(c + 1) == 0) s.listScroll -= 1u;
        } else {
            s.scrollStep = int8_t(c - 1);
            if (int8_t(c - 1) == 0) s.listScroll += 1u;
        }
    } else if (c > 0) {
        s.scrollStep = int8_t(c - 3);
        s.listScroll += 1u;
    } else {
        s.scrollStep = int8_t(c + 3);
        s.listScroll -= 1u;
    }
}

} // namespace

const ItemEntry& itemEntry(int16_t id) {
    if (id < int16_t(kUsableItems)) return keyItemEntries[size_t(uint32_t(int32_t(id - int16_t(kKeyItems))))];
    return usableItemEntries[size_t(uint32_t(int32_t(id - int16_t(kUsableItems))))];
}

uint32_t itemIconArt(uint32_t kind) { return itemIconArts[kind]; }

PatchedRecord<StatusPageTables> gPageTables{TableId::StatusPageTables};
StatusPageTables& statusPageTables() { return *gPageTables; }

void storeCursorEntry(uint32_t pointer, const CursorEntry& e) {
    if (builtDataAt(pointer, CursorEntry::kBytes)) {
        storeData(pointer, e.head());
        storeData(pointer + 4, e.link);
        return;
    }
    patchedRecordFor<CursorEntry>(pointer) = e;
}

int menuWindowOpen(int8_t win, uint32_t bank, int16_t id) { return windowOpen(win, langBank(bank), id); }

void menuCursorStep(StatusScreen& s, int levels) {
    uint32_t ent = 0, bank = 0, tab = stateAddress(TableId::StatusControlsCursorTree), pc = 0;
    bool clamp = false, done = false;
    s.answer = kAnswerNone;
    if (levels != 0) {
        int n = levels;
        do {
            --n;
            const int8_t c = int8_t(s.level(pc));
            if (c < int8_t(kFirstChildState)) break;
            ent = tab + uint32_t(int32_t(c) * 8 - 0x10);
            const CursorEntry e = cursorEntryAt(ent);
            if (e.flags == 0) {
                bank = e.link;
                if (bank != 0) {
                    done = true;
                    break;
                }
            } else {
                tab = e.link;
            }
            ++pc;
        } while (n != 0);
        if (!done && ent != 0 && (cursorEntryAt(ent).flags & kCursorClamp)) {
            if (!(cursorEntryAt(ent).flags & kCursorSubTable)) clamp = true;
            done = true;
        }
    }
    if (!done) {
        ent = tab;
        bank = cursorEntryAt(ent).link;
    }
    const CursorEntry e = cursorEntryAt(ent);
    const int cur = s.cursor;
    const int8_t anim = s.cursorStep;
    if (anim != 0) {
        s.cursorStep = int8_t(anim < 1 ? anim + 1 : anim - 1);
        if (bank != 0) menuWindowOpen(0, bank, s.cursor);
        if (!menuSnappy()) return; // OpenDash: a move is taken during the animation (player_input setMenuSnappy)
    }
    const uint8_t rep = uint8_t(repeated());
    int v = cur;
    uint8_t inc;
    if (e.horizontal == 0) {
        if (rep & kPadUp) v = cur - 1;
        inc = rep & kPadDown;
    } else {
        if (rep & kPadLeft) v = cur - 1;
        inc = rep & kPadRight;
    }
    if (inc) ++v;
    const int lo = e.first, hi = int(e.end) - 1;
    if (MenuPointer& pt = menuPointer(); pt.moved && pt.row < kListCell) { // OpenDash: the mouse's row
        if (pt.row >= lo && pt.row <= hi) v = pt.row;
        pt.moved = false;
    }
    if (clamp) {
        if (v < lo) v = lo;
        if (hi < v) v = hi;
    } else {
        if (v < lo) v = hi;
        if (hi < v) v = lo;
    }
    if (v != cur) {
        s.cursorStep = int8_t(moveSteps(v <= cur));
        soundPlayFlat(UiSound::Cursor);
    }
    s.cursorKept = uint8_t(v);
    s.cursor = int8_t(v);
    if (bank != 0) menuWindowOpen(0, bank, int16_t(v));
    if (menuPadPressed() & kPadConfirm) {
        soundPlayFlatForced(uint16_t(UiSound::Confirm));
        s.answer = kAnswerConfirm;
    }
    if (menuPadPressed() & kPadCross) {
        soundPlayFlatForced(uint16_t(UiSound::Cancel));
        s.level(levels != 0 ? uint32_t(levels) : 0) = kLevelBack;
        s.answer = kAnswerCancel;
    }
}

void cursorReset(StatusScreen& s) {
    s.cursorShown = 0;
    s.cursor = 0;
    s.listRow = 0;
    s.listScroll = 0;
    s.cursorKept = 0;
    s.slideIn = 1;
    s.slideX = 0;
    s.rowStep = 0;
    s.scrollStep = 0;
}

void levelBack(StatusScreen& s, int8_t level) {
    if (level <= 0) return;
    if (s.slideIn == 0) s.slideX = 0;
    else s.slideIn = 2;
    const uint32_t pc = uint32_t(level - 1);
    s.cursorKept = uint8_t(s.level(pc) - kFirstChildState);
    s.level(pc) = 1;
    s.level(pc + 1) = 0;
}

int twoColumnListStep(StatusScreen& s, int level, int count, int cols) {
    int8_t c = s.rowStep;
    if (c != 0) {
        s.rowStep = int8_t(c < 1 ? c + 1 : c - 1);
        return 0;
    }
    c = s.scrollStep;
    if (c != 0) {
        scrollAnimationTick(s, c);
        return 0;
    }
    const uint8_t r0 = s.listRow;
    const int row = r0, scroll = s.listScroll;
    int nr = row, ns = scroll;
    const uint16_t rep = repeated();
    if ((held() & kPadR1) == 0 || (held() & kPadL1) == 0) {
        if (rep & kPadR1) {
            nr = int(s.listVisible) - 1;
            if (row == nr) {
                nr = row;
                ns = scroll - 1 + s.listVisible;
            }
        }
        if (rep & kPadL1) {
            if (nr == 0) ns = ns + (1 - int(s.listVisible));
            else nr = 0;
        }
    }
    if (rep & kPadUp) --nr;
    if (rep & kPadDown) ++nr;
    MenuPointer& pt = menuPointer();
    int ptCol = -1;
    if (pt.moved && pt.row >= kListCell) { // OpenDash: the mouse's cell (visible row, column)
        nr = (pt.row - kListCell) >> 1;
        ns = scroll;
        ptCol = (pt.row - kListCell) & 1;
        pt.moved = false;
    }
    if (ns < 0) {
        nr = 0;
        ns = 0;
    } else if (nr < 0) {
        ns = (nr + ns < 0) ? 0 : ns - 1;
        nr = 0;
    }
    const int rows = s.listRows, vis = s.listVisible;
    if (vis < rows) {
        if (ns + nr < rows) {
            if (vis <= nr) {
                nr = vis - 1;
                ns = ns + 1;
            }
        } else {
            nr = vis - 1;
            ns = rows - nr - 1;
        }
    } else {
        ns = 0;
        if (rows <= nr) nr = rows - 1;
    }
    if (nr >= 0) {
        if (nr == row) {
            if (ns != scroll) s.scrollStep = int8_t(scrollSteps(int8_t(ns - int(s.listScroll))));
        } else {
            s.listRow = uint8_t(nr);
            s.rowStep = int8_t(nr - r0);
        }
    }
    c = s.cursorStep;
    if (c != 0) {
        s.cursorStep = int8_t(c < 1 ? c + 1 : c - 1);
        return 0;
    }
    int col = s.cursor;
    if (rep & kPadLeft) --col;
    if (rep & kPadRight) ++col;
    if (ptCol >= 0) col = ptCol;
    int per = cols;
    if (count == 0) per = 1;
    else if (count % per != 0 && rows == int(s.listRow) + 1 + scroll) per = count % per;
    if (col < 0) col = per - 1;
    if (per - 1 < col) col = 0;
    if (col != s.cursor) s.cursorStep = int8_t(moveSteps(col <= s.cursor));
    s.cursorKept = uint8_t(col);
    s.cursor = int8_t(col);
    if (s.cursorStep != 0 || s.rowStep != 0 || s.scrollStep != 0) soundPlayFlat(UiSound::Cursor);
    s.cursorShown = 1;
    int r = (menuPadPressed() & kPadConfirm) != 0;
    if (pt.click && !pt.moved && s.cursorStep == 0 && s.rowStep == 0 && s.scrollStep == 0) { // OpenDash: a click
        if (pt.row == kListCell + int(s.listRow) * 2 + s.cursor) r = 1; // on the cursor's cell: confirm
        pt.click = false;
    }
    if (r) soundPlayFlat(UiSound::Confirm);
    if (menuPadPressed() & kPadCross) {
        soundPlayFlat(UiSound::Cancel);
        s.level(level != 0 ? uint32_t(level) : 0) = kLevelBack;
        s.cursorShown = 0;
        r = kAnswerCancel;
    }
    if (s.scrollStep != 0) r = 0;
    return r;
}

int oneColumnListStep(StatusScreen& s) {
    int8_t c = s.rowStep;
    if (c != 0) {
        s.rowStep = int8_t(c > 0 ? c - 1 : c + 1);
        return 0;
    }
    c = s.scrollStep;
    if (c != 0) {
        scrollAnimationTick(s, c);
        return 0;
    }
    const uint8_t r0 = s.listRow, s0 = s.listScroll;
    int16_t row = r0, scr = s0;
    const uint16_t h = held(), rp = repeated();
    if (!((h & kPadR1) && (h & kPadL1))) {
        if (rp & kPadR1) {
            const uint8_t vis = s.listVisible;
            if (row == vis - 1) scr = int16_t(s0 - 1 + vis);
            else row = int16_t(vis - 1);
        }
        if (rp & kPadL1) {
            if (row == 0) scr = int16_t(scr + 1 - s.listVisible);
            else row = 0;
        }
    }
    if (rp & kPadUp) --row;
    if (rp & kPadDown) ++row;
    MenuPointer& pt = menuPointer();
    if (pt.moved && pt.row >= kListCell) { // OpenDash: the mouse's row
        row = int16_t((pt.row - kListCell) >> 1);
        scr = s0;
        pt.moved = false;
    }
    if (scr < 0) {
        row = 0;
        scr = 0;
    } else if (row < 0) {
        scr = scr + row < 0 ? 0 : int16_t(scr - 1);
        row = 0;
    }
    const uint8_t n = s.listRows, vis = s.listVisible;
    if (vis < n) {
        if (scr + row < n) {
            if (vis <= row) {
                row = int16_t(vis - 1);
                ++scr;
            }
        } else {
            row = int16_t(vis - 1);
            scr = int16_t(n - row - 1);
        }
    } else {
        scr = 0;
        if (n <= row) row = int16_t(n - 1);
    }
    s.cursorShown = 1;
    if (row >= 0) {
        bool moved = true;
        if (row == r0) {
            if (scr == s0) moved = false;
            else s.scrollStep = int8_t(scrollSteps(int8_t(scr - s0)));
        } else {
            s.listRow = uint8_t(row);
            s.rowStep = int8_t(row - r0);
        }
        if (moved) soundPlayFlat(UiSound::Cursor);
    }
    int r = (menuPadPressed() & kPadConfirm) != 0;
    if (pt.click && !pt.moved && s.rowStep == 0 && s.scrollStep == 0) { // OpenDash: a click on the cursor's row
        if (pt.row == kListCell + int(s.listRow) * 2) {
            r = 1;
            soundPlayFlat(UiSound::Confirm);
        }
        pt.click = false;
    }
    if (menuPadPressed() & kPadCross) {
        soundPlayFlatForced(uint16_t(UiSound::Cancel));
        s.cursorShown = 0;
        r = kAnswerCancel;
    }
    if (s.scrollStep != 0) r = 0;
    return r;
}

void frameDraw(int x, int y, int w, int h, int style) {
    const uint32_t tab = stateAddress(TableId::StatusControlsFrameStyles) + uint32_t(style) * kFrameStyleSize; // its 9 pieces, 8 bytes each
    const int midW = w - framePiece(tab, 2).w - framePiece(tab, 0).w, midH = h - framePiece(tab, 6).h - framePiece(tab, 0).h;
    if (midW < 0 || midH < 0) return;
    const size_t c = otOpen(kOtFrames);
    otDrawMode(c, kFrameMode);
    artBegin(uint16_t(kVecFrame0 + style));
    int yy = y;
    for (int r = 0; r < 3; ++r) {
        const int rh = r == 1 ? midH : framePiece(tab, size_t(r * 3)).h;
        int xx = x;
        for (int k = 0; k < 3; ++k) {
            const FramePiece e = framePiece(tab, size_t(r * 3 + k));
            const int cw = k == 1 ? midW : e.w;
            if (k == 1 || r == 1) {
                const uint32_t t = e.texture;
                const int u0 = uint8_t(t), v0 = uint8_t(t >> 8), u1 = uint8_t(e.w) + u0 - 1, v1 = uint8_t(e.h) + v0 - 1;
                otQuad(c, {xx, xx + cw, xx, xx + cw}, {yy, yy, yy + rh, yy + rh}, kNeutral, kPolyFT4, {u0, u1, u0, u1},
                       {v0, v0, v1, v1}, uint16_t(t >> 16), kFrameMode);
            } else {
                otSprite(c, xx, yy, e.texture, int16_t(e.w), int16_t(e.h));
            }
            xx += cw;
        }
        yy += rh;
    }
    artEnd();
    if (style == kFrameArrow) {
        labelNext(kLblFrameArrow);
        otSprite(c, x + w - 0x10, y + (h >> 1) - 8, kFrameArrowArt, 0x10, 0x10);
    }
}

void rowsDraw(StatusScreen& s, uint32_t table) {
    const RowTable tbl(table);
    const size_t c = otOpen(kOtRows);
    otDrawMode(c, kRowMode);
    bool done = true;
    uint32_t pc = 0;
    if (tbl.levelOffset != 0) pc = uint32_t(int32_t(tbl.levelOffset));
    for (uint32_t n = tbl.skipLevels; n != 0 && int8_t(s.level(pc)) > 2; --n) ++pc;
    const int8_t level = int8_t(s.level(pc));
    for (int i = 0; i < tbl.count; ++i) {
        const RowArt e = tbl.row(size_t(i));
        int x = e.x;
        const int y = e.y;
        uint16_t clut;
        uint16_t state;
        if (level < int8_t(kFirstChildState)) {
            clut = s.cursor == i ? kCursorClut : uint16_t(e.clut);
            if (s.slideIn != 0 && tbl.slideIn != 0 && int16_t(s.slideX << 4) < x) {
                done = false;
                x = int16_t(s.slideX << 4);
            }
            if (clut == kCursorClut && e.clut == kOtherClut) clut = kChosenClut;
            state = s.cursor == i ? 1 : 0;
            rowNext(i); // the active level's rows: the mouse picks them
        } else {
            clut = uint16_t((level - int(kFirstChildState) != i) + kChosenClut);
            state = level - int(kFirstChildState) == i ? 2 : 3;
        }
        labelNext(kLblPageRow, uint16_t(i | state << 8), uint32_t(s.page) << 16 | e.uv);
        otSprite(c, x, y, uint32_t(clut) << 16 | e.uv, int16_t(e.w), int16_t(e.h));
    }
    if (level < int8_t(kFirstChildState)) {
        if (done) s.slideIn = 0;
        if (s.slideIn != 0) s.slideX = int8_t(uint8_t(s.slideX) + 1u);
    }
    if (s.slideIn == 2) {
        s.slideIn = 0;
        s.slideX = 0;
    }
}

void pageDraw(StatusScreen& s, int k) {
    StatusPageTables& t = statusPageTables();
    const uint32_t rows = t.rows[size_t(k)];
    if (rows != 0) {
        const uint16_t fh = t.frameHeights[size_t(k)];
        if (fh != 0) frameDraw(0x10, 0x20, 0x40, fh, kFrameArrow);
        rowsDraw(s, rows);
    }
    const size_t c = otOpen(kOtRows);
    otDrawMode(c, kRowMode);
    labelNext(kLblPageTitle, uint16_t(k));
    if (k <= 2) otSprite(c, 0x78, 0x10, kPageTitleArt, 0x50, 0x10);
    else if (k == 3) otSprite(c, 0x70, 0x10, kPageTitleArt, 0x60, 0x10);
    otDrawMode(c, kRowMode);
    const PageFrame& f = statusPageFrames[size_t(k)];
    frameDraw(f.x, f.y, f.w, f.h, kFrameRinged);
}

void statusScrollBarDraw(const StatusScreen& s, int x, int y) {
    const int rows = s.listRows, vis = s.listVisible;
    const size_t c = otOpen(kOtRows);
    otDrawMode(c, kRowMode);
    labelNext(kLblScrollCap, 0);
    otSprite(c, x, y, kScrollTopArt, 8, 0x10);
    const int len = vis * 0x10 - 0x18, top = y + 0x10;
    labelNext(kLblScrollTrack);
    otQuad(c, {x, x + 8, x, x + 8}, {top, top, top + len, top + len}, kNeutral, kPolyFT4, {0xf0, 0xf8, 0xf0, 0xf8},
           {0x20, 0x20, 0x21, 0x21}, kScrollClut, kRowMode);
    labelNext(kLblScrollCap, 1);
    otSprite(c, x, top + len, kScrollBottomArt, 8, 0x10);
    const int anim = s.scrollStep + s.rowStep;
    if (anim < 0) {
        labelNext(kLblScrollLit, 0);
        otSprite(c, x, y + 8, kScrollUpArt, 8, 8);
    } else if (anim > 0) {
        labelNext(kLblScrollLit, 1);
        otSprite(c, x, top + len, kScrollDownArt, 8, 8);
    }
    int ty = top, th = 0;
    if (rows != 0) {
        th = len;
        if (vis < rows) {
            ty += (int(s.listScroll) * len) / rows;
            th = (vis * len - 1 + rows) / rows;
        }
    }
    labelNext(kLblScrollThumb);
    otQuad(c, {x, x + 7, x, x + 7}, {ty, ty, ty + th, ty + th}, kNeutral, kPolyFT4, {0xf8, 0xff, 0xf8, 0xff},
           {0x20, 0x20, 0x21, 0x21}, kScrollClut, kRowMode);
}

void itemIconDraw(int x, int y, int16_t id, bool dim) {
    uint32_t k;
    if (id < int16_t(kBusterPartItems)) k = 0;
    else if (id < int16_t(kHelmetItems)) k = 1;
    else if (id < int16_t(kArmourItems)) k = 2;
    else if (id < int16_t(kLegItems)) k = 3;
    else if (id < int16_t(kKeyItems)) k = 4;
    else k = itemEntry(id).icon;
    if (id >= int16_t(kKeyItems) && k == 0) return;
    const size_t c = otOpen(kOtText);
    otDrawMode(c, kIconMode);
    artBegin(uint16_t(kVecItemIcon + k));
    otSprite(c, x, y, itemIconArt(k), 0x10, 0x10, dim ? kIconDim : kNeutral, kSprite16);
    artEnd();
}

} // namespace od::port
