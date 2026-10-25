// The flame ribbon between the lava boss's two orbiting fire bombs (ST28 pool 0xa0 type 0x15, exe 0x4383d0; hypothesis
// names). States [0x88d4f0]: 0 start 0x4383f0 (its grey 0x60), 1 draw 0x438410 (while the first bomb lives: two
// textured quads between the bombs, 0x20 above / below them, their texels alternating by its frame), 2 freed. The OT
// link of the quads (0x6d004c / 0x6d0048) is the draw commands' side channel (as game/global_fx).
#include "content/saul_kada/st28_bomb_ribbon.hpp"
#include "content/saul_kada/st28_bomb_ribbon_state.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/draw_commands.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/projection_state.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st28_bomb_ribbon_state;

namespace {

constexpr uint8_t kBombRibbonType = 0x15;
enum class RibbonState : uint8_t { Start = 0, Draw = 1, Free = 2 };
constexpr uint8_t kRibbonGrey = 0x60;
const DataTable<ScalarTable<int16_t, 2>> quadOffsets{TableId::St28BombRibbonQuadOffsets}; // the two quads' heights
constexpr uint32_t kPacketCode = 0x2e, kRibbonClut = 0x7dd1, kRibbonTpage = 0x2f;

// a texel pair (u, v) of the table
struct Texel : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    uint16_t u = 0; // +0x0
    uint16_t v = 0; // +0x2
    template <class R> static Texel load(const R& r) {
        Texel v;
        v.u = r.template read<uint16_t>(0x0);
        v.v = r.template read<uint16_t>(0x2);
        return v;
    }
};

// exe 0x438430: two quads between the bombs, their corners 0x20 above / below each
void drawRibbon(EffectActor& actor, RibbonFields& ribbon) {
    const ProjectileActor& second = *ribbon.secondBomb;
    const ProjectileActor& first = *ribbon.firstBomb;
    ProjectionScratch& scratch = projectionScratch();
    for (uint32_t i = 0; i < 2; ++i) {
        const int off = quadOffsets[i];
        int16_t v[4][3] = {
            {second.pos.x.whole, int16_t(uint16_t(second.pos.y.whole + off - 0x20)), second.pos.z.whole},
            {first.pos.x.whole, int16_t(uint16_t(first.pos.y.whole + off - 0x20)), first.pos.z.whole},
            {second.pos.x.whole, int16_t(uint16_t(second.pos.y.whole + off + 0x20)), second.pos.z.whole},
            {first.pos.x.whole, int16_t(uint16_t(first.pos.y.whole + off + 0x20)), first.pos.z.whole}};
        for (uint32_t j = 0; j < 4; ++j) {
            LaneVec4 corner = scratch.quadCorners[j];
            corner.x = v[j][0];
            corner.y = v[j][1];
            corner.z = v[j][2];
        }
        int16_t sxy[4][2];
        const int32_t otz = projectQuad(v, sxy); // exe 0x40c3c0 .. 0x40c460 + 0x40cad0
        uint32_t xy[4];
        for (uint32_t j = 0; j < 4; ++j) {
            xy[j] = uint32_t(uint16_t(sxy[j][1])) << 16 | uint16_t(sxy[j][0]);
            scratch.quadScreenPoints[j] = xy[j];
        }
        scratch.quadDepth = otz;
        if (otz > int32_t(stage().depthRange) || otz < 1) {
            if (i == 1) return; // the exe returns before the frame's count when the last quad is culled
            continue;
        }
        const uint32_t c = ribbon.grey;
        const uint32_t table = ((ribbon.frame & 1) ? stateAddress(TableId::St28BombRibbonTexelsOdd) : stateAddress(TableId::St28BombRibbonTexelsEven)) + i * 0x10;
        const Texel t0 = dataRecord<Texel>(table, Texel::kBytes), t1 = dataRecord<Texel>(table + 4, Texel::kBytes),
                    t2 = dataRecord<Texel>(table + 8, Texel::kBytes), t3 = dataRecord<Texel>(table + 12, Texel::kBytes);
        const uint32_t w[10] = {0,
                                ((c | kPacketCode << 8) << 8 | c) << 8 | c,
                                xy[0],
                                (uint32_t(t0.v) | kRibbonClut << 8) << 8 | t0.u,
                                xy[1],
                                (uint32_t(t1.v) | kRibbonTpage << 8) << 8 | t1.u,
                                xy[2],
                                uint32_t(t2.v) << 8 | t2.u,
                                xy[3],
                                uint32_t(t3.v) << 8 | t3.u};
        dc::otQuadFT4(actor.at, w, v);
    }
    ribbon.frame += 1u;
}
void bombRibbon(EffectActor& actor) { // exe 0x4383d0: [0x88d4f0]
    RibbonFields& ribbon = classState<RibbonFields>(actor);
    switch (RibbonState(actor.state)) {
    case RibbonState::Start: // exe 0x4383f0
        ribbon.grey = kRibbonGrey;
        ribbon.frame = 0;
        actor.state += 1u;
        return;
    case RibbonState::Draw: { // exe 0x438410
        const uint32_t first = slotIdentityWord(ribbon.firstBomb);
        if (!first) notPorted(kStoryPort, "0x438410: a ribbon without its bomb (the exe reads [0])");
        if (projectileActorAt(first).flags != 0) drawRibbon(actor, ribbon);
        else freeActor(actor); // exe 0x4358c0
        return;
    }
    case RibbonState::Free: freeActor(actor); return; // exe 0x4358c0
    default: notPortedAt(kStoryPort, "0x4383d0 state past [0x88d4f0]", actor.state);
    }
}

} // namespace

void spawnBombRibbon(uint32_t firstBomb, uint32_t secondBomb) { // exe 0x438720
    const uint32_t slot = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!slot) return;
    EffectActor& ribbon = effectActorAt(slot);
    RibbonFields& fields = classState<RibbonFields>(ribbon);
    ribbon.flags = kActorLive;
    ribbon.type = kBombRibbonType;
    pointAtWord(fields.firstBomb, firstBomb);
    pointAtWord(fields.secondBomb, secondBomb);
}

void bombRibbonUpdate(const PlayerEnv&, uint32_t actor) { bombRibbon(effectActorAt(actor)); }

} // namespace od::port
