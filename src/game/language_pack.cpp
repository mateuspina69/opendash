// What a language pack changes in the message system (see language_pack.hpp).
#include "game/language_pack.hpp"

#include <algorithm>
#include <array>
#include <map>

#include "game/loaded_data.hpp"

namespace od::port {

namespace {

constexpr uint16_t kSystemStringBase = 0x380; // the first system string's id
constexpr int kPcColumn = 0xc;  // the PC's glyph column (12 px)
constexpr int kPs1Column = 2;   // the PS1 scripts' unit (2 px)

uint32_t gNameBank = 0; // the name bank set (0: none, the exe's system strings TableId::MenuBankItemNames)
std::map<uint32_t, uint32_t> gRedirect; // exe bank -> the pack's copy
bool gPs1Text = false;                  // a Latin pack's PS1 conventions are on (in a window's scope: for its text)
std::array<uint8_t, 256> gPs1Widths{};
int gPs1Limit = 0; // 0 = no table: 12 px

// a bank in the game program's own data (the banks no pack replaces; a pack's and the stage's are loaded files)
bool exeBank(uint32_t b) {
    return !loadedFileMapped(b) && dataSourceBound() && !dataSource().stateData(b, 1).empty();
}

} // namespace

void setNameBank(uint32_t bank) { gNameBank = bank; }
uint32_t nameBank() { return gNameBank != 0 ? gNameBank : stateAddress(TableId::MenuBankItemNames); }
uint32_t systemString(uint16_t id) { return MessageBank(nameBank()).message(uint16_t(id - kSystemStringBase)); }

void setBankRedirect(uint32_t exeBank, uint32_t packBank) {
    if (packBank == 0 || packBank == exeBank) gRedirect.erase(exeBank);
    else gRedirect[exeBank] = packBank;
}
void clearBankRedirects() { gRedirect.clear(); }
uint32_t bankRedirect(uint32_t b) {
    const auto it = gRedirect.find(b);
    return it == gRedirect.end() ? b : it->second;
}

void setPs1Text(bool on) { gPs1Text = on; }
bool ps1Text() { return gPs1Text; }
void setPs1Widths(const uint8_t* widths, int limit) {
    gPs1Limit = widths ? limit : 0;
    if (widths) std::copy_n(widths, 256, gPs1Widths.begin());
}
int ps1Advance(uint8_t g) { return gPs1Limit != 0 && g < gPs1Limit ? gPs1Widths[g] : kPcColumn; }
bool ps1WidthsLoaded() { return gPs1Limit != 0; }
bool ps1Window(const MessageWindow& w) { return gPs1Text && !exeBank(w.bank.value()); }
int columnUnit() { return gPs1Text ? kPs1Column : kPcColumn; }

Ps1TextScope::Ps1TextScope(const MessageWindow& w) : previous_(gPs1Text) { gPs1Text = ps1Window(w); }
Ps1TextScope::Ps1TextScope(bool ps1) : previous_(gPs1Text) { gPs1Text = gPs1Text && ps1; }
Ps1TextScope::~Ps1TextScope() { gPs1Text = previous_; }

} // namespace od::port
