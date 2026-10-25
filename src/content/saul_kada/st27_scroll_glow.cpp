// The ST27 scrolling glow (pool 0xa0, ST27 sub 2, story 14, exe 0x4391b0; hypothesis names): a glowing billboard whose
// texture scrolls 0x20 a tick, one 0x2c record a tick (its word 0 unwritten). States [0x88d540]: 0 start 0x4391d0,
// 1 glow 0x4391f0, 2 freed 0x4358c0.
#include "content/saul_kada/st27_scroll_glow.hpp"
#include "content/saul_kada/st27_scroll_glow_state.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/draw_commands.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st27_scroll_glow_state;

namespace {

enum class GlowState : uint8_t { Start = 0, Glow = 1, Free = 2 };

void scrollGlow(EffectActor& actor) {
    GlowFields& glow = classState<GlowFields>(actor);
    switch (GlowState(actor.state)) {
    case GlowState::Start: // 0x4391d0
        glow.scroll = 0;
        actor.loopFlags |= kActorLateLoop;
        actor.state = uint8_t(GlowState::Glow);
        actor.subState = 0;
        return;
    case GlowState::Glow: { // 0x4391f0
        const uint32_t u = uint8_t(glow.scroll + 0x20u);
        glow.scroll = uint8_t(u);
        dc::emit({0, 0x2e606060u, uint32_t(uint16_t(actor.pos.x)) | uint32_t(int32_t(actor.pos.y)) << 16,
                  u | 0x7cd24000u, uint32_t(uint16_t(actor.pos.z)), (u + 0x1f) | 0x2f4000u, 0x30u, u | 0x5f00u, 0u,
                  (u + 0x1f) | 0x5f00u},
                 actor.at, false);
        return;
    }
    case GlowState::Free: freeActor(actor); return; // 0x4358c0 -> exe 0x5e1580
    default: notPortedAt(kStoryPort, "0x4391b0 state (table 0x88d540)", actor.state);
    }
}

} // namespace

void scrollGlowUpdate(const PlayerEnv&, uint32_t actor) { scrollGlow(effectActorAt(actor)); }

} // namespace od::port
