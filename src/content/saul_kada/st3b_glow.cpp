// The ST3B glow bound to its owner (static, exe 0x598150; hypothesis names): an additive model, scaled x2, that rises
// out of the ground fading in and spinning while its owner (a type-10 actor) lives and the player stays near, then
// rises fading out (or wobbles fading out) and is freed.
//
// States [0x8ce31c]: 0 bind 0x5981a0 (the model of its variant, box 0x8ce310, 0x28 above the ground), 1 glow 0x598260,
// 2 rise away 0x598370, 3 wobble away 0x5983e0. Left out (render output): its shadow (exe 0x5f20f0).
#include "content/saul_kada/st3b_glow.hpp"
#include "content/saul_kada/st3b_glow_state.hpp"

#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st3b_glow_state;

namespace {

enum class GlowState : uint8_t { Bind = 0, Glow = 1, RiseAway = 2, WobbleAway = 3 };
enum class GlowSub : uint8_t { Rising = 0, Up = 1 };
// The glow's parameter: its owner (an actor's address)
struct GlowParams {
    explicit GlowParams(const StaticActor& actor) : GlowParams(const_cast<StaticActor&>(actor)) {}
    explicit GlowParams(StaticActor& actor) : owner(actor.params, 0) {}
    BytesField<uint32_t, 4> owner; // the owner's identity word
};
constexpr uint8_t kOwnerType = 10;
constexpr int8_t kFullBrightness = 0x10;
constexpr int kGlowHeight = 0x28;

// the tint of the brightness: the grey (c << 10 | c << 5 | c)
uint16_t greyTint(const GlowFields& glow) {
    const uint16_t c = uint16_t(int16_t(glow.brightness));
    return uint16_t((uint16_t(c << 5 | c) << 5) | c);
}

// exe 0x5981a0
void bind(const PlayerEnv& env, StaticActor& actor, GlowFields& glow) {
    if (!bindActorModel(actor, actor.variant)) {
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    actor.rotation.setXY(0, 0);
    actor.rotation.setZSpare(0, 0);
    actor.hitPoints = 0;
    actor.maxHitPoints = 0;
    actor.collisionBox.pointAt(TableId::St3bGlowBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.velocity.z = 0;
    actor.velocity.setXY(0, int16_t(0xfe00));
    actor.airborne = 1;
    actor.onPlatform = 0;
    actor.clearClassBlock();
    glow.brightness = 0;
    actor.semiTransparency = semiTransparencyBits(1, 1); // exe 0x5e1a70(a, 1, 1): additive
    actor.scale.x = int16_t(actor.scale.x << 1);
    actor.scale.y = int16_t(actor.scale.y << 1);
    actor.scale.z = int16_t(actor.scale.z << 1);
    actor.tint = 0;
    actor.pos.y.whole = int16_t(actorGround(env, actor) - kGlowHeight);
    actor.enterState(uint8_t(GlowState::Glow));
}
// exe 0x598260: rises to rest, fades in, spins; away once the player is 0x200 off (in 0x200 cells) or the owner is gone
void glowing(StaticActor& actor, GlowFields& glow) {
    if (GlowSub(actor.subState) == GlowSub::Rising) {
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        actor.velocity.y = int16_t(actor.velocity.y + 0x40);
        if (actor.velocity.y >= 0) {
            actor.velocity.y = 0;
            actor.subState = uint8_t(GlowSub::Up);
        }
    }
    if (glow.brightness <= 0xf) {
        const int8_t c = int8_t(glow.brightness + 2);
        glow.brightness = c;
        if (c >= kFullBrightness) {
            glow.brightness = kFullBrightness;
            actor.semiTransparency = semiTransparencyBits(0, 0);
        }
    }
    actor.rotation.y = int16_t(actor.rotation.y + 0x80);
    actor.tint = greyTint(glow);
    const ActorHeader& owner = *slotByIdentityWord<ActorHeader>(GlowParams(actor).owner);
    const Player& p = player();
    const uint32_t dx = (uint32_t(int32_t(p.pos.x.whole)) & 0xfffffe00u) -
                        (uint32_t(int32_t(actor.pos.x.whole)) & 0xfffffe00u) + 0x200u;
    const uint32_t dz = (uint32_t(int32_t(p.pos.z.whole)) & 0xfffffe00u) -
                        (uint32_t(int32_t(actor.pos.z.whole)) & 0xfffffe00u) + 0x200u;
    if (dx > 0x400 || dz > 0x400 || !(owner.flags & kActorLive) || owner.type != kOwnerType) {
        actor.enterState(uint8_t(GlowState::RiseAway));
        actor.semiTransparency = semiTransparencyBits(1, 1);
    }
    // exe 0x5f20f0(a, 0x60, 0x100, 0): the shadow (render output)
}
// the fade of the away states: one step darker, freed when dark
void fadeAway(StaticActor& actor, GlowFields& glow) {
    actor.tint = greyTint(glow);
    const uint8_t c = uint8_t(uint8_t(glow.brightness) - 1);
    glow.brightness = int8_t(c);
    if (c == 0) freeActor(actor);
}
// exe 0x598370
void riseAway(StaticActor& actor, GlowFields& glow) {
    integrateVelocity(actor, 0, actor.velocity.y, 0);
    actor.velocity.y = int16_t(actor.velocity.y + 0x10);
    actor.rotation.y = int16_t(actor.rotation.y + 0x80);
    fadeAway(actor, glow); // live: the shadow exe 0x5f20f0(a, 0x60, 0x100, 0) (render output)
}
// exe 0x5983e0
void wobbleAway(StaticActor& actor, GlowFields& glow) {
    actor.rotation.y = int16_t(actor.rotation.y + 0x100);
    actor.rotation.z = int16_t((int32_t(trig64Tables().sine[(gameState().frameWord() & 0x1f) * 2]) * 0xc0) >> 12);
    fadeAway(actor, glow); // live: the shadow exe 0x5f20f0(a, 0x30, 0x100, 0) (render output)
}

// exe 0x598150
void ownerGlow(const PlayerEnv& env, StaticActor& actor) {
    GlowFields& glow = classState<GlowFields>(actor);
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (GlowState(actor.state)) { // [0x8ce31c]
    case GlowState::Bind: bind(env, actor, glow); return;
    case GlowState::Glow: glowing(actor, glow); return;
    case GlowState::RiseAway: riseAway(actor, glow); return;
    case GlowState::WobbleAway: wobbleAway(actor, glow); return;
    default: notPortedAt(kStoryPort, "static 0x598150 state", actor.state);
    }
}

} // namespace

void ownerGlowUpdate(const PlayerEnv& env, uint32_t actor) { ownerGlow(env, staticActorAt(actor)); }

} // namespace od::port
