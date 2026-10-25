// The ST13 wall-break burst controller 0x41a500 (see st13_wall_break.hpp). Its falling quads are the wall crumble's
// (game/wall_crumble): the same texture cell by tick, grey and billboard.
#include "content/manda/st13_wall_break.hpp"

#include <cstdint>

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/breakable_walls.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/room_grid.hpp"
#include "game/wall_crumble.hpp"

namespace od::port {

namespace {

constexpr uint32_t kBurstTicks = 0x10; // the burst's ticks (the state byte counts them)
constexpr int32_t kRingStep = 0xc0;    // the quads' spacing
constexpr uint32_t kRiseShift = 4;     // the rings rise t << 4 units by tick t

// the cell of the wall the burst opens, by its variant (grid column / row, cell units + kCellBias)
constexpr uint32_t kWallColumnAlongX = 0x3f, kWallRowAlongX = 0x3e;
constexpr uint32_t kWallColumnAlongZ = 0x3d, kWallRowAlongZ = 0x3f;
constexpr uint8_t kOpenVariant = 2;

// tick 0: the wall's debris (global static type 0xc) and its explosion (type 8) at the wall's point, the wall's cell
// opened
void burstStart(ControllerActor& burst, const WallCrumbleFields& wall) {
    const SegmentEntry segment(wall.segment);
    const uint32_t poolEntry = wall.poolEntry;
    const uint32_t debrisAddress = allocActor60(0); // exe 0x5e1300
    if (debrisAddress != 0) {
        StaticActor& debris = staticActorAt(debrisAddress);
        debris.flags |= kActorLive;
        debris.type = typeByte(GlobalStaticType::WallDebris);
        debris.variant = uint8_t(SegmentPoolEntry(poolEntry).reaction & kSegmentReactionMask);
        debris.loopFlags |= kActorLateLoop;
        debris.pos.x.whole = wall.point.x;
        debris.pos.y.whole = wall.point.y;
        debris.pos.z.whole = wall.point.z;
        debris.setParamsWord(poolEntry); // the debris's pool entry
        const uint32_t blastAddress = allocActor60(0);
        if (blastAddress != 0) {
            StaticActor& blast = staticActorAt(blastAddress);
            blast.flags = kActorLive;
            blast.type = typeByte(GlobalStaticType::Explosion);
            blast.variant = 0;
            blast.loopFlags |= kActorSweepGroup2;
            blast.pos.x.whole = wall.point.x;
            blast.pos.y.whole = wall.point.y;
            blast.pos.z.whole = wall.point.z;
            ExplosionParams params(blast);
            params.spread = uint16_t(
                uint32_t(int32_t(uint32_t(uint16_t(wall.width)) + uint32_t(uint16_t(wall.height)) * 2) >> 1) + 0x20);
            params.flashes = uint16_t(uint32_t(segment.flags & kSegmentVariantMask) * 4 + 4);
        }
    }
    if (burst.variant == 0) switchGridCellVariant(kWallColumnAlongX, kWallRowAlongX, kOpenVariant); // exe 0x58d5f0
    else switchGridCellVariant(kWallColumnAlongZ, kWallRowAlongZ, kOpenVariant);
}

// exe 0x41a500: tick 0 the start; then 16 ticks of rings of falling quads rising and fading over the wall's box (along
// x for variant 0, along z else); freed at tick 0x10
void wallBreakUpdate(ControllerActor& burst) {
    const WallCrumbleFields& wall = classState<WallCrumbleFields>(burst);
    if (burst.state == 0) burstStart(burst, wall);
    const uint32_t t = burst.state;
    if (t >= kBurstTicks) {
        freeActor(burst); // exe 0x5e15c0
        return;
    }
    wallCrumbleSelectCell(burst);
    const uint32_t grey = wallCrumbleGrey(t);
    const uint32_t sizeWord = ((t << 0x16) & 0xfff0000u) | ((t >> 1) + 0x100u);
    const int32_t bottom = int32_t(wall.height) * 2;
    int32_t xa, xb, za, zb;
    if (burst.variant == 0) {
        xa = -400, xb = 400, za = -0x80, zb = 0x100;
    } else {
        xa = -0x80, xb = 0x100, za = -400, zb = 400;
    }
    if (bottom >= 0) {
        for (int32_t row = 0; row <= bottom; row += kRingStep) {
            const int16_t y = int16_t(uint16_t(wall.point.y - uint16_t(t << kRiseShift) - row));
            for (int32_t x = xa; x <= xb; x += kRingStep) {
                const int16_t qx = int16_t(wall.point.x + x);
                wallCrumbleQuad(burst, grey, qx, y, int16_t(za + wall.point.z), sizeWord);
                wallCrumbleQuad(burst, grey, qx, y, int16_t(zb + wall.point.z), sizeWord);
            }
            for (int32_t z = za; z <= zb; z += kRingStep) {
                const int16_t qz = int16_t(z + wall.point.z);
                wallCrumbleQuad(burst, grey, int16_t(wall.point.x + xa), y, qz, sizeWord);
                wallCrumbleQuad(burst, grey, int16_t(wall.point.x + xb), y, qz, sizeWord);
            }
        }
    }
    ++burst.state;
}

} // namespace

void wallBreakBurstUpdate(const PlayerEnv&, uint32_t actor) { wallBreakUpdate(controllerActorAt(actor)); }

} // namespace od::port
