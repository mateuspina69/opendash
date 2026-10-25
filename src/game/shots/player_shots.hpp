// The player's shot classes (pool 0x10, the global class table 0x8b1fb0: type = the weapon table's shot type, weapon w
// fires type w - 1; the buster and the town's kick by the buster's type) and the effects only they spawn, one file per
// weapon in game/shots/. The weapon names are hypotheses from the observed behaviour (the special weapons in id order).
//
//   type 0  exe 0x56bde0  the town's kick (buster type 1)              buster.cpp
//   type 1  exe 0x56bfa0  the buster shot                              buster.cpp
//   type 2  exe 0x56c6a0  weapon 3: the crusher (+ its energy orb)     crusher.cpp
//   type 3  exe 0x56cad0  weapon 4: the buster cannon (+ flash burst)  buster_cannon.cpp
//   type 4  exe 0x56d1b0  weapon 5: the hyper shell (+ its dark trail) hyper_shell.cpp
//   type 5  exe 0x56da70  weapon 6: the homing missile                 homing_missile.cpp
//   type 6  exe 0x56df60  weapon 7: the ground crawler                 ground_crawler.cpp
//   type 7  exe 0x56e630  weapon 8: the vacuum arm                     vacuum_arm.cpp (its effect: game/suction_effect)
//   type 8  exe 0x56e730  weapon 9: the reflector (+ spark spray)      reflector.cpp
//   (none)                weapon 10: the charge (marker, shock wave)   charge.cpp
//   type 10 exe 0x56ee10  weapon 11: the Blade Arm (+ its trail)       blade_arm.cpp
//   type 11 exe 0x56f410  weapon 12: the Shining Laser (+ effects)     shining_laser.cpp
//   type 12 exe 0x56f8e0  weapon 13: the machine gun (+ effects)       machine_gun.cpp
//   type 13 exe 0x56ff30  weapon 14: the spread buster (+ sparkle)     spread_buster.cpp
//   type 14 exe 0x5705a0  weapon 15: the extinguisher (+ its mist)     extinguisher.cpp
//   type 15 exe 0x570e50  weapon 16: the hunter seeker (+ crystal)     hunter_seeker.cpp
//   type 16 exe 0x571280  weapon 17: the drill arm (+ its cone)        drill_arm.cpp
// Weapon 10 (the weapon table's row 10: no shot type) has no shot class: its arm spawns its marker and its shock wave.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

void townKickUpdate(const PlayerEnv& env, uint32_t actor);          // exe 0x56bde0
void busterShotUpdate(const PlayerEnv& env, uint32_t actor);        // exe 0x56bfa0
void crusherShotUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x56c6a0
void energyOrbUpdate(const PlayerEnv& env, uint32_t actor);         // exe 0x567080 (static global type 0x10)
void busterCannonShotUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x56cad0
void flashBurstUpdate(const PlayerEnv& env, uint32_t actor);        // exe 0x564e90 (static global type 0xb)
void hyperShellShotUpdate(const PlayerEnv& env, uint32_t actor);    // exe 0x56d1b0
void darkTrailUpdate(const PlayerEnv& env, uint32_t actor);         // exe 0x563400 (static global type 5)
void homingMissileShotUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x56da70
void groundCrawlerShotUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x56df60
void vacuumArmShotUpdate(const PlayerEnv& env, uint32_t actor);     // exe 0x56e630
void reflectorShotUpdate(const PlayerEnv& env, uint32_t actor);     // exe 0x56e730
void sparkSprayUpdate(const PlayerEnv& env, uint32_t actor);        // exe 0x566d40 (static global type 0xf)
// the charge's static classes (GlobalStaticType ChargeMarker / ShockWave; the arm spawns them, game/player/player_weapons)
void chargeMarkerUpdate(const PlayerEnv& env, uint32_t actor);      // exe 0x566540 (static global type 0xe)
void shockWaveUpdate(const PlayerEnv& env, uint32_t actor);         // exe 0x56aae0 (static global type 0x17)
void bladeArmShotUpdate(const PlayerEnv& env, uint32_t actor);      // exe 0x56ee10
void bladeTrailUpdate(const PlayerEnv& env, uint32_t actor);        // exe 0x55e870 (effect type 5)
void shiningLaserShotUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x56f410
void shiningLaserBeamUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x55fa10 (effect type 8)
void laserWallSparkUpdate(const PlayerEnv& env, uint32_t actor);    // exe 0x560b70 (effect type 9)
// the RNG values the beam's muzzle drew its 4 spikes with on the effect's last tick (a presentation side channel, not
// game state: {angle & 0xfff, length} x 4; false = not drawn, the muzzle out of the depth range)
bool laserSpikes(uint32_t effect, uint16_t out[8]);
void machineGunShotUpdate(const PlayerEnv& env, uint32_t actor);   // exe 0x56f8e0
void shellCasingUpdate(const PlayerEnv& env, uint32_t actor);      // exe 0x568260 (static global type 0x11)
void machineGunMuzzleUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x560df0 (effect type 10)
void machineGunImpactUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x5616e0 (effect type 11)
void spreadBusterShotUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x56ff30
void sparkleUpdate(const PlayerEnv& env, uint32_t actor);          // exe 0x56a110 (static global type 0x15)
void extinguisherShotUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x5705a0
void extinguisherMistUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x56a690 (static type 0x16)
void hunterSeekerShotUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x570e50
void crystalShotUpdate(const PlayerEnv& env, uint32_t actor);      // exe 0x56b150 (static global type 0x18)
void drillArmShotUpdate(const PlayerEnv& env, uint32_t actor);     // exe 0x571280
void drillConeUpdate(const PlayerEnv& env, uint32_t actor);        // exe 0x56b970 (static global type 0x19)

} // namespace od::port
