// The attract intro's effect classes (see attract_effects.hpp). Their quads, triangles and sprites are render output;
// the logic below is what the game state keeps: the bolts' points and fades, the ring's radius and the RNG steps its
// draw takes, the VRAM tile's image moves (game/vram_moves).
#include "content/intro/attract_effects.hpp"
#include "content/intro/attract_effects_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/global_class_types.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/vram_moves.hpp"

namespace od::port {

using namespace attract_effects_state;

namespace {

// ---- the lightning (exe 0x59aea0, ST02 static type 0x15)
// ---------------------------------------------------------------
// The emitters (variants 0 / 1) spawn a bolt (a variant 2 of the same type) every 0x1e + rng % 0x1e ticks at their
// position, jittered; variant 1 also starts the CLUT animations (game/clut_animation) of its sequences. A bolt is 16
// points grown 4 a tick from the emitter down, its points fading by 0x10 a tick; freed once 15 points faded out.
enum class LightningState : uint8_t { Init = 0, Run = 1 }; // exe 0x59aec0 / 0x59afc0 (table 0x8ce4f0)
const DataTable<RoutineTable<3>> lightningVariants{TableId::AttractEffectsLightningVariants};
constexpr uint8_t kBoltVariant = 2;

constexpr int16_t kBoltFadeStart = 0x80, kBoltFadeStep = 0x10;
constexpr int32_t kBoltJitter = 100, kBoltSpreadXZ = 0x32, kBoltDrop = 100;
constexpr uint8_t kBoltGrowth = 4;
constexpr uint8_t kBoltFadedOut = 0xf;
constexpr int32_t kEmitterPeriod = 0x1e;

constexpr uint8_t kNoPointOut = 0; // the bolt's points faded out (its step byte) at its start

// exe 0x59aec0: a bolt's points laid out from the position, each a random step down from the last (the emitters lay
// them out too); state 1, the counts and the timer cleared
void lightningInit(StaticActor& a) {
    LightningFields& f = classState<LightningFields>(a);
    if (a.variant != 0) {
        for (size_t i = 0; i < kBoltPoints; ++i) f.points[i].fade = kBoltFadeStart;
        auto&& first = f.points[0];
        first.x = a.pos.x.whole;
        first.y = a.pos.y.whole;
        first.z = a.pos.z.whole;
        for (size_t i = 1; i < kBoltPoints; ++i) {
            auto&& q = f.points[i];
            const BoltPoint prev = f.points[i - 1];
            rngStep();
            q.x = int16_t(uint32_t(int32_t(uint16_t(rngLastValue())) % kBoltJitter) + uint16_t(prev.x) -
                          uint32_t(kBoltSpreadXZ));
            rngStep();
            q.y = int16_t(uint32_t(int32_t(uint16_t(rngLastValue())) % kBoltJitter) + uint16_t(prev.y) +
                          uint32_t(kBoltDrop));
            rngStep();
            // the division takes the whole unsigned 32-bit state
            q.z = int16_t((rngLastValue() % uint32_t(kBoltJitter)) + uint16_t(prev.z) - uint32_t(kBoltSpreadXZ));
        }
    }
    a.enterStateBytes(uint8_t(LightningState::Run)); // the points grown (the sub-state) cleared
    a.step = kNoPointOut;
    f.timer = 0;
}

// the emitters' bolt: a variant 2 of the same type at the position, x += (rng & 0x7ff) - 0x400, z += (rng & zMask) -
// zHalf, y += dy
void spawnBolt(StaticActor& a, uint32_t zMask, uint32_t zHalf, uint32_t dy) {
    const uint32_t at = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (at == 0) return;
    StaticActor& c = staticActorAt(at);
    c.flags = kActorLive;
    c.type = a.type;
    c.variant = kBoltVariant;
    c.loopFlags |= kActorLateLoop;
    c.pos = a.pos;
    rngStep();
    c.pos.x.whole = int16_t(uint16_t(c.pos.x.whole) + ((uint16_t(rngLastValue()) & 0x7ffu) - 0x400u));
    rngStep();
    const uint32_t dz = (uint16_t(rngLastValue()) & zMask) - zHalf;
    c.setParamsWord(0);
    c.pos.z.whole = int16_t(uint16_t(c.pos.z.whole) + dz);
    c.pos.y.whole = int16_t(uint16_t(c.pos.y.whole) + dy);
}

// the emitters' timer: counts; past 0x1e it restarts at rng % 0x1e (true: a bolt now)
bool emitterDue(StaticActor& a) {
    LightningFields& f = classState<LightningFields>(a);
    const int32_t t = f.timer;
    f.timer = t + 1;
    if (t < kEmitterPeriod) return false;
    rngStep();
    f.timer = int32_t(uint16_t(rngLastValue())) % kEmitterPeriod;
    return true;
}

// exe 0x59b240 (variant 2): the bolt grows (4 points a tick, 16 at most); every point after the faded ones fades,
// one reaching below 0 counts out
void bolt(StaticActor& a) {
    LightningFields& f = classState<LightningFields>(a);
    const uint8_t n = uint8_t(a.subState + kBoltGrowth);
    a.subState = n;
    if (n > kBoltPoints) a.subState = uint8_t(kBoltPoints);
    const int32_t grown = a.subState;
    const int32_t out = a.step;
    // the segment out .. grown - 1: two semi-transparent gouraud quads per segment (render output)
    for (int32_t k = out + 1; k < grown; ++k) {
        auto&& fade = f.points[size_t(k)].fade;
        const uint16_t v = uint16_t(uint16_t(fade) - uint32_t(kBoltFadeStep));
        fade = int16_t(v);
        if (int16_t(v) < 0) {
            fade = 0;
            ++a.step;
        }
    }
    if (a.step >= kBoltFadedOut) freeActor(a); // exe 0x5e1540
}

// exe 0x59aea0: the state (exe table 0x8ce4f0), then the variant (exe table 0x8ce4f8)
void lightning(StaticActor& a) {
    switch (LightningState(a.state)) {
    case LightningState::Init: lightningInit(a); return;
    case LightningState::Run: break;
    default: notPortedAt(kStoryPort, "0x59aea0 state", a.state);
    }
    switch (lightningVariants->idAt(a.variant)) {
    case RoutineId::AttractEmitter:
        if (emitterDue(a)) spawnBolt(a, 0x7ff, 0x400, 0xf000);
        break;
    case RoutineId::AttractFlashingEmitter:
        if (emitterDue(a)) {
            spawnBolt(a, 0x3ff, 0x200, 0xf800);
            LightningFields& f = classState<LightningFields>(a);
            for (int32_t i = f.firstSequence; i < f.endSequence; ++i) {
                const uint32_t at = allocActorA0(0); // exe 0x5e13b0 (the global table)
                if (at == 0) continue;
                EffectActor& e = effectActorAt(at);
                e.flags = kActorLive;
                e.type = uint8_t(GlobalEffectType::ClutAnimation);
                e.variant = 0;
                e.loopFlags |= kActorLateLoop;
                e.params[0] = uint8_t(i); // the CLUT sequence (game/clut_animation)
            }
        }
        break;
    case RoutineId::AttractBolt: bolt(a); break;
    default: notPortedAt(kStoryPort, "0x59aea0 variant", a.variant);
    }
}

// ---- the ring / flash (exe 0x4236a0, controller type 0xe)
// --------------------------------------------------------------
// The ring (variant 0) grows 4 ticks, then shrinks 3 + 3 ticks at half the step, freed; the flashes (variants 1 / 2)
// last 7 ticks, freed (variant 1 at the screen point (0, 0, 0xc0)). Its triangles are render output; the ring's outer
// band draws one RNG step a segment but the last: 15 steps of the game RNG a tick.
const DataTable<RoutineTable<3>> ringFlashStates{TableId::AttractEffectsRingFlashStates};
enum class RingFlashState : uint8_t { Init = 0, Ring = 1, Flash = 2 };
enum class RingPhase : uint8_t { Start = 0, Grow = 1, Shrink = 2, Done = 3 };
enum class FlashPhase : uint8_t { Start = 0, Shine = 1, Done = 2 };
constexpr uint32_t kRadiusStepShift = 4;
constexpr uint8_t kSeedMask = 0xf;
constexpr uint8_t kGrowTicks = 4, kShrinkTicks = 3, kShrinkSeedStep = 0xa, kFlashTicks = 7;
constexpr int16_t kScreenFlashDepth = 0xc0;
constexpr int kRingDrawRngSteps = 15;

// exe 0x4236c0: the variant's state and centre, the radius's step, the wobble seed
void ringFlashInit(ControllerActor& a, RingFlashFields& f) {
    switch (RingFlashVariant(a.variant)) {
    case RingFlashVariant::Ring: a.state = uint8_t(RingFlashState::Ring); break;
    case RingFlashVariant::ScreenFlash:
        f.centre.x = 0;
        f.centre.y = 0;
        f.centre.z = kScreenFlashDepth;
        a.state = uint8_t(RingFlashState::Flash);
        break;
    case RingFlashVariant::Flash: a.state = uint8_t(RingFlashState::Flash); break;
    default: break;
    }
    a.restartStep();
    a.loopFlags |= kActorLateLoop;
    f.radiusStep = uint16_t(f.radius >> kRadiusStepShift);
    f.phaseTicks = 0;
    rngStep();
    f.wobbleSeed = uint8_t(uint8_t(rngLastValue()) & kSeedMask);
}

// exe 0x423730: the ring's phases, then the RNG steps of its draw
void ring(ControllerActor& a, RingFlashFields& f) {
    // the grey level of its triangles (exe 0x423f00): render output
    auto growEnd = [&]() { // exe 0x4237a2
        const uint8_t s = f.wobbleSeed;
        f.phaseTicks = kShrinkTicks;
        f.radiusStep = uint16_t(f.radiusStep >> 1);
        f.wobbleSeed = uint8_t(s + kShrinkSeedStep);
        ++a.step;
    };
    switch (RingPhase(a.step)) { // exe jump table 0x423bd8
    case RingPhase::Start:
        f.phaseTicks = kGrowTicks;
        ++a.step;
        rngStep();
        f.wobbleSeed = uint8_t(uint8_t(rngLastValue()) & kSeedMask);
        [[fallthrough]];
    case RingPhase::Grow:
        f.radius += f.radiusStep;
        --f.phaseTicks;
        if (f.phaseTicks == 0) growEnd();
        break;
    case RingPhase::Shrink:
        f.radius -= f.radiusStep;
        --f.phaseTicks;
        if (f.phaseTicks == 0) ++a.step;
        break;
    case RingPhase::Done:
        freeActor(a); // exe 0x5e15c0
        return;
    default: break;
    }
    for (int i = 0; i < kRingDrawRngSteps; ++i) rngStep();
}

// exe 0x423bf0: the flash's 7 ticks, then freed
void flash(ControllerActor& a, RingFlashFields& f) {
    switch (FlashPhase(a.step)) {
    case FlashPhase::Start:
        f.phaseTicks = kFlashTicks;
        ++a.step;
        [[fallthrough]];
    case FlashPhase::Shine:
        // the disc's grey level (exe 0x423f00): render output
        --f.phaseTicks;
        if (f.phaseTicks == 0) ++a.step;
        break;
    case FlashPhase::Done:
        freeActor(a); // exe 0x5e15c0
        return;
    default: break;
    }
    // its 16-triangle disc (render output, no RNG)
}

// exe 0x4236a0: the state's routine (exe table 0x88bd70)
void ringFlash(ControllerActor& a) {
    RingFlashFields& f = classState<RingFlashFields>(a);
    switch (ringFlashStates->idAt(a.state)) {
    case RoutineId::AttractRingFlashInit: ringFlashInit(a, f); return;
    case RoutineId::AttractRing: ring(a, f); return;
    case RoutineId::AttractFlash: flash(a, f); return;
    default: notPortedAt(kStoryPort, "0x4236a0 state", a.state);
    }
}

// ---- the animated VRAM tile (exe 0x418550, ST03 controller type 5)
// ---------------------------------------------------------
// its key (0 .. 5) moved on when its ticks run out, the key's frame and ticks from the exe table 0x88a0f8; every tick
// the frame's two image moves (exe 0x40b710: queued through game/vram_moves)
struct VramTileFields {
    explicit VramTileFields(const ControllerActor& actor) : VramTileFields(const_cast<ControllerActor&>(actor)) {}
    explicit VramTileFields(ControllerActor& actor) : key(actor.params[0]), frame(actor.params[1]), ticks(actor.params[2]) {}
    FieldAlias<int8_t, uint8_t> key;
    FieldAlias<int8_t, uint8_t> frame;
    uint8_t& ticks;
};
struct VramTileKey : DataRecord {
    int8_t frame = 0;
    uint8_t ticks = 0;
    static VramTileKey load(const TableReader& r) {
        VramTileKey rec;
        rec.frame = r.s8(0);
        rec.ticks = r.u8(1);
        return rec;
    }
};
const DataTable<RecordTable<VramTileKey, 6, 2>> vramTileKeys{TableId::AttractEffectsVramTileKeys};
constexpr int8_t kVramTileKeys = 6;

// exe 0x418550: the key stepped when its ticks run out, the frame's two image moves
void vramTile(ControllerActor& a) {
    VramTileFields f(a);
    if (f.ticks == 0) {
        const int8_t k = int8_t(f.key + 1);
        f.key = k;
        if (k == kVramTileKeys) f.key = 0;
        const VramTileKey key = vramTileKeys[size_t(int32_t(f.key))];
        f.frame = key.frame;
        f.ticks = key.ticks;
    }
    const int d = f.frame;
    vramMove(int16_t(uint16_t(uint32_t(0x280 + ((d << 6) >> 2)))), 0x1e0, 0x10, 0x20, 0x3b0, 0x1a0); // exe 0x40b710
    vramMove(0x2c0, int16_t(uint16_t(uint32_t((d + 4) << 6))), 0x40, 0x40, 0x380, 0x1c0);
    --f.ticks;
}

} // namespace

// the classes' updates (attract_effects.hpp)
void attractLightningUpdate(const PlayerEnv&, uint32_t actor) { lightning(staticActorAt(actor)); }
void attractRingFlashUpdate(const PlayerEnv&, uint32_t actor) { ringFlash(controllerActorAt(actor)); }
void attractVramTileUpdate(const PlayerEnv&, uint32_t actor) { vramTile(controllerActorAt(actor)); }

} // namespace od::port
