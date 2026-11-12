#include "app/lang_sfx.hpp"

#include <SDL3/SDL.h>
#include <cctype>
#include <filesystem>
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
    std::vector<uint8_t> used; // the bank loaded (the pack's or the PC's): its entries name their samples
};
std::map<uint32_t, Loaded> gSlots; // by slot (TOC arg)
Heard gHeard;

// od::port::gSfxStarted: the entry's sample (byte 0 of its 8-byte record, after the table's 4-byte size), keyed as
// ferramentas/extrai_falas_sfx.py names the samples
void started(uint8_t slot, uint8_t entry) {
    const auto it = gSlots.find(slot);
    if (it == gSlots.end()) return;
    const Loaded& l = it->second;
    const size_t at = 4 + size_t(entry) * 8;
    if (at >= l.used.size()) return;
    std::string stem = std::filesystem::path(l.datName).stem().string();
    for (char& c : stem) c = char(std::toupper(uint8_t(c)));
    gHeard.key = stem + "_" + std::to_string(slot) + "_" + std::to_string(l.extra) + "_s" + std::to_string(l.used[at]);
    gHeard.ns = SDL_GetTicksNS();
    ++gHeard.serial;
}

void apply(const od::lang::LangPack* pack, uint32_t arg, Loaded& l) {
    od::port::gSfxStarted = started;
    if (pack) {
        try {
            if (const auto b = pack->sfxBank(l.datName, arg, l.extra, l.pc); !b.empty()) {
                od::port::loadSfxBank(b, arg, l.extra);
                l.used = b;
                SDL_Log("lang: SFX bank %s %u %u (%s)", l.datName.c_str(), arg, l.extra, pack->id().c_str());
                return;
            }
        } catch (const std::exception& e) {
            SDL_Log("lang: SFX bank %s %u %u: %s (the PC's)", l.datName.c_str(), arg, l.extra, e.what());
        }
    }
    od::port::loadSfxBank(l.pc, arg, l.extra);
    l.used = l.pc;
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
    for (auto& [arg, l] : gSlots) apply(pack, arg, l);
}

const Heard& lastHeard() { return gHeard; }

} // namespace od::lang_sfx
