// The banks the PC keeps in dash2.exe (item names, menus, the shops, the memory card screens ...) and their PS1 copies
// on the USA disc, at fixed raw offsets of SLUS_011.40, COMMON/SUBSCN.BIN, COMMON/DEMO.BIN and the stage overlays:
// the English pack's exe/<name>.bin (engine/lang_pack) and the PS1 data source's bank tables (engine/ps1_data_source).
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "engine/disc_image.hpp"
#include "engine/table_ids.hpp"

namespace od {

struct Ps1ExeBank {
    const char* name;   // the pack's exe/<name>.bin
    TableId table;      // the PC bank's table
    const char* file;   // the disc file
    uint32_t offset;    // its raw offset there
    uint16_t count;     // the message count (the PC bank's)
};
std::span<const Ps1ExeBank> ps1ExeBanks();

// The bank: its n u16 offsets (the first one 2n, non-decreasing) up to the 0xff after its last message (a last entry
// that is no message runs to the next 0xff, at most 0x400 bytes); throws std::runtime_error when the disc has no such
// bank there. The system bank's message 0xb (the PS1's "No MEMORY CARD detected!") points at its 0xd: the PC reuses id
// 0xb for "no save data", the bytes of its 0xd.
std::vector<uint8_t> ps1ExeBank(const DiscImage& disc, const Ps1ExeBank& bank);

// the shop list's 7 price strings (7 x 12 bytes: u16 2, 7 digits, the zenny sign, fb 1f) of DAT/ST0AT.BIN, the PC's
// MenuBankShopPriceStrings; throws std::runtime_error when they are not there
std::vector<uint8_t> ps1ShopPrices(const DiscImage& disc);

} // namespace od
