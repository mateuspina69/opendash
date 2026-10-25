// The skid sparks (see skid_sparks.hpp).
#include "game/loaded_data.hpp"
#include "game/skid_sparks.hpp"

#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/draw_commands.hpp"
#include "game/port_gap.hpp"
#include "game/suction_effect.hpp"

namespace od::port {

namespace {

// the sprites' texture: CLUT 0x7c91, tpage 0x2e (additive) / 0x4e (subtractive), cells kCellTexels + 1 texels wide
constexpr uint16_t kSparkClut = 0x7c91, kSparkTpage = 0x2e, kSparkGlowTpage = 0x4e;
constexpr uint32_t kCellTexels = 0x2f;

inline constexpr const char* kSkidSparksArea = "skid sparks";

// [0x8c3f0c](effect, effect + 0x18)
enum class SkidSparksState : uint8_t {
    Start = 0,    // exe 0x55dee0
    Fly = 1,      // exe 0x561fd0
    JetStart = 2, // exe 0x55df60 (the suction effect's)
    JetHold = 3,  // exe 0x55e030
    JetFade = 4,  // exe 0x55e0c0
};
constexpr uint32_t kEntrySize = 0x14;

// a colour word's three channels scaled by m / 256
uint32_t scaleColour(uint32_t c, uint32_t m) {
    return (((c & 0xff) * m >> 8) & 0xff) | (((c >> 8 & 0xff) * m >> 8) & 0xff) << 8 |
           (((c >> 16 & 0xff) << 8) * m & 0xff0000u);
}

// exe 0x561fd0: the uv cell (while the sub-state is 0 the 4 x 2 sheet by tick, then two cells alternating with the
// tick's parity), the fade over the life, the move along the yaw (falling), the two 0x2c sprites
void fly(EffectActor& spark, SkidSparkFields& fields) {
    const int32_t k = fields.tick;
    uint32_t u, v;
    if (spark.subState == 0) {
        u = uint32_t(k & 3) * 0x30;
        v = uint32_t((k >> 2) & 1) * 0x30;
        if (k > 2) ++spark.subState;
    } else if (uint8_t(fields.tick) & 1) {
        u = 0x90;
        v = 0;
    } else {
        u = 0;
        v = 0x30;
    }
    if (k >= fields.life) {
        freeActor(spark); // exe 0x5e1580
        return;
    }
    // the fade f = (L - |L - 2k|) * 0xc0 / L (a triangle over the life L), the entry's colours * f and * (3f >> 2), the
    // size = the entry's size * k / L + 0x10 (computed before the move in the exe: pure reads)
    const int32_t life = fields.life;
    const int32_t d = (life - 2 * k) < 0 ? 2 * k - life : life - 2 * k;
    const uint32_t f = uint32_t((life - d) * 0xc0 / life);
    const SkidSparkEntry entry = *fields.entry;
    const uint32_t colour0 = scaleColour(entry.colour0, f), colour1 = scaleColour(entry.colour1, (f * 3) >> 2);
    const uint32_t size = uint32_t(int32_t(entry.size) * k / life + 0x10);
    fields.speedZ = int16_t(fields.speedZ - fields.gravity);
    moveAlongYaw16(spark.pos, fields.yaw, fields.speedX, fields.speedY, -int(fields.speedZ)); // exe 0x5e3750
    // the two sprites: billboards, the uv cell kCellTexels wide, rotated k << 6; the second one's tpage 0x4e
    // (subtractive). A black colour skips its record.
    dc::Billboard b;
    b.xy = dc::packXY(spark.pos.x, spark.pos.y);
    b.z = dc::signedZWord(spark.pos.z);
    b.clut = kSparkClut;
    b.uv[0] = dc::texel(u, v);
    b.uv[1] = dc::texel(u + kCellTexels, v);
    b.uv[2] = dc::texel(u, v + kCellTexels);
    b.uv[3] = dc::texel(u + kCellTexels, v + kCellTexels);
    b.sizeRotation = dc::sizeRotation(size, (uint32_t(k) << 6) & 0xfff);
    if (colour0 != 0) {
        b.colour = dc::billboardColour(colour0);
        b.tpage = kSparkTpage;
        dc::emit(b, spark.at);
    }
    if (colour1 != 0) {
        b.colour = dc::billboardColour(colour1);
        b.tpage = kSparkGlowTpage;
        dc::emit(b, spark.at);
    }
    ++fields.tick;
}

} // namespace

void skidSparksUpdate(const PlayerEnv&, uint32_t address) {
    EffectActor& spark = effectActorAt(address);
    auto&& fields = classState<SkidSparkFields>(spark);
    SuctionParticles jet(spark); // the jet's particle block: from the slot's trail on (it runs past the slot)
    switch (SkidSparksState(spark.state)) {
    case SkidSparksState::JetStart: return suctionStart(spark, jet);
    case SkidSparksState::JetHold: return suctionHold(spark, jet);
    case SkidSparksState::JetFade: return suctionFade(spark, jet);
    case SkidSparksState::Start: {
        const uint32_t e = stateAddress(TableId::SkidSparksEntries) + uint32_t(spark.variant) * kEntrySize;
        const SkidSparkEntry entry = dataRecord<SkidSparkEntry>(e, SkidSparkEntry::kBytes);
        fields.entry.pointAt(e);
        fields.speedX = entry.speedX;
        fields.speedY = entry.speedY;
        fields.speedZ = entry.speedZ;
        fields.gravity = entry.gravity;
        fields.tick = 0;
        fields.life = int16_t(entry.life);
        fields.yaw = uint16_t((fields.yaw - uint32_t(kHalfTurn)) & kAngleMask);
        ++spark.state;
        return;
    }
    case SkidSparksState::Fly: fly(spark, fields); return;
    default: notPortedAt(kSkidSparksArea, "effect type 3 state", spark.state);
    }
}

} // namespace od::port
