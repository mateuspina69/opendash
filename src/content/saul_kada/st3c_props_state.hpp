// The state st3c_props.cpp keeps (its classes', its script parts, its globals): the members and the exe
// addresses they stand for.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port::st3c_props_state {

struct PublishedLink {
    uint32_t block = 0; // 0xa5a900
};
inline PublishedLink& publishedLink() {
    static PublishedLink s;
    return s;
}

} // namespace od::port::st3c_props_state
