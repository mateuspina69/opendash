// The type bytes of the exe's global class tables (an actor slot's +4): the shots' table (pool 0x10, and pool 0x40
// when its +3 & kActorStageClass is clear: GlobalShotType), the effects' (pool 0xa0: GlobalEffectType) and the
// statics' (pool 0x60: GlobalStaticType), one enumerator per class the game spawns from game/ or content. A slot with
// kActorStageClass set -- and every slot of the pools without a global table (0x20 animated, 0xe0 controllers) --
// takes its class from the current stage's table, where the same number means a different class in each stage: those
// types stay per-class constants in the file of their class (e.g. game/footstep_dust kFootstepDustType, game/lens_flare
// kLensFlareType, game/enemy_shot kEnemyShotType, game/wall_crumble kWallCrumbleType).
#pragma once

#include <cstdint>

namespace od::port {

// pool 0x10 (and pool 0x40 without kActorStageClass), global table (exe 0x8b1fb0): the player's shots by weapon (the
// weapon record's shot type) and the copies the shots spawn of themselves
enum class GlobalShotType : uint8_t {
    TownKick = 0,       // exe 0x56bde0 (game/shots/buster)
    BusterShot = 1,     // exe 0x56bfa0 (game/shots/buster)
    Crusher = 2,        // exe 0x56c6a0 (game/shots/crusher)
    BusterCannon = 3,   // exe 0x56cad0 (game/shots/buster_cannon)
    HyperShell = 4,     // exe 0x56d1b0 (game/shots/hyper_shell)
    HomingMissile = 5,  // exe 0x56da70 (game/shots/homing_missile)
    GroundCrawler = 6,  // exe 0x56df60 (game/shots/ground_crawler)
    VacuumArm = 7,      // exe 0x56e630 (game/shots/vacuum_arm)
    Reflector = 8,      // exe 0x56e730 (game/shots/reflector)
    BladeArm = 0xa,     // exe 0x56ee10 (game/shots/blade_arm)
    ShiningLaser = 0xb, // exe 0x56f410 (game/shots/shining_laser)
    MachineGun = 0xc,   // exe 0x56f8e0 (game/shots/machine_gun)
    SpreadBuster = 0xd, // exe 0x56ff30 (game/shots/spread_buster: its copies)
    Extinguisher = 0xe, // exe 0x5705a0 (game/shots/extinguisher: its partner stream)
    HunterSeeker = 0xf, // exe 0x570e50 (game/shots/hunter_seeker)
    DrillArm = 0x10,    // exe 0x571280 (game/shots/drill_arm)
};

// pool 0xa0, global table
enum class GlobalEffectType : uint8_t {
    HitSpark = 0,         // exe 0x55d210 (game/hit_effects)
    Burst = 1,            // exe 0x55d3c0 (game/hit_effects)
    ClutAnimation = 2,    // exe 0x55da50 (game/clut_animation)
    SkidSparks = 3,       // exe 0x55dec0 (game/skid_sparks)
    Suction = 4,          // exe 0x55df40 (game/suction_effect)
    BladeTrail = 5,       // exe 0x55e870 (game/shots/blade_arm)
    ShieldRing = 6,       // the shield's ring where it stops a shot (game/player/player_weapons)
    DustPuff = 7,         // exe 0x55f850 (game/dust_puff)
    LaserBeam = 8,        // exe 0x55fa10 (game/shots/shining_laser)
    LaserWallSpark = 9,   // exe 0x560b70 (game/shots/shining_laser)
    MuzzleFlash = 0xa,    // exe 0x560df0 (game/shots/machine_gun)
    MachineGunImpact = 0xb, // exe 0x5616e0 (game/shots/machine_gun)
};

// pool 0x60, global table
enum class GlobalStaticType : uint8_t {
    Pickup = 0,           // exe 0x562240 (game/pickups)
    PlacedModel = 4,      // exe 0x563180 (game/placed_models)
    DarkTrail = 5,        // exe 0x563400 (game/shots/hyper_shell)
    MissileTrail = 6,     // exe 0x5636b0 (game/missile_trail)
    Blast = 7,            // exe 0x563b10 (game/explosions)
    Explosion = 8,        // exe 0x564370 (game/explosions)
    Throwable = 9,        // exe 0x564690 (game/throwable)
    TurningModel = 0xa,   // exe 0x564d60 (game/placed_models)
    FlashBurst = 0xb,     // exe 0x564e90 (game/shots/buster_cannon)
    WallDebris = 0xc,     // exe 0x565af0 (game/wall_debris)
    HurtSparks = 0xd,     // exe 0x566130 (game/hit_effects)
    ChargeMarker = 0xe,   // exe 0x566540 (game/shots/charge)
    SparkSpray = 0xf,     // exe 0x566d40 (game/shots/reflector)
    EnergyOrb = 0x10,     // exe 0x567080 (game/shots/crusher)
    ShellCasing = 0x11,   // exe 0x568260 (game/shots/machine_gun)
    DrainBurnMarker = 0x12, // exe 0x5684a0 (game/status_markers)
    SlowMarker = 0x13,    // exe 0x568b40 (game/status_markers)
    SmokeColumn = 0x14,   // a fire put out's smoke, the orbit status's marker (game/smoke_column)
    Sparkle = 0x15,       // exe 0x56a110 (game/shots/spread_buster)
    ExtinguisherMist = 0x16, // exe 0x56a690 (game/shots/extinguisher)
    ShockWave = 0x17,     // exe 0x56aae0 (game/shots/charge)
    Crystal = 0x18,       // exe 0x56b150 (game/shots/hunter_seeker)
    DrillCone = 0x19,     // exe 0x56b970 (game/shots/drill_arm)
};

// the type byte a slot stores
constexpr uint8_t typeByte(GlobalShotType t) { return uint8_t(t); }
constexpr uint8_t typeByte(GlobalEffectType t) { return uint8_t(t); }
constexpr uint8_t typeByte(GlobalStaticType t) { return uint8_t(t); }

} // namespace od::port
