// The spark / smoke emitter class 0x592050 (see spark_emitter.hpp). States [0x8cdeec]: 0 0x592070 (set up), 1 0x5920e0
// (the particles, then the sub-state [0x8cdef4]: 0 0x5c2720 -> 1, 1 0x592350 emit, 2 0x592560 freed when no particle is
// left), 2 0x5c2720 (-> sub-state 1).
#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/spark_emitter_state.hpp"
#include "game/spark_emitter.hpp"

#include <unordered_map>

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_pools.hpp"
#include "game/draw_commands.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"

namespace od::port {

using namespace spark_emitter_state;

namespace {

constexpr const char* kSparkEmitterPort = "spark emitter port"; // the area of its port-gap errors


// A variant's row (exe table 0x8cde54, 4 bytes per variant)
struct SparkEmitterVariant : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    uint8_t flags = 0; // +0x0: kEmitAtParent | kEmitOnTpage2e | kEmitOnParentBone
    uint8_t rise = 0; // +0x1: the particles' rise per tick (+ 3 - the RNG's 0..7)
    uint8_t interval = 0; // +0x2: the emits' interval: interval + RNG % interval ticks
    uint8_t size = 0; // +0x3: the sprites' scale
    template <class R> static SparkEmitterVariant load(const R& r) {
        SparkEmitterVariant v;
        v.flags = r.template read<uint8_t>(0x0);
        v.rise = r.template read<uint8_t>(0x1);
        v.interval = r.template read<uint8_t>(0x2);
        v.size = r.template read<uint8_t>(0x3);
        return v;
    }
};
constexpr uint32_t kSparkEmitterVariantSize = 4;
constexpr uint8_t kEmitAtParent = 0x01;     // the particles start at the parent (its position, or its bone)
constexpr uint8_t kEmitOnTpage2e = 0x02;    // the sprites' texture page 0x2e (else 0x4e)
constexpr uint8_t kEmitOnParentBone = 0x80; // on the parent's bone (+7) at the emitter's point; follows its carry
const DataTable<ScalarTable<uint8_t, 8>> sparkSpriteSizes{TableId::SparkEmitterSpriteSizes}; // the sprite's u by its age >> 3

enum class SparkEmitterState : uint8_t { Setup = 0, Run = 1, Resume = 2 };
enum class SparkEmitterSub : uint8_t { Start = 0, Emit = 1, Drain = 2 };
constexpr uint16_t kParticleLives = 0x3fff;
constexpr uint16_t kParticleLife = 0x30; // + the RNG's 0..15

// presentation only: a serial per (emitter, slot), new at each birth -- the identity dc::emit gives the particle's
// sprite (its own object for the tick blend; the logic keeps none)
std::unordered_map<uint64_t, uint32_t> g_particleSerial;
uint32_t g_particleNext = 0;
void particleBorn(uint32_t emitter, uint32_t k) { g_particleSerial[uint64_t(emitter) << 8 | k] = ++g_particleNext; }
int32_t particleId(uint32_t emitter, uint32_t k) {
    const auto it = g_particleSerial.find(uint64_t(emitter) << 8 | k);
    return int32_t((k << 16 | ((it == g_particleSerial.end() ? 0u : it->second) & 0xffffu)) & 0x7fffffffu);
}

SparkEmitterVariant variantOf(const StaticActor& a) {
    return dataRecord<SparkEmitterVariant>(stateAddress(TableId::SparkEmitterVariants) + uint32_t(a.variant) * kSparkEmitterVariantSize, SparkEmitterVariant::kBytes);
}

// one wobble counter's step: the drift + 1 while positive (counted down), - 1 else (counted up); a new random count at
// 0
int wobbleStep(int8_t& counter, int drift, uint32_t random) {
    int8_t b = counter;
    int d = drift;
    if (b > 0) {
        ++d;
        --b;
    } else {
        --d;
        ++b;
    }
    counter = b;
    if (b == 0) counter = int8_t(uint8_t((random & 0xf) - 8));
    return d;
}

void emitterParticles(StaticActor& a, SparkEmitterFields& f) { // the state part of exe 0x5920e0
    const SparkEmitterVariant vt = variantOf(a);
    if (a.prevPos.x != 0 || a.prevPos.z != 0) { // the carry offset applied to the live particles
        for (uint32_t k = 0; k < kParticles; ++k)
            if ((f.liveMask >> k) & 1) {
                ShortVec4& p = f.particles[k];
                p.x = int16_t(uint16_t(p.x) + uint16_t(a.prevPos.x.whole));
                p.z = int16_t(uint16_t(p.z) + uint16_t(a.prevPos.z.whole));
            }
        if (vt.flags & kEmitOnParentBone) {
            a.pos.x.whole = int16_t(uint16_t(a.pos.x.whole) - uint16_t(a.prevPos.x.whole));
            a.pos.z.whole = int16_t(uint16_t(a.pos.z.whole) - uint16_t(a.prevPos.z.whole));
        }
        a.prevPos.x = 0;
        a.prevPos.z = 0;
    }
    for (uint32_t k = 0; k < kParticles; ++k) {
        if (!((f.liveMask >> k) & 1)) continue;
        ShortVec4& p = f.particles[k];
        auto life = f.life(k);
        life = uint16_t(life - 1);
        if (life == 0) {
            f.liveMask = uint16_t(f.liveMask & (~(1u << k) & kParticleLives));
            continue;
        }
        const uint32_t r = rngStep();
        p.y = int16_t(uint16_t(p.y) + uint16_t(3 - int(f.rise) - int(r & 7)));
        auto&& wobble = f.wobble[k];
        p.x = int16_t(uint16_t(p.x) + uint16_t(wobbleStep(wobble[0], f.driftX, (r & 0xff) >> 4)));
        p.z = int16_t(uint16_t(p.z) + uint16_t(wobbleStep(wobble[1], f.driftZ, r >> 8)));
        // the particle's sprite (0x2c, 10 words at the draw cursor): grey 2 * life + 0x10, its u by the age, the v row
        // 0x30 once 0x40 - life >= 0x20, tpage 0x2e (kEmitOnTpage2e) or 0x4e, scale (0x50 - life) * the variant's size
        // >> 4
        const int32_t ticks = int16_t(life);
        const int32_t age = 0x40 - ticks;
        const uint32_t sz = sparkSpriteSizes[size_t(age >> 3)];
        const uint32_t grey = uint32_t(ticks * 2 + 0x10);
        const uint32_t o = (uint32_t(age) & 0xffffffe0u) != 0 ? 0x30u : 0u;
        const uint32_t tp = (vt.flags & kEmitOnTpage2e) ? 0x2eu : 0x4eu;
        const uint32_t scale = uint32_t((int32_t(uint32_t(vt.size) * uint32_t(age + 0x10)) << 2) >> 6) & 0xffffu;
        dc::emit({0, ((grey | 0x2e00u) << 8 | grey) << 8 | grey,
                  uint32_t(uint16_t(p.x)) | uint32_t(int32_t(p.y)) << 16, (o | 0x7c9100u) << 8 | sz,
                  uint32_t(int32_t(p.z)), ((tp << 8) | o) << 8 | (sz + 0x2f), scale, (o + 0x2f) << 8 | sz, 0,
                  (o + 0x2f) << 8 | (sz + 0x2f)},
                 a.at, false, particleId(a.at, k)); // slot | birth serial
    }
}

void emitterEmit(StaticActor& a, SparkEmitterFields& f) { // exe 0x592350
    const SparkEmitterVariant vt = variantOf(a);
    if (f.emitTimer == 0) {
        for (uint32_t k = 0; k < kParticles; ++k) {
            if ((f.liveMask >> k) & 1) continue;
            ShortVec4& p = f.particles[k];
            const uint8_t flags = vt.flags;
            if (!(flags & kEmitAtParent)) {
                p.x = a.pos.x.whole;
                p.y = a.pos.y.whole;
                p.z = a.pos.z.whole;
            } else if (f.parent) {
                Actor& par = *f.parent;
                if (!(flags & kEmitOnParentBone)) {
                    p.x = par.pos.x.whole;
                    p.y = par.pos.y.whole;
                    p.z = par.pos.z.whole;
                } else {
                    const int16_t v[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
                    int16_t o[3];
                    bonePointOffset(asAnimated(par), a.spawnByte, v, o);
                    p.x = o[0];
                    p.y = o[1];
                    p.z = o[2];
                }
            }
            const uint32_t r1 = rngStep();
            f.life(k) = uint16_t((r1 & 0xf) + kParticleLife);
            const uint32_t r2 = rngStep();
            auto&& wobble = f.wobble[k];
            wobble[0] = int8_t(uint8_t(((r2 >> 4) & 0xf) - 8));
            wobble[1] = int8_t(uint8_t(((r2 >> 8) & 0xf) - 8));
            f.liveMask = uint16_t(f.liveMask | (1u << k));
            particleBorn(a.at, k);
            const uint32_t iv = vt.interval;
            const uint32_t r3 = rngStep();
            f.emitTimer = uint16_t((r3 & 0xffff) % iv + iv);
            break;
        }
    } else {
        f.emitTimer = uint16_t(f.emitTimer - 1);
    }
    if (f.lifetime != 0) {
        f.ticksLeft = uint16_t(f.ticksLeft - 1);
        if (f.ticksLeft == 0) a.subState = uint8_t(SparkEmitterSub::Drain);
    }
    if ((vt.flags & kEmitAtParent) && f.parent && f.parent->flags == 0) a.subState = uint8_t(SparkEmitterSub::Drain);
}

void sparkEmitter(StaticActor& a) { // exe 0x592050
    SparkEmitterFields& f = classState<SparkEmitterFields>(a);
    switch (SparkEmitterState(a.state)) {
    case SparkEmitterState::Setup: { // exe 0x592070
        const SparkEmitterVariant vt = variantOf(a);
        a.flags &= uint8_t(~kActorDrawn);
        a.runFlags |= kActorWarpKept;
        a.prevPos.x = {};
        a.prevPos.y = {};
        a.prevPos.z = {};
        f.liveMask = 0;
        f.emitTimer = 0;
        f.ticksLeft = uint16_t(f.lifetime);
        f.rise = int8_t(vt.rise);
        if (!(vt.flags & kEmitAtParent)) f.parent = nullptr;
        a.state = uint8_t(SparkEmitterState::Run);
        return;
    }
    case SparkEmitterState::Run: // exe 0x5920e0
        emitterParticles(a, f);
        switch (SparkEmitterSub(a.subState)) {
        case SparkEmitterSub::Start: a.subState = uint8_t(SparkEmitterSub::Emit); return; // exe 0x5c2720
        case SparkEmitterSub::Emit: emitterEmit(a, f); return;                            // exe 0x592350
        case SparkEmitterSub::Drain:                                                      // exe 0x592560
            if (f.liveMask == 0) freeActor(a);
            return;
        default: notPortedAt(kSparkEmitterPort, "0x592050 sub-state", a.subState);
        }
        return;
    case SparkEmitterState::Resume: a.subState = uint8_t(SparkEmitterSub::Emit); return; // exe 0x5c2720
    default: notPortedAt(kSparkEmitterPort, "0x592050 state", a.state);
    }
}

} // namespace

void sparkEmitterUpdate(const PlayerEnv&, uint32_t actor) { sparkEmitter(staticActorAt(actor)); }

} // namespace od::port
