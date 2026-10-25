// The emulated VRAM of the sub-screens (game/status_screen): what the exe keeps resident and the page DATs it loads.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "engine/lang_pack.hpp"
#include "engine/vram.hpp"

namespace od::app {

// The sub-screen's VRAM (game/status_screen): what the exe keeps resident (INIT.DAT, GAME.DAT) and the page DATs it loads;
// the host side of FUN_004012c0 / Maybe_LoadImageToVram for game/status_screen, with the rects written since the last draw
struct MenuVram {
    od::Vram vram;
    bool ready = false;
    std::vector<std::array<int, 4>> dirty;
    std::vector<std::pair<std::string, od::GameFile>> loaded; // the files applied, in order (rebuild)
    std::shared_ptr<const od::lang::LangPack> artPack; // the language pack (its art/), nullptr = Japanese
    bool titlePs1 = false;        // the pack's TITLE art is in VRAM: the title draws with the PS1 overlay's geometry
    void reset();
    // a DAT's VRAM segments, then the language pack's art for it (over the PC's Japanese pictures)
    void applyDat(const std::string& name, od::GameFile f);
    // The exe never clears VRAM: the status sub-screen draws over the field's (the equipment page's weapon model is
    // skinned by the player's weapon pages, PL00R / PL00T, that no sub-screen DAT brings). On entering it the VRAM is
    // rebuilt as the field's with the DATs applied so far (INIT / GAME / the page DATs) over it.
    std::unique_ptr<od::Vram> fieldBase;
    uint32_t baseGen = 0; // g_roomVramGen of fieldBase
    void setFieldBase(const od::Vram& field);
    void rebuild();
    // the language changed: the DATs again with the new pack's art
    void setArtPack(std::shared_ptr<const od::lang::LangPack> pack);
    // the pack's art/<stem>.bin: b"ODAR", u32 count, {u16 tpage, u, v, w, h, w * h 4-bit indices}, or b"ODVR", u32
    // count, {u16 x, y, w, h, w * h u16} VRAM rects
    void packArt(const std::string& name);
    // OpenDash's art (game/menu_art) for Japanese (the packs bring their own): the language row's caption (tpage 7
    // (0xc0, 0xe0)) in the PC font: げんご
    std::vector<uint8_t> font; // INIT.DAT type-7 slot 0
    std::string fontDat; // the game file the font comes from ("": not yet)
    void art();
};

extern MenuVram* g_menuVram;

 // the VRAM of the task running the sub-screen code (play or title)
void bindMenuVramIo();

} // namespace od::app
