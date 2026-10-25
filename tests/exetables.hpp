// The PC data source's locator of the per-stage tables (engine/pc_data_source); test_room checks it against the extractor.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/pe_image.hpp"
#include "engine/room.hpp"

namespace od {

struct StageParams {
    int stage = 0;
    FloorUvTable floorUv{};
    std::array<uint8_t, 0x2C> params{}; // sub-room 0 param struct
    uint32_t paramsVa = 0;               // the param structs (0x2c per sub-room) in the exe image
    uint32_t floorUvVa = 0;              // the floor UV table in the exe image
    int shift() const { return params[3]; }
    int renderMode() const { return params[7]; }
};

class ExeTables {
public:
    static constexpr int kStages = 93;

    explicit ExeTables(std::vector<uint8_t> exe); // throws on a non-PE / unexpected layout
    const PeImage& image() const { return image_; }
    StageParams stage(int n) const;
    FloorOrientTable floorOrient() const;
    // The stage's camera records (12 bytes per sub-room: FUN_0040e290(table + sub * 12) in the per-stage room setup
    // [0x8d397c + stage * 4], pattern lea r32,[r32*4 + table]; push r32; call 0x40e290); 0 when not found
    uint32_t cameraTable(int n) const;

private:
    const uint8_t* at(uint32_t va, size_t n) const; // VA -> pointer into the file image (throws)
    uint32_t u32(uint32_t va) const;

    PeImage image_;
};

} // namespace od
