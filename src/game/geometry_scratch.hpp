// The geometry scratch: one persistent object every geometry user shares, as the exe
// shares its globals 0x6d00c0..0x6d0147. Its canonical members are the room collision queries' state (game/collision
// CollisionState, +0x00..+0x7f: every query starts from what the last user left) and the carried point (+0x80..+0x87,
// left by the actor collision pass and the player update for the readers after them). The other users -- the
// transforms and projections (game/projection_state ProjectionScratch, game/camera_state CameraTransform), the pose
// (game/player/player_pose), the frame decode (game/actor_animation), the lock-on selection, the effects' points --
// name the bytes they use as views of named fields over these members: a lane (16 bits) of the scratch is one of the
// canonical members (or two of its bytes, or a half of one of its words), by the table of lane() below, written once
// here. The lanes past +0x87 are each user's own (only it reads what it wrote there).
#pragma once

#include <array>
#include <cstdint>
#include <utility>

#include "game/collision.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/view_transform.hpp"

namespace od::port {

struct GeometryScratch {
    CollisionState collision; // +0x00..+0x7f
    ShortVec4 carriedPoint;   // +0x80..+0x87
    // +0x88..+0x13f: the lanes each user keeps for itself between its own routines (no other user reads them: the read
    // map's "only local reads"), named by its views as the shared ones are
    StateArray<int16_t, 0x5c> userLanes{};

    // the scratch's lane at byte offset `offset` (even, < 0x140): its canonical member
    Lane16 lane(uint32_t offset);
    Lane32 word(uint32_t offset) { return {lane(offset), lane(offset + 2)}; }
    LaneVec4 vec4(uint32_t offset) { return {lane(offset), lane(offset + 2), lane(offset + 4), lane(offset + 6)}; }
    LaneVec3 vec3(uint32_t offset) { return {lane(offset), lane(offset + 2), lane(offset + 4)}; }
    // N points / words one after the other from `offset` (8 / 4 bytes apart)
    template <size_t N> std::array<LaneVec4, N> vec4s(uint32_t offset) {
        return [&]<size_t... I>(std::index_sequence<I...>) {
            return std::array<LaneVec4, N>{vec4(offset + uint32_t(I) * 8)...};
        }(std::make_index_sequence<N>{});
    }
    template <size_t N> std::array<Lane16, N> lanes(uint32_t offset) {
        return [&]<size_t... I>(std::index_sequence<I...>) {
            return std::array<Lane16, N>{lane(offset + uint32_t(I) * 2)...};
        }(std::make_index_sequence<N>{});
    }
    // N bytes one after the other from `offset` (each the low / high half of its lane)
    template <size_t N> std::array<Lane8, N> bytes(uint32_t offset) {
        return [&]<size_t... I>(std::index_sequence<I...>) {
            return std::array<Lane8, N>{Lane8(lane((offset + uint32_t(I)) & ~1u), (offset + uint32_t(I)) & 1u)...};
        }(std::make_index_sequence<N>{});
    }
    template <size_t N> std::array<Lane32, N> words(uint32_t offset) {
        return [&]<size_t... I>(std::index_sequence<I...>) {
            return std::array<Lane32, N>{word(offset + uint32_t(I) * 4)...};
        }(std::make_index_sequence<N>{});
    }
    // a view transform at `offset` (its 0x20 bytes: the matrix, its padding short, the translation)
    ViewTransform transform(uint32_t offset) {
        return ViewTransform(LaneMatrix3({lane(offset), lane(offset + 2), lane(offset + 4), lane(offset + 6),
                                          lane(offset + 8), lane(offset + 0xa), lane(offset + 0xc), lane(offset + 0xe),
                                          lane(offset + 0x10)}),
                             lane(offset + 0x12), {word(offset + 0x14), word(offset + 0x18), word(offset + 0x1c)});
    }
};
// the scratch (one object for the life of the program)
GeometryScratch& geometryScratch();
// the room collision queries' state, the shape-0x1c gate set from the running game (a script runs and story flag
// kFlagScriptSolids is clear: the kShapeScriptPassable volumes are ignored)
CollisionState& queryCollisionState();

} // namespace od::port
