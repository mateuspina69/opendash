#pragma once
// The see-through (OpenDash, Baldur's Gate style): the scenery pieces (render/scene_pieces) that stand between the
// camera and a character in focus open. Each logic tick casts segments from the eye to points spread over each
// character's box against the room's opaque triangles and the dynamic scenery's (segments, doors, the static pool's
// objects); a piece crossed by enough of them hides that character. Each frame the pieces' openings ease toward it
// (open fast, held a moment, closed slower), and the renderer gets the open ones (renderer SeeThroughPiece).

#include "render/renderer.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

class SeeThrough {
public:
    // A room was loaded: numbers its pieces into tris (RenderVertex::piece, 1 ..) and keeps its opaque triangles
    // (tris[0 .. opaqueEnd)) for the segments. Every piece state of the last room is dropped
    void roomLoaded(std::vector<od::RenderVertex>& tris, size_t opaqueEnd);
    // A logic tick: numbers the dynamic scenery's pieces (RenderVertex::piece from kDynamicPieces on) and finds the
    // pieces hiding each body (eye and bodies in the room's units; no bodies: nothing hides anything)
    void tick(std::vector<od::DynamicObject>& objects, const float eye[3], const std::vector<od::SeeThroughBody>& bodies);
    // A frame: the openings ease toward the last tick's findings by dt seconds; out = the open pieces, the most open
    // first (at most od::kMaxSeeThroughPieces)
    void frame(float dt, std::vector<od::SeeThroughPiece>& out);
    void clear() { states_.clear(); }

    static constexpr uint32_t kDynamicPieces = 1u << 20; // the room's pieces stay below (RenderVertex::piece)

private:
    struct Tri {
        float a[3], b[3], c[3];
        uint64_t key; // its piece's state key (pieceKey)
    };
    struct Piece {
        uint32_t number = 0; // this tick's RenderVertex::piece
        float extent = 0.0f; // its largest side (world units)
    };
    struct State {
        uint32_t number = 0;
        float open = 0.0f, hold = 0.0f;
        uint8_t bodies = 0; // the bodies it hid at the last tick (0: none)
        uint8_t shown = 0;  // the bodies its hole goes around (the last ones it hid, kept while it closes)
        bool whole = false;
    };
    // a piece's key: the room's piece numbers as they are; a dynamic object's piece by the object's id and its
    // piece's index in that object (its number can change from tick to tick)
    static uint64_t roomKey(uint32_t piece) { return piece; }
    static uint64_t dynamicKey(uint32_t object, uint32_t index) { return 1ull << 63 | uint64_t(object) << 20 | index; }

    std::vector<Tri> room_;                       // the room's opaque triangles
    std::unordered_map<uint64_t, Piece> pieces_;  // the room's and this tick's dynamic pieces
    std::unordered_map<uint64_t, State> states_;  // the pieces opening / open / closing
};
