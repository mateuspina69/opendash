// The field HUD of dash2.exe. Six HUD elements of 0x1c bytes at 0xa7b1a0 (HudElementRecord), set up by exe 0x415f40,
// updated every frame by exe 0x415fe0 (handlers 0x889dc0[type]) and drawn in the draw pass by exe 0x416010 (handlers
// 0x889dd4[type], flags & kHudElementDrawn and state != Hidden). Types (HudElementType): the life gauge (0x416050 /
// 0x4161b0), the action icon box shown without a special weapon (0x416410 / 0x416520), the special weapon gauges
// (0x416570 / 0x4165c0), the enemy gauges in elements 3..5 of the game state -- a boss's life (added by exe 0x415d30;
// 0x416870 / 0x416980) and the mission targets' bars (added by exe 0x415e50, e.g. the ST18 machine Roll repairs;
// 0x416b90 / 0x416ca0). The player's three are presentation (od::hud::Hud keeps its own copy of their state and reads
// the game's); the enemy gauges live in the game state (the boss code adds and checks them) and are updated there.
// Frames are sprites of tpage 0x0d with the element's clut (+0xe: 0x7fd0, 0x7fd2 when an enemy is near / the lock-on
// target is not liftable), part lists 0x889ce8 (exe 0x416f80); bars are flat quads / lines. Also the scripted sprite
// banner (exe 0x5f25e0 / 0x5f2690) and a script's white screen (exe 0x5f30e0). Coordinates are the 320x240 PS1
// screen.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/data_source.hpp"
#include "game/actor_state.hpp"
#include "game/loaded_data.hpp"
#include "game/state_array.hpp"

namespace od::hud {

struct Prim {
    enum Kind : uint8_t { Textured, Flat, Line } kind = Flat;
    float x[4]{}, y[4]{};   // corners in PS1 quad order (0 1 2 3 = TL TR BL BR); a line uses 0 and 1
    float u[4]{}, v[4]{};   // texels of tpage 0x0d (Textured)
    uint16_t clut = 0;      // Textured
    uint16_t tpage = 0x0d;  // Textured: the texture page (the HUD frames: 0x0d)
    uint32_t colour = 0;    // 0xBBGGRR (Flat / Line; Textured: 0x808080)
    bool semi = false;      // PS1 semi-transparency (mode 0: half the sprite over half the screen)
};

struct Element { // the 0x1c-byte record at 0xa7b1a0 + i * 0x1c
    uint8_t flags = 0, type = 0, index = 0, style = 0, state = 0;
    int16_t timer = 0;  // +6
    int16_t x = 0;      // +0xc
    uint16_t clut = 0;  // +0xe
    int16_t last = 0;   // +0x10
    uint16_t fill = 0;  // +0x12
    uint16_t trail = 0; // +0x14
    int16_t len = 0;    // +0x16
};

class Hud {
public:
    // exe 0x415f40 (the field mode set up): the exe's part / slide / style tables copied from the game state (the HUD
    // is not ready while they are not mapped), the player's three elements
    void init();
    bool ready() const { return ready_; }
    // exe 0x415fe0, once per game frame (logic tick); reads the player / event state from the game state
    void tick();
    // exe 0x416870 / 0x416b90 for the enemy gauges (elements 3..5 of the game state: the boss code allocates them and
    // checks them), so this update writes them like the game does. Call once per logic tick, after tick().
    void tickEnemyGauges();
    // exe 0x416010: the primitives in drawing order (back to front)
    std::vector<Prim> draw() const;
    const std::array<Element, 6>& elements() const { return e_; }

private:
    struct Part { uint8_t type, b1; int8_t dx, dy; uint8_t u, v, w, h; };
    void slide(Element& e) const;
    bool free() const;
    void frame(const Element& e, float y, std::vector<Prim>& out) const;
    std::array<Element, 6> e_{};
    std::vector<Part> parts_;                          // copy of 0x889ce8 (the updates patch a few bytes)
    std::array<std::array<int16_t, 4>, 5> slide_{};    // 0x889de8: {dir, speed, shown x, hidden x} by type
    std::array<std::array<uint8_t, 4>, 8> style_{};    // 0x889e10: {orientation, length bias, first part, count} by
                                                       // element +3 (0..2 the player's, 3..6 type-3 gauges, 7 type 4)
    std::array<uint16_t, 3> cluts_{};                  // 0x889ce0
    uint32_t frame_ = 0;                               // stands for the frame counter 0x6d0006
    uint16_t anim_ = 0;                                // 0xa7b258: life icon frame (0..5)
    bool ready_ = false;
};

// exe 0x5f2690 (draw pass): the scripted 2D sprite banner (od::port::HudBanner, shown by hudBannerShow): style SlideIn
// slides each sprite up into place (radius -8 per frame, y = radius * 2), MissionStart / MissionComplete fly them in
// along their angle (radius -0xc); semi-transparent until in place. ST1E: MissionStart at (0x54, 0x28) while the mission
// briefing runs (room handler 0x580750), MissionComplete at (0x44, 0x30) for the result (script 0xd). Like the game's
// draw pass it advances the animation in the game state: call once per drawn game frame.
std::vector<Prim> overlayDrawPass();
// exe 0x5f30e0: a script's white screen, while a running script the content registers asks for it
// (ContentRegistry::scriptWhiteScreens)
std::vector<Prim> scriptWhiteDraw();
// A HUD sprite of tpage 0x0e (the stage controllers' plates and digits): the rect (x0, y0)..(x1, y1), the texels
// (u0, v0)..(u1, v1), the clut and the colour
Prim plateSprite(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint16_t clut,
                 uint32_t colour = 0x808080);

// A plate of a stage controller's picture (exe data, 0x10 bytes): the rect and its texels (x0 y0 x1 y1 u0 v0 u1 v1)
struct PictureQuad : DataRecord {
    std::array<uint16_t, 8> words{};
    static PictureQuad load(const TableReader& r) {
        PictureQuad q;
        for (size_t i = 0; i < q.words.size(); ++i) q.words[i] = r.u16(i * 2);
        return q;
    }
};
// the plate as a sprite of the controllers' clut
Prim pictureQuad(const PictureQuad& q, uint32_t colour);
// a digit of the controllers' font row (v 0x80, 8 x 0x10) at (x, y)
Prim pictureDigit(float x, float y, uint32_t digit, uint32_t colour);

} // namespace od::hud

namespace od::port {

struct Actor;

// +1: what a HUD element shows
enum class HudElementType : uint8_t {
    LifeGauge = 0,
    ActionIcon = 1,   // the action icon box (no special weapon)
    WeaponGauges = 2, // the special weapon's ammunition and energy
    BossGauge = 3,    // an enemy's life (exe 0x415d30)
    TargetGauge = 4,  // a mission target's bar (exe 0x415e50)
};
// +4: its slide (exe 0x416eb0)
enum class HudSlide : uint8_t {
    Hidden = 0,
    SlideIn = 1,
    Shown = 2,
    SlideOut = 3,
};
// +0: the flags
constexpr uint8_t kHudElementUpdated = 1;
constexpr uint8_t kHudElementDrawn = 2;
constexpr uint8_t kHudElementShown = kHudElementUpdated | kHudElementDrawn;

// One HUD element record (od::hud::Element mirrors what it reads of them)
struct HudElementRecord {
    uint8_t flags = 0;     // kHudElement*
    uint8_t type = 0;      // HudElementType
    uint8_t index = 0;     // the enemy gauge's rank among its type's (its row)
    uint8_t style = 0;     // the element's style (exe table 0x889e10)
    uint8_t state = 0;     // HudSlide
    uint8_t unread05 = 0;  // (cleared with the records; no reader)
    int16_t timer = 0;     // the damage trail's delay
    StateArray<uint8_t, 3> colour{}; // the enemy gauge's bar colour (R, G, B)
    uint8_t shift = 0;     // the enemy gauge's hit points >> shift = its length in pixels
    int16_t x = 0;
    uint16_t clut = 0;
    int16_t lastValue = 0; // the hit points last shown
    uint16_t fill = 0;
    uint16_t trail = 0;    // the damage trail (follows the fill after the timer)
    int16_t length = 0;
    ActorRef<Actor> target; // the actor it shows (the player's elements: the player)

    HudElementType elementType() const { return HudElementType(type); }
    HudSlide slide() const { return HudSlide(state); }
    void setSlide(HudSlide s) { state = uint8_t(s); }
    // the bar colour as 0xBBGGRR (the exe's dword read of +8, masked)
    uint32_t colourRgb() const { return uint32_t(colour[0]) | uint32_t(colour[1]) << 8 | uint32_t(colour[2]) << 16; }
    // the trail's delay cleared as the exe's two byte stores
    void clearTimerBytes() { timer = 0; }
};
constexpr size_t kHudElements = 6;
constexpr size_t kHudFirstEnemyGauge = 3; // elements 3..5
constexpr size_t kHudEnemyGauges = 3;
struct HudMemory {
    StateArray<HudElementRecord, kHudElements> elements{};
    StateArray<uint8_t, 3> elementTail{}; // the bytes after the records the clear covers (no reader is traced)
    StateArray<uint32_t, 3> stateWords{}; // the three words after them (cleared together; no reader is traced)

    // the elements' bytes cleared (exe 0x40dc40(0xa7b1a0, 0xa7b247): the 6 records and 3 bytes after)
    void clearElementBytes() {
        elements.fill(HudElementRecord{});
        elementTail.fill(0);
    }
    // the three words after the elements cleared
    void clearStateWords() { stateWords.fill(0); }
};
inline HudMemory& hudMemory() {
    static HudMemory s;
    return s;
}
// ---- the scripted sprite banner (exe 0x5f25e0 shows it, 0x5f2680 hides it; od::hud::overlayDrawPass draws it) ----
// A kind of banner (exe table 0x91ba40, 8 bytes per kind): its sprites, its texture page and clut
struct HudBannerKind : DataRecord {
    int16_t count = 0;
    uint16_t tpage = 0, clut = 0;
    static HudBannerKind load(const TableReader& r) { return {{}, r.s16(0), r.u16(4), r.u16(6)}; }
};
// A sprite of a kind's layout (exe data, 12 bytes): its place, where it flies in from, its texels
struct HudBannerLayoutSprite : DataRecord {
    static constexpr uint32_t kBytes = 0xc;
    int16_t x = 0;
    int16_t y = 0;
    uint16_t radius = 0;
    uint16_t angle = 0;
    uint8_t u = 0, v = 0, w = 0, h = 0;
    template <class R> static HudBannerLayoutSprite load(const R& r) {
        return {{}, r.s16(0), r.s16(2), r.u16(4), r.u16(6), r.u8(8), r.u8(9), r.u8(0xa), r.u8(0xb)};
    }
};
constexpr uint32_t kBannerLayoutSpriteSize = 0xc;
// sprite i of a kind's layout (its sprites one after the other)
inline HudBannerLayoutSprite bannerLayoutSprite(uint32_t layout, uint32_t i) {
    return dataRecord<HudBannerLayoutSprite>(layout + i * kBannerLayoutSpriteSize, HudBannerLayoutSprite::kBytes);
}
// A sprite of the banner shown: its place and how far it still flies in
struct HudBannerSprite {
    int16_t x = 0;
    int16_t y = 0;
    uint16_t radius = 0;
    uint16_t angle = 0;
};
struct HudBanner {
    uint8_t shown = 0;
    int8_t kind = 0;       // HudBannerStyle
    uint16_t progress = 0; // the fly-in's count (the draw pass advances it)
    int16_t count = 0;     // the kind's sprites
    uint16_t unread06 = 0; // (no reader)
    uint16_t tpage = 0;
    uint16_t clut = 0;
    uint16_t x = 0;        // the banner's origin
    uint16_t y = 0;
    StateArray<HudBannerSprite, 0x20> sprites{};
    DataPtr<> layout; // the kind's layout (exe data: HudBannerLayoutSprite records)

    // exe 0x5f2680: hidden (the head's dword cleared: shown, kind, progress)
    void hide() {
        shown = 0;
        kind = 0;
        progress = 0;
    }
};
inline HudBanner& hudBanner() {
    static HudBanner s;
    return s;
}
// The banners (the kinds of exe table 0x91ba40)
enum class HudBannerStyle : uint8_t {
    SlideIn = 0,         // 24 sprites sliding up into place
    MissionStart = 1,    // "MISSION START": the sprites flying in along their angles
    MissionComplete = 2, // "MISSION COMPLETE": likewise
};
// exe 0x5f25e0(x, y, kind): the banner of `style` shown at (x, y), its sprites taken from the kind's layout
void hudBannerShow(uint16_t x, uint16_t y, HudBannerStyle style);

// exe 0x4153e0's HUD part (exe 0x415f40 on the game state, at a stage load): the elements cleared, the player's three
// (the life gauge, the action icon box, the special weapon gauges) shown at their places, the enemy-near flag cleared
void hudElementsInit();
// exe 0x415d30(actor, style, shift, colour): a boss's life gauge in the first free enemy element (none in the demo,
// none twice for one actor): frame style 3 + `style`, its length the actor's maximum hit points >> shift, the bar's
// colour (0xBBGGRR, each channel's low bit cleared)
void hudAddBossGauge(Actor& actor, uint8_t style, uint8_t shift, uint32_t colour);
// exe 0x415e50(actor, style, shift, colour): a mission target's bar (frame style 7 + `style`) in the first free enemy
// element, none twice for one actor
void hudAddTargetGauge(Actor& actor, uint8_t style, uint8_t shift, uint32_t colour);
// exe 0x415e30(actor, style, shift): exe 0x415e50 with the exe's own colour argument (the address 0x63ffde, passed as
// the colour)
constexpr uint32_t kTargetGaugeColour = 0x63ffde;
inline void hudAddTargetGauge(Actor& actor, uint8_t style, uint8_t shift) {
    hudAddTargetGauge(actor, style, shift, kTargetGaugeColour);
}

} // namespace od::port
