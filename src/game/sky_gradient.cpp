// The rooms' sky gradients (see sky_gradient.hpp).
#include "game/sky_gradient.hpp"

namespace od::port {

namespace {
constexpr int kStormySkyLastStory = 1;
constexpr uint32_t kBandSize = 8;
} // namespace

SkyGradient SkyGradient::load(const TableReader& r) {
    SkyGradient g;
    g.cleared = r.u32(0) == 0;
    const TableReader bands = r.follow(0, kMaxBands * kBandSize);
    for (uint32_t i = 0; (i + 1) * kBandSize <= bands.size() && int32_t(bands.u32(i * kBandSize)) >= 0; ++i)
        g.bands.push_back({bands.u32(i * kBandSize), bands.u32(i * kBandSize + 4)});
    return g;
}

namespace {
const TableFamily<SkyGradient> skyGradients{TableId::RoomSkyGradient};
const TableFamily<SkyGradient> earlySkyGradients{TableId::RoomSkyGradientEarly};
} // namespace

std::optional<SkyGradient> roomSkyGradient(int stage, int sub, int story) {
    if (stage < 0 || sub < 0) return std::nullopt;
    const uint32_t index = roomIndex(stage, sub);
    const TableFamily<SkyGradient>& family =
        story <= kStormySkyLastStory && earlySkyGradients.has(index) ? earlySkyGradients : skyGradients;
    if (!family.has(index)) return std::nullopt;
    return family[index];
}

} // namespace od::port
