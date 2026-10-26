#include "engine/lang_pack.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <mutex>
#include <stdexcept>

#include "engine/dat.hpp"
#include "engine/game_file.hpp"
#include "engine/lang.hpp"
#include "engine/ps1_container.hpp"
#include "engine/ps1_exe_banks.hpp"
#include "engine/ps1_sfx.hpp"
#include "engine/ps1_xa.hpp"
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
    std::vector<uint8_t> voiceTrack(const std::string&, uint32_t) const override { return {}; } // the folder's snd/
    std::vector<uint8_t> sfxBank(const std::string& datName, uint32_t arg, uint32_t extra,
                                 std::span<const uint8_t>) const override {
        const std::string name = "sfx/" + std::filesystem::path(datName).stem().string() + "_" + std::to_string(arg) +
                                 "_" + std::to_string(extra) + ".bin";
        return has(name) ? file(name) : std::vector<uint8_t>();
    }

private:
    std::filesystem::path dir_;
};

// SLUS_011.40 (USA), RAM addresses (the glyph routine 0x800498c8 and its tables; docs/ps1_disc.md)
constexpr uint32_t kWidthTable = 0x8006b90c;     // 256 glyph widths
constexpr uint32_t kWidthLimitInsn = 0x80049b10; // sltiu v0, v0, limit: glyphs below the limit take the table's width
constexpr uint32_t kTextCluts = 0x80078c0c;      // the CLUT ids of the text colours 0..7 (gp + 0x2ec)
constexpr uint32_t kButtonCluts = 0x8006b988;    // the CLUT ids of the button glyphs 0x51..0x54

// The PS1 title's pictures (COMMON/TITLE.BIN), drawn with the PS1 overlay's geometry: the menu / difficulty pages,
// their CLUT rows, the logo (four strips of 60 lines) and its CLUT
constexpr std::array<std::array<int16_t, 4>, 11> kTitleRects = {{{640, 0, 64, 256}, {704, 0, 64, 256}, {768, 0, 64, 256},
    {0, 496, 48, 1}, {0, 497, 32, 1}, {0, 498, 96, 1}, {640, 256, 272, 60}, {640, 316, 272, 60}, {640, 376, 272, 60},
    {640, 436, 272, 60}, {0, 503, 256, 1}}};

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

        for (const Ps1ExeBank& b : ps1ExeBanks()) files_[std::string("exe/") + b.name + ".bin"] = ps1ExeBank(*disc_, b);
        files_["exe/shop_prices.bin"] = ps1ShopPrices(*disc_);

        // art/TITLE.bin: b"ODVR", u32 count, {u16 x, y, w, h, w * h u16} (the rects over TITLE.DAT's)
        const GameFile title = ps1GameFile("TITLE.DAT", disc_->file("COMMON/TITLE.BIN"));
        std::vector<uint8_t> art = {'O', 'D', 'V', 'R'};
        for (int k = 0; k < 4; ++k) art.push_back(uint8_t(kTitleRects.size() >> (8 * k)));
        for (const auto& r : kTitleRects) {
            const VramRect* found = nullptr;
            for (const FilePart& p : title.parts)
                for (const VramRect& v : p.rects)
                    if (v.x == r[0] && v.y == r[1] && v.w == r[2] && v.h == r[3]) found = &v;
            if (!found || found->pixels.size() != size_t(r[2]) * size_t(r[3]) * 2)
                throw std::runtime_error("language pack (disc): COMMON/TITLE.BIN has no rect " + std::to_string(r[0]) +
                                         "," + std::to_string(r[1]));
            for (const int16_t v : r) {
                art.push_back(uint8_t(v));
                art.push_back(uint8_t(uint16_t(v) >> 8));
            }
            art.insert(art.end(), found->pixels.begin(), found->pixels.end());
        }
        files_["art/TITLE.bin"] = std::move(art);
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

    std::vector<uint8_t> voiceTrack(const std::string& file, uint32_t track) const override {
        // "xa02_37.dat" -> XA/ST02_37.XA (a stage's), "xacom_18.dat" -> XA/COM_18.XA
        if (file.size() < 7 || (file[0] | 0x20) != 'x' || (file[1] | 0x20) != 'a') return {};
        std::string stem = file.substr(2, file.find('.') - 2);
        for (char& c : stem) c = char(std::toupper(uint8_t(c)));
        const DiscFile* xa = disc_->find("XA/" + std::string(std::isdigit(uint8_t(stem[0])) ? "ST" : "") + stem + ".XA");
        if (!xa) return {};
        const std::vector<Ps1XaClip>* clips;
        {
            std::lock_guard lock(clipLock_);
            auto it = clips_.find(xa->path);
            if (it == clips_.end()) it = clips_.emplace(xa->path, ps1XaClips(*disc_, *xa)).first;
            clips = &it->second;
        }
        if (track >= clips->size()) return {};
        const Ps1XaClip& c = (*clips)[track];
        // "ODXA" u8 coding, 3 pad, u32 sectors, then each sector's 18 sound groups
        std::vector<uint8_t> out = {'O', 'D', 'X', 'A', c.coding, 0, 0, 0};
        for (int k = 0; k < 4; ++k) out.push_back(uint8_t(c.sectors.size() >> (8 * k)));
        out.reserve(out.size() + c.sectors.size() * kXaSectorBytes);
        for (const uint32_t lba : c.sectors) {
            const RawSector s = disc_->rawSector(lba);
            out.insert(out.end(), s.begin() + 24, s.begin() + 24 + kXaSectorBytes);
        }
        return out;
    }

    std::vector<uint8_t> sfxBank(const std::string& datName, uint32_t arg, uint32_t extra,
                                 std::span<const uint8_t> pcBank) const override {
        std::string stem = std::filesystem::path(datName).stem().string();
        for (char& c : stem) c = char(std::toupper(uint8_t(c)));
        const DiscFile* f = disc_->find("DAT/" + stem + ".BIN");
        if (!f) f = disc_->find("COMMON/" + stem + ".BIN");
        if (!f) return {};
        const auto bytes = disc_->file(f->path);
        for (const Ps1Block& block : ps1Blocks(bytes)) {
            if (block.type != kPs1SoundBank) continue;
            const auto whole = std::span(bytes).subspan(block.offset, size_t(block.sectors) * kPs1Sector);
            if (ps1SfxBankKey(whole) == (extra << 16 | arg)) return ps1SfxBank(whole, pcBank);
        }
        return {};
    }

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
    mutable std::mutex clipLock_;
    mutable std::map<std::string, std::vector<Ps1XaClip>> clips_; // per XA file, made on its first voice
};

} // namespace

std::unique_ptr<LangPack> folderPack(std::filesystem::path dir) { return std::make_unique<FolderPack>(std::move(dir)); }

std::unique_ptr<LangPack> discPack(std::shared_ptr<const DiscImage> disc, std::string id) {
    return std::make_unique<DiscPack>(std::move(disc), std::move(id));
}

} // namespace od::lang
