// The state st3c_gold_piece.cpp keeps (its classes', its script parts, its globals): the members and the exe
// addresses they stand for.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port::st3c_gold_piece_state {

// The exe globals st3c_gold_piece.cpp keeps (their exe addresses in the comments)
struct St3cGoldPieceGlobals {
    uint32_t pieceBlock = 0; // 0xa5a92c
};
inline St3cGoldPieceGlobals& st3cGoldPieceGlobals() {
    static St3cGoldPieceGlobals s;
    return s;
}

} // namespace od::port::st3c_gold_piece_state
