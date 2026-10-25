// The snow's draw blocks (game/weather_draw SnowDrawBlock: the inputs of the last tick's exe 0x591c10 calls, per snow
// static) at the pseudo-memory the weather goldens record them in.
#include <cstdint>

#include "game/actor_pools.hpp"
#include "game/weather_draw.hpp"
#include "translator/layout.hpp"

namespace od::test {

namespace {
constexpr uint32_t kSnowDrawBase = 0x7f101000, kSnowBlockSize = 0x100, kSnowDrawnMask = 0xc0, kSnowPointSize = 6;
} // namespace

void weatherDrawLayout(LayoutVisitor& v) {
    for (uint32_t i = 0; i < od::port::StaticPool::size(); ++i) {
        od::port::SnowDrawBlock& b = od::port::snowDrawBlock(i);
        const uint32_t at = kSnowDrawBase + i * kSnowBlockSize;
        v.records(at, b.particles, kSnowPointSize);
        v.field(at + kSnowDrawnMask, b.drawn);
    }
}

} // namespace od::test
