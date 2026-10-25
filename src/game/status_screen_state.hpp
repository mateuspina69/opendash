// The state status_screen.cpp keeps (its classes', its script parts, its globals): the members and the exe
// addresses they stand for.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port::status_screen_state {

// The highlight clut's colours: the copy kept at the open and the pulsed copy uploaded every frame
struct HighlightClut {
    StateArray<uint16_t, 0x10> kept{}; // 0xa5e52c
    StateArray<uint16_t, 0x10> pulsed{}; // 0xa5e54c
};
inline HighlightClut& highlightClut() {
    static HighlightClut s;
    return s;
}
// The page DATs' VRAM area the mode keeps while the sub-screen runs (the exe saves it into the spare buffer in use: +0
// and +0x18000; the pixels are the host's VRAM, so the native state keeps them on its own)
struct KeptPageArea {
    std::array<std::vector<uint8_t>, 2> pixels;
};
inline KeptPageArea& keptPageArea() {
    static KeptPageArea s;
    return s;
}

} // namespace od::port::status_screen_state
