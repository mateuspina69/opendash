// What a language pack changes in the message system: the system string bank fb 20 / fb 3d read and the exe banks it
// replaces with its own copies (the windows opened on them, fb 44's place names), and a Latin pack's PS1 conventions
// (its scripts' 2-px widths and option columns, the PS1 font's digits and glyph advances). A window keeps the PC's
// conventions when its bank is one of dash2.exe's own (one no pack replaces, e.g. the PC's system messages).
#pragma once

#include <cstdint>

#include "game/message_state.hpp"

namespace od::port {

// The system string bank (the item names, ids - 0x380) fb 20 / fb 3d read: the exe's 0x8b6dfc by default; a language
// pack maps its own copy (the PS1 SLUS bank, same ids) and points here
void setNameBank(uint32_t bank);
uint32_t nameBank();
// the address of system string `id` (an id from 0x380 on) in the name bank
uint32_t systemString(uint16_t id);
// A language pack's copy of an exe bank (mapped by the host): the windows opened on `exeBank` (game/message
// windowOpen) and fb 44's place names read `packBank` instead (the load / save menus' banks 0x8bc918 / 0x8bcacc /
// 0x8b656c); packBank 0 = the exe's own again
void setBankRedirect(uint32_t exeBank, uint32_t packBank);
void clearBankRedirects();
uint32_t bankRedirect(uint32_t exeBank); // the bank read for `exeBank`

// A Latin pack's texts (every bank a PS1 one): fb 06 widths in 2 px units (PS1 0x8a = PC 0x17 columns of 12 px), and
// the number / time formatters' digits (fb 21 / 23, the shop's prices) the PS1's (SLUS_011.40 0x80048ff4: glyph d,
// pad 0x50, minus 0x4f) instead of the PC font's ('0' + d, ' ', '-')
void setPs1Text(bool on);
bool ps1Text();
// The pack font's widths (widths.bin: 256 + the limit): a pack window's layout pass advances its pen by them (the PS1
// glyph routine 0x800498c8) instead of 12 px; nullptr = 12 px (Japanese)
void setPs1Widths(const uint8_t* widths, int limit);
// the pack font's advance of glyph g (12 without a table)
int ps1Advance(uint8_t glyph);
// the pack's widths are loaded (a pack window's layout advances by them)
bool ps1WidthsLoaded();
// the window's text is a pack's (a Latin pack, and its bank not one of dash2.exe's own)
bool ps1Window(const MessageWindow& w);
// the unit of the scripts' columns (fb 06's width, fb 10 / 39's option columns): the PC's 12 px, the PS1 scripts' 2 px
// (SLUS 0x8004cac8 x << 1)
int columnUnit();

// The PS1 conventions applied while a window's codes run: on only when the pack's are on and the window's bank is the
// pack's (restored at the scope's end)
class Ps1TextScope {
public:
    explicit Ps1TextScope(const MessageWindow& w);
    // the PS1 conventions on only when the pack's are and `ps1` asks for them (a host formatting an exe bank's text)
    explicit Ps1TextScope(bool ps1);
    ~Ps1TextScope();
    Ps1TextScope(const Ps1TextScope&) = delete;
    Ps1TextScope& operator=(const Ps1TextScope&) = delete;

private:
    bool previous_;
};

} // namespace od::port
