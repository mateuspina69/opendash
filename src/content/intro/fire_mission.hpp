// The opening's fire mission on the Flutter (ST1E, after ST39's script 5, before the crash landing): fires to put out
// with the extinguisher, a wall that bursts into flames, Data running around the kitchen on fire. The mission's state
// shared by its classes (content/intro/flutter_fire.cpp: the fires, their flames and effects;
// content/intro/burning_data.cpp: Data), its rooms' script (content/intro/fire_mission_rooms.cpp: the fires spawned,
// the timers, the hints) and its result (content/intro/mission_result.cpp: script 0xd), and their entry points.
#pragma once

#include <cstdint>

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/effects_draw.hpp"
#include "game/stage_state.hpp"
#include "game/state_array.hpp"

namespace od::port {

// The mission's block (the stage block's part, game/stage_state): the heat, the fires left and put out, the wall's cue,
// Data's flags, the rooms' timers, the hint's ticks
struct FireMissionState : StagePart<FireMissionState> {
    uint32_t heat = 0;           // +0x00: the heat the player feels this frame (the shimmer's strength)
    uint8_t firesLeft = 0;       // +0x04: the fires still burning (Data on fire counts as one)
    uint8_t firesOut = 0;        // +0x05: the fires put out
    uint16_t specialFireOut = 0; // +0x06: the fire in front of the wall is out (the wall blast's cue)
    uint16_t dataFlags = 0;      // +0x08: kDataPanicking
    StateArray<uint16_t, 3> roomTimers{}; // +0x0a: the ticks spent in rooms 0 / 1 / 2 (each room's time limit)
    uint16_t hintTicks = 0;      // +0x10: the ticks since the last hint

    // the block 0xa6d298..0xa6d2d7 cleared one byte at a time (exe 0x40dc40)
    static void clearBlock();
    // Data starts panicking (the exe's byte read-modify-write)
    void setDataPanicking() { dataFlags = uint16_t(dataFlags | kDataPanicking); }
    bool dataPanicking() const { return (dataFlags & kDataPanicking) != 0; }

    static constexpr uint8_t kDataPanicking = 0x01;
};
inline FireMissionState& fireMission() { return stageBlock().part<FireMissionState>(); }
inline void FireMissionState::clearBlock() { stageBlock().clearAll(); }
// The exe's pointers to the mission's blocks, rewritten every tick (the room handlers' block 0xa5a6c4 -- ST1E points
// it at the mission's --, the mission's and Data's 0xa59e7c / 0xa59e80, the result scene's Data block 0xa59e84):
// values the exe keeps (no reader in the port)
struct FireMissionPointers {
    uint32_t roomBlock = 0;
    uint32_t missionBlock = 0;
    uint32_t dataBlock = 0;
    uint32_t sceneDataBlock = 0;
};
inline FireMissionPointers& fireMissionPointers() {
    static FireMissionPointers s;
    return s;
}
// the mission block's exe address (the value the pointers hold)
constexpr uint32_t kFireMissionBlock = 0xa6d298;

// the mission's flags: its success (all the fires put out in time) and its bonus
constexpr uint32_t kMissionSuccessFlag = 0x129;
constexpr uint32_t kMissionBonusFlag = 0x12a;

// The stage's fire effect (effect type 0x12, exe 0x437520), the effect every class of the mission spawns: its kind
// (sub-type) and the kind's parameters, its position, its vertical speed, its hit box, its keyframed sprite (ticks,
// size, key, spin)
enum class FireEffectKind : uint8_t {
    Shimmer = 0,    // the heat shimmer (the screen tint by the mission's heat)
    SmokePuff = 1,  // a puff of smoke rising
    BlastEmber = 2, // an ember swinging out of the wall blast (hurts)
    BlastFlash = 3, // the wall blast's screen flash
    Debris = 4,     // a piece thrown up by the wall blast
    Ember = 5,      // an ember thrown from a fire toward the camera side (hurts)
    DataFlame = 6,  // the flame on the burning Data's head
};
constexpr uint8_t kFireEffectType = 0x12;
struct FireEffectFields : ActorClassState {
    explicit FireEffectFields(EffectActor& actor)
        : pos(actor.pos), riseSpeed(actor.posSpare), keyTicks(actor.trail[4]), size(actor.trail[5]),
          key(actor.trail[6], 0), spin(actor.trail[6], 1), trail_(actor.trail) {}
    ShortVec3& pos;
    int16_t& riseSpeed;
    HitBoxHead hitBox;                        // over the trail's first four shorts in the exe
    FieldAlias<uint16_t, int16_t> keyTicks;
    int16_t& size;
    PartField<uint8_t, int16_t> key;
    PartField<uint8_t, int16_t> spin;         // the sprite's rotation >> 4

    // the sprite / hit block cleared
    void clearSprite() {
        hitBox = {};
        trail_ = {};
    }

private:
    StateArray<int16_t, 8>& trail_;
};
// the parameters of a blast ember (kind 2): its direction (a byte, << 4 the yaw), its damage, its planar step
struct BlastEmberParams {
    explicit BlastEmberParams(const EffectActor& actor) : BlastEmberParams(const_cast<EffectActor&>(actor)) {}
    explicit BlastEmberParams(EffectActor& actor) : direction(actor.params[0]), attack(actor.params[1]), reach(actor.params[2]) {}
    FieldAlias<int8_t, uint8_t> direction;
    uint8_t& attack;
    FieldAlias<int8_t, uint8_t> reach;
};
// ... of a debris piece (kind 4): its direction, its age, its planar step
struct DebrisParams {
    explicit DebrisParams(const EffectActor& actor) : DebrisParams(const_cast<EffectActor&>(actor)) {}
    explicit DebrisParams(EffectActor& actor) : direction(actor.params[0]), age(actor.params[1]), reach(actor.params[2]) {}
    FieldAlias<int8_t, uint8_t> direction;
    uint8_t& age;
    FieldAlias<int8_t, uint8_t> reach;
};
// ... of the blast's flash (kind 3): its brightness
struct BlastFlashParams {
    explicit BlastFlashParams(const EffectActor& actor) : BlastFlashParams(const_cast<EffectActor&>(actor)) {}
    explicit BlastFlashParams(EffectActor& actor) : level(actor.params[0]) {}
    uint8_t& level;
};
// ... of an ember (kind 5): its yaw, its age
struct EmberParams {
    explicit EmberParams(const EffectActor& actor) : EmberParams(const_cast<EffectActor&>(actor)) {}
    explicit EmberParams(EffectActor& actor) : yaw(actor.params, 0), age(actor.params[2]) {}
    BytesField<uint16_t, 4> yaw;
    uint8_t& age;
};
// ... of Data's flame (kind 6): its size (the burning Data's hit points >> 6)
struct DataFlameParams {
    explicit DataFlameParams(const EffectActor& actor) : DataFlameParams(const_cast<EffectActor&>(actor)) {}
    explicit DataFlameParams(EffectActor& actor) : size(actor.params, 0) {}
    BytesField<uint16_t, 4> size;
};

// ---- the classes' state the draw reads too (content/intro/fire_mission_draw.cpp) ------------------------------
// the flame tongues (exe 0x5b0f20)
constexpr uint32_t kTongues = 0x12;
// One tongue: its rise, its sideways offset, its height, its frame (kFreeTongue: free), its spin
struct FlameTongue {
    int16_t rise = 0;
    int8_t dx = 0;
    int8_t dz = 0;
    int16_t height = 0;
    uint8_t frame = 0;
    uint8_t spin = 0;
};
constexpr uint8_t kFreeTongue = 0xff;
// The flame's fields: stopped (set by its fire), its emit timer, its flicker phase, the tongues, the flicker's timer
struct FlameFields : ActorClassState {
    explicit FlameFields(StaticActor& actor)
        : stopped(actor.params[0]), emitTimer(actor.params[1]), flickerPhase(actor.params[2]) {}
    uint8_t& stopped;
    FieldAlias<int8_t, uint8_t> emitTimer;
    uint8_t& flickerPhase;
    StateArray<FlameTongue, kTongues> tongues{};
    uint8_t flickerTimer = 0;
    StateArray<uint8_t, 3> afterFlicker{}; // the three bytes after it (set with the tongues)

    // the tongues, the flicker timer and the bytes after it set to 0xff (every tongue free)
    void freeTongues() {
        for (FlameTongue& t : tongues) {
            t.rise = -1;
            t.dx = -1;
            t.dz = -1;
            t.height = -1;
            t.frame = 0xff;
            t.spin = 0xff;
        }
        flickerTimer = 0xff;
        afterFlicker = {0xff, 0xff, 0xff};
    }
};
// [0x8cfa38]
enum class FlameState : uint8_t { Start = 0, Emit = 1, Die = 2 }; // exe 0x5b0f40 / 0x5b0fa0 / 0x5b0ff0
// a variant's tongue frames (exe data 0x8cfa68: 12-byte records by variant * 0x40 + frame: the sprite's colour word
// (bit 25: semi-transparent), its uv rectangle, +9 next (-1: done), +0xa the height's step, +0xb the spin's step)
struct TongueFrame : DataRecord {
    uint32_t colour = 0;
    uint8_t u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    int8_t next = 0, heightStep = 0;
    uint8_t spinStep = 0;
    static TongueFrame load(const TableReader& r) {
        return {{}, r.u32(0), r.u8(4), r.u8(5), r.u8(6), r.u8(7), r.s8(9), r.s8(0xa), r.u8(0xb)};
    }
};
constexpr uint32_t kTongueFramesPerVariant = 0x40;
const DataTable<RecordTable<TongueFrame, 0x100, 0xc>> tongueFrames{TableId::FireTongueFrames};
// a fire (exe 0x5ae490) and the wall blast (exe 0x5aed50)
// What a burning thing keeps (both classes; cleared from +0x74 at the spawn): its hit box, its strength and its full
// strength, its sprite's frame and ticks, a timer, the flicker flame spawned while it is sprayed, its flame (later its
// smoke column), the flicker's ticks, the blast's swing counter, the ember timer, the crackle timer
struct BurningFields : ActorClassState {
    explicit BurningFields(StaticActor&) {}
    HitBoxHead hitBox;
    int32_t strength = 0;
    int32_t fullStrength = 0;
    uint8_t frame = 0;
    uint8_t frameTicks = 0;
    uint16_t timer = 0;
    uint32_t flicker = 0;
    uint32_t flame = 0;        // the flame tongues, then the smoke column
    uint16_t flickerTicks = 0;
    uint16_t swing = 0;
    uint16_t emberTimer = 0;
    uint8_t crackleTimer = 0;

    // the block cleared (the spawn): its members, and the slot's model fields the clear runs over (from the model
    // word's last byte to the semi-transparency: the fire binds no model)
    void clear(StaticActor& slot) {
        slot.model &= kModelWordKeptBytes;
        slot.extraGeometry = 0;
        slot.collisionMesh = {};
        slot.colourTables = {};
        slot.cullWidth = 0;
        slot.cullHeight = 0;
        slot.scale = {};
        slot.tint = 0;
        slot.scaleBytes = {};
        slot.semiTransparency = 0;
        hitBox = {};
        strength = 0;
        fullStrength = 0;
        frame = 0;
        frameTicks = 0;
        timer = 0;
        flicker = 0;
        flame = 0;
        flickerTicks = 0;
        swing = 0;
        emberTimer = 0;
        crackleTimer = 0;
    }
    static constexpr uint32_t kModelWordKeptBytes = 0x00ffffff; // the model word's bytes before the clear's start
};
// [0x8cf730] / [0x8cf960]
enum class FireState : uint8_t {
    Spawn = 0,
    Burn = 1,
    PutOut = 2
}; // exe 0x5ae4e0 / 0x5ae5d0 / 0x5ae8d0 (the wall blast:
   // 0x5aeda0 / 0x5aef10 / 0x5af1e0)
// a size's frames (exe data 0x8cf760: 8-byte records by variant * 0x10 + frame: the sprite's uv corner and size, +4
// ticks, +5 next (-1: back to 0))
struct FireFrame : DataRecord {
    uint8_t u0 = 0, v0 = 0, width = 0, height = 0, ticks = 0;
    int8_t next = 0;
    static FireFrame load(const TableReader& r) { return {{}, r.u8(0), r.u8(1), r.u8(2), r.u8(3), r.u8(4), r.s8(5)}; }
};
constexpr uint32_t kFireFramesPerSize = 0x10;
const DataTable<RecordTable<FireFrame, 0x40, 8>> fireFrames{TableId::FireFrames};
// the fires' sizes (exe data 0x8cf740, 8-byte records): the sprite's texture page and CLUT, the full strength
struct FireSize : DataRecord {
    uint16_t tpage = 0, clut = 0, strength = 0;
    static FireSize load(const TableReader& r) { return {{}, r.u16(0), r.u16(2), r.u16(4)}; }
};
const DataTable<RecordTable<FireSize, 4, 8>> fireSizes{TableId::FireSizes};
// the mission's effects (exe 0x437520)
// [[0x88cc94 + kind * 4] + state * 4]
enum class FireEffectState : uint8_t { Start = 0, Run = 1, Done = 2 }; // Done: exe 0x4358c0 (freed)
// One key of a sprite's script (exe data, 12 bytes: the sprite's colour word (bit 25: semi-transparent), its uv
// rectangle, +8 ticks, +9 next (-1: done, -0x80: loop), +0xa the size's step, +0xb the spin's step); 16 keys per variant
struct SpriteKey : DataRecord {
    uint32_t colour = 0;
    uint8_t u0 = 0, v0 = 0, u1 = 0, v1 = 0, ticks = 0;
    int8_t next = 0, sizeStep = 0;
    uint8_t spinStep = 0;
    static SpriteKey load(const TableReader& r) {
        return {{}, r.u32(0), r.u8(4), r.u8(5), r.u8(6), r.u8(7), r.u8(8), r.s8(9), r.s8(0xa), r.u8(0xb)};
    }
};
constexpr uint32_t kKeysPerScript = 16, kKeyScriptStride = 0xc0, kSpriteVariants = 8;
constexpr int8_t kKeyDone = -1, kKeyLoop = -0x80;
// a variant's key script: its 16 keys
class KeyScript {
public:
    explicit KeyScript(const SpriteKey* first) : first_(first) {}
    const SpriteKey& operator[](size_t k) const {
        if (k >= kKeysPerScript) tableIndexError(k, kKeysPerScript);
        return first_[k];
    }

private:
    const SpriteKey* first_;
};
// a kind's key scripts (exe data, by variant)
template <size_t Variants> struct KeyScripts : DataRecord {
    static constexpr uint32_t kBytes = Variants * kKeyScriptStride;
    RecordTable<SpriteKey, Variants * kKeysPerScript, 0xc> keys;
    KeyScript operator[](size_t variant) const {
        if (variant >= Variants) tableIndexError(variant, Variants);
        return KeyScript(&keys.records[variant * kKeysPerScript]);
    }
    static constexpr size_t size() { return Variants; }
    static KeyScripts load(const TableReader& r) { return {{}, decltype(keys)::load(r)}; }
};
// each kind's key scripts (by variant)
const DataTable<KeyScripts<5>> smokeKeys{TableId::FireSmokeKeys};
const DataTable<KeyScripts<1>> blastEmberKeys{TableId::FireBlastEmberKeys}, emberKeys{TableId::FireEmberKeys};
const DataTable<KeyScripts<3>> debrisKeys{TableId::FireDebrisKeys};
// each kind's sprite styles (exe data, 8-byte records by variant): the texture page, the CLUT, semi-transparent (bit 0)
struct SpriteStyle : DataRecord {
    uint16_t tpage = 0, clut = 0, semi = 0;
    static SpriteStyle load(const TableReader& r) { return {{}, r.u16(0), r.u16(2), r.u16(4)}; }
};
using SpriteStyles = RecordTable<SpriteStyle, kSpriteVariants, 8>;
const DataTable<SpriteStyles> smokeStyles{TableId::FireSmokeStyles}, blastEmberStyles{TableId::FireBlastEmberStyles},
    debrisStyles{TableId::FireDebrisStyles}, emberStyles{TableId::FireEmberStyles};
// the flame tongues' sprite styles (exe data 0x8cfa48, by variant)
const DataTable<SpriteStyles> tongueStyles{TableId::FireTongueStyles};

// exe 0x5ae490 (ST1E static type 0x36): a fire to put out (its flames, embers, crackle and heat)
void fireUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5b0f20 (ST1E static type 0x3e): the flame tongues of a fire
void flameUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5aed50 (ST1E static type 0x36 sub 1): the fire bursting out of the wall
void wallBlastUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x437520 (ST1E effect type 0x12): the mission's effects (FireEffectKind)
void fireEffectUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4e8530 (ST1E pool 0x20 type 0x63): Data in the kitchen, catching fire and running in panic
void burningDataUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4ea1d0 (ST1E pool 0x20 type 0x63 sub 1): Data in the mission's result scene
void resultSceneDataUpdate(const PlayerEnv& env, uint32_t actor);

// The classes' draws (content/intro/fire_mission_draw.cpp; game/effects_draw ClassDraw, registered in effectDraws): the
// fire's sprite, the flame tongues', the mission's effects' keyframed sprites; and the texture keys they sample
void fireDraw(fx::DrawContext& c, const fx::State& st, uint32_t actor, uint32_t slot);
void flameDraw(fx::DrawContext& c, const fx::State& st, uint32_t actor, uint32_t slot);
void fireEffectDraw(fx::DrawContext& c, const fx::State& st, uint32_t actor, uint32_t slot);
void fireMissionTextures(std::vector<fx::Texture>& out);

// ST1E's room case (exe 0x580710): the room's handler (the fires, the timers, the hints; the result script 0xd)
bool fireMissionRoomCase();
bool fireMissionRoomCaseKnown(uint8_t stage); // ST1E
// script 0xd (exe handler 0x519df0): the mission's result -- whether `handler` is it / its tick
bool missionResultScriptPorted(RoutineId handler);
bool missionResultScriptTick(const PlayerEnv& env, RoutineId handler);

} // namespace od::port
