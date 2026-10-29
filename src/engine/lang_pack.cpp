#include "engine/lang_pack.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
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

// The top-level string members of a JSON object (a pack's manifest.json: "source", "patch"); other values skipped.
// Throws std::runtime_error on text that is not one JSON object.
std::map<std::string, std::string> jsonStrings(std::string_view j) {
    size_t p = 0;
    auto bad = [&] { return std::runtime_error("manifest.json: bad JSON at byte " + std::to_string(p)); };
    auto ws = [&] {
        while (p < j.size() && std::isspace(uint8_t(j[p]))) ++p;
    };
    auto expect = [&](char c) {
        ws();
        if (p >= j.size() || j[p] != c) throw bad();
        ++p;
    };
    auto hex4 = [&] {
        if (p + 4 > j.size()) throw bad();
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = j[p++];
            if (!std::isxdigit(uint8_t(c))) throw bad();
            v = v << 4 | uint32_t(std::isdigit(uint8_t(c)) ? c - '0' : std::tolower(uint8_t(c)) - 'a' + 10);
        }
        return v;
    };
    auto utf8 = [](std::string& out, uint32_t c) {
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xc0 | c >> 6);
            out += char(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            out += char(0xe0 | c >> 12);
            out += char(0x80 | (c >> 6 & 0x3f));
            out += char(0x80 | (c & 0x3f));
        } else {
            out += char(0xf0 | c >> 18);
            out += char(0x80 | (c >> 12 & 0x3f));
            out += char(0x80 | (c >> 6 & 0x3f));
            out += char(0x80 | (c & 0x3f));
        }
    };
    auto string = [&] {
        expect('"');
        std::string out;
        while (true) {
            if (p >= j.size()) throw bad();
            const char c = j[p++];
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p >= j.size()) throw bad();
            switch (const char e = j[p++]) {
            case '"': case '\\': case '/': out += e; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t c1 = hex4();
                if (c1 >= 0xd800 && c1 < 0xdc00) { // a surrogate pair
                    if (j.substr(p, 2) != "\\u") throw bad();
                    p += 2;
                    const uint32_t c2 = hex4();
                    if (c2 < 0xdc00 || c2 >= 0xe000) throw bad();
                    c1 = 0x10000 + ((c1 - 0xd800) << 10) + (c2 - 0xdc00);
                }
                utf8(out, c1);
                break;
            }
            default: throw bad();
            }
        }
    };
    std::function<void()> skip = [&] {
        ws();
        if (p >= j.size()) throw bad();
        const char c = j[p];
        if (c == '"') {
            string();
        } else if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            ++p;
            ws();
            if (p < j.size() && j[p] == close) {
                ++p;
                return;
            }
            while (true) {
                if (close == '}') {
                    string();
                    expect(':');
                }
                skip();
                ws();
                if (p < j.size() && j[p] == ',') {
                    ++p;
                    ws();
                    continue;
                }
                expect(close);
                return;
            }
        } else { // a number, true, false, null
            const size_t start = p;
            while (p < j.size() && (std::isalnum(uint8_t(j[p])) || j[p] == '-' || j[p] == '+' || j[p] == '.')) ++p;
            if (p == start) throw bad();
        }
    };
    std::map<std::string, std::string> out;
    expect('{');
    ws();
    if (p < j.size() && j[p] == '}') return out;
    while (true) {
        ws();
        std::string key = string();
        expect(':');
        ws();
        if (p < j.size() && j[p] == '"') out[std::move(key)] = string();
        else skip();
        ws();
        if (p < j.size() && j[p] == ',') {
            ++p;
            continue;
        }
        expect('}');
        return out;
    }
}

std::unique_ptr<LangPack> makeDiscPack(std::shared_ptr<const DiscImage> disc, std::string id);

// A pack folder. The files it lacks are made from the disc its manifest.json names (the generator's "source" cue
// and its "patch", the fan translation's VCDIFF over track 1, the disc the folder was made from), as the disc pack
// makes them; the folder's own files come first.
class FolderPack final : public LangPack {
public:
    explicit FolderPack(std::filesystem::path dir) : dir_(std::move(dir)) {}
    std::string id() const override { return dir_.string(); }
    bool has(const std::string& name) const override { return inFolder(name) || (disc() && disc()->has(name)); }
    std::vector<uint8_t> file(const std::string& name) const override {
        if (inFolder(name)) return readFile(dir_ / name);
        if (disc() && disc()->has(name)) return disc()->file(name);
        throw std::runtime_error("language pack: no " + (dir_ / name).string() +
                                 (discError_.empty() ? "" : " (its disc: " + discError_ + ")"));
    }
    std::filesystem::path folder() const override { return dir_; }
    std::vector<uint8_t> voiceTrack(const std::string&, uint32_t) const override { return {}; } // the folder's snd/
    std::vector<uint8_t> sfxBank(const std::string& datName, uint32_t arg, uint32_t extra,
                                 std::span<const uint8_t>) const override {
        const std::string name = "sfx/" + std::filesystem::path(datName).stem().string() + "_" + std::to_string(arg) +
                                 "_" + std::to_string(extra) + ".bin";
        return inFolder(name) ? readFile(dir_ / name) : std::vector<uint8_t>();
    }

private:
    bool inFolder(const std::string& name) const {
        std::error_code ec;
        return std::filesystem::is_regular_file(dir_ / name, ec);
    }
    // the manifest's disc, opened on the first file the folder lacks; nullptr when no manifest names one, or when
    // its image or patch cannot be read (discError_: why)
    const LangPack* disc() const {
        std::call_once(discOnce_, [this] {
            std::error_code ec;
            const auto manifest = dir_ / "manifest.json";
            if (!std::filesystem::is_regular_file(manifest, ec)) return;
            try {
                const auto bytes = readFile(manifest);
                const auto m = jsonStrings(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
                const auto source = m.find("source");
                if (source == m.end()) return;
                const auto path = [](const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); };
                const auto patch = m.find("patch"); // null (no string) for a pack of the disc as it is
                auto image = patch == m.end()
                                 ? std::make_shared<const DiscImage>(path(source->second))
                                 : std::make_shared<const DiscImage>(path(source->second), path(patch->second));
                disc_ = makeDiscPack(std::move(image), dir_.string() + " disc");
            } catch (const std::exception& e) {
                discError_ = e.what();
            }
        });
        return disc_.get();
    }

    std::filesystem::path dir_;
    mutable std::once_flag discOnce_;
    mutable std::unique_ptr<LangPack> disc_;
    mutable std::string discError_;
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

        std::vector<uint8_t> w(ram(kWidthTable, 256), ram(kWidthTable, 256) + 256);
        w.push_back(uint8_t(rd32(ram(kWidthLimitInsn, 4)) & 0xff)); // the immediate's low byte
        files_["widths.bin"] = std::move(w);

        // the rest is made when it is first asked for: a file the disc does not hold as the USA one does (the PT-BR
        // patch's COMMON/TITLE.BIN, its last block's header overwritten) fails only the files made from it
        makers_["font.bin"] = [this] {
            const FilePart* page = init().first(FilePart::Kind::Vram);
            if (!page || page->rects.empty() || page->rects.back().pixels.size() != 0x8000)
                throw std::runtime_error("language pack: COMMON/INIT.BIN's first image is not the 256x256 font page");
            return page->rects.back().pixels;
        };
        makers_["colours.bin"] = [this] { return cluts(initVram(), kTextCluts, 8); };
        makers_["buttons.bin"] = [this] { return cluts(initVram(), kButtonCluts, 4); };
        for (const Ps1ExeBank& b : ps1ExeBanks())
            makers_[std::string("exe/") + b.name + ".bin"] = [this, &b] { return ps1ExeBank(*disc_, b); };
        makers_["exe/shop_prices.bin"] = [this] { return ps1ShopPrices(*disc_); };
        makers_["exe/quiz_answers.bin"] = [this] { return ps1QuizAnswers(*disc_); };

        // art/TITLE.bin: b"ODVR", u32 count, {u16 x, y, w, h, w * h u16} (the rects over TITLE.DAT's)
        makers_["art/TITLE.bin"] = [this] {
            const GameFile title = ps1GameFile("TITLE.DAT", disc_->file("COMMON/TITLE.BIN"));
            std::vector<uint8_t> art = {'O', 'D', 'V', 'R'};
            for (int k = 0; k < 4; ++k) art.push_back(uint8_t(kTitleRects.size() >> (8 * k)));
            for (const auto& r : kTitleRects) {
                const VramRect* found = nullptr;
                for (const FilePart& p : title.parts)
                    for (const VramRect& v : p.rects)
                        if (v.x == r[0] && v.y == r[1] && v.w == r[2] && v.h == r[3]) found = &v;
                if (!found || found->pixels.size() != size_t(r[2]) * size_t(r[3]) * 2)
                    throw std::runtime_error("language pack (disc): COMMON/TITLE.BIN has no rect " +
                                             std::to_string(r[0]) + "," + std::to_string(r[1]));
                for (const int16_t v : r) {
                    art.push_back(uint8_t(v));
                    art.push_back(uint8_t(uint16_t(v) >> 8));
                }
                art.insert(art.end(), found->pixels.begin(), found->pixels.end());
            }
            return art;
        };
    }

    std::string id() const override { return id_; }
    bool has(const std::string& name) const override {
        if (files_.count(name) || makers_.count(name)) return true;
        const std::string bin = messageFile(name);
        if (bin.empty() || !disc_->find(bin)) return false;
        return ps1GameFile(name, disc_->file(bin)).has(FilePart::Kind::Ps1Messages);
    }
    std::vector<uint8_t> file(const std::string& name) const override {
        if (const auto it = files_.find(name); it != files_.end()) return it->second;
        if (const auto it = makers_.find(name); it != makers_.end()) {
            std::lock_guard lock(madeLock_);
            auto m = made_.find(name);
            if (m == made_.end()) m = made_.emplace(name, it->second()).first;
            return m->second;
        }
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

    // COMMON/INIT.BIN (the font page, the text / button CLUTs' colours) and its VRAM, read once; under madeLock_
    const GameFile& init() const {
        if (!init_) init_ = std::make_unique<GameFile>(ps1GameFile("INIT.DAT", disc_->file("COMMON/INIT.BIN")));
        return *init_;
    }
    const Vram& initVram() const {
        if (!initVram_) {
            initVram_ = std::make_unique<Vram>();
            initVram_->apply(init());
        }
        return *initVram_;
    }

    std::shared_ptr<const DiscImage> disc_;
    std::string id_;
    std::vector<uint8_t> exe_;
    uint32_t textAddr_ = 0;
    std::map<std::string, std::vector<uint8_t>> files_;
    std::map<std::string, std::function<std::vector<uint8_t>()>> makers_; // the files made on their first read
    mutable std::mutex madeLock_;
    mutable std::map<std::string, std::vector<uint8_t>> made_;
    mutable std::unique_ptr<GameFile> init_;
    mutable std::unique_ptr<Vram> initVram_;
    mutable std::mutex clipLock_;
    mutable std::map<std::string, std::vector<Ps1XaClip>> clips_; // per XA file, made on its first voice
};

} // namespace

std::unique_ptr<LangPack> folderPack(std::filesystem::path dir) { return std::make_unique<FolderPack>(std::move(dir)); }

std::unique_ptr<LangPack> discPack(std::shared_ptr<const DiscImage> disc, std::string id) {
    return std::make_unique<DiscPack>(std::move(disc), std::move(id));
}

namespace {
std::unique_ptr<LangPack> makeDiscPack(std::shared_ptr<const DiscImage> disc, std::string id) {
    return std::make_unique<DiscPack>(std::move(disc), std::move(id));
}
} // namespace

} // namespace od::lang
