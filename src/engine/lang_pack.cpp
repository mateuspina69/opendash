#include "engine/lang_pack.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

#include "engine/dat.hpp"
#include "engine/game_file.hpp"
#include "engine/vram.hpp"

namespace od::lang {

namespace {

class FolderPack final : public LangPack {
public:
    explicit FolderPack(std::filesystem::path dir) : dir_(std::move(dir)) {}
    std::string id() const override { return dir_.string(); }
    bool has(const std::string& name) const override {
        std::error_code ec;
        return std::filesystem::is_regular_file(dir_ / name, ec);
    }
    std::vector<uint8_t> file(const std::string& name) const override {
        if (!has(name)) throw std::runtime_error("language pack: no " + (dir_ / name).string());
        return readFile(dir_ / name);
    }
    std::filesystem::path folder() const override { return dir_; }

private:
    std::filesystem::path dir_;
};

// SLUS_011.40 (USA), RAM addresses (the glyph routine 0x800498c8 and its tables; docs/ps1_disc.md)
constexpr uint32_t kWidthTable = 0x8006b90c;     // 256 glyph widths
constexpr uint32_t kWidthLimitInsn = 0x80049b10; // sltiu v0, v0, limit: glyphs below the limit take the table's width
constexpr uint32_t kTextCluts = 0x80078c0c;      // the CLUT ids of the text colours 0..7 (gp + 0x2ec)
constexpr uint32_t kButtonCluts = 0x8006b988;    // the CLUT ids of the button glyphs 0x51..0x54

// The banks the PC keeps in dash2.exe and their PS1 (USA) copies: the disc file, the raw file offset and the message
// count (the PC bank's). The shop's 7 price strings are no bank: 7 x 12 bytes (u16 2, 7 digits, the zenny sign, fb 1f).
struct ExeBank {
    const char* name;
    const char* file;
    uint32_t offset;
    uint16_t count;
};
constexpr ExeBank kExeBanks[] = {
    {"item_names", "SLUS_011.40", 0x5d7cc, 263},   {"item_descs", "SLUS_011.40", 0x5e5c0, 263},
    {"status_help", "SLUS_011.40", 0x5cad8, 6},    {"status_misc", "SLUS_011.40", 0x5ca64, 6},
    {"system", "SLUS_011.40", 0x5cf58, 30},        {"items_help", "COMMON/SUBSCN.BIN", 0x353e4, 4},
    {"item_use", "COMMON/SUBSCN.BIN", 0x35440, 37}, {"areas", "SLUS_011.40", 0x60b2c, 117},
    {"opt_help", "COMMON/SUBSCN.BIN", 0x370a0, 10}, {"opt_camera", "COMMON/SUBSCN.BIN", 0x37454, 3},
    {"opt_lock", "COMMON/SUBSCN.BIN", 0x374cc, 3},  {"opt_lockmode", "COMMON/SUBSCN.BIN", 0x37540, 3},
    {"opt_vibration", "COMMON/SUBSCN.BIN", 0x375b0, 3}, {"opt_sound", "COMMON/SUBSCN.BIN", 0x37644, 3},
    {"opt_volume", "COMMON/SUBSCN.BIN", 0x37698, 4}, {"opt_defaults", "COMMON/SUBSCN.BIN", 0x377a4, 2},
    {"equip_help", "COMMON/SUBSCN.BIN", 0x36530, 5}, {"equip_weapon", "COMMON/SUBSCN.BIN", 0x365cc, 4},
    {"equip_buster", "COMMON/SUBSCN.BIN", 0x36650, 4}, {"equip_sort", "COMMON/SUBSCN.BIN", 0x366c8, 9},
    {"equip_body", "COMMON/SUBSCN.BIN", 0x367dc, 5}, {"shop", "DAT/ST0AT.BIN", 0x8e88, 101},
    {"places", "SLUS_011.40", 0x5d598, 29},        {"load_menu", "COMMON/DEMO.BIN", 0x419c, 8},
    {"save_menu", "DAT/ST04T.BIN", 0x9400, 9},     {"dev_room", "DAT/ST04T.BIN", 0xb5e4, 75},
};
constexpr const char* kShopPricesFile = "DAT/ST0AT.BIN";
constexpr uint32_t kShopPrices = 0xa894;
// The PC reuses the system bank's message 0xb (the PS1's "No MEMORY CARD detected!") for "no save data", the bytes of
// its 0xd: the copy points 0xb at the PS1's 0xd.
constexpr int kSystemNoSave = 0xb, kSystemNoSaveText = 0xd;

// the bank at `at` (n u16 offsets, the first one 2n, non-decreasing, the last message ending 0xff) up to the 0xff
// after its last offset (a last entry that is no message runs to the next 0xff, at most 0x400 bytes)
std::vector<uint8_t> exeBank(const std::vector<uint8_t>& d, const ExeBank& b) {
    const size_t at = b.offset, n = b.count;
    auto fail = [&] { throw std::runtime_error(std::string("language pack (disc): no ") + b.name + " bank at " +
                                               b.file + " offset " + std::to_string(at)); };
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
    return {d.begin() + std::ptrdiff_t(at), d.begin() + std::ptrdiff_t(end)};
}

class DiscPack final : public LangPack {
public:
    DiscPack(std::shared_ptr<const DiscImage> disc, std::string id) : disc_(std::move(disc)), id_(std::move(id)) {
        const auto cnf = disc_->file("SYSTEM.CNF");
        if (std::string(cnf.begin(), cnf.end()).find("SLUS_011.40") == std::string::npos)
            throw std::runtime_error("language pack: the disc is not the USA release (no SLUS_011.40 boot)");
        exe_ = disc_->file("SLUS_011.40");
        if (exe_.size() < 0x800 || std::string(exe_.begin(), exe_.begin() + 8) != "PS-X EXE")
            throw std::runtime_error("language pack: SLUS_011.40 is not a PS-X EXE");
        textAddr_ = rd32(exe_.data() + 0x18);

        const GameFile init = ps1GameFile("INIT.DAT", disc_->file("COMMON/INIT.BIN"));
        const FilePart* page = init.first(FilePart::Kind::Vram);
        if (!page || page->rects.empty() || page->rects.back().pixels.size() != 0x8000)
            throw std::runtime_error("language pack: COMMON/INIT.BIN's first image is not the 256x256 font page");
        files_["font.bin"] = page->rects.back().pixels;

        std::vector<uint8_t> w(ram(kWidthTable, 256), ram(kWidthTable, 256) + 256);
        w.push_back(uint8_t(rd32(ram(kWidthLimitInsn, 4)) & 0xff)); // the immediate's low byte
        files_["widths.bin"] = std::move(w);

        Vram vram;
        vram.apply(init);
        files_["colours.bin"] = cluts(vram, kTextCluts, 8);
        files_["buttons.bin"] = cluts(vram, kButtonCluts, 4);

        std::map<std::string, std::vector<uint8_t>> raw; // the disc files the banks are in
        auto fileBytes = [&](const char* path) -> const std::vector<uint8_t>& {
            auto it = raw.find(path);
            if (it == raw.end()) it = raw.emplace(path, disc_->file(path)).first;
            return it->second;
        };
        for (const ExeBank& b : kExeBanks) {
            std::vector<uint8_t> bank = exeBank(fileBytes(b.file), b);
            if (std::string_view(b.name) == "system")
                std::copy_n(bank.begin() + 2 * kSystemNoSaveText, 2, bank.begin() + 2 * kSystemNoSave);
            files_[std::string("exe/") + b.name + ".bin"] = std::move(bank);
        }
        const std::vector<uint8_t>& shop = fileBytes(kShopPricesFile);
        if (kShopPrices + 7 * 12 > shop.size()) throw std::runtime_error("language pack (disc): no shop prices");
        for (int k = 0; k < 7; ++k) {
            const uint8_t* p = shop.data() + kShopPrices + k * 12;
            if (rd16(p) != 2 || p[10] != 0xfb || p[11] != 0x1f)
                throw std::runtime_error("language pack (disc): the shop prices are not at " + std::string(kShopPricesFile));
        }
        files_["exe/shop_prices.bin"].assign(shop.begin() + kShopPrices, shop.begin() + kShopPrices + 7 * 12);
    }

    std::string id() const override { return id_; }
    bool has(const std::string& name) const override {
        if (files_.count(name)) return true;
        const std::string bin = messageFile(name);
        if (bin.empty() || !disc_->find(bin)) return false;
        return ps1GameFile(name, disc_->file(bin)).has(FilePart::Kind::Ps1Messages);
    }
    std::vector<uint8_t> file(const std::string& name) const override {
        if (const auto it = files_.find(name); it != files_.end()) return it->second;
        const std::string bin = messageFile(name);
        if (bin.empty() || !disc_->find(bin)) throw std::runtime_error("language pack (disc): no " + name);
        return ps1GameFile(name, disc_->file(bin)).require(FilePart::Kind::Ps1Messages).bytes;
    }
    std::filesystem::path folder() const override { return {}; }

private:
    // "msg/ST09.bin" -> "DAT/ST09.BIN" ("" for any other name)
    static std::string messageFile(const std::string& name) {
        if (name.rfind("msg/", 0) != 0 || name.size() < 9 || name.compare(name.size() - 4, 4, ".bin") != 0) return {};
        return "DAT/" + name.substr(4, name.size() - 8) + ".BIN";
    }
    const uint8_t* ram(uint32_t addr, uint32_t n) const {
        const size_t off = size_t(addr - textAddr_) + 0x800;
        if (addr < textAddr_ || off + n > exe_.size()) throw std::runtime_error("language pack: SLUS address out of the exe");
        return exe_.data() + off;
    }
    // n CLUTs of 16 colours: the u16 CLUT ids at `table` (x = (id & 0x3f) * 16, y = id >> 6), read from VRAM
    std::vector<uint8_t> cluts(const Vram& vram, uint32_t table, int n) const {
        std::vector<uint8_t> out(size_t(n) * 32);
        for (int k = 0; k < n; ++k) {
            const uint16_t id = rd16(ram(table + uint32_t(k) * 2, 2));
            vram.readImage((id & 0x3f) * 16, id >> 6, 16, 1, out.data() + size_t(k) * 32);
        }
        return out;
    }

    std::shared_ptr<const DiscImage> disc_;
    std::string id_;
    std::vector<uint8_t> exe_;
    uint32_t textAddr_ = 0;
    std::map<std::string, std::vector<uint8_t>> files_;
};

} // namespace

std::unique_ptr<LangPack> folderPack(std::filesystem::path dir) { return std::make_unique<FolderPack>(std::move(dir)); }

std::unique_ptr<LangPack> discPack(std::shared_ptr<const DiscImage> disc, std::string id) {
    return std::make_unique<DiscPack>(std::move(disc), std::move(id));
}

} // namespace od::lang
