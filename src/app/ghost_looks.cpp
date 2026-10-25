// The ghosts' look meshes and slices (see ghost_looks.hpp).
#include "app/ghost_looks.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <map>

#include "engine/dat.hpp"
#include "game/player/player_model.hpp"

namespace od::app {

namespace {
std::string describe(const od::port::PlayerLook& look) {
    char what[64];
    std::snprintf(what, sizeof what, "ghost look kind %u costume %u/%u weapon %d", unsigned(look.kind),
                  unsigned(look.costume), unsigned(look.costumeVariant), int(look.specialWeapon));
    return what;
}
} // namespace

const CharacterMesh* GhostLooks::find(const od::port::PlayerLook& look) const {
    for (const Look& l : looks_)
        if (l.mesh.look == look) return &l.mesh;
    return nullptr;
}

std::string GhostLooks::failure(const od::port::PlayerLook& look) const {
    for (const auto& [l, why] : failed_)
        if (l == look) return why;
    return "";
}

void GhostLooks::sync(const std::vector<od::port::PlayerLook>& wanted, uint32_t generation, int roomSlices,
                      const od::Vram& vram, const od::ghost::ReadFile& read, const SliceSink& sink) {
    if (!started_ || generation != generation_) { // a new room upload: only the room's slices are on the GPU
        started_ = true;
        generation_ = generation;
        looks_.clear();
        failed_.clear();
        free_.clear();
        roomSlices_ = nextSlice_ = roomSlices;
    }
    // the looks no slot wants any more: dropped, their slices free for the next look
    for (auto it = looks_.begin(); it != looks_.end();) {
        if (std::find(wanted.begin(), wanted.end(), it->mesh.look) != wanted.end()) {
            ++it;
            continue;
        }
        SDL_Log("%s dropped (no slot shows it), %zu slices freed", describe(it->mesh.look).c_str(), it->slices.size());
        free_.insert(free_.end(), it->slices.begin(), it->slices.end());
        it = looks_.erase(it);
    }
    for (const od::port::PlayerLook& look : wanted)
        if (!find(look) && failure(look).empty()) build(look, vram, read, sink);
}

void GhostLooks::build(const od::port::PlayerLook& look, const od::Vram& vram, const od::ghost::ReadFile& read,
                       const SliceSink& sink) {
    const std::string what = describe(look);
    auto fail = [&](const std::string& why) {
        SDL_Log("%s not built: %s", what.c_str(), why.c_str());
        failed_.emplace_back(look, why);
    };
    const auto files = od::playerLookFiles(look);
    if (!files) return fail("character not supported (kind != 0 or look outside the tables)");
    try {
        const od::GameFile& body = read(files->body);
        const od::GameFile* weapon = files->weapon.empty() ? nullptr : &read(files->weapon);
        const od::PlayerModel pm = od::decodePlayerModel(body, weapon);
        // the look's own files over a copy of the room's VRAM (not the live VRAM: the CLUT animation and the image
        // moves re-decode the room's slices from it, and it holds the local player's weapon CLUT)
        od::Vram gv = vram;
        gv.apply(body);
        if (weapon) gv.apply(*weapon);
        // its slices: freed ones first (written over), then new ones appended in one batch
        std::map<uint32_t, int> sliceOf;
        std::vector<std::pair<int, std::vector<uint8_t>>> reused;
        std::vector<uint8_t> appended;
        int appendedCount = 0;
        size_t freeUsed = 0;
        auto slice = [&](uint32_t tex) { // tex = (tpage << 16) | clut
            auto [it, added] = sliceOf.try_emplace(tex, 0);
            if (!added) return it->second;
            auto rgba = gv.decode(uint16_t(tex >> 16), uint16_t(tex & 0xFFFF), true);
            if (freeUsed < free_.size()) {
                it->second = free_[free_.size() - 1 - freeUsed++];
                reused.emplace_back(it->second, std::move(rgba));
            } else {
                it->second = nextSlice_ + appendedCount++;
                appended.insert(appended.end(), rgba.begin(), rgba.end());
            }
            return it->second;
        };
        CharacterMesh cm = characterMesh(pm, look, slice);
        if (appendedCount > 0 && !sink.append(appended, appendedCount))
            return fail("the renderer did not take its " + std::to_string(appendedCount) + " new texture slices");
        for (const auto& [index, rgba] : reused) sink.replace(index, rgba);
        free_.resize(free_.size() - freeUsed);
        nextSlice_ += appendedCount;
        Look l{std::move(cm), {}};
        for (const auto& [tex, index] : sliceOf) l.slices.push_back(index);
        SDL_Log("%s: %s%s%s, %zu slices (%d appended, %zu reused)", what.c_str(), files->body.c_str(),
                files->weapon.empty() ? "" : " + ", files->weapon.c_str(), sliceOf.size(), appendedCount, freeUsed);
        looks_.push_back(std::move(l));
    } catch (const std::exception& e) {
        fail(e.what());
    }
}

} // namespace od::app
