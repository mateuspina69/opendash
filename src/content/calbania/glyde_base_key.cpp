// The keys of Glyde's base (ST30 static type 0x42; see glyde_base_key.hpp), transcribed from dash2.exe one function per
// exe function. States [0x8d079c]: 0 init 0x5b2720, 1 0x5b27c0, 2 0x5afdd0 (freed). Variant 0 falls (spinning) to the
// floor (y -0x20) and, in ST30, bounces up and settles into the hover; variant 1 starts 0x80 up, hovering. The hover
// bobs around y -0x68; the player within 0x32 (carrying nothing) picks a variant 1 key up: the flag of its parameter
// [0x8d07a8] and the event 0x20001. The shadow (exe 0x5f20f0) is render output.
#include "content/calbania/glyde_base_key.hpp"
#include "content/calbania/glyde_base_key_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/distances.hpp"
#include "game/event.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace glyde_base_key_state;

namespace {

constexpr const char* kKeyPort = "glyde base key port"; // the area of its port-gap errors


enum class KeyState : uint8_t { Init = 0, Active = 1, Free = 2 };
enum class KeySub : uint8_t { Spin = 0, Landed = 1, Bounce = 2, Hover = 3 };
enum class HoverStep : uint8_t { Sink = 0, Rise = 1 };

const DataTable<ScalarTable<uint16_t, 3>> keyFlags{TableId::GlydeBaseKeyFlags}; // the story flags of the base's keys (0x3e5..0x3e7)
constexpr uint8_t kBaseStage = 0x30;
constexpr int16_t kFloorY = -0x20, kHoverY = -0x68;
constexpr int kPickUpRange = 0x32;
constexpr uint32_t kPickUpEvent = 0x20001;
constexpr uint16_t kPickUpMessage = 0xf; // the event's message: the room's + 0xf

// exe 0x5b27c0
void active(StaticActor& actor, GlydeKeyFields& f) {
    if (actor.airborne != 0) { // falling
        actor.velocity.y += 0x20u;
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        if (actor.pos.y.whole > kFloorY) {
            if (gameState().stage == kBaseStage) actor.subState = uint8_t(KeySub::Landed);
            actor.airborne = 0;
            actor.pos.y.whole = kFloorY;
        }
    } else {
        actor.rotation.y += 0x20u;
    }
    switch (KeySub(actor.subState)) {
    case KeySub::Landed:
        actor.velocity.y = int16_t(0xfe00);
        actor.subState = uint8_t(KeySub::Bounce);
        f.landedMark = 8;
        return;
    case KeySub::Bounce:
        actor.velocity.y += 0x40u;
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        if (actor.velocity.y > 0) {
            actor.subState = uint8_t(KeySub::Hover);
            actor.velocity.y = 0;
        }
        return;
    case KeySub::Hover: {
        const HoverStep s = HoverStep(actor.step);
        if (s == HoverStep::Sink) {
            const int v = actor.velocity.y + 4;
            actor.velocity.y = int16_t(v);
            actor.pos.y += uint32_t(v) << 12;
            if (actor.pos.y.whole > kHoverY) actor.step = uint8_t(uint8_t(s) + 1u);
        } else if (s == HoverStep::Rise) {
            const int v = actor.velocity.y - 4;
            actor.velocity.y = int16_t(v);
            actor.pos.y += uint32_t(v) << 12;
            if (actor.pos.y.whole < kHoverY) actor.step = uint8_t(HoverStep::Sink);
        }
        const Player& p = player();
        if (actor.variant != 0 && p.carryFlags == 0 && planarDistance(actor.pos, p.pos) < kPickUpRange) {
            ++actor.state;
            setStoryFlag(keyFlags[size_t(int32_t(f.flagIndex))]); // exe 0x510e10
            eventStart(&actor.pos, &actor.rotation, kPickUpEvent, 0,
                       uint16_t(gameState().room + kPickUpMessage)); // exe 0x543af0
        }
        return;
    }
    default: return;
    }
}
// exe 0x5b2720
void init(StaticActor& actor) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.enterState(uint8_t(KeyState::Active));
    actor.collisionBox.pointAt(TableId::GlydeBaseKeyBox);
    actor.setDrawn(true);
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.clearClassBlock();
    if (actor.variant == 0) {
        actor.airborne = 1;
        actor.rotation.z = 0;
        actor.rotation.x = 0;
    } else {
        actor.pos.y.whole = int16_t(actor.pos.y.whole - 0x80);
        actor.subState = uint8_t(KeySub::Hover);
        actor.velocity.x = 0;
        actor.velocity.y = 0;
        actor.velocity.z = 0;
        actor.rotation.z = 0;
        actor.rotation.y = 0;
        actor.rotation.x = 0;
    }
}

// exe 0x5b26d0's body
void key(StaticActor& actor) {
    GlydeKeyFields& f = classState<GlydeKeyFields>(actor);
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (KeyState(actor.state)) { // [0x8d079c]
    case KeyState::Init: init(actor); return;
    case KeyState::Active: active(actor, f); return;
    case KeyState::Free: freeActor(actor); return; // exe 0x5afdd0
    default: notPortedAt(kKeyPort, "class 0x5b26d0 state", actor.state);
    }
}

} // namespace

void glydeBaseKeyUpdate(const PlayerEnv&, uint32_t actor) { key(staticActorAt(actor)); }

} // namespace od::port
