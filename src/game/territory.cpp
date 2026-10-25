// The room's territory rectangles (see territory.hpp).
#include "game/loaded_data.hpp"
#include "game/territory.hpp"

#include "game/actor_state.hpp"
#include "game/collision.hpp"
#include "game/room_records.hpp"
#include "game/vectors.hpp"

namespace od::port {

int territoryViolated(const Fixed16Vec3& pos) { return territoryViolated(pos.x.whole, pos.z.whole); }

int territoryViolated(int16_t x, int16_t z) {
    const uint32_t table = territory().table;
    if (table == 0) return 0;
    int32_t kind = dataRecord<TerritoryRect>(table, TerritoryRect::kBytes).kind;
    int index = 1;
    for (uint32_t at = table; kind >= 0; at += kTerritoryRectSize, ++index) {
        const TerritoryRect rect = dataRecord<TerritoryRect>(at, TerritoryRect::kBytes);
        const uint32_t ux = uint32_t((int(x) >> kCellShift) - rect.cellX);
        const uint32_t uz = uint32_t((int(z) >> kCellShift) - rect.cellZ);
        if (kind == 0) {
            if (ux < uint32_t(int(rect.width)) && uz < uint32_t(int(rect.depth))) return index;
        } else {
            if (uint32_t(int(rect.width)) < ux) return index;
            if (uint32_t(int(rect.depth)) < uz) return index;
        }
        kind = dataRecord<TerritoryRect>(at + kTerritoryRectSize, TerritoryRect::kBytes).kind;
    }
    return 0;
}

uint32_t territoryPushBack(Actor& actor, int rect) {
    const uint32_t table = territory().table;
    if (table == 0) return 0;
    const TerritoryRect r = dataRecord<TerritoryRect>(table + uint32_t(rect) * kTerritoryRectSize - kTerritoryRectSize, TerritoryRect::kBytes);
    const int x0 = r.cellX * kCellSize, x1 = r.width * kCellSize - 1 + x0;
    const int z0 = r.cellZ * kCellSize, z1 = r.depth * kCellSize - 1 + z0;
    const int px = actor.prevPos.x.whole, pz = actor.prevPos.z.whole;
    const uint32_t dx = uint32_t(int(actor.pos.x.whole) - px), dz = uint32_t(int(actor.pos.z.whole) - pz);
    uint32_t result = 0;
    if (r.kind == 0) { // pushed back out
        if (int32_t(dx) >= 1 && dx > uint32_t(x0 - px)) {
            result = kWallOnXPlus;
            actor.pos.x.whole = int16_t(x0 - 1);
        }
        if (int32_t(dx) < 0 && dx < uint32_t(x1 - px)) {
            result |= kWallOnXMinus;
            actor.pos.x.whole = int16_t(x1 + 1);
        }
        if (int32_t(dz) > 0 && uint32_t(z0 - pz) < dz) {
            result |= kWallOnZPlus;
            actor.pos.z.whole = int16_t(z0 - 1);
        }
        if (int32_t(dz) < 0 && dz < uint32_t(z1 - pz)) {
            actor.pos.z.whole = int16_t(z1 + 1);
            return result | kWallOnZMinus;
        }
    } else { // pushed back in
        if (int32_t(dx) >= 1 && dx > uint32_t(x1 - px)) {
            actor.pos.x.whole = int16_t(x1 - 1);
            result = kWallOnXPlus;
        }
        if (int32_t(dx) < 0 && dx < uint32_t(x0 - px)) {
            actor.pos.x.whole = int16_t(x0 + 1);
            result |= kWallOnXMinus;
        }
        if (int32_t(dz) > 0 && uint32_t(z1 - pz) < dz) {
            result |= kWallOnZPlus;
            actor.pos.z.whole = int16_t(z1 - 1);
        }
        if (int32_t(dz) < 0 && dz < uint32_t(z0 - pz)) {
            result |= kWallOnZMinus;
            actor.pos.z.whole = int16_t(z0 + 1);
        }
    }
    return result;
}

} // namespace od::port
