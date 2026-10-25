// The ice ruin's ST2F hidden block that appears (static type 0x1f sub 2, exe 0x5a5020; hypothesis names; its siblings: sub 1
// content/saul_kada/st41_crumbling_platform, content/subruins/ruin_blocks). Hidden until the flag [0x8ced90 + variant * 2]
// is set with the player below y -0x200: then shown semi-transparent, sound 0x260, its grid cell (its parameters'
// column / row) switched to variant 1, and it fades in (3 -> 0x10).
// States [0x8ced80]: 0 bind 0x5a5070, 1 run 0x5a50d0, 2 nothing, 3 freed 0x5afdd0.
#include "content/yosyonke/ice_ruin_appearing_block.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/field_alias.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

enum class BlockState : uint8_t { Bind = 0, Run = 1, Idle = 2, Free = 3 };
enum class BlockSub : uint8_t { Hidden = 0, FadeIn = 1, Shown = 2 };
// The block's parameters: its grid cell (column / row, cell units + the grid's bias)
struct BlockParams {
    explicit BlockParams(const StaticActor& actor) : BlockParams(const_cast<StaticActor&>(actor)) {}
    explicit BlockParams(StaticActor& actor) : column(actor.params[0]), row(actor.params[1]) {}
    FieldAlias<int8_t, uint8_t> column;
    FieldAlias<int8_t, uint8_t> row;
};
// Its fade level (the acceleration's spare lane)
struct BlockFields {
    explicit BlockFields(const StaticActor& actor) : BlockFields(const_cast<StaticActor&>(actor)) {}
    explicit BlockFields(StaticActor& actor) : fade(actor.acceleration.spare) {}
    int16_t& fade;
};
const DataTable<ScalarTable<uint16_t, 8>> appearFlags{TableId::IceRuinAppearingBlockAppearFlags}; // by variant
constexpr int16_t kBlockHeight = 0x200, kBlockWidth = 0x1400;
constexpr int kAppearBelowY = -0x200;
constexpr uint16_t kAppearTint = 0xc63, kAppearSound = 0x260;
constexpr int16_t kFadeStart = 3, kFadeFull = 0x10;

void appearingBlock(StaticActor& actor) {
    BlockFields block(actor);
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (BlockState(actor.state)) {
    case BlockState::Bind: // 0x5a5070 (no free when the model is missing)
        actor.setDrawn(false);
        actor.enterState(uint8_t(BlockState::Run));
        actor.clearClassBlock();
        if (!bindActorModel(actor, 0)) return; // exe 0x5e0ba0
        actor.scale.y = kBlockHeight;
        actor.scale.x = kBlockWidth;
        actor.scale.z = kBlockWidth;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.platform = nullptr;
        actor.actorHit = 0;
        return;
    case BlockState::Run: // 0x5a50d0
        if (BlockSub(actor.subState) == BlockSub::Hidden) {
            if (!storyFlag(appearFlags[actor.variant]) || player().pos.y.whole > kAppearBelowY) return;
            actor.subState = uint8_t(BlockSub::FadeIn);
            actor.flags |= kActorDrawn;
            block.fade = kFadeStart;
            actor.tint = kAppearTint;
            actor.semiTransparency = semiTransparencyBits(1, 0); // exe 0x5e1a70(a, 1, 0)
            soundPlayAtPos(kAppearSound, actor.pos);              // exe 0x5d5380
            const BlockParams params(actor);
            switchGridCellVariant(uint32_t(int32_t(params.column.get())), uint32_t(int32_t(params.row.get())),
                                  1); // exe 0x58d5f0
        } else if (BlockSub(actor.subState) == BlockSub::FadeIn) {
            ++block.fade;
            const int v = block.fade;
            if (v == kFadeFull) {
                actor.subState = uint8_t(BlockSub::Shown);
                actor.restartStep();
            }
            actor.tint = uint16_t((((v << 5) | v) << 5) | v);
        }
        return;
    case BlockState::Idle: return;                    // 0x52ef00
    case BlockState::Free: freeActor(actor); return; // 0x5afdd0
    default: notPortedAt(kStoryPort, "0x5a5020 state (table 0x8ced80)", actor.state);
    }
}

} // namespace

void appearingBlockUpdate(const PlayerEnv&, uint32_t actor) { appearingBlock(staticActorAt(actor)); }

} // namespace od::port
