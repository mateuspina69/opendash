// A rising ring of light of the ruins (pool 0xa0 type 0x14 of ST26 / 27 / 34 / 40 / 41 / 42, exe 0x4382f0; hypothesis
// names). States [0x88d4e8]: 0 start 0x438310 (8 ticks), 1 rise 0x438340 (0x40 up a tick; its ticks out: freed); each
// tick a ring record (0x438370: its colour by the ticks left, an ellipse, radius 0x30).
#include "content/saul_kada/ruin_light_ring.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/draw_commands.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

enum class RingState : uint8_t { Start = 0, Rise = 1 };
// The ring's bytes: its ticks left
struct RingFields {
    explicit RingFields(const EffectActor& actor) : RingFields(const_cast<EffectActor&>(actor)) {}
    explicit RingFields(EffectActor& actor) : ticks(actor.trail[0]) {}
    int16_t& ticks;
};
constexpr int16_t kRingTicks = 8;
constexpr uint32_t kRingEllipse = 0x60u << 24;
constexpr uint32_t kGreyStep = 0x1014; // the colour of a tick left

// exe 0x438370(actor, ticks): its ring record
void emitRing(const EffectActor& actor, const RingFields& ring) {
    dc::Ring r;
    r.arc = dc::kRingFullArc | kRingEllipse;
    r.inner = dc::ringColour(uint32_t(int32_t(ring.ticks)) * kGreyStep);
    r.xy = actor.positionWord(0);
    r.outer = dc::kRingFadedRim;
    r.zStep = dc::zWord(actor.pos.z) | dc::ringStep(2);
    r.radii = dc::ringRadii(0, 0x30);
    dc::emit(r, actor.at);
}
void lightRing(EffectActor& actor) { // exe 0x4382f0: [0x88d4e8](actor, its ticks)
    RingFields ring(actor);
    switch (RingState(actor.state)) {
    case RingState::Start: // exe 0x438310
        ring.ticks = kRingTicks;
        emitRing(actor, ring);
        actor.state += 1u;
        return;
    case RingState::Rise: // exe 0x438340
        ring.ticks -= 1u;
        if (ring.ticks != 0) {
            actor.pos.y = int16_t(uint16_t(actor.pos.y) - 0x40u);
            emitRing(actor, ring);
            return;
        }
        freeActor(actor); // exe 0x5e1580
        return;
    default: notPortedAt(kStoryPort, "0x4382f0: state past [0x88d4e8] (the ribbon 0x4383d0's table follows)", actor.state);
    }
}

} // namespace

void lightRingUpdate(const PlayerEnv&, uint32_t actor) { lightRing(effectActorAt(actor)); }

} // namespace od::port
