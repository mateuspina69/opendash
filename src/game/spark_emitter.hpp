// The spark / smoke emitter (pool 0x60 class 0x592050, stage static type 3 sub 0 of most stages; also stage animated
// types): up to 14 particles that rise and drift from its point (or its parent's position / one of its parent's bones,
// following the parent's carry), one emitted every few ticks while its lifetime runs; freed once it stopped and no
// particle is left. Each live particle is a 0x2c sprite record (dc::emit).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

struct PlayerEnv;

constexpr uint8_t kSparkEmitterStageType = 3; // the stage static type of the emitter

// The emitter's spawn parameters (its spawners write them): the particles' drift per tick (x / z), the emits'
// lifetime (0: unlimited) and the parent it follows (the variants that emit at their parent)
struct SparkEmitterParams : ActorClassState {
    explicit SparkEmitterParams(StaticActor& actor) : driftX(actor.params[0]), driftZ(actor.params[1]), lifetime(actor.params, 2) {}
    FieldAlias<int8_t, uint8_t> driftX;
    FieldAlias<int8_t, uint8_t> driftZ;
    BytesField<int16_t, 4> lifetime;
    ActorRef<Actor> parent;

    void clearBlock() override {
        parent = nullptr;
    }
};

// exe 0x592050: the emitter's update (ActorUpdateFn)
void sparkEmitterUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
