// The options page's key configuration (see key_config.hpp), one function per exe function.
#include "game/key_config.hpp"

#include <string>

#include "engine/data_source.hpp"
#include "engine/screen_prim.hpp"
#include "game/audio_logic.hpp"
#include "game/input_bindings.hpp"
#include "game/menu_banks.hpp"
#include "game/menu_draw.hpp"
#include "game/player/player_input.hpp"
#include "game/state_array.hpp"
#include "game/status_controls.hpp"
#include "game/status_options.hpp"
#include "game/status_screen.hpp"

namespace od::port {

using namespace menu_draw;

namespace {

// +2 of the state: set up, the columns, back, an action being set
enum class KeyConfigStep : uint8_t { Enter = 0, Columns = 1, Back = 2, Action = 3 };
// the columns (the cursor)
constexpr int kPadColumn = 0, kKeysColumn = 1, kMouseColumn = 2;
// the actions' PS1 bits by column (exe tables: the pad's 1..8, the keys' nine, the mouse's two)
const DataTable<ScalarTable<uint16_t, 10>> padActions{TableId::KeyConfigPadActions};
const DataTable<ScalarTable<uint16_t, 9>> keyActions{TableId::KeyConfigActions};
const DataTable<ScalarTable<uint16_t, 2>> mouseActions{TableId::KeyConfigMouseActions};
constexpr uint32_t kPadActions = 9, kKeyActions = 9, kMouseActions = 2;
constexpr uint32_t kPadUpperAction = 4; // the pad's fifth action: its own row under the others
// the buttons' art (exe tables: {u8 u, u8 v} by the button's number - 1)
struct ButtonArt : DataRecord {
    uint8_t u = 0, v = 0;
    static ButtonArt load(const TableReader& r) {
        ButtonArt rec;
        rec.u = r.u8(0);
        rec.v = r.u8(1);
        return rec;
    }
};
const DataTable<RecordTable<ButtonArt, 0x10, 2>> padButtonArts{TableId::KeyConfigPadButtonArts};
const DataTable<RecordTable<ButtonArt, 2, 2>> mouseButtonArts{TableId::KeyConfigMouseButtonArts};
// The tables kept on entry (cross restores them) and the action being set
// the art (the device pictures in the 8-bit tpage 8; the PC's half-size sprites)
constexpr uint16_t kPictureMode = 0x88, kOptMode = 7;
constexpr uint32_t kPadPictureArt = 0x3fdc0000, kMousePictureArt = 0x3fdc5a2e, kPadStartArt = 0x3fdcd200;
constexpr uint32_t kButtonArtBase = 0x3fdc0000, kKeyBoxRightArt = 0x3fdcc0e8, kKeyBoxMiddleArt = 0x3fdcc0d0,
                   kKeyBoxLeftArt = 0x3fdcc0c0;
constexpr uint16_t kKeyboardClut = 0x3fdc;
constexpr uint32_t kPanelColour = 0x101010, kCursorColour = 0x101080;
constexpr int8_t kHintWindow = 2;

// the kept tables (the entry's copy, cross's restore)
template <size_t N> void keep(StateArray<uint32_t, N>& to, const BindingTable<N>& from) {
    for (size_t i = 0; i < N; ++i) to[i] = from.word(i);
}
template <size_t N> void restore(BindingTable<N>& to, const StateArray<uint32_t, N>& from) {
    for (size_t i = 0; i < N; ++i) to.setWord(i, from[i]);
}

// exe 0x5ef2f0(block, action): the key configuration of the cursor's column -- the device code pressed now bound to
// the action's PS1 bit, swapped with the action that had it; then the column's picture, the cursor bar and the bound
// codes (the pad's numbered buttons, the keys' names, the mouse's buttons); window 2 the column's hint
void keyConfigure(StatusScreen& s, uint32_t act) {
    const int col = s.cursor;
    OptionsHost& host = optionsHost();
    InputBindings& b = inputBindings();
    if (col == kPadColumn) {
        const int btn = (act != 0 && host.padPressed) ? host.padPressed() : 0;
        if (btn != 0) {
            const uint16_t old = b.pad.codeOf(padActions[act]);
            if (old != btn) {
                for (uint32_t k = 1; k < kPadActions; ++k)
                    if (k != act && b.pad.codeOf(padActions[k]) == btn) {
                        b.pad.bind(padActions[k], old);
                        break;
                    }
                soundPlayFlat(UiSound::Cursor);
                b.pad.bind(padActions[act], uint16_t(btn));
                if (host.bindingsChanged) host.bindingsChanged();
            }
        }
    } else if (col == kKeysColumn) {
        const int key = (!(pad0().held & kPadConfirm) && host.keyPressed) ? host.keyPressed() : 0;
        if (key != 0 && keyNameable(uint32_t(key))) {
            const uint16_t old = b.keys.codeOf(keyActions[act]);
            if (old != key) {
                for (uint32_t k = 0; k < kKeyActions; ++k)
                    if (k != act && b.keys.codeOf(keyActions[k]) == key) {
                        b.keys.bind(keyActions[k], old);
                        break;
                    }
                soundPlayFlat(UiSound::Cursor);
                b.keys.bind(keyActions[act], uint16_t(key));
                if (host.bindingsChanged) host.bindingsChanged();
            }
        }
    } else if (col == kMouseColumn) {
        const int mb = host.mousePressed ? host.mousePressed() : 0;
        if (mb != 0 && b.mouse.codeOf(mouseActions[act]) != mb) {
            soundPlayFlat(UiSound::Cursor);
            const uint16_t old = b.mouse.codeOf(mouseActions[act]);
            b.mouse.bind(mouseActions[act ^ 1], old);
            b.mouse.bind(mouseActions[act], uint16_t(mb));
            if (host.bindingsChanged) host.bindingsChanged();
        }
    }
    if (drawing()) {
        const size_t c = otOpen(kOtRows);
        otDrawMode(c, kOptMode);
        otTile(c, 0x14, 0x37, 0x118, 0x60, kPanelColour, kTileSemi);
        if (col == kPadColumn) {
            otDrawMode(c, kPictureMode);
            otSprite(c, 0x60, 0x3a, kPadPictureArt, 0x80, 0x58, kNeutral, kSpriteFree); // the pad (exe 0x580080's word)
        } else if (col == kKeysColumn) {
            otQuad(c, {0x4c, 0xf4, 0x4c, 0xf4}, {0x4e, 0x4e, 0x7c, 0x7c}, kNeutral, kPolyFT4, {0, 0, 0x2e, 0x2e},
                   {0xfe, 0x58, 0xfe, 0x58}, kKeyboardClut, kPictureMode); // the keyboard, turned
        } else if (col == kMouseColumn) {
            otDrawMode(c, kPictureMode);
            otSprite(c, 0x88, 0x38, kMousePictureArt, 0x34, 0x5a, kNeutral, kSpriteFree);
        }
        otTile(c, 0x14, 0x37, 0x118, 0x60, kPanelColour, kTileSemi);
        if (col == kMouseColumn) otTile(c, int(act) * 0x84 + 0x1c, 0x5e, 0x84, 0x10, kCursorColour, kTileSemi);
        else if (col == kPadColumn && act == kPadUpperAction) otTile(c, 0x1c, 0x7e, 0xa4, 0x10, kCursorColour, kTileSemi);
        else otTile(c, int(act / 5) * 0x84 + 0x1c, int(act % 5) * 0x10 + 0x3e, 0x84, 0x10, kCursorColour, kTileSemi);
        if (col == kPadColumn) {
            otDrawMode(c, kOptMode);
            otSprite(c, 0x82, 0x3e, kPadStartArt, 0x10, 0x10, kNeutral, kSpriteHalf);
            for (uint32_t k = 1; k < kPadActions; ++k) {
                const int n = int(b.pad.codeOf(padActions[k])) - 1;
                const ButtonArt art = padButtonArts[size_t(uint32_t(n))];
                const uint32_t uv = uint32_t(art.u) | uint32_t(art.v) << 8;
                const int x = k == kPadUpperAction ? 0xa2 : int(k / 5) * 0x84 + 0x82;
                const int y = k == kPadUpperAction ? 0x7e : int(k % 5) * 0x10 + 0x3e;
                otSprite(c, x, y, kButtonArtBase | uv, 0x10, 0x10, kNeutral, kSpriteHalf);
            }
        } else if (col == kKeysColumn) {
            otDrawMode(c, kOptMode);
            for (uint32_t k = 0; k < kKeyActions; ++k) {
                const int x0 = int(k / 5) * 0x84, y = int(k % 5) * 0x10 + 0x3e;
                otSprite(c, x0 + 0x96, y, kKeyBoxRightArt, 8, 0x10, kNeutral, kSpriteHalf);
                const std::string name = keyName(b.keys.codeOf(keyActions[k]));
                int w = 0;
                for (size_t i = 1; i < name.size(); ++i) {
                    w += 0xc;
                    otSprite(c, x0 - w + 0x96, y, kKeyBoxMiddleArt, 0xc, 0x10, kNeutral, kSpriteHalf);
                }
                otSprite(c, x0 - w + 0x8e, y, kKeyBoxLeftArt, 8, 0x10, kNeutral, kSpriteHalf);
            }
        } else if (col == kMouseColumn) {
            otDrawMode(c, kPictureMode);
            for (uint32_t k = 0; k < kMouseActions; ++k) {
                const int n = int(b.mouse.codeOf(mouseActions[k])) - 1;
                const ButtonArt art = mouseButtonArts[size_t(uint32_t(n))];
                const uint32_t uv = uint32_t(art.u) | uint32_t(art.v) << 8;
                otSprite(c, int(0x8e + k * 0x84), 0x5e, kButtonArtBase | uv, 0x10, 0x10, kNeutral, kSpriteHalf);
            }
        }
        otDrawMode(c, kOptMode);
    }
    menuWindowOpen(kHintWindow, stateAddress(TableId::MenuKeyConfigListsBank), int16_t(col));
    if (col == kKeysColumn)
        for (uint32_t k = 0; k < kKeyActions; ++k) {
            const std::string name = keyName(b.keys.codeOf(keyActions[k]));
            // x = (column * 11 - strlen + 13) * 12 (the exe's ~n - 1 of its strlen loop is the length itself): the
            // name inside its box (the box has strlen - 1 cells of 12 between two 8 px caps), 2 px in
            asciiTextDraw(name, (int(k / 5) * 0xb - int(name.size()) + 0xd) * 0xc, int(k % 5 + 4) * 0x10, false);
        }
}

} // namespace

void keyConfigState(StatusScreen& s) {
    InputBindings& b = inputBindings();
    KeyConfigKeep& kept = keyConfigKeep();
    const uint16_t pressed = menuPadPressed();
    switch (KeyConfigStep(s.sub)) {
    case KeyConfigStep::Enter:
        cursorReset(s);
        s.cursorKept = 0;
        s.sub += 1;
        s.cursor = 0;
        break;
    case KeyConfigStep::Columns:
        menuCursorStep(s, 2);
        b.rawDevices = 0; // exe 0x40a3e0(0): the bindings drive the game again
        s.cursorKept = uint8_t(s.cursor);
        if (pressed & kPadConfirm) {
            s.sub = uint8_t(KeyConfigStep::Action);
            s.step = uint8_t(s.cursor);
            keep(kept.keys, b.keys);
            keep(kept.pad, b.pad);
            keep(kept.mouse, b.mouse);
            b.rawDevices = 1; // exe 0x40a3e0(1): the devices are read raw (no bindings) while one is set
            kept.action = 0;
        }
        break;
    case KeyConfigStep::Back:
        levelBack(s, 2);
        break;
    case KeyConfigStep::Action: {
        const uint8_t p = uint8_t(pressed);
        int32_t a = kept.action;
        if (p & (kPadLeft | kPadRight)) {
            soundPlayFlat(UiSound::Cursor);
            if (s.cursor == kMouseColumn) {
                a ^= 1;
            } else {
                a = (a + 5) % 10;
                if (a == 9) a = 4;
            }
        }
        if (s.cursor != kMouseColumn) {
            if (p & kPadUp) {
                soundPlayFlat(UiSound::Cursor);
                --a;
                if (a < 0) a = 4;
                else if (a == 4) a = 8;
            }
            if (p & kPadDown) {
                soundPlayFlat(UiSound::Cursor);
                ++a;
                if (a == 5) a = 0;
                else if (a == 9) a = 5;
            }
        }
        kept.action = a;
        if (pressed & (kPadCross | kPadStart)) {
            s.sub = uint8_t(KeyConfigStep::Columns);
            if (pressed & kPadCross) {
                soundPlayFlatForced(uint16_t(UiSound::Cancel));
                restore(b.keys, kept.keys);
                restore(b.pad, kept.pad);
                restore(b.mouse, kept.mouse);
                if (optionsHost().bindingsChanged) optionsHost().bindingsChanged();
                keyConfigure(s, uint32_t(kept.action));
                break;
            }
            soundPlayFlatForced(uint16_t(UiSound::Confirm));
        }
        keyConfigure(s, uint32_t(kept.action));
        break;
    }
    default:
        break;
    }
    rowsDraw(s, optionsValueTable(0));
    menuWindowOpen(0, stateAddress(TableId::MenuKeyConfigHelpBank), s.cursor);
    s.cursor = int8_t(s.cursorKept);
}

KeyConfigKeep& keyConfigKeep() {
    static KeyConfigKeep s;
    return s;
}

} // namespace od::port
