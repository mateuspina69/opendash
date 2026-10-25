// The Shining Laser's state (game/shots/shining_laser.cpp; the effects' draw, game/effects_draw, draws the beam, the
// muzzle and the charge from it): the beam's block, the beam effect's fields and states.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/shots/shot_common.hpp"
#include "game/state_array.hpp"

namespace od::port {

constexpr uint32_t kLaserChargeSparks = 0x20;
// The beam's block (the shot's +0x74; the beam effect's +0x18 points at it): the stats (range: the beam's steps), the
// steps the beam reached, the effect's counter (charge ticks, then the glow's size, then the fade's ticks), the hum's
// ticks, the beam out (set by the effect), the muzzle (s16), the aim (pitch, yaw, 0) and the charge sparks' lengths and
// angles
struct LaserBlock : ActorClassState {
    explicit LaserBlock(ShotActor&) {}
    ShotStats stats;
    uint16_t drawnSteps = 0;
    uint16_t counter = 0;
    uint16_t humTicks = 0;
    uint16_t beamOut = 0;
    ShortVec3 muzzle;
    ShortVec3 aim;
    StateArray<uint8_t, kLaserChargeSparks> sparkLengths{};
    StateArray<uint8_t, kLaserChargeSparks> sparkAngles{};
};
// The beam effect's fields: the block it reads
struct LaserBeamFields : ActorClassState {
    explicit LaserBeamFields(EffectActor&) {}
    ActorRef<ShotActor> laserShot; // the laser's shot (the exe keeps the address of its block)
};
// [0x8c3fc4]
enum class BeamState : uint8_t {
    Start = 0,
    Charge = 1,
    Beam = 2,
    Fade = 3
}; // exe 0x55fa30 / 0x55fad0 / 0x55fb50 /
   // 0x55fbd0

} // namespace od::port
