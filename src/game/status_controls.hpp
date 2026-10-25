// The status sub-screen's controls the pages share: the cursor of the row tree (exe 0x5e83d0) and of the lists (the
// two-column list exe 0x5ea8a0, the one-column list exe 0x5e9730), a level's set-up and its way back, the frames
// (exe 0x5e8130), a level's rows (exe 0x5e9970), a page's rows / title / frame (exe 0x5e9b40), the lists' scroll bar
// (exe 0x5e9ca0), the items' icons (exe 0x5ea6d0) and the item table they read (the icon, the class, usable here).
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/patched_data.hpp"
#include "game/player/player_input.hpp"
#include "game/status_screen.hpp"

namespace od::port {

// the first pad's buttons pressed this tick (the sub-screen reads that pad)
inline uint16_t menuPadPressed() { return pad0().pressed; }

// ---- the exe's tables ----
// An entry of the row tree (exe table 0x91a7a0, 8 bytes; a level's entries are its rows - 2.. of the table above; the
// options page's are OpenDash's copies, game/status_options): kCursorSubTable / kCursorClamp, horizontal, the first row
// and the end, its help bank (or its sub-table)
struct CursorEntry : DataRecord {
    static constexpr uint32_t kBytes = 8;
    uint8_t flags = 0;
    uint8_t horizontal = 0;
    uint8_t first = 0;
    uint8_t end = 0;
    uint32_t link = 0; // the help bank (flags without kCursorSubTable) or the sub-table

    // the four bytes as one dword (OpenDash's copies)
    void setHead(uint32_t word) {
        flags = uint8_t(word);
        horizontal = uint8_t(word >> 8);
        first = uint8_t(word >> 16);
        end = uint8_t(word >> 24);
    }
    uint32_t head() const {
        return uint32_t(flags) | uint32_t(horizontal) << 8 | uint32_t(first) << 16 | uint32_t(end) << 24;
    }
    template <class R> static CursorEntry load(const R& r) { return {{}, r.u8(0), r.u8(1), r.u8(2), r.u8(3), r.u32(4)}; }
    void save(std::span<uint8_t> out) const {
        storeLe(out, 0, head());
        storeLe(out, 4, link);
    }
};
constexpr uint8_t kCursorSubTable = 0x1, kCursorClamp = 0x2;
// the entry at a pointer of the tree (the options page's patched entry of the exe's tree, the game's copies of its
// entries, the exe's)
inline CursorEntry cursorEntryAt(uint32_t pointer) { return recordAt<CursorEntry>(pointer); }
// an entry written: a copy in the game's built data, else the patched entry of the program's tree
void storeCursorEntry(uint32_t pointer, const CursorEntry& e);
// the row tree's entry of a page (its sub-table: the page's levels)
inline uint32_t pageCursorEntryAddress(uint32_t page) { return stateAddress(TableId::StatusControlsCursorTree) + (page - 2) * 8; }
inline CursorEntry pageCursorEntry(uint32_t page) { return cursorEntryAt(pageCursorEntryAddress(page)); }
// A page's rows table and its frame's height (exe data by the page index, written at run time: the options page's
// rows table and height are pointed at OpenDash's copies, game/status_options), its frame (exe table 0x91ad90)
struct PageFrame : DataRecord {
    int16_t x = 0, y = 0, w = 0, h = 0;
    static PageFrame load(const TableReader& r) { return {{}, r.s16(0), r.s16(2), r.s16(4), r.s16(6)}; }
};
inline const DataTable<RecordTable<PageFrame, 4, 8>> statusPageFrames{TableId::StatusPageFrames};
struct StatusPageTables : DataRecord {
    static constexpr uint32_t kBytes = 0x18;
    std::array<uint32_t, 4> rows{};         // 0x91adb0
    std::array<uint16_t, 4> frameHeights{}; // 0x91adc0
    template <class R> static StatusPageTables load(const R& r) {
        StatusPageTables t;
        for (size_t k = 0; k < 4; ++k) {
            t.rows[k] = r.u32(k * 4);
            t.frameHeights[k] = r.u16(0x10 + k * 2);
        }
        return t;
    }
    void save(std::span<uint8_t> out) const {
        for (size_t k = 0; k < 4; ++k) {
            storeLe(out, k * 4, rows[k]);
            storeLe(out, 0x10 + k * 2, frameHeights[k]);
        }
    }
};
// the pages' tables (exe data the options page rewrites: a patched record)
StatusPageTables& statusPageTables();
// A row's picture of a level's row table (12 bytes; the panel's rows too): its place, its caption's uv and clut, its
// size
struct RowArt : DataRecord {
    static constexpr uint32_t kBytes = 12;
    int16_t x = 0;
    int16_t y = 0;
    uint16_t uv = 0;
    uint16_t clut = 0;
    int16_t w = 0;
    int16_t h = 0;
    template <class R> static RowArt load(const R& r) { return {{}, r.s16(0), r.s16(2), r.u16(4), r.u16(6), r.s16(8), r.s16(0xa)}; }
};
// a row's picture as a table record (the status screens' fixed tables of rows)
struct RowArtRecord : DataRecord {
    int16_t x = 0, y = 0;
    uint16_t uv = 0, clut = 0;
    int16_t w = 0, h = 0;
    static RowArtRecord load(const TableReader& r) {
        return {{}, r.s16(0), r.s16(2), r.u16(4), r.u16(6), r.s16(8), r.s16(0xa)};
    }
};
// A level's row table (exe data; the options page's are copies at OpenDash's region): the row count, the offset of
// the level byte its rows belong to, the levels it skips while they are chosen, the slide-in, then the rows
struct RowTable {
    explicit RowTable(uint32_t pointer) : at(pointer) {
        const DataReader r = dataAt(pointer, 0xc);
        count = r.s16(0);
        levelOffset = r.s16(2);
        skipLevels = r.u8(4);
        slideIn = r.s16(0xa);
    }
    uint32_t at;
    int16_t count = 0;
    int16_t levelOffset = 0;
    uint8_t skipLevels = 0;
    int16_t slideIn = 0;

    static constexpr uint32_t kRows = 0xc; // the rows from +0xc, RowArt::kBytes each
    RowArt row(size_t i) const {
        if (i >= 0x10) stateIndexError(i, 0x10);
        return dataRecord<RowArt>(at + kRows + uint32_t(i) * RowArt::kBytes, RowArt::kBytes);
    }
    // the table's first 0x40 bytes as dwords (OpenDash's copies: the header is the first three)
    uint32_t word(size_t i) const { return dataScalar<uint32_t>(at + uint32_t(i) * 4); }
    static constexpr size_t kHeaderWords = 3, kCopyWords = 0x10;
};
// An item's entry (exe tables: the key items from kKeyItems, the items from kUsableItems; 4 bytes): its icon kind, its
// class (its use handler on the items page), usable on the sub-screen
struct ItemEntry : DataRecord {
    uint8_t icon = 0;
    int8_t useClass = 0;
    int8_t usable = 0;
    static ItemEntry load(const TableReader& r) { return {{}, r.u8(0), r.s8(1), r.s8(2)}; }
};
// the item's entry (the key items' table below kUsableItems, the items' from it)
const ItemEntry& itemEntry(int16_t id);

// a window opened on the language's copy of an exe bank (MenuBanks)
int menuWindowOpen(int8_t win, uint32_t bank, int16_t id);

// ---- the cursors ----
// exe 0x5e83d0(block, levels): the cursor of the row tree (exe table 0x91a7a0: one level per level byte >=
// kFirstChildState; the entry's flags, horizontal, first and end rows, its help bank or sub-table): up / down (left /
// right) with the move's animation and sound, wrapping or clamped; the row's help in window 0; circle / start answer
// kAnswerConfirm, cross kAnswerCancel (the level `levels` back)
void menuCursorStep(StatusScreen& s, int levels);
// exe 0x5e96c0: the lists' cursor and the slide-in reset
void cursorReset(StatusScreen& s);
// exe 0x5e96f0(block, level): back one level (1: to the panel, 2: to the page's rows ...), the cursor on the row it came
// from
void levelBack(StatusScreen& s, int8_t level);
// exe 0x5ea8a0(block, level, count, cols): the two-column list's cursor (row / scroll / column, L1 / R1 a page);
// kAnswerConfirm / kAnswerCancel (the level back), else 0
int twoColumnListStep(StatusScreen& s, int level, int count, int cols);
// exe 0x5e9730: the one-column list's cursor; kAnswerConfirm / kAnswerCancel, else 0
int oneColumnListStep(StatusScreen& s);

// ---- the drawing ----
// exe 0x5e8130(x, y, w, h, style): a frame of 3 x 3 pieces (exe table 0x91a9d8 by style; tpage 7), the middle row /
// column stretched as FT4; style 2 adds the arrow at the right; in kOtFrames
void frameDraw(int x, int y, int w, int h, int style);
constexpr int kFrameRinged = 0, kFrameRound = 1, kFrameArrow = 2;
// exe 0x5e9970(block, table): a level's rows (the cursor's row clut 0x7f13, a row chosen on a deeper level 0x7f50, the
// others the row's own); the rows slide in while slideIn is set
void rowsDraw(StatusScreen& s, uint32_t table);
// exe 0x5e9b40(block, k): page k's rows (and their frame), its title and its frame
void pageDraw(StatusScreen& s, int k);
// exe 0x5e9ca0(block, x, y): the lists' scroll bar (tpage 7, clut 0x7f53) in kOtRows
void statusScrollBarDraw(const StatusScreen& s, int x, int y);
// exe 0x5ea6d0(x, y, item, dim): the item's icon (16 x 16, tpage 0xd, the art of its kind) in kOtText
void itemIconDraw(int x, int y, int16_t id, bool dim);
// the icons' art by kind (exe table 0x91aab0: a sprite's texture word)
uint32_t itemIconArt(uint32_t kind);

} // namespace od::port
