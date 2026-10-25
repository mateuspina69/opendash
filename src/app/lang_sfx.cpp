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

void apply(const od::lang::LangPack* pack, uint32_t arg, const Loaded& l) {
    if (pack) {
        try {
            if (const auto b = pack->sfxBank(l.datName, arg, l.extra, l.pc); !b.empty()) {
                od::port::loadSfxBank(b, arg, l.extra);
                SDL_Log("lang: SFX bank %s %u %u (%s)", l.datName.c_str(), arg, l.extra, pack->id().c_str());
                return;
            }
        } catch (const std::exception& e) {
            SDL_Log("lang: SFX bank %s %u %u: %s (the PC's)", l.datName.c_str(), arg, l.extra, e.what());
        }
    }
    od::port::loadSfxBank(l.pc, arg, l.extra);
}

} // namespace

void load(const od::lang::LangPack* pack, std::string_view datName, std::span<const uint8_t> pcSeg,
          uint32_t arg, uint32_t extra) {
    Loaded& l = gSlots[arg];
    l.datName = std::string(datName);
    l.pc.assign(pcSeg.begin(), pcSeg.end());
    l.extra = extra;
    apply(pack, arg, l);
}

void reloadAll(const od::lang::LangPack* pack) {
    for (const auto& [arg, l] : gSlots) apply(pack, arg, l);
}

} // namespace od::lang_sfx
