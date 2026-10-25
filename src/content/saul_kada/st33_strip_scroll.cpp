// The ST33 VRAM strip scroller (pool e0 type 0x1a, exe 0x426020; hypothesis names): the 16 x 64 strip at (0x630,
// 0x180) copied into (0x630, 0x140) wrapped by a scroll offset that runs at its speed; a speed of 1 counts its life down
// and frees it. Render side: its VRAM copies (exe 0x40b710) go through vramMove (game/vram_moves).
#include "content/saul_kada/st33_strip_scroll.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/vram_moves.hpp"

namespace od::port {

namespace {

enum class ScrollState : uint8_t { Init = 0, Run = 1 };
// The scroller's bytes (the header's sub-state / step bytes): the scroll offset (* 2), its speed, the life left
struct StripScrollFields {
    explicit StripScrollFields(const ControllerActor& actor) : StripScrollFields(const_cast<ControllerActor&>(actor)) {}
    explicit StripScrollFields(ControllerActor& actor) : offset(actor.subState), speed(actor.step), life(actor.stepCounter) {}
    uint8_t& offset;
    uint8_t& speed;
    uint8_t& life;
};
constexpr uint8_t kStartSpeed = 2, kStartLife = 0x20, kCountingSpeed = 1;
constexpr int kStripX = 0x630, kSourceY = 0x180, kTargetY = 0x140, kStripW = 0x10, kStripH = 0x40;

// exe 0x426020
void stripScroll(ControllerActor& actor) {
    StripScrollFields scroll(actor);
    if (ScrollState(actor.state) == ScrollState::Init) {
        actor.state = uint8_t(ScrollState::Run);
        scroll.offset = 0;
        scroll.speed = kStartSpeed;
        scroll.life = kStartLife;
    }
    const int o = scroll.offset >> 1;
    if (o == 0) {
        vramMove(kStripX, kSourceY, kStripW, kStripH, kStripX, kTargetY); // exe 0x40b710
    } else {
        vramMove(kStripX, kSourceY, kStripW, kStripH - o, kStripX, kTargetY + o);
        vramMove(kStripX, kSourceY + kStripH - o, kStripW, o, kStripX, kTargetY);
    }
    if (scroll.speed == kCountingSpeed) {
        --scroll.life;
        if (scroll.life == 0) {
            freeActor(actor); // exe 0x5e15c0
            return;
        }
    }
    scroll.offset = uint8_t((scroll.offset + scroll.speed) & 0x7fu);
}

} // namespace

void st33StripScrollUpdate(const PlayerEnv&, uint32_t actor) { stripScroll(controllerActorAt(actor)); }

} // namespace od::port
