// The room UV animation on screen: the e0 controller 0x418350 (game/room_uv_animation) rewrites the 8-byte UV pairs of
// room faces in the room block every few ticks (0x4183d0, ST00 / ST03 sub 1, ST0A sub 2); the renderer built the room
// geometry once from the file. RoomUvAnim indexes, per face UV address (RoomQuad::uvOfs), the vertices loadRoom made of
// that face -- static room vertices (patched in the renderer's vertex buffer, Renderer::updateStaticUvs) or a
// switchable segment quad (SegQuad, drawn per tick by liveScene) -- and applies the queued RoomUvWrites to them.
// The texture page / CLUT of a face is not part of the UV pair (strip tpage table + face d1 >> 24), so the face keeps
// its texture slice: only the UVs change.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "engine/room.hpp"
#include "render/renderer.hpp"

namespace od {

class RoomUvAnim {
public:
    // loadRoom, per room quad, before its 6 vertices are appended (quads without uvOfs are ignored):
    //  bucket 0 = mesh.tris (opaque), 1..3 = semiTris[bucket - 1]; first = the bucket's size before the append
    void addStatic(const RoomQuad& q, int bucket, uint32_t first);
    void addSeg(const RoomQuad& q, uint32_t segQuad); // index into mesh.segQuads
    // after mesh.semiStart is set: the semi buckets' indices become absolute vertex indices
    void finalize(const std::array<uint32_t, 4>& semiStart);

    // bound = the room block mapped at blockBase in the loaded files is the one this mesh was built from (the shown
    // room). Then the faces the game rewrote since the last call (od::port::takeRoomUvWrites) and the faces of the
    // animation list are re-read from the loaded files and their changed UVs applied: static vertices ->
    // renderer.updateStaticUvs, switchable quads -> segVerts(index) patched in place. Not bound: the queued writes
    // are dropped.
    void apply(bool bound, uint32_t blockBase, Renderer& renderer,
               const std::function<std::array<RenderVertex, 6>*(uint32_t)>& segVerts);

    size_t faces() const { return refs_.size(); }
    // after finalize: the static faces' first vertex moved (old -> new); faces not in the map are dropped
    void remapStatic(const std::unordered_map<uint32_t, uint32_t>& firstOf);

private:
    struct Ref {
        uint32_t first;  // static: first of the 6 vertices; seg: SegQuad index
        uint8_t bucket;  // 0..3 static (see addStatic), 4 = seg
        uint32_t w0, w1; // the UV pair the vertices show
    };
    void patch(Ref& r, uint32_t w0, uint32_t w1, std::vector<StaticUvPatch>& patches,
               const std::function<std::array<RenderVertex, 6>*(uint32_t)>& segVerts);
    std::unordered_multimap<uint32_t, Ref> refs_; // block offset of the UV pair -> the face's vertices
    bool finalized_ = false;
};

} // namespace od
