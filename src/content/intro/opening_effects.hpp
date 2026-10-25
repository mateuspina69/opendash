// The opening's effect class (exe 0x435960, effect type 0xf of ST39 and ST3A): fuses, sparkles, sparks riding an
// actor, debris chips, the Flutter's engine flame, streaks and puffs, a flash ring -- the logic of each variant (their
// sprites and quads are render output: content/intro/opening_effects_draw draws the engine flame, the streaks and the
// puffs). The spawners the
// opening's classes and scripts call (content/intro/opening_cast.cpp, crash_landing_script.cpp, crash_landing_cast.cpp)
// fill the effect's parameters.
#pragma once

#include <cstdint>

#include "engine/data_source.hpp"
#include "game/actor_state.hpp"
#include "game/effects_draw.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"

namespace od::port {

struct PlayerEnv;

// +8 of the effect: its first state (its tail cleared; the flash ring projects its position), then running
enum class OpeningEffectState : uint8_t { Start = 0, Running = 1 };
// the effect's class (pool 0xa0 type) and its variants
constexpr uint8_t kOpeningEffectType = 0xf;
enum class OpeningEffect : uint8_t {
    Fuse = 0,        // runs along a line, flashes on the ground under it
    Sparkle = 1,     // a sparkle of four sprites
    RiderSpark = 2,  // a spark riding an actor (sound 0xb1)
    Chip = 3,        // a debris chip thrown up, then along its yaw
    EngineFlame = 4, // the flame that follows the Flutter (or a craft)
    Streak = 5,      // a cloud streak along its yaw
    LiftStreak = 6,  // a streak along its yaw, rising
    OwnerPuff = 7,   // a puff thrown back from its owner
    BonePuff = 8,    // a puff riding its owner's first bone
    BurstPuff = 9,   // a puff thrown from its owner, growing
    ThrownPuff = 10, // a puff thrown along its yaw, growing
    FlashRing = 11,  // a flash ring on the screen
};

// The effect's bytes past its position that its spawners set (the streaks' and the puffs' yaw)
struct OpeningEffectFields {
    explicit OpeningEffectFields(const EffectActor& actor) : OpeningEffectFields(const_cast<EffectActor&>(actor)) {}
    explicit OpeningEffectFields(EffectActor& actor) : yaw(actor.posSpare) {}
    int16_t& yaw;
};

// ---- the variants' fields their spawners set (the opening's and the attract's scripts and classes) ---------------
// the fuse (Fuse): runs from (fromX, fromZ) to (toX, toZ) in `steps` steps (16.16 x / z, the steps << 4), a flash on the
// ground under it every other tick while it is drawn; freed after `life` ticks
struct FuseFields : ActorClassState {
    explicit FuseFields(EffectActor& actor) : steps(actor.params[0]), life(actor.params[1]), flashParam(actor.params, 2), fromX(actor.pos.x), fromZ(actor.pos.y), toX(actor.pos.z), toZ(actor.posSpare), stepX(actor.trail[4]), stepZ(actor.trail[5]) {}
    FieldAlias<int8_t, uint8_t> steps;
    FieldAlias<int8_t, uint8_t> life;
    BytesField<uint16_t, 4> flashParam; // handed to the flashes
    int16_t& fromX;
    int16_t& fromZ;
    int16_t& toX;
    int16_t& toZ;
    int16_t& stepX;
    int16_t& stepZ;
    uint32_t x = 0;
    uint32_t z = 0;
    int16_t age = 0;
    uint16_t flashWord = 0;

    void clearBlock() override {
        x = 0;
        z = 0;
        age = 0;
        flashWord = 0;
    }
};
// the sparkle (Sparkle): four sprites k = 0, 4, 8, 12 shown while age + k < life (its yaw: the spawn's direction << 4;
// the sprites, drawn with the spawn's sprite bytes, are render output); none left: freed
struct SparkleFields {
    explicit SparkleFields(const EffectActor& actor) : SparkleFields(const_cast<EffectActor&>(actor)) {}
    explicit SparkleFields(EffectActor& actor) : direction(actor.params[0]), spriteSize(actor.params[1]), spriteShade(actor.params[2]), lifeParam(actor.params[3]), spriteWord(actor.posSpare), age(actor.trail[0]), life(actor.trail[1]), yaw(actor.trail[2]) {}
    FieldAlias<int8_t, uint8_t> direction;
    uint8_t& spriteSize; // read by the sprites only
    uint8_t& spriteShade; // read by the sprites only
    FieldAlias<int8_t, uint8_t> lifeParam;
    int16_t& spriteWord; // the short after the position (read by the sprites only)
    int16_t& age;
    int16_t& life;
    int16_t& yaw;
};
// the rider spark (RiderSpark): rides the actor of slot `owner` (moved by its yaw and velocity), sound 0xb1 each tick it
// is drawn; `ticks` (> 0: freed then; 0 in the spawn: 2)
struct RiderSparkFields : ActorClassState {
    explicit RiderSparkFields(EffectActor& actor) : ownerSlot(actor.params[0]), colour(actor.params[1]), ticksParam(actor.params[2]), unusedParam(actor.params[3]), drawParam(actor.posSpare), age(actor.trail[2]), ticks(actor.trail[3]) {}
    FieldAlias<int8_t, uint8_t> ownerSlot;
    uint8_t& colour;
    FieldAlias<int8_t, uint8_t> ticksParam;
    uint8_t& unusedParam; // the spawn's fourth byte (no reader)
    FieldAlias<uint16_t, int16_t> drawParam; // the short after the position (the quads' low word)
    int16_t& age;
    int16_t& ticks;
    uint32_t owner = 0; // the owner's address (exe 0x5e0fa0 of the slot)
    uint32_t drawWord = 0; // the colour << 20 | the spare short (the quads' word)

    void clearBlock() override {
        owner = 0;
        drawWord = 0;
    }
};
// the chip (Chip): first `riseTicks` ticks rising (along yaw 0xe0 by (0, lift >> 5, reach >> 3)), then along its yaw by
// (0, lift, reach) for its life (its quad: render output)
struct ChipFields : ActorClassState {
    explicit ChipFields(EffectActor& actor) : direction(actor.params[0]), reachParam(actor.params[1]), liftParam(actor.params[2]), lifeParam(actor.params[3]), clutParam(actor.posSpare), age(actor.trail[0]), life(actor.trail[1]), yaw(actor.trail[2]), lift(actor.trail[4]), reach(actor.trail[5]) {}
    uint8_t& direction;
    FieldAlias<int8_t, uint8_t> reachParam;
    FieldAlias<int8_t, uint8_t> liftParam;
    FieldAlias<int8_t, uint8_t> lifeParam;
    int16_t& clutParam;
    int16_t& age;
    int16_t& life;
    int16_t& yaw;
    int16_t& lift;
    int16_t& reach;
    uint8_t riseTick = 0;
    uint8_t riseTicks = 0;
    uint32_t clutWord = 0;

    void clearBlock() override {
        riseTick = 0;
        riseTicks = 0;
        clutWord = 0;
    }
};

// ---- the variants' fields the draw reads too (content/intro/opening_effects_draw.cpp) ------------------------
// the engine flame (EngineFlame): follows the actor of slot `ownerSlot` (with `follows`: its position and yaw copied each
// tick); the owner freed: freed
struct EngineFlameFields : ActorClassState {
    explicit EngineFlameFields(EffectActor& actor) : ownerSlot(actor.params[0]), follows(actor.params[1]), drawParams(actor.params, 2), yaw(actor.posSpare) {}
    FieldAlias<int8_t, uint8_t> ownerSlot;
    uint8_t& follows;
    ArrayFields<uint8_t, 2, uint8_t, 4> drawParams; // the spawn's last two bytes (no reader traced)
    int16_t& yaw;
    uint32_t owner = 0;

    void clearBlock() override { owner = 0; }
};
// the streaks (Streak / LiftStreak): along their yaw for (lifeParam << 2) ticks, by their kind's speed (Streak) or by
// (0, d, d) of the lifting kind's (LiftStreak); `side` set: the puffs fade in, `length` & 0xf: the puffs' growth
struct StreakFields {
    explicit StreakFields(const EffectActor& actor) : StreakFields(const_cast<EffectActor&>(actor)) {}
    explicit StreakFields(EffectActor& actor) : speed(actor.params[0]), lifeParam(actor.params[1]), side(actor.params[2]), length(actor.params[3]), yaw(actor.posSpare), age(actor.trail[0]), life(actor.trail[1]) {}
    FieldAlias<int8_t, uint8_t> speed;
    FieldAlias<int8_t, uint8_t> lifeParam;
    uint8_t& side;
    uint8_t& length;
    int16_t& yaw;
    int16_t& age;
    int16_t& life;
};
// A streak's kind (exe data 0x88cbd0, 8-byte records by `speed`): its speed, its puffs' first size and their size's step
struct StreakKind : DataRecord {
    int16_t speed = 0, size = 0, sizeStep = 0;
    static StreakKind load(const TableReader& r) { return {{}, r.s16(0), r.s16(4), r.s16(6)}; }
};
// ... of a lifting streak (exe data 0x88cc00, 12-byte records): also its puffs' step along the lift and that step's step
struct LiftStreakKind : DataRecord {
    int16_t speed = 0, size = 0, sizeStep = 0, step = 0, stepStep = 0;
    static LiftStreakKind load(const TableReader& r) { return {{}, r.s16(0), r.s16(4), r.s16(6), r.s16(8), r.s16(0xa)}; }
};
const DataTable<RecordTable<StreakKind, 8, 8>> streakKinds{TableId::OpeningStreakKinds};
const DataTable<RecordTable<LiftStreakKind, 8, 0xc>> liftStreakKinds{TableId::OpeningLiftStreakKinds};
// the sprites' frames (exe data: the u offsets of the 4 frames) of the engine flame, the streaks, the lifting streaks
const DataTable<ScalarTable<uint32_t, 4>> engineFlameFrames{TableId::EngineFlameFrames};
const DataTable<ScalarTable<uint32_t, 4>> streakFrames{TableId::StreakFrames};
const DataTable<ScalarTable<uint32_t, 4>> liftStreakFrames{TableId::LiftStreakFrames};
// the puffs thrown from their owner (OwnerPuff, BonePuff, BurstPuff): the owner's slot, thrown (BonePuff), the age, the
// velocity, the growth (BurstPuff; its reader is the draw)
struct OwnerPuffFields : ActorClassState {
    explicit OwnerPuffFields(EffectActor& actor) : ownerSlot(actor.params[0]), thrown(actor.params[1]), age(actor.trail[0]) {}
    FieldAlias<int8_t, uint8_t> ownerSlot;
    uint8_t& thrown;
    int16_t& age;
    ShortVec3 velocity;
    ShortVec3 growth;

    void clearBlock() override {
        velocity = {};
        growth = {};
    }
};
// the puff thrown along its yaw (variant 10): its kind, its life >> 3, the age, the life, the velocity, the size
struct ThrownPuffFields : ActorClassState {
    explicit ThrownPuffFields(EffectActor& actor) : kind(actor.params[0]), lifeParam(actor.params[1]), yaw(actor.posSpare), age(actor.trail[0]), life(actor.trail[1]), size(actor.trail[5]) {}
    FieldAlias<int8_t, uint8_t> kind;
    FieldAlias<int8_t, uint8_t> lifeParam;
    int16_t& yaw;
    int16_t& age;
    int16_t& life;
    int16_t& size;
    ShortVec3 velocity;

    void clearBlock() override { velocity = {}; }
};

// exe 0x49dd10(point, param): a streak (Streak) at the point, of length param & 0xf, on side param >> 7 & 1
void spawnStreak(int16_t x, int16_t y, int16_t z, uint32_t param);
// exe 0x517e60(point, param): a streak (Streak, LiftStreak with flags & 0x40) at the point; length flags & 0xf, side
// flags >> 7, speed `speed`
void spawnFlagStreak(const int16_t point[3], uint8_t flags, uint8_t speed);
// the engine flame (EngineFlame) of `owner`, at its position and yaw, following it
void spawnEngineFlame(const Actor& owner);
// a thrown puff's slot (ThrownPuff; `flags` its slot's flags; nullptr when the pool is full) ...
EffectActor* allocThrownPuff(uint8_t flags);
// ... placed at the point along `yaw`, of `kind`, its life (lifeParam << 3) ticks (the kind and the life stored as the
// parameters' dword)
void placeThrownPuff(EffectActor& puff, const int16_t point[3], uint16_t yaw, uint8_t kind, uint8_t lifeParam);
// the flash ring's ticks (FlashRing: its sub-state counts them; projected onto the screen once, at its position)
constexpr uint8_t kFlashRingTicks = 8;
constexpr uint8_t kFlashRingNoTicks = 0; // the ring's sub-state before its first tick drawn
// the streak's flags byte that makes it a rising one
constexpr uint8_t kStreakRises = 0x40;

// exe 0x435960 (ST39 / ST3A effect type 0xf)
void openingEffectUpdate(const PlayerEnv& env, uint32_t actor);
// its draw (content/intro/opening_effects_draw.cpp; game/effects_draw ClassDraw, registered in effectDraws), the texture
// keys and the gradient row it uses
void openingEffectDraw(fx::DrawContext& c, const fx::State& st, uint32_t actor, uint32_t slot);
void openingEffectTextures(std::vector<fx::Texture>& out);
constexpr uint32_t kExhaustGlow = 0x802020; // the exhaust glow's disc: this colour at the centre, black at the rim

} // namespace od::port
