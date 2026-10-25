// The geometry scratch (geometry_scratch.hpp): its lanes' table, the one place that says which canonical member each
// 16-bit lane of the exe's scratch is.
#include "game/geometry_scratch.hpp"

#include <stdexcept>
#include <string>

#include "game/script_state.hpp"
#include "game/story_flags.hpp"

namespace od::port {

Lane16 GeometryScratch::lane(uint32_t offset) {
    CollisionState& c = collision;
    if (offset & 1 || offset >= 0x140)
        throw std::logic_error("geometry scratch: no lane at +" + std::to_string(offset));
    if (offset >= 0x88) return Lane16(userLanes[(offset - 0x88) / 2]);
    if (offset >= 0x80) {
        int16_t* const p[4] = {&carriedPoint.x, &carriedPoint.y, &carriedPoint.z, &carriedPoint.spare};
        return Lane16(*p[(offset - 0x80) / 2]);
    }
    switch (offset) {
    case 0x00: return Lane16(c.point[0]);
    case 0x02: return Lane16(c.point[1]);
    case 0x04: return Lane16(c.point[2]);
    case 0x06: return Lane16(c.airborne, c.landed);
    case 0x08: return Lane16(c.previous[0]);
    case 0x0a: return Lane16(c.previous[1]);
    case 0x0c: return Lane16(c.previous[2]);
    case 0x0e: return Lane16(c.reserved0E);
    case 0x10: return Lane16(c.delta[0]);
    case 0x12: return Lane16(c.delta[1]);
    case 0x14: return Lane16(c.delta[2]);
    case 0x16: return Lane16(c.wallDirection);
    case 0x18: return Lane16(c.hitX0);
    case 0x1a: return Lane16(c.hitTop);
    case 0x1c: return Lane16(c.hitZ0);
    case 0x1e: return Lane16(c.noStepUp, c.lastPushAxis);
    case 0x20: return Lane16(c.hitX1);
    case 0x22: return Lane16(c.hitBottom);
    case 0x24: return Lane16(c.hitZ1);
    case 0x26: return Lane16(c.hitShape);
    case 0x28: return Lane16(c.pendingPush[0]);
    case 0x2a: return Lane16(c.pendingPush[1]);
    case 0x2c: return Lane16(c.pendingPush[2]);
    case 0x2e: return Lane16(c.appliedPushZ);
    case 0x30: return Lane16(c.appliedPushX);
    case 0x32: return Lane16(c.appliedPushY);
    default: break;
    }
    if (offset < 0x44) return Lane16(c.gatheredCells[(offset - 0x34) / 4], (offset - 0x34) / 2 % 2);
    if (offset < 0x50) return Lane16(c.box[(offset - 0x44) / 2]);
    if (offset < 0x60) return Lane16(c.gatherOffsets[(offset - 0x50) / 4][(offset - 0x50) / 2 % 2]);
    if (offset < 0x70) return Lane16(c.specialCells[(offset - 0x60) / 4], (offset - 0x60) / 2 % 2);
    return Lane16(c.userLanes[(offset - 0x70) / 2]);
}

GeometryScratch& geometryScratch() {
    static GeometryScratch s;
    return s;
}

CollisionState& queryCollisionState() {
    CollisionState& st = geometryScratch().collision;
    st.scriptPassable = scriptState().inUse() && !storyFlag(kFlagScriptSolids);
    return st;
}

} // namespace od::port
