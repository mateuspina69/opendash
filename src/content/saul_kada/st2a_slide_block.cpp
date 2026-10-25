// The ST2A sliding block (static type 0xc, exe 0x597310; hypothesis names): it slides along its yaw's axis to stay in
// line with the player within 0x200 (0x280 a tick), rocking (its roll from a table while still / sliding); breakable
// (the hit points of its type's row; broken: a blast). States [0x8ce1a4]: 0 bind 0x5973f0, 1 slide 0x5974c0, 2 broken
// 0x597670; 3..5 are another class's 0x5976d0 (no case for these values: nothing), 6..8 nothing.
#include "content/saul_kada/st2a_slide_block.hpp"
#include "content/saul_kada/st2a_slide_block_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st2a_slide_block_state;

namespace {

enum class BlockState : uint8_t { Bind = 0, Slide = 1, Broken = 2, LastIdle = 8 };
// The block's parameter: its home offset (<< 10)
struct BlockParams {
    explicit BlockParams(const StaticActor& actor) : BlockParams(const_cast<StaticActor&>(actor)) {}
    explicit BlockParams(StaticActor& actor) : homeOffset(actor.params[0]) {}
    FieldAlias<int8_t, uint8_t> homeOffset;
};
const DataTable<ScalarTable<uint16_t, 12>> stillRolls{TableId::St2aSlideBlockStillRolls};
const DataTable<ScalarTable<uint16_t, 12>> slidingRolls{TableId::St2aSlideBlockSlidingRolls};
constexpr uint32_t kRockSteps = 0xc;
constexpr int kBlockAboveGround = 0x40;
constexpr int kFollowReach = 0x200;
constexpr int16_t kSlideSpeed = 0x280;
constexpr uint16_t kHurtSound = 0x8d, kStrongHurtSound = 0x8e;
constexpr uint16_t kBlastSize = 0x100;
// its hit entry: a body (radius 0x40), its type's damage, thrown and solid
constexpr uint32_t kBlockShape = hitGroup(kHitGroupBodies) | hitRadius(0x40);
constexpr uint32_t kBlockAttack = kHitAttackThrown | kHitAttackSolid;

// exe 0x597670: a blast and freed
void breakBlock(StaticActor& actor) {
    const StaticActorStats stats = staticStats(actor.type, 0);
    if (const uint32_t slot = allocActor60(0)) {
        StaticActor& blast = staticActorAt(slot);
        blast.flags = kActorLive;
        blast.loopFlags |= kActorSweepGroup2;
        blast.type = typeByte(GlobalStaticType::Blast);
        blast.variant = 0;
        blast.pos = actor.pos;
        BlastParams params(actorHeaderAt(slot));
        params.size = kBlastSize;
        params.attack = stats.damage;
    }
    freeActor(actor);
}
// one axis of the slide: in line with the player (snapped) or sliding toward him
void follow(Fixed16& coordinate, int16_t whole, int16_t playerWhole, int32_t playerCoordinate,
            int16_t& speed) {
    const int d = int(whole) - int(playerWhole);
    if (uint32_t((d + 0x28) << 4) <= 0x500) coordinate = playerCoordinate;
    else speed = d >= 0 ? int16_t(-kSlideSpeed) : kSlideSpeed;
}
// exe 0x5973f0
void bind(const PlayerEnv& env, StaticActor& actor, BlockFields& block) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    const uint16_t hp = hasStaticStats(actor.type) ? uint16_t(staticStats(actor.type, 0).hitPoints) : uint16_t(0);
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.velocity.clearWords();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(TableId::St2aSlideBlockBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.lastMover = nullptr;
    actor.pos.y.whole = int16_t(actorGround(env, actor) - kBlockAboveGround); // exe 0x40f190
    actor.clearClassBlock();
    block.home = uint16_t(int16_t(BlockParams(actor).homeOffset.get()) << 10);
    block.rockIndex = uint8_t(rngStep() % kRockSteps);
    actor.enterState(uint8_t(BlockState::Slide));
}
// exe 0x5974c0; false when broken
bool slide(const PlayerEnv& env, StaticActor& actor, BlockFields& block) {
    actor.velocity.z = 0;
    actor.velocity.x = 0;
    const uint32_t hit = block.hitBox.result;
    if (hit & kHitAttackBump) {
        breakBlock(actor);
        return false;
    }
    const uint32_t damage = hit & kHitDamageMask;
    if (damage || (hit & kHitAttackByPlayer)) {
        actor.tint = kTintFlash;
        soundPlayAtPos((hit & kHitAttackStrongPush) ? kStrongHurtSound : kHurtSound, actor.pos);
        if (hitPointsAdd(actor, -int(damage))) {
            breakBlock(actor);
            return false;
        }
    }
    const Player& p = player();
    if (planarDistance(actor.pos, p.pos) < kFollowReach) { // exe 0x5e3b00
        const uint16_t yaw = uint16_t(actor.rotation.y);
        if (!(yaw & 0x800)) follow(actor.pos.x, actor.pos.x.whole, p.pos.x.whole, p.pos.x, actor.velocity.x);
        else follow(actor.pos.z, actor.pos.z.whole, p.pos.z.whole, p.pos.z, actor.velocity.z);
        moveAlongYaw(actor.pos, int16_t(yaw), actor.velocity.x, 0, actor.velocity.z); // exe 0x5e36f0
    }
    const uint32_t k = block.rockIndex;
    actor.rotation.z = int16_t((uint16_t(actor.velocity.x) | uint16_t(actor.velocity.z)) == 0 ? stillRolls[k]
                                                                                              : slidingRolls[k]);
    block.rockIndex = uint8_t((k + 1) % kRockSteps);
    if (int32_t(actorTestMove(env, actor)) < 0) actorPushOut(env, actor);
    return true;
}

void slideBlock(const PlayerEnv& env, StaticActor& actor) {
    BlockFields& block = classState<BlockFields>(actor);
    actor.tint = kTintNeutral;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (BlockState(actor.state)) {
    case BlockState::Bind: bind(env, actor, block); break;
    case BlockState::Slide:
        if (!slide(env, actor, block)) return;
        break;
    case BlockState::Broken: breakBlock(actor); return;
    default:
        if (actor.state <= uint8_t(BlockState::LastIdle)) break;
        notPortedAt(kStoryPort, "0x597310 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    block.hitBox.result = 0;
    block.hitBox.previousX = actor.prevPos.x.whole;
    block.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    hitListSubmit(pos, block.hitBox, hitOwner(actor.slot) | kBlockShape,
                  uint32_t(staticStats(actor.type, 0).damage) | kBlockAttack); // exe 0x5e4120
}

} // namespace

void slideBlockUpdate(const PlayerEnv& env, uint32_t actor) { slideBlock(env, staticActorAt(actor)); }

} // namespace od::port
