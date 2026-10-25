// The player model as the host draws it: one look's model-space mesh with its texture slices, and that mesh posed
// in world space from a posed player's bones (the local Mega Man and the ghosts alike).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "game/player/player_model.hpp"
#include "game/player/player_snapshot.hpp"
#include "game/player/player_state.hpp"
#include "render/renderer.hpp"

namespace od::app {

// One look of the player model (od::port::PlayerLook: body PL00P0xx + special weapon PL00R##): per part, model-space
// triangles (game axes) with texture slices, posed by od::port::playerPose. Texture-cell faces: the
// (tpage << 16) | clut of every vertex of the eyes / mouth parts (7 / 8 = head-block parts 1 / 2 of
// Maybe_DrawPlayerModel 0x5f3440), and this look's slices by key (page offsets 1..3 of those textures included): a
// ghost look decodes its own (its weapon file's CLUT at VRAM (192, 240) gives the same key other texels).
struct CharacterMesh {
    od::port::PlayerLook look; // ghost looks; the local player's mesh is built from g_playerBodyDat / g_playerWeaponDat
    std::vector<std::vector<od::RenderVertex>> parts;
    std::vector<int> bones, blocks; // bone and od::playerPartShown block of each part (hand / buster arm ...)
    std::vector<std::vector<uint32_t>> partTex; // eyes / mouth cell keys (parts 7 / 8)
    std::map<uint32_t, int> sliceOfTex;          // this look's (tpage << 16 | clut) -> slice
};

// debug (--model-cull 0): draw every model face on both sides, as before
extern bool g_modelsBothSides;

// One look's mesh (model-space parts, game axes, double sided like the props); slice(key) gives the texture slice of
// a (tpage << 16) | clut key, recorded in the look's own sliceOfTex
CharacterMesh characterMesh(const od::PlayerModel& pm, const od::port::PlayerLook& look,
                            const std::function<int(uint32_t)>& slice);

// A texture cell offset on a part (FUN_005f3820: the texel word u | v << 8 + the cell's low 16 bits, the tpage + the
// page offset): moves the vertices' UVs and switches their slice to the offset page (preloaded by loadRoom)
void applyTextureCell(const std::map<uint32_t, int>& sliceOfTex, const std::vector<uint32_t>& keys, size_t first,
                      uint32_t uvOff, uint32_t pageOff, od::RenderVertex& v, size_t vi);

// the posed character in world space (FUN_005f3290 / Maybe_DrawPlayerModel 0x5f3440) from the bones of a posed player
// (the local one or a ghost's): bone translations are << the live shift 0x6d00b5, scaled to the room's shift
// (RoomMesh::shift)
std::vector<od::RenderVertex> buildCharacter(int roomShift, const CharacterMesh& cm, const od::port::Player& pl);

} // namespace od::app
