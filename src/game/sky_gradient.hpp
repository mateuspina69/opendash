// The rooms' sky gradients as the data source holds them (exe 0x5f0370's argument, the room function's table [table +
// sub * 4]: the sub's gradient, u32 pairs {top colour, bottom colour} (0xBBGGRR) per 32-line band ended by a negative
// word; a null pointer: the black clear). ST10 / ST11 / ST1D pick the table by the story (<= 1: the stormy table, e.g.
// Forbidden Island's purple sky before the elephant; else the clear one). Drawn by the host (app/sky).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "engine/data_source.hpp"

namespace od::port {

struct SkyGradient : DataRecord {
    static constexpr uint32_t kBytes = 4;        // the sub's pointer
    static constexpr uint32_t kMaxBands = 64;    // the read's bound
    bool cleared = false;                         // a null gradient: the black clear (ST27 sub 0 ...)
    std::vector<std::array<uint32_t, 2>> bands;   // {top, bottom} per 32-line band
    static SkyGradient load(const TableReader& r);
};
// the gradient of the stage's sub-room at story `story` (nullopt: the room function sets none)
std::optional<SkyGradient> roomSkyGradient(int stage, int sub, int story);

} // namespace od::port
