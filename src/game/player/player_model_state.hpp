// The state player_model.cpp keeps (its classes', its script parts, its globals): the members and the exe
// addresses they stand for.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od {
// a load-index word that names no file
constexpr uint16_t kNoPlayerFile = 0xffff;
} // namespace od

namespace od::port::player_model_state {

// The player files the game holds (the load indices exe 0x5d5c40 records; 0: none)
struct PlayerFiles {
    StateArray<uint16_t, 2> before{}; // 0xa6d1d4: the two load-index words before them
    uint16_t body = 0; // 0xa6d1d8
    uint16_t weapon = 0; // 0xa6d1da

    // the four load-index words (the two before body / weapon too) set to kNoPlayerFile, first to last
    void forgetAll() {
        before = {kNoPlayerFile, kNoPlayerFile};
        body = kNoPlayerFile;
        weapon = kNoPlayerFile;
    }
};
inline PlayerFiles& playerFiles() {
    static PlayerFiles s;
    return s;
}

} // namespace od::port::player_model_state
