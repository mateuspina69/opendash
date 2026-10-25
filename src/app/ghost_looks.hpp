// The meshes of the ghosts' looks (body PL00P + special weapon PL00R) and their texture slices, kept apart from the
// room's: a new look is built alone -- only its two files decoded, over a copy of the room's VRAM (its weapon's CLUT at
// (192, 240) differs per weapon) -- and its slices appended to the uploaded texture array (or written over the slices
// of a look no slot uses any more), without rebuilding the room or resetting the motion history. The set follows the
// looks the slots want: a look no slot wants is dropped and its slices freed.
//
// A room load (a new localGeneration) uploads only the room's slices: the looks are built again after it, from the
// new VRAM.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "app/character_mesh.hpp"
#include "engine/vram.hpp"
#include "game/ghost.hpp"
#include "game/player/player_snapshot.hpp"

namespace od::app {

// the renderer's slice paths (od::Renderer::appendSlices / updateSlice)
struct SliceSink {
    std::function<bool(const std::vector<uint8_t>& rgba, int count)> append; // after the uploaded slices
    std::function<void(int slice, const std::vector<uint8_t>& rgba)> replace;
};

class GhostLooks {
public:
    // Makes the set the `wanted` looks for the room upload of `generation` (roomSlices = its slice count, vram = its
    // VRAM): missing looks are built, unwanted ones dropped. A look that cannot be built (unsupported, a file missing,
    // the renderer refusing the slices) is not retried in that generation; failure() says why.
    void sync(const std::vector<od::port::PlayerLook>& wanted, uint32_t generation, int roomSlices, const od::Vram& vram,
              const od::ghost::ReadFile& read, const SliceSink& sink);
    // the look's mesh (null: not built)
    const CharacterMesh* find(const od::port::PlayerLook& look) const;
    // why a wanted look is not built ("" when it is)
    std::string failure(const od::port::PlayerLook& look) const;
    size_t size() const { return looks_.size(); }
    // the slices past the room's in use or free (appended so far in this generation)
    int appendedSlices() const { return nextSlice_ - roomSlices_; }

private:
    struct Look {
        CharacterMesh mesh;
        std::vector<int> slices;
    };
    void build(const od::port::PlayerLook& look, const od::Vram& vram, const od::ghost::ReadFile& read,
               const SliceSink& sink);

    std::vector<Look> looks_;
    std::vector<std::pair<od::port::PlayerLook, std::string>> failed_;
    bool started_ = false;
    uint32_t generation_ = 0;
    int roomSlices_ = 0, nextSlice_ = 0; // the room's slice count; the first slice past those appended
    std::vector<int> free_;               // slices of dropped looks
};

} // namespace od::app
