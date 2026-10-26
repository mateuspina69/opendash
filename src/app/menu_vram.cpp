#include "app/menu_vram.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstring>
#include <exception>
#include <span>
#include <system_error>

#include "app/room_mesh.hpp"
#include "engine/dat.hpp"
#include "engine/data_source.hpp"
#include "game/status_screen.hpp"
#include "engine/lang.hpp"
#include "game/menu_art.hpp"
#include "game/status_options.hpp"
#include "game/status_title.hpp"

namespace od::app {

void MenuVram::reset() {
    vram = {};
    loaded.clear();
    titlePs1 = false;
    fontDat = "INIT.DAT";
    for (const char* f : {"INIT.DAT", "GAME.DAT"}) {
        try {
            applyDat(f, od::dataSource().gameFile(f));
        } catch (const std::exception& e) {
            SDL_Log("menu vram: %s: %s", f, e.what());
        }
    }
    ready = true;
}

void MenuVram::applyDat(const std::string& name, od::GameFile f) {
    vram.apply(f);
    packArt(name);
    art();
    dirty.push_back({0, 0, od::Vram::kWidth, od::Vram::kHeight});
    std::erase_if(loaded, [&](const auto& e) { return e.first == name; }); // a reload replays at its new place
    loaded.emplace_back(name, std::move(f));
}

void MenuVram::setFieldBase(const od::Vram& field) {
    fieldBase = std::make_unique<od::Vram>(field);
    baseGen = g_roomVramGen;
    rebuild();
}

void MenuVram::rebuild() {
    auto l = std::move(loaded);
    vram = fieldBase ? *fieldBase : od::Vram{};
    titlePs1 = false;
    loaded.clear();
    for (auto& [n, f] : l) applyDat(n, std::move(f));
}

void MenuVram::setArtPack(std::shared_ptr<const od::lang::LangPack> pack) {
    if (pack == artPack) return;
    artPack = std::move(pack);
    auto l = std::move(loaded);
    vram = fieldBase ? *fieldBase : od::Vram{};
    titlePs1 = false;
    loaded.clear();
    for (auto& [n, f] : l) applyDat(n, std::move(f));
}

void MenuVram::packArt(const std::string& name) {
    if (!artPack) return;
    const std::string path = "art/" + std::filesystem::path(name).stem().string() + ".bin";
    if (!artPack->has(path)) return;
    try {
        const auto b = artPack->file(path);
        if (b.size() >= 8 && std::memcmp(b.data(), "ODVR", 4) == 0) { // raw VRAM rects {u16 x, y, w, h, w * h u16}
            uint32_t n = 0;
            std::memcpy(&n, b.data() + 4, 4);
            size_t o = 8;
            for (uint32_t i = 0; i < n && o + 8 <= b.size(); ++i) {
                uint16_t r4[4];
                std::memcpy(r4, b.data() + o, 8);
                o += 8;
                const size_t bytes = size_t(r4[2]) * r4[3] * 2;
                if (o + bytes > b.size()) break;
                vram.loadImage(r4[0], r4[1], r4[2], r4[3], std::span<const uint8_t>(b.data() + o, bytes));
                o += bytes;
            }
            if (std::filesystem::path(name).stem() == "TITLE") titlePs1 = true;
            return;
        }
        if (b.size() < 8 || std::memcmp(b.data(), "ODAR", 4) != 0) return;
        uint32_t n = 0;
        std::memcpy(&n, b.data() + 4, 4);
        size_t o = 8;
        for (uint32_t i = 0; i < n && o + 10 <= b.size(); ++i) {
            uint16_t h5[5];
            std::memcpy(h5, b.data() + o, 10);
            o += 10;
            const int tp = h5[0], u = h5[1], v = h5[2], w = h5[3], h = h5[4];
            if (o + size_t(w) * h > b.size()) break;
            const int px = (tp & 0xf) * 64, py = ((tp >> 4) & 1) * 256;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const int uu = u + x, hx = px + (uu >> 2), sh = (uu & 3) * 4;
                    uint16_t cur = vram.pixel(hx, py + v + y);
                    cur = uint16_t((cur & ~(0xf << sh)) | (b[o + size_t(y) * w + x] & 0xf) << sh);
                    const uint8_t le[2] = {uint8_t(cur), uint8_t(cur >> 8)};
                    vram.loadImage(hx, py + v + y, 1, 1, le);
                }
            o += size_t(w) * h;
        }
    } catch (const std::exception& e) {
        SDL_Log("menu art %s (%s): %s", path.c_str(), artPack->id().c_str(), e.what());
    }
}

void MenuVram::art() {
    namespace ma = od::menuart;
    const bool packOptions = artPack && artPack->has("art/SUBSCN06.bin"); // the pack's own pictures of the options page
    std::vector<ma::Glyph> g;
    if (!artPack) { // Japanese: the captions of OpenDash's rows in the PC font
        if (font.empty() && !fontDat.empty()) {
            try {
                const od::GameFile init = od::dataSource().gameFile(fontDat);
                for (const od::FilePart& p : init.parts)
                    if (p.kind == od::FilePart::Kind::PcFont && p.arg == 0) {
                        font = p.bytes;
                        break;
                    }
            } catch (const std::exception& e) {
                SDL_Log("menu art: %s", e.what());
            }
        }
        if (font.empty()) return;
        for (const uint32_t c : {0xbau, 0xadu, 0xbbu}) g.push_back(ma::pcGlyph(font, c)); // げんご
        ma::caption(vram, 7, ma::kCellLanguage.u, ma::kCellLanguage.v, ma::kCellWidth, g);
        g.clear();
        for (const uint16_t c : od::port::kTitleCaptionJp) g.push_back(ma::pcGlyph(font, c));
        ma::caption(vram, ma::kTitleCellTpage, ma::kCellTitle.u, ma::kCellTitle.v, ma::kCellWidth, g);
        return;
    }
    // a pack with the PS1 message font (the disc's, or an installed one): OpenDash's cells in its language
    if (!artPack->has("font.bin") || !artPack->has("widths.bin")) return;
    try {
        const std::vector<uint8_t> page = artPack->file("font.bin"), widths = artPack->file("widths.bin");
        const int lang = od::port::optionsHost().language ? od::port::optionsHost().language() : 1;
        struct Text {
            ma::Cell cell;
            uint16_t tpage;
            const char* en;
            const char* pt;
        };
        constexpr uint16_t kOptionsTpage = 7;
        static constexpr Text kTexts[] = {{ma::kCellLanguage, kOptionsTpage, "Language", "Idioma"},
                                          {ma::kCellVideo, kOptionsTpage, "Video", "Vídeo"},
                                          {ma::kCellKeys[0], kOptionsTpage, "Pad", "Controle"},
                                          {ma::kCellKeys[1], kOptionsTpage, "Keys", "Teclado"},
                                          {ma::kCellKeys[2], kOptionsTpage, "Mouse", "Mouse"},
                                          {ma::kCellTitle, ma::kTitleCellTpage, od::port::kTitleCaptionLatin[1],
                                           od::port::kTitleCaptionLatin[2]}};
        for (const Text& t : kTexts) {
            if (packOptions && t.tpage == kOptionsTpage) continue;
            g.clear();
            for (const uint8_t c : od::lang::ps1Encode(lang == 2 ? t.pt : t.en)) g.push_back(ma::ps1Glyph(page, widths, c));
            ma::caption(vram, t.tpage, t.cell.u, t.cell.v, ma::kCellWidth, g);
        }
    } catch (const std::exception& e) {
        SDL_Log("menu art (%s): %s", artPack->id().c_str(), e.what());
    }
}

MenuVram* g_menuVram = nullptr;
void bindMenuVramIo() {
    od::port::setMenuVram([](const od::port::VramArea& a, std::span<uint8_t> px, bool toVram) {
        if (!g_menuVram) return;
        const int x = a.x, y = a.y, w = a.w, h = a.h;
        if (w <= 0 || h <= 0) return;
        if (toVram) {
            if (g_menuVram->vram.loadImage(x, y, w, h, px))
                g_menuVram->dirty.push_back({x, y, w, h});
        } else {
            g_menuVram->vram.readImage(x, y, w, h, px.data());
        }
    });
}

} // namespace od::app
