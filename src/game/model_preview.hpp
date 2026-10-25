// The sub-screens' 3D model previews (the equipment page's turning special weapon, the development room's): the exe
// draws those actors with its PS1 GTE emulation (exe 0x5f4160 / 0x5f4980 / 0x5f5500) into the actor ordering table;
// OpenDash (not the game's) captures their polygons in view space for a GPU model preview of the box instead (the user
// wants the previews modern, not the exe's 320 x 240 look). The capture transcribes the exe's per-part walk (the LOD,
// the bone blocks' matrices, the vertex words, the colours, the texture words) and the exe's test of the actor on
// screen; the preview's sprite stands where the exe's polygons would be.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/screen_prim.hpp"
#include "game/actor_state.hpp"

namespace od::port {

struct AnimatedActor;

// OpenDash: the model preview a ScreenPrim::model sprite shows (kPreviewEquipment the equipment page's weapon,
// kPreviewDevRoom the development room's), as captured by the last screen pass that drew it
const ModelPreview& modelPreview(int slot);
constexpr int kPreviewEquipment = 0, kPreviewDevRoom = 1;

// the actors' polygons captured as model preview `slot`, shown by a Sprite (ScreenPrim::model) of the 48 x 48 box
// centred on (cx, cy) in the menu chain c; `centre`, when given, is a world point the capture's camera looks at from up
// to 0x400 in front (the development room's model sits in the field: inside its depth range)
void modelPreviewDraw(size_t c, const std::vector<const AnimatedActor*>& actors, int cx, int cy, int slot,
                      const int32_t* centre = nullptr);

} // namespace od::port
