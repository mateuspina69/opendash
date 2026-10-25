// The bubble trail of the ice ruin's torpedo, static stage type 0x68 of ST2F (see ice_ruin_bubble_trail.hpp), one
// routine per exe routine. Spawned by the torpedo 0x5cac60's init (its class block's first word = the torpedo).
// States [0x8d0ed0]: 0 0x5c2580 clears the 8 bubble slots, 1 0x5c25c0: each live bubble rises and drifts back (exe
// 0x5e3750 by its yaw byte << 4), drawn as a 0x2c billboard (size 0x32, uv by its life [0x8d0ee4] / [0x8d0eec]); then
// [0x8d0ed8 + sub * 4]: 0 0x5c2720 -> 1, 1 0x5c2730 a new bubble 0x700 behind the torpedo each tick (an 8-slot ring)
// while it is drawn, else 2 0x5c2800: freed once no bubble is left. Exe quirk kept: the drift reads the life byte of
// slot "life", not of the bubble's own slot. The shadows (exe 0x5f20f0) are render output and left out.
#include "content/yosyonke/ice_ruin_bubble_trail.hpp"
#include "content/yosyonke/ice_ruin_bubble_trail_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/draw_commands.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace ice_ruin_bubble_trail_state;

namespace {

enum class TrailState : uint8_t { Init = 0, Run = 1 };
enum class TrailSub : uint8_t { Start = 0, Emit = 1, Drain = 2 };

constexpr uint8_t kBubbleLive = 0x01;
constexpr uint8_t kBubbleLife = 8;
constexpr int kBubbleSpawnBehind = 0x700, kBubbleRise = -0x30;

// the billboard's uv by a bubble's life
const DataTable<ScalarTable<uint8_t, kBubbleSlots>> kBubbleU{TableId::IceRuinBubbleTrailBubbleU};
const DataTable<ScalarTable<uint8_t, kBubbleSlots>> kBubbleV{TableId::IceRuinBubbleTrailBubbleV};

// 0x5c2730: a new bubble behind the torpedo while it is drawn (else the trail drains)
void emitBubble(StaticActor& actor, TrailFields& trail) {
    const ProjectileActor& torpedo = *trail.torpedo;
    if (!(torpedo.flags & kActorDrawn)) {
        actor.subState = uint8_t(TrailSub::Drain);
        return;
    }
    const uint32_t i = trail.nextSlot;
    trail.nextSlot = uint8_t(i + 1u);
    if (i >= kBubbleSlots - 1) trail.nextSlot = 0;
    if (trail.bubbleFlags[i] & kBubbleLive) return;
    trail.bubbleFlags[i] |= kBubbleLive;
    trail.life[i] = kBubbleLife;
    int16_t p[3] = {torpedo.pos.x.whole, torpedo.pos.y.whole, torpedo.pos.z.whole};
    moveAlongYaw16(p, torpedo.rotation.y, 0, 0, kBubbleSpawnBehind); // exe 0x5e3750
    trail.bubbleYaw[i] = uint8_t((uint32_t(uint16_t(torpedo.rotation.y)) >> 4) & 0xff);
    trail.bubbleX[i] = p[0];
    trail.bubbleY[i] = p[1];
    trail.bubbleZ[i] = p[2];
}
// 0x5c25c0: the live bubbles drift and are drawn; then the sub-state
void runTrail(StaticActor& actor, TrailFields& trail) {
    for (uint32_t i = 0; i < kBubbleSlots; ++i) {
        if (!(trail.bubbleFlags[i] & kBubbleLive)) continue;
        const int8_t life = int8_t(trail.life[i] - 1u);
        trail.life[i] = uint8_t(life);
        const uint32_t li = uint32_t(int32_t(life));
        const uint32_t u = kBubbleU[li], vv = kBubbleV[li];
        int16_t p[3] = {trail.bubbleX[i], trail.bubbleY[i], trail.bubbleZ[i]};
        const int back = int((0xfffffff4u - uint32_t(int32_t(trail.lifeSigned[li]))) << 5);
        moveAlongYaw16(p, int(uint32_t(trail.bubbleYaw[i]) << 4), 0, kBubbleRise, back); // exe 0x5e3750
        trail.bubbleX[i] = p[0];
        trail.bubbleY[i] = p[1];
        trail.bubbleZ[i] = p[2];
        dc::emit({0, 0x2ea0a0a0u, (uint32_t(int32_t(p[1])) << 16) | uint16_t(p[0]), ((vv | 0x7c9100u) << 8) | u,
                  uint32_t(int32_t(p[2])), ((vv | 0x2e00u) << 8) | (u + 0x2f), 0x32, ((vv + 0x2f) << 8) | u, 0,
                  ((vv + 0x2f) << 8) | (u + 0x2f)},
                 actor.at, false);
        if (trail.life[i] == 0) trail.bubbleFlags[i] &= uint8_t(~kBubbleLive);
    }
    switch (TrailSub(actor.subState)) { // [0x8d0ed8 + sub * 4]
    case TrailSub::Start: actor.subState = uint8_t(TrailSub::Emit); return; // 0x5c2720
    case TrailSub::Emit: emitBubble(actor, trail); return;
    case TrailSub::Drain: // 0x5c2800
        for (uint32_t i = 0; i < kBubbleSlots; ++i)
            if (trail.bubbleFlags[i] & kBubbleLive) return;
        freeActor(actor); // exe 0x5e1540
        return;
    default: notPortedAt(kStoryPort, "0x5c2560 sub-state (table 0x8d0ed8)", actor.subState);
    }
}

void bubbleTrail(StaticActor& actor) { // 0x5c2560
    TrailFields& trail = classState<TrailFields>(actor);
    switch (TrailState(actor.state)) {
    case TrailState::Init: // 0x5c2580
        actor.state = uint8_t(TrailState::Run);
        actor.setDrawn(false);
        actor.prevPos.x = {};
        actor.prevPos.y = {};
        actor.prevPos.z = {};
        trail.clearBubbles();
        return;
    case TrailState::Run: runTrail(actor, trail); return;
    default: notPortedAt(kStoryPort, "0x5c2560 state (table 0x8d0ed0)", actor.state);
    }
}

} // namespace

void bubbleTrailUpdate(const PlayerEnv&, uint32_t actor) { bubbleTrail(staticActorAt(actor)); }

} // namespace od::port
