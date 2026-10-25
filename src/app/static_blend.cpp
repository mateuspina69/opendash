#include "app/static_blend.hpp"
#include "game/actor_pools.hpp"

namespace od::app {

uint8_t staticModelBlend(uint32_t actor) {
    const uint8_t bb = od::port::meshActorAt(actor).semiTransparency;
    if (!(bb & 0x20)) return 0;
    return uint8_t(((bb >> 1) & 3) + 1);
}

} // namespace od::app
