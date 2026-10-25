// The sign class 0x58d6e0 (see sign.hpp).
#include "game/actor_state.hpp"
#include "game/sign.hpp"
#include "game/sign_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/collision.hpp"
#include "game/event_state.hpp"
#include "game/hit_list.hpp"
#include "game/message.hpp"
#include "game/event.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/actor_movement.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace sign_state;

namespace {

const PlayerEnv* E = nullptr;

constexpr const char* kSignPort = "sign"; // the area of its port-gap errors


// the states (+8; table 0x8cd914)
enum class SignState : uint8_t {
    Init = 0,  // exe 0x58d7b0
    Main = 1,  // exe 0x58d840: talk / the drop
    Bind = 2,  // exe 0x58d970: bind only (variant 2: scenery)
    Turn = 3,  // exe 0x58d9a0: variant 4 turns
};
// the main state's sub-states (+9)
enum class SignSub : uint8_t {
    Idle = 0,
    Talking = 1,
    Drop = 2,     // the hit dropped it: up, then falls
    Falling = 3,  // falls, bounces once ...
    Bouncing = 4, // ... and lands
};

constexpr uint16_t kNoFlag = 0xffff;
constexpr uint32_t kDropHit = kHitAttackThrown; // a thrown object's hit drops it
constexpr uint32_t kSignEventParam = 0x12;  // the event's mode / flags (exe 0x543b40)
constexpr int16_t kDropSpeed = -0x140, kBounceSpeed = -0xa0, kGravity = 0x40;
constexpr uint8_t kSceneryVariant = 2, kTurningVariant = 4;
constexpr int kTurnSpeed = 0x80;
constexpr int16_t kHitHeight = 0x40; // the hit entry's centre above the feet

const DataTable<CollisionBoxRecord> signCollisionBox{TableId::SignCollisionBox};
const DataTable<InteractionBoxRecord> signTalkBox{TableId::SignTalkBox};

// exe 0x58d6e0's state machine, then the hit entry and a stand-on solid
void signStates(StaticActor& actor) {
    SignFields& sign = classState<SignFields>(actor);
    switch (SignState(actor.state)) {
    case SignState::Init:
        if (!bindActorModel(actor, actor.variant)) {
            freeActor(actor);
            return;
        }
        actor.clearClassBlock();
        actor.hitPoints = 1;
        actor.maxHitPoints = 1;
        actor.velocity.clearBytes();
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.collisionBox.pointAt(signCollisionBox.stateAddress());
        actor.interactionBox.pointAt(signTalkBox.stateAddress());
        actor.interaction = kInteractTalk;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.platform = nullptr;
        actor.lastMover = nullptr;
        {
            const int g = actorGround(*E, actor);
            actor.enterState(uint8_t(SignState::Main));
            actor.pos.y.whole = int16_t(g);
        }
        break;
    case SignState::Main:
        switch (SignSub(actor.subState)) {
        case SignSub::Idle:
            if (sign.dropFlag != kNoFlag && storyFlag(sign.dropFlag) && (sign.hitBox.result & kDropHit))
                actor.subState = uint8_t(SignSub::Drop);
            if (player().talksTo(actor) && eventState().active == 0) {
                eventStartAt(0, nullptr, kSignEventParam, 0, sign.message);
                actor.subState = uint8_t(SignSub::Talking);
            }
            break;
        case SignSub::Talking:
            if (eventState().active == 0) actor.subState = uint8_t(SignSub::Idle);
            break;
        case SignSub::Drop:
            actor.velocity.y = kDropSpeed;
            ++actor.subState;
            [[fallthrough]];
        case SignSub::Falling:
        case SignSub::Bouncing:
            actor.pos.y += uint32_t(int32_t(actor.velocity.y) * 0x1000);
            actor.velocity.y = int16_t(uint16_t(actor.velocity.y + kGravity));
            if (actorTestMove(*E, actor) & kMoveBlocked) {
                actor.pos.y.whole = int16_t(actorGround(*E, actor));
                if (actor.subState == uint8_t(SignSub::Bouncing)) {
                    actor.subState = uint8_t(SignSub::Idle);
                    break;
                }
                actor.velocity.y = kBounceSpeed;
                ++actor.subState;
            }
            break;
        default: break;
        }
        break;
    case SignState::Bind:
        if (bindActorModel(actor, actor.variant)) {
            if (actor.variant == kSceneryVariant) actor.flags |= kActorScenery;
            ++actor.state;
        }
        break;
    case SignState::Turn:
        if (actor.variant == kTurningVariant) actor.rotation.z = int16_t((actor.rotation.z - kTurnSpeed) & kAngleMask);
        break;
    default: notPortedAt(kSignPort, "state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole - kHitHeight)), actor.pos.z.whole};
    sign.hitBox.result = 0;
    sign.hitBox.previousX = actor.prevPos.x.whole;
    sign.hitBox.previousZ = actor.prevPos.z.whole;
    hitListSubmit(pos, static_cast<HitBoxHead&>(sign.hitBox),
                  hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x20),
                  kHitAttackHarmless | kHitAttackBump);
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0);
}

} // namespace

// the copies 0x58d6e0 does before its state, then the state
void signUpdate(const PlayerEnv& env, uint32_t address) {
    E = &env;
    StaticActor& actor = staticActorAt(address);
    actor.savePreviousPose();
    actor.savePreviousContact();
    signStates(actor);
}

} // namespace od::port
