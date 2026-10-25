#include "app/ui/prompts.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "app/input.hpp"

namespace od::app {

namespace {

PromptDevice gDevice = PromptDevice::Keyboard;

PromptDevice family(SDL_JoystickID id) {
    switch (SDL_GetGamepadTypeForID(id)) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return PromptDevice::PlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return PromptDevice::Nintendo;
    default: return PromptDevice::Xbox; // Xbox and the unknown pads: the most common labels
    }
}

std::vector<ui::Vec2> circlePts(float cx, float cy, float r, int n = 28) {
    std::vector<ui::Vec2> p;
    for (int i = 0; i < n; ++i) {
        const float a = 6.2831853f * float(i) / float(n);
        p.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
    }
    return p;
}

void disc(ui::Canvas& cv, float cx, float cy, float r, ui::Color c) {
    const auto p = circlePts(cx, cy, r);
    cv.convexFillAA(p.data(), int(p.size()), c);
}

void roundRect(ui::Canvas& cv, float x0, float y0, float x1, float y1, float r, ui::Color c) {
    std::vector<ui::Vec2> p;
    r = std::min({r, (x1 - x0) * 0.5f, (y1 - y0) * 0.5f});
    const float cx[4] = {x1 - r, x1 - r, x0 + r, x0 + r}, cy[4] = {y0 + r, y1 - r, y1 - r, y0 + r};
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i <= 6; ++i) {
            const float a = 6.2831853f * (float(k) * 0.25f - 0.25f + 0.25f * float(i) / 6.0f);
            p.push_back({cx[k] + std::cos(a) * r, cy[k] + std::sin(a) * r});
        }
    cv.convexFillAA(p.data(), int(p.size()), c);
}

// a polyline of `pts` (closed when `loop`) as round-ended strokes
void stroke(ui::Canvas& cv, const std::vector<ui::Vec2>& pts, bool loop, float w, ui::Color c) {
    const size_t n = pts.size();
    for (size_t i = 0; i + (loop ? 0 : 1) < n; ++i) {
        const ui::Vec2 a = pts[i], b = pts[(i + 1) % n];
        const float dx = b.x - a.x, dy = b.y - a.y, l = std::sqrt(dx * dx + dy * dy);
        if (l <= 0.0f) continue;
        const float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f, ex = dx / l * w * 0.5f, ey = dy / l * w * 0.5f;
        const ui::Vec2 q[4] = {{a.x + nx - ex, a.y + ny - ey}, {b.x + nx + ex, b.y + ny + ey},
                               {b.x - nx + ex, b.y - ny + ey}, {a.x - nx - ex, a.y - ny - ey}};
        cv.convexFillAA(q, 4, c);
    }
}

// the pad button bound to PS1 bit `ps1` (1..10, OpenDash's numbering: input.hpp padButtonDown), 0 = none
int padButtonOf(uint16_t ps1) {
    const od::port::BindingTable<od::port::kPadBindings>& padTable = od::port::inputBindings().pad;
    for (size_t i = 0; i < padTable.size(); ++i)
        if (padTable.bindings[i].ps1Bit & ps1) return padTable.bindings[i].code;
    switch (ps1) { // the exe's default table 0x8478f0
    case 0x200: return 1;
    case 0x4000: return 2;
    case 0x2000: return 3;
    case 0x8000: return 4;
    case 0x1000: return 5;
    case 0x100: return 6;
    case 0x400: return 7;
    case 0x800: return 8;
    case 0x8: return 9;
    case 0x1: return 10;
    default: return 0;
    }
}

// the key bound to PS1 bit `ps1` as a cap label
std::string keyOf(uint16_t ps1) {
    uint16_t dik = 0;
    const od::port::BindingTable<od::port::kKeyBindings>& keyTable = od::port::inputBindings().keys;
    for (size_t i = 0; i < keyTable.size() && !dik; ++i)
        if (keyTable.bindings[i].ps1Bit & ps1) dik = keyTable.bindings[i].code;

    switch (dik) {
    case 0x01: return "ESC";
    case 0x1c: return "ENTER";
    case 0x0e: return "BKSP";
    case 0x0f: return "TAB";
    case 0x39: return "SPACE";
    case 0x1d: case 0x9d: return "CTRL";
    case 0x2a: case 0x36: return "SHIFT";
    case 0: break;
    default:
        if (const SDL_Scancode sc = dikScancode(dik); sc != SDL_SCANCODE_UNKNOWN) {
            std::string n = SDL_GetScancodeName(sc);
            for (char& ch : n) ch = char(std::toupper(static_cast<unsigned char>(ch)));
            return n;
        }
    }
    switch (ps1) { // OpenDash's fixed keys (input.hpp pollButtons)
    case 0x8: return "ENTER";
    case 0x1: return "BKSP";
    case 0x2000: return "K";
    case 0x8000: return "J";
    default: return "?";
    }
}

} // namespace

PromptDevice promptDevice() { return gDevice; }

void notePromptEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_DOWN: gDevice = PromptDevice::Keyboard; break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: gDevice = family(e.gbutton.which); break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (std::abs(int(e.gaxis.value)) > 20000) gDevice = family(e.gaxis.which);
        break;
    default: break;
    }
}

float drawPrompt(ui::Canvas& cv, UiText& text, uint16_t ps1, float x, float y, float h) {
    const float r = h * 0.5f;
    const ui::Color plate = ui::rgba(0x1e, 0x26, 0x30), rim = ui::rgba(0xd8, 0xe0, 0xe8);
    auto label = [&](const char* s, ui::Color c, float cx, float cap) {
        const float px = text.em(cap);
        text.draw(cv, s, cx - text.width(s, px) * 0.5f, y + cap * 0.5f, px, c);
    };
    if (gDevice == PromptDevice::Keyboard) {
        const std::string k = keyOf(ps1);
        const float cap = h * 0.42f, px = text.em(cap);
        const float w = std::max(h, text.width(k, px) + h * 0.55f);
        roundRect(cv, x, y - r, x + w, y + r, h * 0.22f, ui::rgba(0x8c, 0x98, 0xa8));
        roundRect(cv, x + h * 0.07f, y - r + h * 0.07f, x + w - h * 0.07f, y + r - h * 0.14f, h * 0.17f,
                  ui::rgba(0xee, 0xf1, 0xf4));
        text.draw(cv, k, x + (w - text.width(k, px)) * 0.5f, y + cap * 0.5f - h * 0.035f, px, ui::rgba(0x20, 0x28, 0x34));
        return w;
    }
    const int n = padButtonOf(ps1);
    const float cx = x + r;
    if (n >= 2 && n <= 5) { // the face buttons: 2 south, 3 east, 4 west, 5 north
        disc(cv, cx, y, r, rim);
        disc(cv, cx, y, r * 0.86f, plate);
        if (gDevice == PromptDevice::PlayStation) {
            const float s = r * 0.48f, w = h * 0.085f;
            if (n == 5) { // triangle
                stroke(cv, {{cx, y - s}, {cx + s * 0.95f, y + s * 0.62f}, {cx - s * 0.95f, y + s * 0.62f}}, true, w,
                       ui::rgba(0x3f, 0xd9, 0xa6));
            } else if (n == 3) { // circle
                stroke(cv, circlePts(cx, y, s * 0.82f, 24), true, w, ui::rgba(0xff, 0x5f, 0x6d));
            } else if (n == 2) { // cross
                const ui::Color c = ui::rgba(0x7d, 0x9c, 0xff);
                stroke(cv, {{cx - s * 0.75f, y - s * 0.75f}, {cx + s * 0.75f, y + s * 0.75f}}, false, w, c);
                stroke(cv, {{cx + s * 0.75f, y - s * 0.75f}, {cx - s * 0.75f, y + s * 0.75f}}, false, w, c);
            } else { // square
                const float q = s * 0.7f;
                stroke(cv, {{cx - q, y - q}, {cx + q, y - q}, {cx + q, y + q}, {cx - q, y + q}}, true, w,
                       ui::rgba(0xff, 0x7f, 0xd1));
            }
        } else if (gDevice == PromptDevice::Xbox) {
            static const char* kL[4] = {"A", "B", "X", "Y"};
            static const ui::Color kC[4] = {ui::rgba(0x5d, 0xc2, 0x1e), ui::rgba(0xe2, 0x31, 0x2a),
                                            ui::rgba(0x2d, 0x7f, 0xe0), ui::rgba(0xf1, 0xc4, 0x0f)};
            label(kL[n - 2], kC[n - 2], cx, h * 0.46f);
        } else { // Nintendo: the letters by position (south B, east A, west Y, north X)
            static const char* kL[4] = {"B", "A", "Y", "X"};
            label(kL[n - 2], ui::rgba(0xf4, 0xf4, 0xf4), cx, h * 0.46f);
        }
        return h;
    }
    // shoulders, triggers, start / select: a pill with the family's name
    static const char* kPs[11] = {"", "R2", "", "", "", "", "L2", "L1", "R1", "OPTIONS", "SHARE"};
    static const char* kXb[11] = {"", "RT", "", "", "", "", "LT", "LB", "RB", "MENU", "VIEW"};
    static const char* kNi[11] = {"", "ZR", "", "", "", "", "ZL", "L", "R", "+", "-"};
    const char* s = n >= 1 && n <= 10
                        ? (gDevice == PromptDevice::PlayStation ? kPs[n] : gDevice == PromptDevice::Xbox ? kXb[n] : kNi[n])
                        : "?";
    const float cap = h * 0.4f, px = text.em(cap);
    const float w = std::max(h, text.width(s, px) + h * 0.6f);
    roundRect(cv, x, y - r, x + w, y + r, r, rim);
    roundRect(cv, x + h * 0.07f, y - r + h * 0.07f, x + w - h * 0.07f, y + r - h * 0.07f, r * 0.86f, plate);
    text.draw(cv, s, x + (w - text.width(s, px)) * 0.5f, y + cap * 0.5f, px, ui::rgba(0xf0, 0xf0, 0xf0));
    return w;
}

} // namespace od::app
