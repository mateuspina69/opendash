// The gunships of the missions (ST15 static type 0x1b sub 0, exe 0x59d310) and their twin over Glyde's base (ST30 static
// type 0x1b sub 3, exe 0x5a07b0), content/nino/mission_gunships.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

constexpr uint8_t kMissionGunshipType = 0x1b; // its static type in the stage tables
// The spawn parameters a spawner writes (content/nino/field_spawners): the mode (& kGunshipPointMask: where its point
// comes from, kGunshipLeave: fly off), its count (bombs / reinforcements), its reinforcement row, its point
struct MissionGunshipParams {
    explicit MissionGunshipParams(const StaticActor& actor) : MissionGunshipParams(const_cast<StaticActor&>(actor)) {}
    explicit MissionGunshipParams(StaticActor& actor)
        : mode(actor.params[0]), count(actor.params[1]), reinforcementRow(actor.params[2]),
          pointX(actor.velocity.spare), pointZ(actor.acceleration.spare) {}
    uint8_t& mode;
    uint8_t& count;
    uint8_t& reinforcementRow;
    int16_t& pointX;
    int16_t& pointZ;
};
constexpr uint8_t kGunshipPointMask = 0x0f;   // 0: the player's point (the mission gunship follows the player)
constexpr uint8_t kGunshipFixedPoint = 1;     // a point of its own (a target, a landing)
constexpr uint8_t kGunshipShootsPlayer = 2;   // a point of its own, the cruise shooting at the player
constexpr uint8_t kGunshipLeave = 0x80;

// exe 0x59d310: the ST15 mission gunships (the tanks' targets): they circle a point (the player's), bomb, shoot bursts or
// land to drop reinforcements by their variant, veer off walls, fly off when told; shot down they crash
void missionGunshipUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5a07b0: the gunships of the wave over Glyde's base (ST30): the same with their own constants, entering at one of
// four points; the 8th one down drops Glyde's key (flag 0x20)
void glydeBaseGunshipUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
