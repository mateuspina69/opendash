// The class states of turret_bullet.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"
#include "game/turret_bullet.hpp"
#include "game/field_alias.hpp"

namespace od::port::turret_bullet_state {

// One trail entry (4 bytes): the step back to the previous billboard (s8 x / y / z, << 1) and its depth ramp
struct BulletTrailEntry {
    StateArray<int8_t, 3> step{};
    uint8_t depth = 0;
    uint32_t word() const {
        return uint32_t(uint8_t(step[0])) | uint32_t(uint8_t(step[1])) << 8 | uint32_t(uint8_t(step[2])) << 16 |
               uint32_t(depth) << 24;
    }
    void setWord(uint32_t w) {
        for (size_t i = 0; i < 3; ++i) step[i] = int8_t(uint8_t(w >> (8 * i)));
        depth = uint8_t(w >> 24);
    }
};
constexpr uint32_t kTrailEntries = 4;

// The bullet's fields: its parameters, its timer, its bits, its hit box and its trail
struct TurretBulletFields : ActorClassState, TurretBulletParams {
    explicit TurretBulletFields(ProjectileActor& actor) : TurretBulletParams(actor) {}
    uint16_t timer = 0;
    uint8_t bits = 0;       // kBullet*
    int8_t trailLength = 0;
    HitBoxHead hitBox;
    StateArray<BulletTrailEntry, kTrailEntries> trail{};

    // its block (the timer to the trail) cleared at the start, byte by byte
    void clearBlockBytes() {
        timer = 0;
        bits = 0;
        trailLength = 0;
        hitBox = {};
        trail = {};
    }
};

} // namespace od::port::turret_bullet_state
