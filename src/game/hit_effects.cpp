// The effects of a hit (see hit_effects.hpp).
#include "game/loaded_data.hpp"
#include "game/hit_effects.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/global_class_types.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"

namespace od::port {

namespace {

inline constexpr const char* kHitEffectsArea = "hit effects";

// the hit spark's script per variant (exe data 0x8c3c20: the address of its first frame); its sound: its style's
// (the frames' addresses, as the spark stores them: the address is the record's identity)
const DataTable<ScalarTable<uint32_t, 0x20>> sparkScripts{TableId::HitSparkScripts};
constexpr uint8_t kFrameEnd = 0xff;  // a frame's next: the spark ends ...
constexpr uint8_t kFrameJump = 0x80; // ... or jumps to frame next & 0x7f
constexpr uint32_t kSparkFrameSize = 0xc;

constexpr uint16_t kBurstSound = 0x9b;

// exe 0x566290's state part: while the sparks are on screen (exe 0x5f2dd0) each angle swings by 2 (the draw is render
// output)
void swingSparks(StaticActor& actor) {
    if (actorOnScreen(actor.pos) < 0) return;
    auto&& sparks = classState<HurtSparksFields>(actor);
    for (uint32_t i = 0; i < sparks.count; ++i) {
        const uint8_t c = sparks.angles[i];
        if (uint8_t(c + 0x40) < 3)
            sparks.angles[i] = 0xc0;
        else if (uint8_t(c + 0x40) < 0x81)
            sparks.angles[i] = uint8_t(c - 2);
        else
            sparks.angles[i] = uint8_t(c + 2);
    }
}

// exe 0x5e5400's spark without its size: a live, drawn global hit spark at the s16 point (null when the pool is full)
EffectActor* newHitSpark(uint8_t loopFlags, int16_t x, int16_t y, int16_t z) {
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (!address) return nullptr;
    EffectActor& e = effectActorAt(address);
    e.type = typeByte(GlobalEffectType::HitSpark);
    e.variant = kSparkVariantHit;
    e.loopFlags |= loopFlags;
    e.pos.x = x;
    e.flags = kActorLive | kActorDrawn;
    e.pos.y = y;
    e.pos.z = z;
    return &e;
}

} // namespace

void hitSparkUpdate(const PlayerEnv&, uint32_t address) {
    EffectActor& spark = effectActorAt(address);
    auto&& fields = classState<HitSparkFields>(spark);
    switch (HitSparkState(spark.state)) {
    case HitSparkState::Start: { // exe 0x55d230
        spark.runFlags |= kActorWarpKept;
        const uint32_t script = sparkScripts[spark.variant];
        fields.frame.pointAt(script);
        fields.firstFrame.pointAt(script);
        fields.tick = 0;
        fields.frameTicks = int8_t(dataRecord<SparkFrame>(script, SparkFrame::kBytes).ticks);
        const int16_t sound = sparkStyles()[spark.variant].sound;
        if (sound >= 0) soundPlayAtPosS16(uint16_t(sound), spark.pos);
        ++spark.state;
        return;
    }
    case HitSparkState::Play: { // exe 0x55d280
        const int8_t tick = fields.tick;
        const uint32_t frame = fields.frame.value();
        fields.tick = int8_t(tick + 1);
        if (fields.frameTicks < int8_t(tick + 1)) {
            const uint8_t next = dataRecord<SparkFrame>(frame, SparkFrame::kBytes).next;
            if (int8_t(next) < 0) {
                if (next == kFrameEnd)
                    freeActor(spark);
                else
                    fields.frame.pointAt(fields.firstFrame.value() + (next & ~kFrameJump) * kSparkFrameSize);
            } else {
                fields.frame.pointAt(frame + kSparkFrameSize);
            }
            fields.tick = 0;
            fields.frameTicks = int8_t(fields.frame->ticks);
        }
        // the sprite: a draw command (render output)
        return;
    }
    default: notPortedAt(kHitEffectsArea, "hit spark state", spark.state);
    }
}

void burstUpdate(const PlayerEnv&, uint32_t address) {
    EffectActor& burst = effectActorAt(address);
    auto&& fields = classState<BurstFields>(burst);
    switch (BurstState(burst.state)) {
    case BurstState::Start: {
        burst.runFlags |= kActorWarpKept;
        const uint32_t colour = burstColours[fields.sizeClass >> 1];
        fields.red = uint8_t(colour);
        fields.blue = uint8_t(colour >> 16);
        fields.green = uint8_t(colour >> 8);
        if (burst.variant == 0) {
            for (size_t k = 0; k < fields.spokes.size(); ++k) {
                rngStep();
                const uint32_t r = rngStep();
                fields.spokes[k] = uint8_t((r & 0x3f) | ((r << 6) & 0xff));
            }
            soundPlayAtPosS16(kBurstSound, burst.pos);
            fields.ticks = kBurstTicks;
            burst.state = uint8_t(BurstState::Spokes);
            return;
        }
        fields.ticks = kBurstTicks;
        burst.state = uint8_t(BurstState::Plain);
        return;
    }
    case BurstState::Spokes:
    case BurstState::Plain:
        if (fields.ticks != 0) {
            // exe 0x55d4e0 / 0x55d950: draw commands (render output)
            --fields.ticks;
            return;
        }
        freeActor(burst);
        return;
    default: notPortedAt(kHitEffectsArea, "burst state", burst.state);
    }
}

void hurtSparksUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    auto&& sparks = classState<HurtSparksFields>(actor);
    switch (HurtSparksState(actor.state)) {
    case HurtSparksState::Start: { // exe 0x566150
        const HurtSparksVariant v = hurtSparksVariants()[actor.variant];
        actor.runFlags |= kActorWarpKept;
        const int8_t n = v.count;
        const uint8_t mask = v.radiusMask;
        const int8_t base = v.radiusBase;
        sparks.count = uint8_t(n);
        sparks.startTicks = v.ticks;
        sparks.ticks = v.ticks;
        if (n != 0) {
            uint32_t i = 0;
            do {
                uint32_t r = rngStep();
                sparks.angles[i] = uint8_t(((((r >> 4) & 0x3ff) - 0x100) + sparks.pitch) >> 4);
                r = rngStep();
                sparks.heights[i] = uint8_t((((r >> 8) - 0x800) + sparks.yaw) >> 4);
                r = rngStep();
                sparks.radii[i] = uint8_t(((r >> 12) & mask) + uint32_t(base));
                ++i;
            } while (i < sparks.count);
        }
        swingSparks(actor);
        ++actor.state;
        return;
    }
    case HurtSparksState::Swing: // exe 0x566260
        if (sparks.ticks != 0) {
            --sparks.ticks;
            swingSparks(actor);
            return;
        }
        freeActor(actor);
        return;
    default: notPortedAt(kHitEffectsArea, "hurt effect state", actor.state);
    }
}

void spawnHitSpark(uint8_t loopFlags, int16_t x, int16_t y, int16_t z) {
    if (EffectActor* e = newHitSpark(loopFlags, x, y, z)) classState<HitSparkFields>(effectActorAt(e->at)).size = kHitSparkSize;
}

void spawnHitSparkWithoutSize(uint8_t loopFlags, int16_t x, int16_t y, int16_t z) { newHitSpark(loopFlags, x, y, z); }

} // namespace od::port
