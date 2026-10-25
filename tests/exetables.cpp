#include "exetables.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

#include "engine/dat.hpp"
#include "engine/exe_coverage.hpp"

namespace od {

namespace {
constexpr uint32_t kStageFnTable = 0x8D3808;
constexpr uint32_t kRoomParamSetup = 0x5F59D0;
constexpr uint32_t kFloorOrientTable = 0x91BDCC;
constexpr uint32_t kRoomSetupTable = 0x8D397C; // FUN_005d5b30: [table + stage * 4](param)
constexpr uint32_t kCameraRoomInit = 0x40E290;
} // namespace

ExeTables::ExeTables(std::vector<uint8_t> exe) : image_(std::move(exe)) {}

const uint8_t* ExeTables::at(uint32_t va, size_t n) const { return image_.fileView(va, uint32_t(n)).data(); }

uint32_t ExeTables::u32(uint32_t va) const { return rd32(at(va, 4)); }

FloorOrientTable ExeTables::floorOrient() const {
    FloorOrientTable t{};
    const uint8_t* p = at(kFloorOrientTable, 64);
    for (int i = 0; i < 16; ++i)
        for (int k = 0; k < 4; ++k) t[i][k] = p[i * 4 + k];
    return t;
}

StageParams ExeTables::stage(int n) const {
    if (n < 0 || n >= kStages) throw std::runtime_error("stage out of range");
    const PeImage::Section* text = image_.text();
    if (!text) throw std::runtime_error("dash2.exe: no .text");
    const uint8_t* t = image_.file().data() + text->rawOff;
    const uint32_t textVa = text->va;

    const uint32_t fn = u32(kStageFnTable + 4 * uint32_t(n));
    // find the call to the room param setup inside the stage function
    size_t call = 0;
    for (size_t i = fn - textVa; i < fn - textVa + 0x1000 && i + 5 <= text->rawSize; ++i) {
        if (t[i] != 0xE8) continue;
        const int32_t rel = int32_t(rd32(t + i + 1));
        if (textVa + uint32_t(i) + 5 + uint32_t(rel) == kRoomParamSetup) {
            call = i;
            break;
        }
    }
    if (!call || t[call - 5] != 0x68) throw std::runtime_error("stage " + std::to_string(n) + ": setup call not found");
    const uint32_t uvVa = rd32(t + call - 4); // param_1: push imm32 (pushed last)
    uint32_t paramVa = 0;                      // param_3: lea r32,[reg*4 + disp32]
    for (size_t j = call - 7; j > call - 48; --j) {
        if (t[j] == 0x8D && (t[j + 1] & 0xC7) == 0x04 && (t[j + 2] & 0xC7) == 0x85) {
            paramVa = rd32(t + j + 3);
            break;
        }
    }
    if (!paramVa) throw std::runtime_error("stage " + std::to_string(n) + ": param struct not found");
    if (exe_coverage::enabled()) // the scan up to the call and the lea search behind it
        exe_coverage::mark(fn, uint32_t(textVa + call + 5 - fn), exe_coverage::kTextScan);

    StageParams sp;
    sp.stage = n;
    const uint8_t* uv = at(uvVa, 32 * 4);
    for (int i = 0; i < 32; ++i) sp.floorUv[i] = {rd16(uv + i * 4), rd16(uv + i * 4 + 2)};
    std::memcpy(sp.params.data(), at(paramVa, 0x2C), 0x2C);
    sp.paramsVa = paramVa;
    sp.floorUvVa = uvVa;
    return sp;
}

uint32_t ExeTables::cameraTable(int n) const {
    if (n < 0 || n >= kStages) return 0;
    const PeImage::Section* text = image_.text();
    if (!text) return 0;
    const uint8_t* t = image_.file().data() + text->rawOff;
    const uint32_t textVa = text->va;
    const uint32_t fn = u32(kRoomSetupTable + 4 * uint32_t(n));
    if (fn < textVa + 8 || fn >= textVa + text->rawSize) return 0;
    for (size_t i = fn - textVa; i < fn - textVa + 0x600 && i + 5 <= text->rawSize; ++i) {
        if (t[i] != 0xE8 || textVa + uint32_t(i) + 5 + rd32(t + i + 1) != kCameraRoomInit) continue;
        if (exe_coverage::enabled()) exe_coverage::mark(fn, uint32_t(textVa + i + 5 - fn), exe_coverage::kTextScan);
        // push r32 (0x50..0x57) right before, lea r32,[r32*4 + disp32] (8D /r SIB scale 4) before that
        if ((t[i - 1] & 0xF8) == 0x50 && t[i - 8] == 0x8D && (t[i - 7] & 0xC7) == 0x04 && (t[i - 6] >> 6) == 2)
            return rd32(t + i - 5);
        return 0;
    }
    return 0;
}

} // namespace od
