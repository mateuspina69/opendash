// The Flutter's crash landing (ST3A, script 6: the end of the opening, before the town chapter): the scene's ships,
// clouds and falling pieces (content/intro/crash_landing.cpp), the script with its room case
// (content/intro/crash_landing_script.cpp) and its cutscene cast (content/intro/crash_landing_cast.cpp).
#pragma once

#include <cstdint>

#include "engine/data_source.hpp"
#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/script_state.hpp"
#include "game/state_array.hpp"

namespace od::port {

// ---- the state the storm's draw reads too (content/intro/crash_landing_draw) ----------------------------------
// the cloud rings (exe 0x598b00)
// The ring's parameters (the spawn's: its texture's CLUT is the entry's second one, its entry of the cloud texture table)
// and its block (+0x74): its angles (variants 0..3: one per ring of puffs) or scroll counters (variants 3..5); the
// spawn's parameter word is copied to the twin
struct CloudRingFields : ActorClassState {
    explicit CloudRingFields(StaticActor& actor) : altClut(actor.params[0]), textureEntry(actor.params[3]), paramsWord_(actor.params, 0) {}
    uint8_t& altClut;
    uint8_t& textureEntry;
    BytesField<uint32_t, 4> paramsWord_;
    StateArray<int16_t, 9> angles{};
    void copyParamsFrom(const CloudRingFields& o) { paramsWord_ = o.paramsWord_; }
};
// [0x8ce3b0]
enum class CloudRingState : uint8_t { Start = 0, Turn = 1 }; // exe 0x598b20 / [0x8ce3b8 + variant * 4]
// the variants' routines (exe data 0x8ce3b8: a routine per variant, the named routines of engine/routines.csv)
const DataTable<RoutineTable<8>> cloudRoutines{TableId::CrashLandingCloudRoutines};
// the flash (exe 0x431120)
// The flash's size (the spawn's parameters), its three brightness bytes (the draw's: shadeB the rings' green / blue,
// shadeA their red; shadeC has no reader traced) and its rays (variant 0)
struct CrashFlashFields : ActorClassState {
    explicit CrashFlashFields(EffectActor& actor) : size(actor.params, 0), shadeA(actor.stepCounter), shadeB(actor.params[2]), shadeC(actor.params[3]) {}
    BytesField<uint16_t, 4> size;
    uint8_t& shadeA;
    uint8_t& shadeB;
    uint8_t& shadeC;
    StateArray<uint8_t, 0x10> rays{};
};
// [0x88cad0]
enum class CrashFlashState : uint8_t { Start = 0, Rays = 1, Glow = 2 };
constexpr uint8_t kFlashTicks = 6;
// the falling piece (exe 0x5cbf00)
// The falling piece's tick counter (in its class block +0xbc..+0xdb, cleared at the spawn)
struct FallingPieceFields : ActorClassState {
    explicit FallingPieceFields(ProjectileActor& actor) : ticks(actor.hitBox.result) {}
    uint32_t& ticks;
};
enum class FallingPieceState : uint8_t { Spawn = 0, Fall = 1, Dive = 2 };
// the variants' routines (exe data 0x8d1644)
const DataTable<RoutineTable<4>> pieceRoutines{TableId::CrashLandingPieceRoutines};
// script 6
constexpr uint8_t kCrashScript = 6;
// The script's own bytes: the RNG state saved, the 2D backdrop (its scroll, its speeds, shown), the second file
// loaded, the player's walking speed, the alarm sounding, the ticks
struct CrashScriptBytes : ScriptPart<CrashScriptBytes> {
    uint32_t savedRng = 0; // +0xc
    int16_t backdropX = 0; // +0x10
    int16_t backdropY = 0; // +0x12
    int8_t backdropSpeedX = 0; // +0x14
    int8_t backdropSpeedY = 0; // +0x15
    uint8_t backdropShown = 0; // +0x16
    uint8_t secondFileLoaded = 0; // +0x17
    int16_t walkSpeed = 0; // +0x18
    uint8_t alarmOn = 0; // +0x1a
    int32_t ticks = 0; // +0x1c
    // exe 0x40dc40 over +0xc..+0x1b: the members there zeroed (past it: kept)
    void clearSetUpRun() override {
        savedRng = {};
        backdropX = {};
        backdropY = {};
        backdropSpeedX = {};
        backdropSpeedY = {};
        backdropShown = {};
        secondFileLoaded = {};
        walkSpeed = {};
        alarmOn = {};
    }
    // the walking speed set one byte at a time (the exe's two byte stores)
    void setWalkSpeedBytes(uint8_t low, uint8_t high) {
        walkSpeed = int16_t(uint16_t(low | high << 8));
    }
};
// the backdrop grid's draw origin from its scroll (the grid's corner kBackdropOrigin* left / above the scroll's cell)
constexpr uint16_t kBackdropOriginX = 0x160, kBackdropOriginY = 0x188;
inline int16_t backdropOriginX(uint16_t scrollX) { return int16_t(((scrollX >> 4) & 0x7fu) - kBackdropOriginX); }
inline int16_t backdropOriginY(uint16_t scrollY) { return int16_t(((scrollY >> 4) & 0xffu) - kBackdropOriginY); }
// the running script's own bytes as script 6's
inline CrashScriptBytes& crashBytes() { return scriptState().part<CrashScriptBytes>(); }
// the flying craft (exe 0x4bb750; exe 0x4faa20's jets share its layout): its class block -- the flags, the key the
// next animation starts at, the exhaust's clock
struct CraftFields : ActorClassState {
    explicit CraftFields(AnimatedActor&) {}
    uint8_t castFlags = 0;
    uint8_t animKey = 0;
    uint16_t exhaustClock = 0;

    void clearBlock() override {
        castFlags = 0;
        animKey = 0;
        exhaustClock = 0;
    }
};
// the craft's variants (exe table 0x8948a0: a routine per variant)
const DataTable<RoutineTable<5>> craftVariants{TableId::CrashLandingCraftVariants};
// +8 of the craft (the draw reads it too: the landing craft's Act2 is its landed state): bound, then its acts, each
// entered when its step sub comes
enum class CraftState : uint8_t { Bind = 0, Act1 = 1, Act2 = 2, Act3 = 3 };
// its two exhaust jets: at Ry Rx Rz(rotation) * (+-kExhaustSide, 0, kExhaustBack) from it
constexpr int kExhaustSide = 0x400, kExhaustBack = -0x1340;

// exe 0x595570 (ST3A static type 6): a ship in the sky (three kinds): placed high, lowered onto its height, then
// bobbing and firing pairs of shots while the script's step is young
void skyShipUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x598b00 (ST3A static type 0x12): a ring / layer of clouds (its angles and scroll counters; drawn by
// content/intro/crash_landing_draw)
void cloudRingUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5b39a0 (ST3A static type 0x46): drops to the ground, then drops a falling piece whenever the script asks
void piecesDropperUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5b3ae0 (ST3A static type 0x47): rides its parent's first bone (its matrix and point)
void boneRiderUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x431120 (ST3A effect type 7): a flash (with random rays and a sound in variant 0)
void crashFlashUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5cbf00 (ST3A pool 0x40 type 0x21): a falling piece (its speed integrated from its acceleration)
void fallingPieceUpdate(const PlayerEnv& env, uint32_t actor);

// script 6 (exe handler 0x516520): whether `handler` is it / its tick
bool crashLandingScriptPorted(RoutineId handler);
bool crashLandingScriptTick(const PlayerEnv& env, RoutineId handler);
// ST3A's room case (exe 0x587c00): room 0, the first time: script 6
bool crashLandingRoomCase();
bool crashLandingRoomCaseKnown(uint8_t stage); // ST3A

// The cutscene cast of script 6 (keyed on the script block's step sub and step tick)
void crashPropUpdate(const PlayerEnv& env, uint32_t actor);        // exe 0x48a2f0 (pool 0x20, variants 0x8923e8)
void crashPieceUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x59d100 (pool 0x60): the Flutter's pieces
void crashRollUpdate(const PlayerEnv& env, uint32_t actor);        // exe 0x4776e0 (type 0x16, variants 0x8909d8)
void crashBarrelUpdate(const PlayerEnv& env, uint32_t actor);     // exe 0x47cd40 (type 0x1a, variants 0x891110)
void crashSecondBodyUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x49a690 (type 0x2f, variants 0x8930a8)
void crashTalkerUpdate(const PlayerEnv& env, uint32_t actor);      // exe 0x4b0e20 (type 0x39)
void crashCraftUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x4bb750 (type 0x3f, variants 0x8948a0)
void crashJetUpdate(const PlayerEnv& env, uint32_t actor);         // exe 0x4faa20 (type 0x74, variants 0x897900)
void crashWatcherUpdate(const PlayerEnv& env, uint32_t actor);     // exe 0x480120 (type 0x1d)
void crashFaceUpdate(const PlayerEnv& env, uint32_t actor);        // exe 0x4823c0 (type 0x21, variants 0x891c38)
void crashSpeakerUpdate(const PlayerEnv& env, uint32_t actor);     // exe 0x48b530 (type 0x28, variants 0x892558)

} // namespace od::port
