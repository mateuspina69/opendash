// The field HUD of dash2.exe.
#include "game/hud.hpp"

#include <algorithm>

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/content_registry.hpp"
#include "game/display.hpp"
#include "game/event_state.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/script_state.hpp"

namespace od::port {

namespace {

// ---- the HUD's exe tables -------------------------------------------------------------------------------------------
// A frame part (exe 0x889ce8, 8 bytes; exe 0x416f80 draws them): a sprite, or (type 0) a strip stretched along the
// element's length; its clut index (into hudCluts) for the element whose frame starts with it
struct HudPartRecord : DataRecord {
    uint8_t type = 0;
    uint8_t clutIndex = 0;
    int8_t dx = 0;
    int8_t dy = 0;
    uint8_t u = 0;
    uint8_t v = 0;
    uint8_t w = 0;
    uint8_t h = 0;
    static HudPartRecord load(const TableReader& r) {
        HudPartRecord rec;
        rec.type = r.u8(0x0);
        rec.clutIndex = r.u8(0x1);
        rec.dx = r.s8(0x2);
        rec.dy = r.s8(0x3);
        rec.u = r.u8(0x4);
        rec.v = r.u8(0x5);
        rec.w = r.u8(0x6);
        rec.h = r.u8(0x7);
        return rec;
    }
};
// A type's slide (exe 0x889de8, 8 bytes): the direction, the speed, the shown and the hidden x
struct HudSlideRecord : DataRecord {
    std::array<int16_t, 4> words{};
    static HudSlideRecord load(const TableReader& r) {
        HudSlideRecord rec;
        for (size_t i = 0; i < 4; ++i) rec.words[i] = r.s16(i * 2);
        return rec;
    }
};
// An element style (exe 0x889e10, 4 bytes): the orientation, the length bias, the first part, the part count
struct HudStyleRecord : DataRecord {
    std::array<uint8_t, 4> bytes{};
    static HudStyleRecord load(const TableReader& r) {
        HudStyleRecord rec;
        for (size_t i = 0; i < 4; ++i) rec.bytes[i] = r.u8(i);
        return rec;
    }
};
constexpr uint32_t kPartCount = 27, kSlideTypes = 5, kStyleCount = 8;
const DataTable<RecordTable<HudPartRecord, kPartCount, 8>> hudParts{TableId::HudParts};
const DataTable<RecordTable<HudSlideRecord, kSlideTypes, 8>> hudSlides{TableId::HudSlides};
const DataTable<RecordTable<HudStyleRecord, kStyleCount, 4>> hudStyles{TableId::HudStyles};
const DataTable<ScalarTable<uint16_t, 3>> hudCluts{TableId::HudCluts};
// the parts whose clut index the elements' clut is taken from: the weapon gauges', a boss gauge's, a target gauge's
constexpr size_t kWeaponGaugePart = 8, kBossGaugePart = 12, kTargetGaugePart = 24;

// ---- the elements' values ----
constexpr int16_t kLifeGaugeX = -0x26, kRightElementsX = 0x13a;
constexpr int16_t kWeaponGaugesLength = 0x3e;
constexpr int16_t kBossGaugeX = 0x150;    // a boss gauge's hidden x past its length
constexpr int16_t kTargetGaugeX = -0x2c;  // a target gauge's hidden x before its length
constexpr uint8_t kBossGaugeStyles = 3;   // a boss gauge's style: 3 + its own
constexpr uint8_t kTargetGaugeStyles = 7; // a target gauge's: 7 + its own
constexpr uint8_t kColourChannelMask = 0xfe;

} // namespace

} // namespace od::port

namespace od::hud {

using namespace od::port;

namespace {

constexpr uint16_t kHudClut = 0x7fd0;         // the frames' clut
constexpr uint16_t kHudClutAlert = 0x7fd2;    // an enemy near (the life gauge) / the target not liftable (the icon)
constexpr uint16_t kLifeIconPart = 1, kActionIconPart = 5, kActionLabelPart = 7;
constexpr int kLifeIconFrames = 6;
constexpr int16_t kTrailDelay = 0x18;         // the life gauge's trail waits this before following
constexpr int16_t kEnemyTrailDelay = 8;
constexpr int16_t kLifeBottom = 0xb3;

// a flat quad (x0, y0)..(x1, y1) of the colour
Prim flat(float x0, float y0, float x1, float y1, uint32_t colour) {
    Prim p;
    p.kind = Prim::Flat;
    p.x[0] = p.x[2] = x0;
    p.x[1] = p.x[3] = x1;
    p.y[0] = p.y[1] = y0;
    p.y[2] = p.y[3] = y1;
    p.colour = colour & 0xffffff;
    return p;
}

} // namespace

namespace {
constexpr uint16_t kPictureClut = 0x3c38;
constexpr float kDigitWidth = 8, kDigitHeight = 0x10, kDigitV = 0x80;
} // namespace

Prim pictureQuad(const PictureQuad& q, uint32_t colour) {
    auto w = [&](uint32_t i) { return float(q.words[i]); };
    return plateSprite(w(0), w(1), w(2), w(3), w(4), w(5), w(6), w(7), kPictureClut, colour);
}

Prim pictureDigit(float x, float y, uint32_t digit, uint32_t colour) {
    const float u = float(digit << 3);
    return plateSprite(x, y, x + kDigitWidth, y + kDigitHeight, u, kDigitV, u + kDigitWidth, kDigitV + kDigitHeight,
                       kPictureClut, colour);
}

Prim plateSprite(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint16_t clut,
                 uint32_t colour) {
    Prim q;
    q.kind = Prim::Textured;
    q.tpage = 0x0e;
    q.clut = clut;
    q.colour = colour;
    q.x[0] = q.x[2] = x0;
    q.x[1] = q.x[3] = x1;
    q.y[0] = q.y[1] = y0;
    q.y[2] = q.y[3] = y1;
    q.u[0] = q.u[2] = u0;
    q.u[1] = q.u[3] = u1;
    q.v[0] = q.v[1] = v0;
    q.v[2] = q.v[3] = v1;
    return q;
}

void Hud::init() {
    ready_ = false;
    if (!hudParts.available() || !hudSlides.available() || !hudStyles.available()) return;
    parts_.clear();
    for (uint32_t i = 0; i < kPartCount; ++i) {
        const HudPartRecord r = hudParts[i];
        parts_.push_back({r.type, r.clutIndex, r.dx, r.dy, r.u, r.v, r.w, r.h});
    }
    for (uint32_t t = 0; t < kSlideTypes; ++t) {
        const HudSlideRecord r = hudSlides[t];
        for (uint32_t k = 0; k < 4; ++k) slide_[t][k] = r.words[k];
    }
    for (uint32_t t = 0; t < kStyleCount; ++t) {
        const HudStyleRecord r = hudStyles[t];
        for (uint32_t k = 0; k < 4; ++k) style_[t][k] = r.bytes[k];
    }
    for (uint32_t k = 0; k < 3; ++k) cluts_[k] = hudCluts[k];
    // exe 0x415f40: clear, then the life gauge (0), the action box (1) and the special weapon gauge (2)
    e_ = {};
    e_[0] = {kHudElementShown, uint8_t(HudElementType::LifeGauge), 0, 0};
    e_[0].x = kLifeGaugeX;
    e_[1] = {kHudElementShown, uint8_t(HudElementType::ActionIcon), 0, 1};
    e_[1].x = kRightElementsX;
    e_[2] = {kHudElementShown, uint8_t(HudElementType::WeaponGauges), 0, 2};
    e_[2].x = kRightElementsX;
    e_[2].len = kWeaponGaugesLength;
    e_[2].clut = cluts_[std::min<uint32_t>(parts_[kWeaponGaugePart].b1, 2)];
    frame_ = 0;
    anim_ = 0;
    ready_ = true;
}

// no script / fade / event running (the elements slide out while one does)
bool Hud::free() const { return scriptState().flags == 0 && fadeState().busy == 0 && eventState().active == 0; }

// exe 0x416eb0: SlideIn slides in by the type's speed to its shown x (-> Shown), SlideOut slides out to the hidden x
// (-> Hidden); an event / transition sends a shown element out
void Hud::slide(Element& e) const {
    const auto& s = slide_[std::min<uint8_t>(e.type, 4)];
    switch (HudSlide(e.state)) {
    case HudSlide::SlideIn:
        break;
    case HudSlide::Shown:
        return;
    case HudSlide::SlideOut:
        e.x = int16_t(e.x - s[1]);
        if (s[0] != 0 ? e.x >= s[3] : e.x <= s[3]) {
            e.x = s[3];
            e.state = uint8_t(HudSlide::Hidden);
        }
        return;
    default:
        return;
    }
    e.x = int16_t(e.x + s[1]);
    if (s[0] == 0 ? e.x >= s[2] : e.x <= s[2]) {
        e.x = s[2];
        e.state = uint8_t(HudSlide::Shown);
    }
}

void Hud::tick() {
    if (!ready_) return;
    ++frame_;
    const bool fr = free();
    const Player& p = player();
    auto shownOrIn = [](const Element& e) {
        return HudSlide(e.state) == HudSlide::SlideIn || HudSlide(e.state) == HudSlide::Shown;
    };
    // the "not free -> slide out" part of exe 0x416eb0, applied before its slide step
    auto slideStep = [&](Element& e) {
        if (shownOrIn(e) && !fr) {
            e.state = uint8_t(HudSlide::SlideOut);
            return;
        }
        slide(e);
    };
    { // exe 0x416050: the life gauge
        Element& e = e_[0];
        if (fr && HudSlide(e.state) == HudSlide::Hidden) e.state = uint8_t(HudSlide::SlideIn);
        slideStep(e);
        const int8_t near = gameState().enemyNear;
        if (near < 1) {
            if ((frame_ & 3) == 0) anim_ = uint16_t((anim_ + 1) % kLifeIconFrames);
            e.clut = kHudClut;
        } else {
            anim_ = uint16_t((anim_ + 1) % kLifeIconFrames);
            if (near == 2) e.clut = kHudClutAlert; // (the game also moves enemyNear to 3: not written here)
        }
        parts_[kLifeIconPart].u = uint8_t(anim_ << 4); // 0x889cf4: the small icon's animation frame
        const int hp = p.hitPoints, mx = p.maxHitPoints;
        e.len = int16_t((mx * 5 >> 4) - 1);
        e.fill = uint16_t(((hp + 3) * 5) >> 4);
        if (uint16_t(e.len) <= e.fill) e.fill = uint16_t(e.len - (mx != hp ? 1 : 0));
        if (e.last == hp) {
            if (e.timer == 0) e.timer = 1;
        } else {
            e.last = int16_t(hp);
            e.timer = hp == 0 ? kTrailDelay : 0;
        }
        if (e.timer != 0) {
            if (e.fill < e.trail) {
                e.timer = int16_t(e.timer + 1);
                if ((frame_ & 1) && uint16_t(e.timer) > uint16_t(kTrailDelay)) e.trail = uint16_t(e.trail - 1);
            } else {
                e.trail = e.fill;
                e.timer = 0;
            }
        }
    }
    { // exe 0x416410: the action icon box (no special weapon)
        Element& e = e_[1];
        slideStep(e);
        bool run = true;
        if (p.activeSpecial == 0) {
            if (fr && HudSlide(e.state) == HudSlide::Hidden) e.state = uint8_t(HudSlide::SlideIn);
        } else if (HudSlide(e.state) != HudSlide::Hidden) {
            e.state = uint8_t(HudSlide::SlideOut);
        }
        if (HudSlide(e.state) == HudSlide::Hidden) run = false;
        if (run) {
            if (p.carryFlags == 0 && p.sub() != PlayerSub::Lift) { // nothing carried (the exe's word compare)
                parts_[kActionIconPart].u = 0x78;
                parts_[kActionIconPart].v = 0x90;
                e.clut = kHudClut;
                if (p.lockedActor) {
                    if (frame_ & 4) {
                        parts_[kActionIconPart].u = 0x88;
                        parts_[kActionIconPart].v = 0x80;
                    }
                    const Actor& target = actorLayoutOf(*p.lockedActor);
                    if (!(target.interaction & kInteractLift)) e.clut = kHudClutAlert;
                }
                parts_[kActionLabelPart].u = 0x50;
                parts_[kActionLabelPart].dx = -8;
                if (p.holds(PlayerButton::Special) && (frame_ & 4)) parts_[kActionLabelPart].u = 0x70;
            } else {
                parts_[kActionIconPart].u = 0x88;
                parts_[kActionIconPart].v = 0x80;
                e.clut = kHudClut;
                parts_[kActionLabelPart].u = 0x90;
                parts_[kActionLabelPart].dx = -4;
            }
        }
    }
    { // exe 0x416570: the special weapon gauge (shown while a special weapon is equipped)
        Element& e = e_[2];
        slideStep(e);
        if (p.activeSpecial == 0) {
            if (HudSlide(e.state) != HudSlide::Hidden) e.state = uint8_t(HudSlide::SlideOut);
        } else if (fr && HudSlide(e.state) == HudSlide::Hidden) {
            e.state = uint8_t(HudSlide::SlideIn);
        }
    }
}

void Hud::tickEnemyGauges() {
    if (!ready_) return;
    const bool fr = free();
    for (size_t i = 0; i < kHudEnemyGauges; ++i) {
        auto&& g = hudMemory().elements[kHudFirstEnemyGauge + i];
        if (g.flags == 0 ||
            (g.elementType() != HudElementType::BossGauge && g.elementType() != HudElementType::TargetGauge))
            continue;
        // exe 0x416870 / 0x416b90 (the same code): the actor shown (freed -> forgotten); without one the gauge slides
        // out and is freed once hidden; with one it slides in when free
        if (g.target && g.target->flags == 0) g.target = nullptr;
        Actor* actor = g.target.get();
        if (actor == nullptr) {
            if (g.slide() == HudSlide::Hidden) {
                g.flags = 0;
                continue; // (the game still runs the rest on the freed record: nothing visible)
            }
            g.setSlide(HudSlide::SlideOut);
        } else if (fr && g.slide() == HudSlide::Hidden) {
            g.setSlide(HudSlide::SlideIn);
        }
        // exe 0x416eb0
        Element e;
        e.type = g.type;
        e.state = g.state;
        e.x = g.x;
        if ((HudSlide(e.state) == HudSlide::SlideIn || HudSlide(e.state) == HudSlide::Shown) && !fr)
            e.state = uint8_t(HudSlide::SlideOut);
        else slide(e);
        g.state = e.state;
        g.x = e.x;
        const uint8_t shift = g.shift & 0x1f;
        int16_t fill = 0;
        if (actor == nullptr) {
            if (g.lastValue >= 0) fill = int16_t(g.lastValue >> shift);
            if (g.timer == 0) g.timer = 1;
        } else {
            const int16_t hp = actor->hitPoints;
            if (hp >= 0) fill = int16_t(hp >> shift);
            if (g.timer == 0 && g.lastValue != hp) g.timer = 1;
            g.lastValue = hp;
        }
        g.fill = uint16_t(fill);
        if (g.timer != 0) {
            if (g.fill < g.trail) {
                const uint16_t t = uint16_t(uint16_t(g.timer) + 1);
                g.timer = int16_t(t);
                if ((frame_ & 1) && t > uint16_t(kEnemyTrailDelay)) g.trail = uint16_t(g.trail - 1); // 0x6d0006 & 1
            } else {
                g.trail = g.fill;
                g.timer = 0;
            }
        }
    }
}

// exe 0x416f80(element, prims, y): the style's parts from (x, y): sprites step by (dx, -dy); a "stretched" part (type 0)
// is a 1-texel strip stretched along the element's length (+0x16 - style bias)
void Hud::frame(const Element& e, float y, std::vector<Prim>& out) const {
    const auto& st = style_[std::min<size_t>(e.style, style_.size() - 1)];
    const int orient = st[0], bias = st[1];
    float sx = e.x, sy = y;
    for (uint32_t i = 0; i < st[3] && st[2] + i < parts_.size(); ++i) {
        const Part& p = parts_[st[2] + i];
        Prim q;
        q.kind = Prim::Textured;
        q.clut = e.clut;
        q.colour = 0x808080;
        if (p.type == 0) {
            float w, h;
            if (orient == 0) {
                w = p.w;
                h = float(e.len - bias);
                sx += p.dx;
                sy -= h;
            } else if (orient == 1) {
                w = float(e.len - bias);
                h = p.h;
                sx -= w;
                sy += p.dy;
            } else {
                sx += p.dx;
                w = float(e.len - bias);
                h = p.h;
                sy += p.dy;
            }
            const float x0 = sx, y0 = sy;
            q.x[0] = q.x[2] = x0;
            q.x[1] = q.x[3] = x0 + w;
            q.y[0] = q.y[1] = y0;
            q.y[2] = q.y[3] = y0 + h;
            const float u0 = p.u, v0 = p.v, u1 = float(p.u + p.w), v1 = float(p.v + p.h);
            q.u[0] = q.u[2] = u0;
            q.u[1] = q.u[3] = u1;
            q.v[0] = q.v[1] = v0;
            q.v[2] = q.v[3] = v1;
            if (orient == 2) sx += w;
        } else { // 0x64 sprite
            sx += p.dx;
            sy -= p.dy;
            q.x[0] = q.x[2] = sx;
            q.x[1] = q.x[3] = sx + p.w;
            q.y[0] = q.y[1] = sy;
            q.y[2] = q.y[3] = sy + p.h;
            q.u[0] = q.u[2] = p.u;
            q.u[1] = q.u[3] = float(p.u + p.w);
            q.v[0] = q.v[1] = p.v;
            q.v[2] = q.v[3] = float(p.v + p.h);
        }
        out.push_back(q);
    }
}

std::vector<Prim> Hud::draw() const {
    std::vector<Prim> out;
    if (!ready_) return out;
    const Player& pl = player();
    // the OT prepends each element's chain: element 5 first, element 0 last (on top)
    for (int i = 5; i >= 0; --i) {
        if (size_t(i) >= kHudFirstEnemyGauge) { // the enemy gauges live in the game state (tickEnemyGauges)
            const HudElementRecord& g = hudMemory().elements[size_t(i)];
            if (!(g.flags & kHudElementDrawn) || g.slide() == HudSlide::Hidden) continue;
            Element e;
            e.type = g.type;
            e.index = g.index;
            e.style = g.style;
            e.state = g.state;
            e.x = g.x;
            e.clut = g.clut;
            e.len = g.length;
            const uint32_t col = g.colourRgb();
            const uint32_t r = col & 0xff, gr = (col >> 8) & 0xff, b = (col >> 16) & 0xff;
            const uint32_t dim = (r * 9 / 10) | (gr * 9 / 10) << 8 | (b * 9 / 10) << 16, trailColour = (col >> 1) & 0x7fffff;
            const int trail = int16_t(g.trail), fill = int16_t(g.fill);
            if (g.elementType() == HudElementType::TargetGauge) { // exe 0x416ca0: a bottom bar, frame at
                // y = (index + 0xd) * 0x10, the bars from x + 4 rightwards between y + 6 and y + 0xa: the trail (the
                // colour / 2), the fill (the colour * 9 / 10) and its bright line (y + 7 .. y + 9, the colour)
                const float y = float(e.index * 0x10);
                frame(e, y + 0xd0, out);
                const float left = float(e.x + 4);
                out.push_back(flat(left, y + 0xd6, left + float(trail), y + 0xda, trailColour));
                if (fill != 0) {
                    out.push_back(flat(left, y + 0xd6, left + float(fill), y + 0xda, dim));
                    out.push_back(flat(left, y + 0xd7, left + float(fill), y + 0xd9, col));
                }
                continue;
            }
            if (g.elementType() != HudElementType::BossGauge) continue;
            // exe 0x416980: the frame at y = index * 0x11 + 0x12, then the bars between y + 7 and y + 0xa ending at
            // x - 0x10: the damage trail (the colour / 2), the fill (the colour * 9 / 10) and its bright middle line
            // (y + 8 .. y + 9, the colour)
            const float y = float(e.index * 0x11);
            frame(e, y + 0x12, out);
            const float right = float(e.x - 0x10);
            out.push_back(flat(right - float(trail), y + 0x19, right, y + 0x1c, trailColour));
            if (fill != 0) {
                out.push_back(flat(right - float(fill), y + 0x19, right, y + 0x1c, dim));
                out.push_back(flat(right - float(fill), y + 0x1a, right, y + 0x1b, col));
            }
            continue;
        }
        const Element& e = e_[size_t(i)];
        if (!(e.flags & kHudElementDrawn) || HudSlide(e.state) == HudSlide::Hidden) continue;
        if (HudElementType(e.type) == HudElementType::LifeGauge) { // exe 0x4161b0
            frame(e, 0xe4, out);
            const float x0 = float(e.x + 0xd), x1 = float(e.x + 0x13), bottom = kLifeBottom;
            if (e.fill == 0) { // empty: the whole gauge pulses red
                const uint32_t t = frame_ & 0x1f;
                const uint32_t c = t < 0x11 ? t * 8 + 0x7f : 0x17f - t * 8;
                out.push_back(flat(x0, bottom - float(e.len), x1, bottom, c & 0xff));
            }
            const int ticks = pl.maxHitPoints >> 4; // one line per 16 HP (5 px)
            for (int k = 0; k + 1 < ticks; ++k) {
                Prim l;
                l.kind = Prim::Line;
                l.x[0] = x0;
                l.x[1] = x0 + 5;
                l.y[0] = l.y[1] = float(0xae - 5 * k);
                l.colour = 0x606030;
                out.push_back(l);
            }
            out.push_back(flat(x0, bottom - float(e.trail), x1, bottom, 0x00528c)); // the damage trail
            if (e.fill != 0) {
                out.push_back(flat(x0, bottom - float(e.fill), x1, bottom, 0x4a8ad6));
                out.push_back(flat(float(e.x + 0xe), bottom - float(e.fill), float(e.x + 0x12), bottom, 0x63deff));
            }
        } else if (HudElementType(e.type) == HudElementType::ActionIcon) { // exe 0x416520
            frame(e, 0xd6, out);
        } else if (HudElementType(e.type) == HudElementType::WeaponGauges) { // exe 0x4165c0: the special weapon's
            // ammunition (two bars: the current clip, the rest) and its energy
            frame(e, 0xe4, out);
            const int ammo = pl.gauge, clip = pl.shotCost, full = pl.gaugeMax;
            const float xa = float(e.x + 6);
            int top = 0xc9, a = 0, b = 0;
            if (ammo == full) {
                if (clip <= ammo) {
                    a = 0x40;
                    b = 0;
                } else {
                    a = 0;
                    b = 0x40;
                }
            } else if (full != 0 && clip != 0) {
                const int rem = ammo % clip;
                b = (rem * 0x40 + (full >> 1)) / full;
                if (b == 0 && rem > 0) b = 1;
                a = ((ammo - rem) * 0x40 + (full >> 1)) / full;
            }
            if (a != 0) {
                top = 0xc9 - a;
                out.push_back(flat(xa, float(top), xa + 4, 0xc9, 0x008000));
                out.push_back(flat(xa, float(top), xa + 3, 0xc9, 0x30f020));
            }
            if (b != 0) {
                const int t2 = top - b;
                out.push_back(flat(xa, float(t2), xa + 4, float(top), 0x003000));
                out.push_back(flat(xa, float(t2), xa + 3, float(top), 0x10b010));
            }
            const int emax = pl.energyReserve, en = pl.energyPool;
            const int h = emax == 0 ? 0x40 : (en * 0x40 - 1 + emax) / emax;
            out.push_back(flat(float(e.x + 0xc), float(0xc9 - h), float(e.x + 0x12), 0xc9, 0xc03010));
            out.push_back(flat(float(e.x + 0xd), float(0xc9 - h), float(e.x + 0x11), 0xc9, 0xf08040));
        }
    }
    return out;
}

std::vector<Prim> overlayDrawPass() {
    std::vector<Prim> out;
    HudBanner& b = hudBanner();
    if (b.shown == 0) return out;
    const int n = b.count;
    const HudBannerStyle style = HudBannerStyle(b.kind);
    const int8_t kind = b.kind;
    const uint32_t layout = b.layout.value();
    for (int i = 0; i < n; ++i) {
        if (!dataMapped(layout + uint32_t(i) * kBannerLayoutSpriteSize, kBannerLayoutSpriteSize)) break;
        const HudBannerLayoutSprite t = bannerLayoutSprite(layout, uint32_t(i));
        auto&& s = b.sprites[size_t(i)];
        const int16_t r0 = int16_t(s.radius);
        if (style == HudBannerStyle::SlideIn) {
            int16_t r = int16_t(r0 - 8);
            if (r < 0) r = 0;
            s.radius = uint16_t(r);
            s.angle = uint16_t((s.angle + 0x40u) & uint32_t(kAngleMask));
            s.x = t.x;
            s.y = int16_t(r * 2 + t.y);
        } else if (kind > 0 && kind < 3) {
            int16_t r = int16_t(r0 - 0xc);
            if (r < 0) r = 0;
            s.radius = uint16_t(r);
            const int a = s.angle & kAngleMask;
            const int sn = fixedSin(a), cs = fixedCos(a);
            s.x = int16_t(((sn * r) >> 11) + t.x);
            s.y = int16_t(((cs * r) >> 11) + t.y);
        }
        Prim q;
        q.kind = Prim::Textured;
        q.clut = b.clut;
        q.colour = 0x808080;
        q.semi = r0 != 0;
        const float x = float(int16_t(s.x + int16_t(b.x))), y = float(int16_t(s.y + int16_t(b.y)));
        const float u = float(t.u), v = float(t.v), w = float(t.w), h = float(t.h);
        q.x[0] = q.x[2] = x;
        q.x[1] = q.x[3] = x + w;
        q.y[0] = q.y[1] = y;
        q.y[2] = q.y[3] = y + h;
        q.u[0] = q.u[2] = u;
        q.u[1] = q.u[3] = u + w;
        q.v[0] = q.v[1] = v;
        q.v[2] = q.v[3] = v + h;
        out.push_back(q);
    }
    return out;
}

// exe 0x5f30e0 (its callers: the running scripts the content registers, ContentRegistry::scriptWhiteScreens): a
// white tile over the screen
std::vector<Prim> scriptWhiteDraw() {
    std::vector<Prim> out;
    const ScriptState& sc = scriptState();
    if (!(sc.flags & kScriptRunning)) return out;
    bool white = false;
    for (bool (*draws)() : od::port::content().scriptWhiteScreens)
        if (draws()) {
            white = true;
            break;
        }
    if (!white) return out;
    if (display().drawingOff != 0) return out;
    // the TILE 0x60ffffff at (0, 0), its size by the display mode k (320 x 240 at 0)
    const uint32_t k = display().mode;
    Prim q;
    q.kind = Prim::Flat;
    q.colour = 0xffffff;
    q.x[0] = q.x[2] = 0.0f;
    q.x[1] = q.x[3] = float(((k >> 1) + 1) * 5 << 6);
    q.y[0] = q.y[1] = 0.0f;
    q.y[2] = q.y[3] = float((((k + 1) >> 1) + 1) * 15 << 4);
    out.push_back(q);
    return out;
}

} // namespace od::hud

namespace od::port {

namespace {
// the banner's exe tables: the kinds and the kinds' layouts
const DataTable<RecordTable<HudBannerKind, 4, 8>> bannerKinds{TableId::HudBannerKinds};
// the kinds' sprite layouts (exe data 0x91bc6c): the layouts' addresses, as the banner stores them (the address is the record's identity)
const DataTable<ScalarTable<uint32_t, 4>> bannerLayouts{TableId::HudBannerLayouts};
constexpr uint8_t kBannerShown = 1;

// an enemy gauge already shows `actor`
bool gaugeShows(const Actor& actor) {
    for (size_t i = 0; i < kHudEnemyGauges; ++i) {
        const HudElementRecord& g = hudMemory().elements[kHudFirstEnemyGauge + i];
        if (g.flags != 0 && g.target == &actor) return true;
    }
    return false;
}
// the enemy gauges of `type` in use (a new one's rank)
uint8_t gaugesOfType(HudElementType type) {
    uint8_t n = 0;
    for (size_t i = 0; i < kHudEnemyGauges; ++i) {
        const HudElementRecord& g = hudMemory().elements[kHudFirstEnemyGauge + i];
        if (g.flags != 0 && g.elementType() == type) ++n;
    }
    return n;
}
// the colour's channels (each one's low bit cleared) and the shift, as the exe's byte stores (R, B, shift, G)
void setGaugeColour(HudElementRecord& g, uint32_t colour, uint8_t shift) {
    g.colour[0] = uint8_t(colour & kColourChannelMask);
    g.colour[2] = uint8_t((colour >> 16) & kColourChannelMask);
    g.shift = shift;
    g.colour[1] = uint8_t((colour >> 8) & kColourChannelMask);
}
} // namespace

void hudBannerShow(uint16_t x, uint16_t y, HudBannerStyle style) {
    const uint32_t kind = uint32_t(style);
    HudBanner& b = hudBanner();
    b.x = x;
    const uint32_t layout = bannerLayouts[kind];
    b.shown = kBannerShown;
    b.kind = int8_t(kind);
    b.progress = 0;
    b.y = y;
    const HudBannerKind k = bannerKinds[kind];
    const int16_t n = k.count;
    b.count = n;
    b.tpage = k.tpage;
    b.clut = k.clut;
    b.layout.pointAt(layout);
    for (int i = 0; i < n; ++i) {
        const HudBannerLayoutSprite t = bannerLayoutSprite(layout, uint32_t(i));
        b.sprites[size_t(i)].radius = t.radius;
        b.sprites[size_t(i)].angle = t.angle;
    }
}

void hudElementsInit() {
    HudMemory& h = hudMemory();
    h.clearStateWords();
    h.clearElementBytes();
    auto&& life = h.elements[0];
    auto&& action = h.elements[1];
    auto&& weapons = h.elements[2];
    Player& p = player();
    life.type = uint8_t(HudElementType::LifeGauge);
    life.style = uint8_t(HudElementType::LifeGauge);
    life.flags = kHudElementShown;
    life.target = &p;
    action.target = &p;
    weapons.target = &p;
    action.flags = kHudElementShown;
    weapons.flags = kHudElementShown;
    action.type = uint8_t(HudElementType::ActionIcon);
    action.style = uint8_t(HudElementType::ActionIcon);
    weapons.type = uint8_t(HudElementType::WeaponGauges);
    weapons.style = uint8_t(HudElementType::WeaponGauges);
    life.x = kLifeGaugeX;
    action.x = kRightElementsX;
    weapons.x = kRightElementsX;
    weapons.length = kWeaponGaugesLength;
    weapons.clut = hudCluts[hudParts[kWeaponGaugePart].clutIndex];
    gameState().enemyNear = 0;
}

void hudAddBossGauge(Actor& actor, uint8_t style, uint8_t shift, uint32_t colour) {
    if (gameState().frontEnd == kFrontEndDemo) return;
    if (gaugeShows(actor)) return;
    const uint8_t n = gaugesOfType(HudElementType::BossGauge);
    size_t i = 0;
    while (hudMemory().elements[kHudFirstEnemyGauge + i].flags != 0)
        if (++i > kHudEnemyGauges - 1) return;
    auto&& g = hudMemory().elements[kHudFirstEnemyGauge + i];
    g.flags = kHudElementShown;
    g.type = uint8_t(HudElementType::BossGauge);
    g.style = uint8_t(style + kBossGaugeStyles);
    g.index = n;
    g.clearTimerBytes();
    setGaugeColour(g, colour, shift);
    g.length = int16_t(actor.maxHitPoints >> (shift & 0x1f));
    g.target = &actor;
    g.x = int16_t(g.length + kBossGaugeX);
    g.trail = uint16_t(actor.hitPoints >> (shift & 0x1f));
    g.clut = hudCluts[hudParts[kBossGaugePart].clutIndex];
    g.lastValue = actor.hitPoints;
}

void hudAddTargetGauge(Actor& actor, uint8_t style, uint8_t shift, uint32_t colour) {
    if (gaugeShows(actor)) return;
    const uint8_t n = gaugesOfType(HudElementType::TargetGauge);
    for (size_t i = 0; i < kHudEnemyGauges; ++i) {
        auto&& g = hudMemory().elements[kHudFirstEnemyGauge + i];
        if (g.flags != 0) continue;
        g.type = uint8_t(HudElementType::TargetGauge);
        g.flags = kHudElementShown;
        g.style = uint8_t(style + kTargetGaugeStyles);
        g.index = n;
        g.clearTimerBytes();
        setGaugeColour(g, colour, shift);
        g.length = int16_t(actor.maxHitPoints >> (shift & 0x1f));
        g.target = &actor;
        g.trail = uint16_t(actor.hitPoints >> (shift & 0x1f));
        g.x = int16_t(kTargetGaugeX - g.length);
        g.clut = hudCluts[hudParts[kTargetGaugePart].clutIndex];
        g.lastValue = actor.hitPoints;
        return;
    }
}

} // namespace od::port
