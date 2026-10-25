// The effects of a hit (the global effect table 0x8b205c and the global static table): the hit spark (effect type 0:
// a keyframed sprite and its sound), the burst of a shot (effect type 1: a ring of random spokes), the global spark
// spawn every class uses (exe 0x5e5400), and the player's hurt sparks (static type 13: a ring of sparks around the hit
// point that swing while on screen). The sprites are draw commands (render output).
#pragma once

#include <cstdint>

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port {

// The hit spark's fields (its effect slot): the spawn's size, the keyframes of the variant's script (exe data
// 0x8c3c20: 12-byte frames {+0 the sprite's colour word (its code byte +3: bit 1 semi-transparent), +4..+7 the texel
// rect u0 v0 u1 v1, +8 ticks, +9 next: 0xff end, 0x80 | k jump to frame k, +0xa the size added, +0xb the rotation >> 4})
struct SparkFrame : DataRecord {
    static constexpr uint32_t kBytes = 0xc;
    uint32_t colour = 0; // +0x0
    uint8_t code = 0; // +0x3
    uint8_t u0 = 0; // +0x4
    uint8_t v0 = 0; // +0x5
    uint8_t u1 = 0; // +0x6
    uint8_t v1 = 0; // +0x7
    uint8_t ticks = 0; // +0x8
    uint8_t next = 0; // +0x9
    uint8_t sizeAdd = 0; // +0xa
    uint8_t rotation = 0; // +0xb
    template <class R> static SparkFrame load(const R& r) {
        SparkFrame v;
        v.colour = r.template read<uint32_t>(0x0);
        v.code = r.template read<uint8_t>(0x3);
        v.u0 = r.template read<uint8_t>(0x4);
        v.v0 = r.template read<uint8_t>(0x5);
        v.u1 = r.template read<uint8_t>(0x6);
        v.v1 = r.template read<uint8_t>(0x7);
        v.ticks = r.template read<uint8_t>(0x8);
        v.next = r.template read<uint8_t>(0x9);
        v.sizeAdd = r.template read<uint8_t>(0xa);
        v.rotation = r.template read<uint8_t>(0xb);
        return v;
    }
};
// A hit spark variant's style (exe data 0x8c3a08, 8 bytes): its texture page and CLUT, semi-transparent (bit 0), its
// sound (< 0: none)
struct SparkStyle : DataRecord {
    uint16_t tpage = 0, clut = 0, semi = 0;
    int16_t sound = 0;
    static SparkStyle load(const TableReader& r) { return {{}, r.u16(0), r.u16(2), r.u16(4), r.s16(6)}; }
};
constexpr uint32_t kSparkStyleCount = 0x20;
using SparkStyleTable = RecordTable<SparkStyle, kSparkStyleCount, 8>;
inline const SparkStyleTable& sparkStyles() {
    static const DataTable<SparkStyleTable> t{TableId::HitSparkStyles};
    return *t;
}
// the burst's colour by its size class >> 1 (exe data 0x8c3c50: r | g << 8 | b << 16)
inline const DataTable<ScalarTable<uint32_t, 8>> burstColours{TableId::BurstColours};
// the effects' states (+8)
enum class HitSparkState : uint8_t { Start = 0, Play = 1 };     // exe 0x55d230 / 0x55d280
enum class BurstState : uint8_t { Start = 0, Spokes = 1, Plain = 2 };
enum class HurtSparksState : uint8_t { Start = 0, Swing = 1 };  // exe 0x566150 / 0x566260
constexpr uint8_t kBurstTicks = 6; // the burst's life (its ticks count down from it)
// The hurt sparks' variants (exe data 0x8c46d0, 8 bytes each): the radius mask / base, the sparks, the ticks, the
// lines' colour scale and their extra length (the draw's)
struct HurtSparksVariant : DataRecord {
    uint8_t radiusMask = 0;
    int8_t radiusBase = 0;
    int8_t count = 0;
    uint8_t ticks = 0;
    uint16_t colourScale = 0;
    int16_t extraLength = 0;
    static HurtSparksVariant load(const TableReader& r) {
        return {{}, r.u8(0), r.s8(1), r.s8(2), r.u8(3), r.u16(4), r.s16(6)};
    }
};
using HurtSparksVariantTable = RecordTable<HurtSparksVariant, 8, 8>;
inline const HurtSparksVariantTable& hurtSparksVariants() {
    static const DataTable<HurtSparksVariantTable> t{TableId::HurtSparksVariants};
    return *t;
}
constexpr uint16_t kHitSparkSize = 0x80; // the size exe 0x5e5400 gives its spark
// the hit spark's variants (its style in the table kSparkStyles) the game spawns by name
constexpr uint8_t kSparkVariantHit = 0;
constexpr uint8_t kSparkVariantDust = 5;       // a landing's dust (game/step_dust)
constexpr uint8_t kSparkVariantGroundDust = 6; // the dust of a step on some grounds (game/step_dust)
struct HitSparkFields : ActorClassState {
    explicit HitSparkFields(EffectActor& actor) : size(actor.params, 0), frameTicks(actor.params[2]), tick(actor.params[3]) {}
    BytesField<uint16_t, 4> size; // the sprite's size (the spawner's)
    FieldAlias<int8_t, uint8_t> frameTicks; // the frame's ticks (the spawners write 1; the first state sets it)
    FieldAlias<int8_t, uint8_t> tick; // ticks into the frame
    DataPtr<SparkFrame> firstFrame;
    DataPtr<SparkFrame> frame;
};
// The burst's fields: its size class (the spawner's), its life, its colour (in the header's sub-state / step bytes,
// which this class keeps its colour in) and the 16 spokes
struct BurstFields : ActorClassState {
    explicit BurstFields(EffectActor& actor) : size(actor.params, 0), sizeClass(actor.params[2]), ticks(actor.params[3]), red(actor.subState), green(actor.step), blue(actor.stepCounter) {}
    BytesField<uint16_t, 4> size;
    uint8_t& sizeClass;
    uint8_t& ticks;
    uint8_t& red;
    uint8_t& green;
    uint8_t& blue;
    StateArray<uint8_t, 0x10> spokes{};
};
// The hurt sparks' fields (its static slot; the spark rings reuse the bytes past the actor's collision fields): the
// spawn's tilt / direction of the hit, the sparks' count / life (the header's sub-state / step bytes) and per spark its
// angle, height and radius
struct HurtSparksFields : ActorClassState {
    explicit HurtSparksFields(StaticActor& actor) : pitch(actor.params, 0), yaw(actor.params, 2), count(actor.subState), ticks(actor.step), startTicks(actor.stepCounter) {}
    BytesField<uint16_t, 4> pitch; // the hit's tilt (the player's spawn: 0)
    BytesField<uint16_t, 4> yaw; // the hit's direction
    uint8_t& count;
    uint8_t& ticks;
    uint8_t& startTicks;
    StateArray<uint8_t, 0x20> angles{};
    StateArray<uint8_t, 0x20> heights{};
    StateArray<uint8_t, 0x20> radii{};

    void clearBlock() override {
        for (size_t i = 20; i < 32; ++i) radii[i] = {};
    }
};

// exe 0x55d210 (effect type 0): the hit spark
void hitSparkUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x55d3c0 (effect type 1): a shot's burst (6 ticks)
void burstUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x566130 (static type 13): the player's hurt sparks
void hurtSparksUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5e5400(loopFlags, x, y, z): the global hit spark (effect type 0, variant 0) at the s16 point, its sprite's size
// kHitSparkSize
void spawnHitSpark(uint8_t loopFlags, int16_t x, int16_t y, int16_t z);
// exe 0x5e5400 as the transcriptions of its callers had it (content/manda/hopping_drone and six more): the size not
// written (the exe writes kHitSparkSize)
void spawnHitSparkWithoutSize(uint8_t loopFlags, int16_t x, int16_t y, int16_t z);

} // namespace od::port
