// The spark rings (pool 0xa0 class 0x433750, stage effect type 0xd of ST0F, ST22, ST2E, ST41 / ST42, ST4D..ST4F): a
// ring of gouraud segments in its own plane that grows or shrinks toward a size and fades with its life (variants 0..5:
// one colour, a 4-colour cycle, linked twice, LINE_G2 spokes, two fixed gradients), drifting by its velocity. The
// turret's charge (game/charge_glow) and bullet (game/turret_bullet) spawn them. The draw is through dc::otQuadG4 /
// otLineG2 (render output); variant 3 steps the RNG per segment (game state).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port {

struct PlayerEnv;

// The rings' spawn parameters (the spawners write them all)
struct SparkRingParams : ActorClassState {
    explicit SparkRingParams(EffectActor& actor) : velocityX(actor.params[0]), velocityY(actor.params[1]), velocityZ(actor.params[2]), height(actor.params[3]), size(actor.posSpare) {}
    FieldAlias<int8_t, uint8_t> velocityX; // the drift per tick
    FieldAlias<int8_t, uint8_t> velocityY;
    FieldAlias<int8_t, uint8_t> velocityZ;
    FieldAlias<int8_t, uint8_t> height; // the outer points' height (a cone)
    int16_t& size; // the outer radius (from grow << 2)
    StateArray<uint8_t, 3> angles{}; // the plane's rotation (>> 4)
    uint8_t spin = 0; // added to the y angle each tick
    uint8_t life = 0;
    uint8_t grow = 0; // the first size (/ 4); then whether it shrinks
    uint8_t targetSize = 0; // the size it moves to (/ 4)
    uint8_t sizeStep = 0;
    StateArray<uint8_t, 3> colour{};
    uint8_t innerRatio = 0; // the inner radius / 32
    uint8_t gradient = 0; // the gradient's split / 16
    uint8_t rotationOrder = 0; // 0: Rz Rx Ry, else Ry Rx Rz
    uint8_t peak = 0; // the larger of the two sizes (the height's divisor)
    uint8_t ticksLeft = 0;
    void clearVelocityWord() {
        velocityX = 0;
        velocityY = 0;
        velocityZ = 0;
        height = 0;
    }
};
constexpr uint8_t kSparkRingsStageType = 0xd;

// exe 0x433750: the rings' update (ActorUpdateFn)
void sparkRingsUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
