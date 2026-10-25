// The ST3C talking prop (static type 0x50 variant 1, story 13, exe 0x5b7de0; hypothesis names): a stand-on solid that
// opens message 0x6f when the player talks to it (no event running).
#include "content/saul_kada/st3c_talk_prop.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/player/player_state.hpp"

namespace od::port {

namespace {

enum class PropState : uint8_t { Bind = 0, Stand = 1 };
enum class TalkState : uint8_t { Idle = 0, Talking = 1 };
constexpr uint16_t kTalkMessage = 0x6f;

// exe 0x5b7de0
void talkProp(StaticActor& actor) {
    if (PropState(actor.state) == PropState::Bind) {
        if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
            freeActor(actor);            // exe 0x5e1540
            return;
        }
        actor.flags |= kActorDrawn;
        actor.collisionBox = nullptr;
        actor.interactionBox.pointAt(TableId::St3cTalkPropTalkBox);
        actor.interaction = kInteractTalk;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.platform = nullptr;
        actor.lastMover = nullptr;
        ++actor.state;
    }
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050
    if (TalkState(actor.subState) == TalkState::Idle) {
        if (player().talksTo(actor) && eventState().active == 0) { // exe 0x573fa0
            eventStartAt(0, nullptr, 0x12, 0, kTalkMessage);              // exe 0x543b40
            actor.subState = uint8_t(TalkState::Talking);
        }
    } else if (TalkState(actor.subState) == TalkState::Talking && eventState().active == 0) {
        actor.subState = uint8_t(TalkState::Idle);
    }
}

} // namespace

void talkPropUpdate(const PlayerEnv&, uint32_t actor) { talkProp(staticActorAt(actor)); }

} // namespace od::port
