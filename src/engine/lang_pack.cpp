#include "engine/lang_pack.hpp"

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
