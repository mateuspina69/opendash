// The state title.cpp keeps (its classes', its script parts, its globals): the members and the exe
// addresses they stand for.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port::title_state {

// the class table and the clear flag the title keeps while the sub-screens (the load menu, the options) run
struct TitleKeeps {
    DataPtr<> classTables; // 0xa59de4
    uint8_t clearBackground = 0; // 0xa59de8
};
inline TitleKeeps& titleKeeps() {
    static TitleKeeps s;
    return s;
}

} // namespace od::port::title_state
