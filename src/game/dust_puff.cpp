// The dust puff (see dust_puff.hpp).
#include "game/dust_puff.hpp"

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/draw_commands.hpp"
#include "game/game_state.hpp"
#include "game/port_gap.hpp"

namespace od::port {

namespace {

inline constexpr const char* kDustPuffArea = "dust puff";

enum class DustPuffState : uint8_t { Start = 0, Grow = 1 }; // exe 0x55f870 / 0x55f8f0
const DataTable<CollisionBoxRecord> groundProbeBox{TableId::DustPuffGroundProbeBox};
constexpr uint32_t kGroundLayers = 4;

// the puff's texture: CLUT 0x7c91, tpage 0x2f (additive)
constexpr uint16_t kPuffClut = 0x7c91, kPuffTpage = 0x2f;
// exe 0x55f920(effect, tick): the two 0xf8 records (textured gouraud bands in the world plane, word 0 the angles 0x0f /
// 0x0e and the frame counter's low bits): grey 8 * (ticks - tick), centre the puff's x / y / z, radius r = size * tick
// / ticks (the inner band (3r) >> 2 .. r, the outer (r & ~1) >> 1 .. (3r) >> 2), tpage / uv 0x7c9120e0 / 0x2f3fff. A
// puff of 0 ticks draws nothing: the exe's divide would fault (it is spawned so only by a stage table's type 7 that is
// this class; no recorded run reaches it).
void drawPuff(const EffectActor& puff, uint32_t tick) {
    const DustPuffFields& fields = classState<DustPuffFields>(puff);
    const int32_t n = fields.ticks;
    if (n == 0) return;
    const uint32_t c = ((tick << 0x1d) - tick + uint32_t(n)) << 3; // = 8 * (n - tick)
    const uint32_t colour = dc::bandColour(c | ((c << 8 | c) << 8));
    const uint32_t r = uint32_t(int32_t(uint32_t(fields.size) * tick) / n);
    const uint32_t r3 = (r * 3) >> 2;
    dc::Band band;
    band.ring.inner = colour;
    band.ring.xy = dc::packXY(puff.pos.x, puff.pos.y);
    band.ring.zStep = dc::zWord(puff.pos.z) | dc::ringStep(2) | dc::kRingFront;
    band.clutTexel = dc::pageTexel(kPuffClut, dc::texel(0xe0, 0x20));
    band.tpageTexel = dc::pageTexel(kPuffTpage, dc::texel(0xff, 0x3f));
    const uint32_t angle = dc::ringAngle(gameState().frame & 0x3fu) | dc::bandRepeat(2);
    band.ring.arc = angle | dc::ringArc(0x0f, 0x10);
    band.ring.radii = dc::ringRadii(r, r3);
    dc::emit(band, puff.at);
    band.ring.arc = angle | dc::ringArc(0x0e, 0x10);
    band.ring.radii = dc::ringRadii(r3, (r & 0xfffffffeu) >> 1);
    dc::emit(band, puff.at);
}

} // namespace

void dustPuffUpdate(const PlayerEnv& env, uint32_t address) {
    EffectActor& puff = effectActorAt(address);
    DustPuffFields& fields = classState<DustPuffFields>(puff);
    switch (DustPuffState(puff.state)) {
    case DustPuffState::Start: {
        puff.runFlags |= kActorWarpKept;
        if (fields.airborne == 0) {
            const int16_t p[3] = {puff.pos.x, puff.pos.y, puff.pos.z};
            puff.pos.y = int16_t(probeGroundAt(env, p, CollisionBoxRef::record(groundProbeBox.stateAddress()), 0, kGroundLayers));
        }
        fields.tick = 1;
        drawPuff(puff, 1);
        ++puff.state;
        return;
    }
    case DustPuffState::Grow:
        ++fields.tick;
        if (int(fields.tick) < int(fields.ticks)) {
            drawPuff(puff, fields.tick);
            return;
        }
        freeActor(puff);
        return;
    default: notPortedAt(kDustPuffArea, "dust puff state", puff.state);
    }
}

} // namespace od::port
