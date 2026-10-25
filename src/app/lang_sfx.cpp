#include "app/lang_sfx.hpp"

#include <SDL3/SDL.h>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "engine/dat.hpp"
#include "game/audio_logic.hpp"

namespace od::lang_sfx {
namespace {

struct Loaded {
    std::string datName;
    std::vector<uint8_t> pc; // the PC segment (reloaded when the language goes back to Japanese)
    uint32_t extra = 0;
};
std::map<uint32_t, Loaded> gSlots; // by slot (TOC arg)

void apply(const std::filesystem::path& packDir, uint32_t arg, const Loaded& l) {
    if (!packDir.empty()) {
        const auto f = packDir / "sfx" /
                       (std::filesystem::path(l.datName).stem().string() + "_" + std::to_string(arg) + "_" +
                        std::to_string(l.extra) + ".bin");
        std::error_code ec;
        if (std::filesystem::exists(f, ec)) {
            const auto b = od::readFile(f);
            od::port::loadSfxBank(b, arg, l.extra);
            SDL_Log("lang: SFX bank %s", f.filename().string().c_str());
            return;
        }
    }
    od::port::loadSfxBank(l.pc, arg, l.extra);
}

} // namespace

void load(const std::filesystem::path& packDir, std::string_view datName, std::span<const uint8_t> pcSeg,
          uint32_t arg, uint32_t extra) {
    Loaded& l = gSlots[arg];
    l.datName = std::string(datName);
    l.pc.assign(pcSeg.begin(), pcSeg.end());
    l.extra = extra;
    apply(packDir, arg, l);
}

void reloadAll(const std::filesystem::path& packDir) {
    for (const auto& [arg, l] : gSlots) apply(packDir, arg, l);
}

} // namespace od::lang_sfx
