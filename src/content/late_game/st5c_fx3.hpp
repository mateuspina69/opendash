// ST5C: three small effect classes spawned by the boss pieces of content/late_game/st5c_fx2: the beam's impact (static
// type 0x61, exe 0x5bf360), the explosion's shock rings (pool 0xa0 type 2, exe 0x42e360) and the explosion's electric
// sparks (pool 0xe0 type 6, exe 0x418660). See content/late_game/st5c_fx3.cpp.
#pragma once

#include <cstdint>

#include "game/loaded_data.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/vectors.hpp"

namespace od::port {

constexpr uint8_t kBeamImpactType = 0x61, kShockRingType = 2, kElectricSparkType = 6;

// The shock ring's outer radius >> 4 (the step counter's byte; the explosion keeps its spawner's up to date, 0 ends
// the rings)
struct ShockRingParams {
    explicit ShockRingParams(const EffectActor& actor) : ShockRingParams(const_cast<EffectActor&>(actor)) {}
    explicit ShockRingParams(EffectActor& actor) : outerRadius(actor.stepCounter) {}
    uint8_t& outerRadius;
};
// The electric spark's state: its spawn bytes (the radius it follows -- the explosion's -- and its point), its angle,
// length, timer and depth
struct ElectricSparkFields : ActorClassState {
    explicit ElectricSparkFields(ControllerActor&) {}
    const uint16_t* radius = nullptr;
    ShortVec3 point;
    uint16_t angle = 0;
    uint16_t length = 0;
    uint16_t timer = 0;
    int16_t depth = 0;
    // the radius's address is the exe's parameter word (the explosion's field in its slot)
    HeadPointer headPointer() const override { return HeadPointer::toField(kHeadParamsWord, radius); }
    void clearHeadPointer() override { radius = nullptr; }
};

// exe 0x5bf360: the beam's impact -- three screen rings for 4 ticks
void beamImpactUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x42e360: the frame-buffer shock rings and their spawner
void screenShockRingUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x418660: an electric spark of the explosion (a zigzag of POLY_G4 for 8 ticks)
void electricSparkUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
