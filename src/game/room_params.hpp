// The room's draw parameters: the record every per-stage room setup hands exe 0x5f59d0 (exe data, 0x2c bytes: the fog,
// the coordinate shift, the depth ranges by shift, the LOD distances), the fog's distance (exe 0x40ba60) and colour
// (exe 0x40ba90), the coordinate shift and the ranges that follow it (exe 0x5f5df0). The camera scripts and the end of
// a script set / restore them (game/camera_script, game/script_engine). The grid header part of a room (0x6d0088..
// 0x6d0099) and the D3D fog colour are OpenDash's room loader's.
#pragma once

#include <array>
#include <cstdint>

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/state_array.hpp"

namespace od::port {

// A room's draw parameter record (exe data)
struct RoomParams : DataRecord {
    static constexpr uint32_t kBytes = 0x24;
    std::array<uint8_t, 3> fogColour{}; // +0x0
    uint8_t coordinateShift = 0; // +0x3
    uint16_t fogDistance = 0; // +0x4
    uint8_t gridByte = 0; // +0x6: copied to 0x6d009a (its reader is not traced)
    uint8_t roomFlags = 0; // +0x7: game/room_grid kRoomSwitchableSegments
    std::array<uint16_t, 3> depthRanges{}; // +0x8: the draw depth range by coordinate shift
    uint16_t depthOffset = 0; // +0xe
    std::array<uint16_t, 3> effectsFar{}; // +0x10: the projected effects' far depth by coordinate shift
    uint16_t drawWord = 0; // +0x18: copied to 0x6d00b6 / 0x6d00bc / 0x6d00bd (their readers, in the
    uint8_t drawByteA = 0; // +0x1a: draw code, are not traced)
    uint8_t drawByteB = 0; // +0x1b
    std::array<int16_t, 4> lodDistances{}; // +0x1c: the LOD distances (the last one << the coordinate shift)
    template <class R> static RoomParams load(const R& r) {
        RoomParams v;
        for (size_t i = 0; i < v.fogColour.size(); ++i) v.fogColour[i] = r.template read<uint8_t>(0x0 + i * sizeof(uint8_t));
        v.coordinateShift = r.template read<uint8_t>(0x3);
        v.fogDistance = r.template read<uint16_t>(0x4);
        v.gridByte = r.template read<uint8_t>(0x6);
        v.roomFlags = r.template read<uint8_t>(0x7);
        for (size_t i = 0; i < v.depthRanges.size(); ++i) v.depthRanges[i] = r.template read<uint16_t>(0x8 + i * sizeof(uint16_t));
        v.depthOffset = r.template read<uint16_t>(0xe);
        for (size_t i = 0; i < v.effectsFar.size(); ++i) v.effectsFar[i] = r.template read<uint16_t>(0x10 + i * sizeof(uint16_t));
        v.drawWord = r.template read<uint16_t>(0x18);
        v.drawByteA = r.template read<uint8_t>(0x1a);
        v.drawByteB = r.template read<uint8_t>(0x1b);
        for (size_t i = 0; i < v.lodDistances.size(); ++i) v.lodDistances[i] = r.template read<int16_t>(0x1c + i * sizeof(int16_t));
        return v;
    }
};

// The parts of a room's record the host reads to build the room (the data source's copy; TableId::RoomParams by
// roomIndex): the fog colour, the coordinate shift, the PC's D3D fog start / end (floats +0x24 / +0x28, fractions of
// the depth range)
struct RoomParamsRecord : DataRecord {
    static constexpr uint32_t kBytes = 0x2c;
    std::array<uint8_t, 3> fogColour{};
    uint8_t coordinateShift = 0;
    float fogStart = 0.0f, fogEnd = 0.0f;
    static RoomParamsRecord load(const TableReader& r) {
        return {{}, {r.u8(0), r.u8(1), r.u8(2)}, r.u8(3), r.f32(0x24), r.f32(0x28)};
    }
};

// The fog and the room's parameter globals
struct RoomDrawState {
    DataPtr<RoomParams> params; // 0xa6d208: the room's record (exe data)
    StateArray<uint16_t, 3> depthRanges{}; // 0xa72280: the room's depth ranges by coordinate shift (StageState
    StateArray<uint16_t, 3> effectsFar{}; // 0xa72278: depthRange / effectsFar take [shift])
    int32_t fogDistance = 0; // 0xa59a00
    int32_t fogDepth = 0; // 0xa59a50: the fog distance scaled by the projection's focal / 1000
    StateArray<uint8_t, 3> fogColour{}; // 0xa599e0
};
inline RoomDrawState& roomDraw() {
    static RoomDrawState s;
    return s;
}

// exe 0x5f59d0's state part (the per-stage setup calls it with the room's record): the record kept, the fog distance,
// the coordinate shift, the room flags, the depth ranges / far tables and their [shift] in the stage's draw state, the
// depth offset, the draw bytes, the LOD distances (the last one << shift), the texture animations cleared and the
// vertex-colour hook reset. Not covered by a golden.
void roomParamsSetup(uint32_t params);
// exe 0x40ba60(distance): the fog distance and its depth (scaled by the projection's focal length / 1000)
void setFogDistance(int32_t distance);
// exe 0x40ba90(colour): the fog colour from a record's three bytes
void setFogColour(uint8_t r, uint8_t g, uint8_t b);
// exe 0x5f5df0(shift): the coordinate shift and the ranges and fog distance that follow it (the fog distance capped)
void setCoordinateShift(uint8_t shift);

} // namespace od::port
