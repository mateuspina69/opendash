// Native model bank entries, headers and parts from loaded file blocks.
#pragma once
#include "game/loaded_data.hpp"

namespace od::port {

struct ModelPartData : DataRecord {
    uint8_t pointSlot;
    int8_t parentSlot;
    uint8_t boneSlot, flags;
    static ModelPartData load(const FileDataReader& r) {
        return {{}, r.read<uint8_t>(0), r.read<int8_t>(1), r.read<uint8_t>(2), r.read<uint8_t>(3)};
    }
};
inline ModelPartData modelPart(uint32_t pointer, size_t part) {
    return FileDataRef<ModelPartData>(loadedFile(pointer + uint32_t(part * 4), 4)).get();
}

struct ModelBankEntryData : DataRecord {
    uint8_t key, type, variant;
    uint32_t model, frameTable, animTable;
    static ModelBankEntryData load(const FileDataReader& r) {
        return {{}, r.read<uint8_t>(0), r.read<uint8_t>(1), r.read<uint8_t>(2),
                r.read<uint32_t>(4), r.read<uint32_t>(8), r.read<uint32_t>(12)};
    }
};
struct ModelHeaderData : DataRecord {
    uint32_t counts;
    uint8_t flags;
    uint32_t skeleton, parts, extraGeometry, collisionMesh;
    std::array<uint32_t, 3> colourTables;
    int16_t cullWidth, cullHeight;
    LoadedVector scale;
    uint32_t textureCell;
    uint32_t at = 0;
    static ModelHeaderData load(const FileDataReader& r) {
        return {{}, r.read<uint32_t>(0), r.read<uint8_t>(3), r.read<uint32_t>(16),
                r.read<uint32_t>(20), r.read<uint32_t>(24), r.read<uint32_t>(28),
                {r.read<uint32_t>(32), r.read<uint32_t>(36), r.read<uint32_t>(40)},
                r.read<int16_t>(44), r.read<int16_t>(46), LoadedVector::load(r.sub(48)), 0};
    }
};

} // namespace od::port
