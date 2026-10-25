// The room's draw parameters (see room_params.hpp).
#include "game/loaded_data.hpp"
#include "game/room_params.hpp"

#include "game/projection_state.hpp"
#include "game/room_grid.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {
constexpr int32_t kFogDistanceCap = 0x3333;       // exe 0x5f5df0 caps the shifted fog distance
constexpr RoutineId kExeEmptyRoutine = RoutineId::EmptyRoutine;
// the exe's plain `ret` (a callback that does nothing)
constexpr uint32_t kShiftMask = 0x1f;
} // namespace

void setFogDistance(int32_t distance) {
    RoomDrawState& r = roomDraw();
    r.fogDistance = distance;
    const int32_t prod = int32_t(uint32_t(int32_t(projection().focal)) * uint32_t(distance)); // imul (32-bit)
    const int32_t q = int32_t((int64_t(prod) * 0x10624dd3LL) >> 38);                     // / 1000 (imul, sar 6)
    r.fogDepth = q + int32_t(uint32_t(q) >> 31);
}

void setFogColour(uint8_t red, uint8_t green, uint8_t blue) {
    RoomDrawState& r = roomDraw();
    r.fogColour[0] = red;
    r.fogColour[1] = green;
    r.fogColour[2] = blue;
}

void setCoordinateShift(uint8_t shift) {
    const RoomParams params = *roomDraw().params;
    StageState& s = stage();
    s.coordinateShift = shift;
    s.depthRange = roomDraw().depthRanges[shift];
    s.effectsFar = roomDraw().effectsFar[s.coordinateShift];
    int32_t distance = int32_t(uint32_t(params.fogDistance) << (s.coordinateShift & kShiftMask));
    if (distance >= kFogDistanceCap) distance = kFogDistanceCap;
    setFogDistance(distance);
}

void roomParamsSetup(uint32_t address) {
    RoomDrawState& r = roomDraw();
    StageState& s = stage();
    r.params.pointAt(address);
    const RoomParams params = dataRecord<RoomParams>(address, RoomParams::kBytes);
    setFogDistance(int32_t(uint32_t(params.fogDistance)));
    s.coordinateShift = params.coordinateShift;
    s.gridByte = params.gridByte;
    roomGrid().roomFlags = params.roomFlags;
    for (size_t i = 0; i < r.depthRanges.size(); ++i) r.depthRanges[i] = params.depthRanges[i];
    s.depthRange = r.depthRanges[s.coordinateShift];
    s.depthOffset = params.depthOffset;
    for (size_t i = 0; i < r.effectsFar.size(); ++i) r.effectsFar[i] = params.effectsFar[i];
    s.effectsFar = r.effectsFar[s.coordinateShift];
    s.drawWord = params.drawWord;
    s.drawByteA = params.drawByteA;
    s.drawByteB = params.drawByteB;
    for (size_t i = 0; i + 1 < s.lodDistances.size(); ++i) s.lodDistances[i] = params.lodDistances[i];
    s.lodDistances[3] = int16_t(uint32_t(int32_t(params.lodDistances[3])) << (s.coordinateShift & kShiftMask));
    s.textureAnimations = 0;
    s.textureAnimationCount = 0;
    stage().vertexColourHook = kExeEmptyRoutine;
}

} // namespace od::port
