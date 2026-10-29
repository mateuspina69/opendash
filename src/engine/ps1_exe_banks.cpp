#include "engine/ps1_exe_banks.hpp"

#include <algorithm>
#include <stdexcept>

#include "engine/dat.hpp"

namespace od {

namespace {

constexpr Ps1ExeBank kBanks[] = {
    {"item_names", TableId::MenuBankItemNames, "SLUS_011.40", 0x5d7cc, 263},
    {"item_descs", TableId::MenuBankItemDescriptions, "SLUS_011.40", 0x5e5c0, 263},
    {"status_help", TableId::MenuBankPanelHelp, "SLUS_011.40", 0x5cad8, 6},
    {"status_misc", TableId::MenuBankWeaponSwitchMessages, "SLUS_011.40", 0x5ca64, 6},
    {"system", TableId::SaveMenusCardMessageBank, "SLUS_011.40", 0x5cf58, 30},
    {"map_help", TableId::MenuBankMapHelp, "COMMON/SUBSCN.BIN", 0x350dc, 5},
    {"map_display", TableId::MenuBankMapDisplay, "COMMON/SUBSCN.BIN", 0x35170, 4},
    {"items_help", TableId::MenuBankItemsHelp, "COMMON/SUBSCN.BIN", 0x353e4, 4},
    {"item_use", TableId::MenuBankItemUse, "COMMON/SUBSCN.BIN", 0x35440, 37},
    {"areas", TableId::MenuBankAreaNames, "SLUS_011.40", 0x60b2c, 117},
    {"opt_help", TableId::MenuBankOptionsHelp, "COMMON/SUBSCN.BIN", 0x370a0, 10},
    {"opt_camera", TableId::MenuBankOptionsCamera, "COMMON/SUBSCN.BIN", 0x37454, 3},
    {"opt_lock", TableId::MenuBankOptionsLockOn, "COMMON/SUBSCN.BIN", 0x374cc, 3},
    {"opt_lockmode", TableId::MenuBankOptionsLockOnMode, "COMMON/SUBSCN.BIN", 0x37540, 3},
    {"opt_vibration", TableId::MenuBankOptionsVibration, "COMMON/SUBSCN.BIN", 0x375b0, 3},
    {"opt_sound", TableId::MenuBankOptionsSound, "COMMON/SUBSCN.BIN", 0x37644, 3},
    {"opt_volume", TableId::MenuBankOptionsVolume, "COMMON/SUBSCN.BIN", 0x37698, 4},
    {"opt_defaults", TableId::MenuBankOptionsDefaults, "COMMON/SUBSCN.BIN", 0x377a4, 2},
    {"equip_help", TableId::MenuBankEquipmentHelp, "COMMON/SUBSCN.BIN", 0x36530, 5},
    {"equip_weapon", TableId::MenuBankEquipmentWeapon, "COMMON/SUBSCN.BIN", 0x365cc, 4},
    {"equip_buster", TableId::MenuBankEquipmentBuster, "COMMON/SUBSCN.BIN", 0x36650, 4},
    {"equip_sort", TableId::MenuBankEquipmentSort, "COMMON/SUBSCN.BIN", 0x366c8, 9},
    {"equip_body", TableId::MenuBankEquipmentBody, "COMMON/SUBSCN.BIN", 0x367dc, 5},
    {"shop", TableId::MenuBankShop, "DAT/ST0AT.BIN", 0x8e88, 101},
    {"places", TableId::TextScriptPlaceNameBank, "SLUS_011.40", 0x5d598, 29},
    {"load_menu", TableId::SaveMenusLoadTextBank, "COMMON/DEMO.BIN", 0x419c, 8},
    {"save_menu", TableId::SaveMenusSaveTextBank, "DAT/ST04T.BIN", 0x9400, 9},
    {"dev_room", TableId::DevRoomBank, "DAT/ST04T.BIN", 0xb5e4, 75},
    {"quiz", TableId::St32QuizQuestionBank, "DAT/ST32T.BIN", 0x4b58, 212}, // the quiz show's questions (USA: 212)
};
// the quiz show's right answers (1..4) of its 200 questions, in the overlay's data before the question bank
constexpr const char* kQuizFile = "DAT/ST32T.BIN";
constexpr uint32_t kQuizAnswers = 0x4a74;
constexpr const char* kShopPricesFile = "DAT/ST0AT.BIN";
constexpr uint32_t kShopPrices = 0xa894;
constexpr int kSystemNoSave = 0xb, kSystemNoSaveText = 0xd;

} // namespace

std::span<const Ps1ExeBank> ps1ExeBanks() { return kBanks; }

std::vector<uint8_t> ps1ExeBank(const DiscImage& disc, const Ps1ExeBank& b) {
    const std::vector<uint8_t> d = disc.file(b.file);
    const size_t at = b.offset, n = b.count;
    auto fail = [&] {
        throw std::runtime_error(std::string("PS1 exe banks: no ") + b.name + " bank at " + b.file + " offset " +
                                 std::to_string(at));
    };
    if (at + 2 * n > d.size() || rd16(d.data() + at) != 2 * n) fail();
    uint16_t last = 0;
    for (size_t k = 0; k < n; ++k) {
        const uint16_t o = rd16(d.data() + at + 2 * k);
        if (k > 0 && o < rd16(d.data() + at + 2 * (k - 1))) fail();
        last = std::max(last, o);
    }
    if (at + last > d.size() || d[at + rd16(d.data() + at + 2 * (n - 1)) - 1] != 0xff) fail();
    size_t end = at + last;
    while (end < d.size() && d[end] != 0xff) ++end;
    end = std::min({end + 1, d.size(), at + last + 0x400});
    std::vector<uint8_t> bank(d.begin() + std::ptrdiff_t(at), d.begin() + std::ptrdiff_t(end));
    if (b.table == TableId::SaveMenusCardMessageBank)
        std::copy_n(bank.begin() + 2 * kSystemNoSaveText, 2, bank.begin() + 2 * kSystemNoSave);
    return bank;
}

std::vector<uint8_t> ps1QuizAnswers(const DiscImage& disc) {
    const std::vector<uint8_t> d = disc.file(kQuizFile);
    if (kQuizAnswers + kPs1QuizQuestions > d.size()) throw std::runtime_error("PS1 exe banks: no quiz answers");
    const auto first = d.begin() + std::ptrdiff_t(kQuizAnswers);
    if (!std::all_of(first, first + kPs1QuizQuestions, [](uint8_t a) { return a >= 1 && a <= 4; }))
        throw std::runtime_error(std::string("PS1 exe banks: the quiz answers are not at ") + kQuizFile);
    return {first, first + kPs1QuizQuestions};
}

std::vector<uint8_t> ps1ShopPrices(const DiscImage& disc) {
    const std::vector<uint8_t> shop = disc.file(kShopPricesFile);
    if (kShopPrices + 7 * 12 > shop.size()) throw std::runtime_error("PS1 exe banks: no shop prices");
    for (int k = 0; k < 7; ++k) {
        const uint8_t* p = shop.data() + kShopPrices + k * 12;
        if (rd16(p) != 2 || p[10] != 0xfb || p[11] != 0x1f)
            throw std::runtime_error(std::string("PS1 exe banks: the shop prices are not at ") + kShopPricesFile);
    }
    return {shop.begin() + kShopPrices, shop.begin() + kShopPrices + 7 * 12};
}

} // namespace od
