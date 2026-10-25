// The ice ruin's ST2D / ST2E shrinking block (pool 0x20 type 0x52, exe 0x4dd770; hypothesis names): a semi-transparent stand-on
// solid whose texture scrolls. Variant 0 is gone once the flag [0x89583c + its parameter * 2] is set; the others wait
// 0xf ticks, then (sound 0x252) shrink in height (0x200 -> 0 by 0x10 a tick), then in width (by 0x80) and are freed.
// States [0x89582c]: 0 bind 0x4dd820, 1 run 0x4dd8c0, 2 / 3 gone 0x4dd9b0.
#include "content/yosyonke/ice_ruin_shrink_block.hpp"
#include "content/yosyonke/ice_ruin_shrink_block_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/cutscene_actor.hpp"
#include "game/field_alias.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace ice_ruin_shrink_block_state;

namespace {

enum class ShrinkState : uint8_t { Bind = 0, Run = 1, Gone = 2, GoneToo = 3 };
enum class ShrinkStep : uint8_t { Wait = 0, Flatten = 1, Narrow = 2 };
// The block's parameters: its model variant (also its flag's entry for variant 0), its texture's scroll
struct ShrinkParams {
    explicit ShrinkParams(const AnimatedActor& actor) : ShrinkParams(const_cast<AnimatedActor&>(actor)) {}
    explicit ShrinkParams(AnimatedActor& actor) : model(actor.params[0]), scroll(actor.params[1]) {}
    FieldAlias<int8_t, uint8_t> model;
    FieldAlias<int8_t, uint8_t> scroll;
};
const DataTable<ScalarTable<uint16_t, 0x10>> goneFlags{TableId::IceRuinShrinkBlockGoneFlags}; // by the parameter (variant 0)
constexpr int16_t kFullSize = 0x200;
constexpr uint16_t kFirstWait = 0xf, kSecondWait = 0x10;
constexpr uint16_t kShrinkSound = 0x252;
constexpr int8_t kScrollWrap = 0x40;

// exe 0x4dd820
void bind(AnimatedActor& actor, ShrinkFields& block) {
    if (!bindActorModel(actor, uint8_t(ShrinkParams(actor).model))) { // exe 0x5e0bc0
        freeActor(actor);
        return;
    }
    actor.anim = 0;
    actor.prevAnim = kNoAnimShown;
    actor.scale.x = kFullSize;
    actor.scale.y = kFullSize;
    actor.scale.z = kFullSize;
    actor.collisionBox.pointAt(TableId::IceRuinShrinkBlockShrinkBox);
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock();
    block.hitBox.previousX = actor.pos.x.whole;
    block.hitBox.previousZ = actor.pos.z.whole;
    block.hitBox.result = 0;
    block.waitTicks = kFirstWait;
    actor.enterState(uint8_t(ShrinkState::Run));
    actor.semiTransparency = semiTransparencyBits(1, 0); // exe 0x5e1a00(a, 1, 0)
}
// exe 0x4dd8c0
void run(AnimatedActor& actor, ShrinkFields& block) {
    ShrinkParams params(actor);
    setTexturePageTexel(actor.texturePage, uint32_t(int32_t(params.scroll.get())), 0, 0); // exe 0x5e3360
    ++params.scroll;
    if (params.scroll >= kScrollWrap) params.scroll = 0;
    actorCollisionSubmitSolid(actor, SolidType::StandOn, 0, 0); // exe 0x414050
    if (actor.variant == 0) return;
    switch (ShrinkStep(actor.step)) {
    case ShrinkStep::Wait:
        --block.waitTicks;
        if (block.waitTicks == 0) {
            block.size = kFullSize;
            actor.step = uint8_t(ShrinkStep::Flatten);
            soundPlayAtPos(kShrinkSound, actor.pos);
        }
        break;
    case ShrinkStep::Flatten: {
        block.size -= 0x10u;
        int v = block.size;
        if (v <= 0) {
            v = 0;
            actor.step = uint8_t(ShrinkStep::Narrow);
            block.size = kFullSize;
            block.waitTicks = kSecondWait;
        }
        actor.scale.y = int16_t(v);
        break;
    }
    case ShrinkStep::Narrow: {
        block.size -= 0x80u;
        int v = block.size;
        if (v <= 0) {
            v = 0;
            actor.enterState(uint8_t(ShrinkState::GoneToo));
        }
        actor.scale.x = int16_t(v);
        actor.scale.z = int16_t(v);
        break;
    }
    default: break;
    }
}

void shrinkBlock(AnimatedActor& actor) {
    ShrinkFields& block = classState<ShrinkFields>(actor);
    if (actor.variant == 0 && storyFlag(goneFlags[size_t(int32_t(ShrinkParams(actor).model))])) {
        freeActor(actor); // exe 0x5e1490
        return;
    }
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    switch (ShrinkState(actor.state)) {
    case ShrinkState::Bind: bind(actor, block); break;
    case ShrinkState::Run: run(actor, block); break;
    case ShrinkState::Gone:
    case ShrinkState::GoneToo: // 0x4dd9b0
        actorDetachRiders(actor); // exe 0x414e70
        freeActor(actor);
        return;
    default: notPortedAt(kStoryPort, "0x4dd770 state (table 0x89582c)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    animateAsked(actor, 0);
}

} // namespace

void shrinkBlockUpdate(const PlayerEnv&, uint32_t actor) { shrinkBlock(animatedActorAt(actor)); }

} // namespace od::port
