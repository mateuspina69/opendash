// The glow effects (pool 0xa0 class 0x4309b0, stage effect type 6 of many stages: ST0F, ST12..ST14, ST16 / ST17 / ST19,
// ST21, ST26 / ST28, ST2F, ST33..ST38, ST40..ST43, ST48, ST4D..ST51, ST55): a glow that rides an actor (an offset on
// one of its bones, or on its position turned by its rotation) and is freed with it. Variant 0 a glow ring growing,
// holding and shrinking (the ST19 gate lights), 1 a trail (the guardian's: its screen points kept), 2 a fading glow
// (ST41 / ST42). The draws are render output.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port {

struct PlayerEnv;

// The glow's fields (the variants keep different things in the effect's trail words): its draw size and mode (the
// header's step bytes), the actor it rides (kept in its parameters), the parent's bone, and variant 1's last four
// screen points (x | y << 16) / variants 0 and 2's size and phase ticks
struct GlowTrailFields : ActorClassState {
    explicit GlowTrailFields(EffectActor& actor)
        : size(actor.step), mode(actor.stepCounter), bone(actor.posSpare), ringSize(actor.trail[0], 0),
          ringTicks(actor.trail[0], 1), trail_(actor.trail) {}
    uint8_t& size;                  // the trail's width (its draw)
    uint8_t& mode;                  // the draw's mode (the parent sets it)
    ActorRef<ActorHeader> parent;   // the actor it rides (null: its position is a world point; else its position is
                                    // the offset on the bone / turned by the parent's rotation); the exe keeps it in
                                    // the parameters
    FieldAlias<uint16_t, int16_t> bone; // the parent's bone it rides (an animated parent)
    PartField<uint8_t, int16_t> ringSize;  // variants 0 / 2: the glow's size ...
    PartField<uint8_t, int16_t> ringTicks; // ... and its phase's ticks

    // variant 1: screen point k of the history (x | y << 16)
    ArrayField<uint32_t, int16_t, 8> screenPoint(size_t k) { return {trail_, k * 4}; }
    // the parent is the exe's parameter word (its identity word)
    HeadPointer headPointer() const override { return HeadPointer::toSlot(kHeadParamsWord, parent ? parent->at : 0); }
    void clearHeadPointer() override { parent = nullptr; }
    // the history shifted by one (the oldest dropped) and the newest screen point stored (the exe's two short stores)
    void pushScreenPoint(int16_t sx, int16_t sy) {
        for (size_t k = 3; k > 0; --k) screenPoint(k) = uint32_t(screenPoint(k - 1));
        trail_[0] = sx;
        trail_[1] = sy;
    }

private:
    StateArray<int16_t, 8>& trail_;
};
constexpr uint8_t kGlowTrailStageType = 6;
enum class GlowTrailVariant : uint8_t { Ring = 0, Trail = 1, Fade = 2 };

// exe 0x4309b0: the glow's update (ActorUpdateFn)
void glowTrailUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
