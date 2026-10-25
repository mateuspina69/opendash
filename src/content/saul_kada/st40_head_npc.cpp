// The ST40 standing NPC that turns its head (pool 0x20 type 0 sub 1, exe 0x445840; hypothesis names). Its parameters:
// an animation request (0xff none), the head's target (0 straight, 1 toward the player clamped +-0x300, else toward
// (x + 0x400, z) clamped +-0x180), the head turned at once, the head's turn per tick; the head's turn goes onto part
// 1's decoded yaw. Kept on the ground. Its model / box by variant ([0x88e1fc] / [0x88e1f0]).
#include "content/saul_kada/st40_head_npc.hpp"
#include "content/saul_kada/st40_head_npc_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/cutscene_actor.hpp"
#include "game/field_alias.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st40_head_npc_state;

namespace {

enum class NpcState : uint8_t { Bind = 0, Stand = 1 };
enum class HeadTarget : int8_t { Straight = 0, Player = 1 };
// The NPC's parameters
struct NpcParams {
    explicit NpcParams(const AnimatedActor& actor) : NpcParams(const_cast<AnimatedActor&>(actor)) {}
    explicit NpcParams(AnimatedActor& actor) : animRequest(actor.params[0]), headTarget(actor.params[1]), headAtOnce(actor.params[2]), headStep(actor.params[3]) {}
    uint8_t& animRequest; // kNoRequest: none
    FieldAlias<int8_t, uint8_t> headTarget; // HeadTarget (any other: the point beside it)
    uint8_t& headAtOnce;
    uint8_t& headStep;
};
constexpr uint8_t kNoRequest = 0xff;
constexpr uint8_t kNpcHeldPoseBits = 3;
const DataTable<ScalarTable<uint8_t, 0x10>> modelVariants{TableId::St40HeadNpcModelVariants}; // by variant (every 4th byte)
constexpr uint32_t kHeadPart = 1;
constexpr int kPlayerReach = 0x300, kPointReach = 0x180, kPointOffset = 0x400;

// exe 0x5e3bc0 on the exe's stack points (a, b) << 16
int angleBetween(int ax, int az, int bx, int bz) {
    return angleOf(int32_t(uint32_t(ax) << 16), int32_t(uint32_t(az) << 16), int32_t(uint32_t(bx) << 16),
                   int32_t(uint32_t(bz) << 16));
}

void headNpc(const PlayerEnv& env, AnimatedActor& actor) {
    NpcParams params(actor);
    NpcFields& npc = classState<NpcFields>(actor);
    actor.savePreviousPose();
    actor.prevAnim = actor.anim;
    if (actor.variant != 0) {
        notPortedAt(kStoryPort, "0x445840 variant (table 0x88e1e8)", actor.variant);
        return;
    }
    if (NpcState(actor.state) == NpcState::Bind) { // 0x445890's bind
        if (!bindActorModel(actor, modelVariants[size_t(actor.variant) * 4])) { // exe 0x5e0bc0
            freeActor(actor);
            return;
        }
        actor.collisionBox.pointAt(stateAddress(TableId::St40HeadNpcBoxes) + uint32_t(actor.variant) * 12);
        actor.clearClassBlock();
        actor.enterState(uint8_t(NpcState::Stand));
        actor.anim = 0;
        actor.prevAnim = kNoAnimShown;
        npc.headTargetAngle = actor.rotation.y;
        npc.angleC = actor.rotation.y;
        npc.head = actor.rotation.y;
        npc.angleA = actor.rotation.y;
    }
    if (!(actor.flags & kActorLive)) return;
    if (params.animRequest != kNoRequest) {
        actor.anim = params.animRequest;
        actor.prevAnim = kNoAnimShown;
        params.animRequest = kNoRequest;
    }
    int target = 0;
    if (HeadTarget(params.headTarget.get()) == HeadTarget::Player) {
        const int d = angleDiff(uint16_t(actor.rotation.y),
                                uint32_t(angleBetween(player().pos.x.whole, player().pos.z.whole, actor.pos.x.whole,
                                                      actor.pos.z.whole)));
        target = d > kPlayerReach ? kPlayerReach : d < -kPlayerReach ? -kPlayerReach : d;
    } else if (HeadTarget(params.headTarget.get()) != HeadTarget::Straight) {
        const int d = angleDiff(uint16_t(actor.rotation.y),
                                uint32_t(angleBetween(0, 0, actor.pos.x.whole + kPointOffset, actor.pos.z.whole)));
        target = d > kPointReach ? kPointReach : d < -kPointReach ? -kPointReach : d;
    }
    npc.headTargetAngle = int16_t(target);
    const int head = npc.head, step = params.headStep;
    if (head > target && params.headAtOnce == 0 && head - target > step) npc.head = int16_t(head - step);
    else if (head < target && params.headAtOnce == 0 && target - head > step) npc.head = int16_t(head + step);
    else if (head != target) npc.head = int16_t(target);
    actor.pos.y.whole = int16_t(actorGround(env, actor)); // exe 0x40f190
    if (actor.anim != actor.prevAnim) animSet(actor, actor.anim, 0);       // exe 0x5e1c50
    else if (npc.flags & kNpcHeldPoseBits) animDecodeCurrent(actor);      // exe 0x5e1f30
    else animStep(actor);                                                 // exe 0x5e1ea0
    decodedFrame().entries[1 + kHeadPart].y += uint16_t(npc.head);
    animPose(actor); // exe 0x5e2d90
}

} // namespace

void headNpcUpdate(const PlayerEnv& env, uint32_t actor) { headNpc(env, animatedActorAt(actor)); }

} // namespace od::port
