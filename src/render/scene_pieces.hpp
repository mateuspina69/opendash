#pragma once
// The scenery's pieces (OpenDash's see-through): a wall, a table top, a pillar, an awning. The see-through opens only
// the pieces that stand between the camera and a character in focus (app/see_through), so the room is split into
// them: triangles joined by a shared edge whose faces turn less than kPieceCrease from each other.

#include "render/renderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace od {

struct ScenePieces {
    std::vector<uint32_t> of;              // per triangle: its piece (0-based)
    std::vector<std::array<float, 6>> box; // per piece: its bounds (lo x, y, z, hi x, y, z)
};

// The pieces of triangles tris[0 .. count) (count / 3 triangles). A triangle with no area is a piece of its own.
ScenePieces scenePieces(const RenderVertex* tris, size_t count);

// The drawn scenery among the dynamic objects: the room's live parts (switchable segments, doors) and the static
// pool's objects, opaque
bool isSceneryObject(const DynamicObject& o);

// The distance along d (0..1 of its length) at which the segment o -> o + d meets triangle a b c, or a negative value
// when it does not (either side of the triangle)
float segmentHitsTriangle(const float* o, const float* d, const float* a, const float* b, const float* c);

} // namespace od
