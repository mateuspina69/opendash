// The class state of line_of_sight.cpp's probe: the fields it keeps in its slot (game/actor_state ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"

namespace od::port::line_of_sight_state {

// The probe's fields (the controller slot past its type)
struct LineOfSightFields : ActorClassState {
    explicit LineOfSightFields(ControllerActor& actor)
        : boxHalf(actor.params, 0), stepsPerTick(actor.params[2]), ownerSlot(actor.params[3]), point(actor.recordPoint),
          collisionMask(actor.recordPoint.spare) {}
    uint8_t* result = nullptr;          // the owner's result byte (the exe keeps its address over the state bytes: the
                                        // class has none)
    BytesField<int16_t, 4> boxHalf;     // the box's half-size
    FieldAlias<int8_t, uint8_t> stepsPerTick;
    FieldAlias<int8_t, uint8_t> ownerSlot;
    ShortVec4& point;                   // where the walk is (the start, then each tick's end)
    int16_t& collisionMask;             // the point's spare lane: the collision layers
    uint32_t fixedTargetXY = 0;         // a fixed target (variant != 0): its x | y << 16, z | spare << 16
    uint32_t fixedTargetZ = 0;
    PartField<uint8_t, uint32_t> targetSlot{fixedTargetZ, 0}; // the target actor (variant 0): its slot and its bone
    PartField<uint8_t, uint32_t> targetBone{fixedTargetZ, 1};
    PartField<int16_t, uint32_t> stepLength{fixedTargetZ, 2};
    // the result byte's address is the exe's word over the state bytes (a field of the owner's slot)
    HeadPointer headPointer() const override { return HeadPointer::toField(kHeadStateWord, result); }
    void clearHeadPointer() override { result = nullptr; }
};

// The probe's globals (exe 0xa59d38..): its collision box, the target point and the step toward it
struct LineOfSightState {
    BoxExtents box{}; // 0xa59d38: -h, h on the three axes
    ShortVec4 target; // 0xa59d48
    StateArray<int32_t, 3> step{}; // 0xa59d50: (16.16; the exe also stores the fourth, uninitialised, word of its stack
                                         // vector at 0xa59d5c: not written here)
};
inline LineOfSightState& sight() {
    static LineOfSightState s;
    return s;
}

} // namespace od::port::line_of_sight_state
