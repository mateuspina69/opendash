// The photo mode in the host (app/photo_mode): opening / closing it, its input and camera, its controls on the
// canvas, the saved picture. While it is open frame() runs no tick (the game's state does not move), the sound is held,
// no event reaches the game; closing waits for the buttons to come up before the game runs again.
#include "app/app.hpp"
#include "app/screenshot.hpp"
#include "app/ui/prompts.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <ctime>

#include "audio/mixer.hpp"

namespace od::app {

namespace {
constexpr float kMouseLook = 0.004f;                        // radians a pixel (as the viewer's free camera)
constexpr float kPadYaw = 2.2f, kPadPitch = 1.5f;           // radians a second at full stick
constexpr float kRollRate = 0.8f, kZoomRate = 1.2f;         // radians / zoom steps a second
constexpr float kWheelZoom = 0.12f;                         // a wheel notch
constexpr float kStickDead = 0.15f;
constexpr uint64_t kSavedShownNs = 3'000'000'000ull;

float stick(SDL_Gamepad* pad, SDL_GamepadAxis a) {
    if (!pad) return 0.0f;
    const float v = float(SDL_GetGamepadAxis(pad, a)) / 32767.0f;
    return std::abs(v) < kStickDead ? 0.0f : (v - std::copysign(kStickDead, v)) / (1.0f - kStickDead);
}
bool padHeld(SDL_Gamepad* pad, SDL_GamepadButton b) { return pad && SDL_GetGamepadButton(pad, b); }
} // namespace

bool App::photoAllowed() const {
    return !showMenu && photoModeAllowed(playerView.play, titleScreen.active || titleScreen.demoShown,
                                         netplay.sessionActive() || playerView.ownWorld != nullptr,
                                         scripted || !shotPath.empty());
}

void App::photoEnter() {
    // the speed: the camera's distance to the player (the room drawn << shift, y up), a second to cross it
    const float scale = float(1 << current.shift);
    const float p[3] = {playerView.posCur[0] * scale, -playerView.posCur[1] * scale, playerView.posCur[2] * scale};
    float d2 = 0.0f;
    for (int c = 0; c < 3; ++c) d2 += (cam.eye[c] - p[c]) * (cam.eye[c] - p[c]);
    photo.enter(cam, std::max(std::sqrt(d2), 200.0f * scale));
    photo.hint = true;
    photoWheel = 0.0f;
    photoLook[0] = photoLook[1] = 0.0f;
    photoEnteredNs = SDL_GetTicksNS();
    if (playerView.mixer) playerView.mixer->setPaused(true);
    SDL_Log("photo mode: on (tick %llu)", (unsigned long long)playerView.logicTicks);
}

void App::photoLeave() {
    photo.leave();
    SDL_Log("photo mode: off");
}

void App::photoResume() {
    if (!photo.resuming) return;
    const bool mouseHeld = (SDL_GetMouseState(nullptr, nullptr) & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)) != 0;
    if (pollButtons(pad, true, false, false) != 0 || mouseHeld ||
        padHeld(pad, SDL_GAMEPAD_BUTTON_TOUCHPAD) || padHeld(pad, SDL_GAMEPAD_BUTTON_MISC1))
        return; // a button of the mode still down: it would reach the game as a press
    photo.resuming = false;
    tapLatch = 0;
    clickPress = 0;
    const uint64_t held = SDL_GetTicksNS() - photoEnteredNs;
    playerView.holdClock(held);
    subtitles.holdClock(held);
    if (playerView.mixer) playerView.mixer->setPaused(false);
}

bool App::photoEvent(const SDL_Event& e) {
    const bool typing = ImGui::GetIO().WantTextInput;
    if (!photo.active) {
        bool open = false;
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && !typing && e.key.scancode == SDL_SCANCODE_P) open = true;
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            const auto b = SDL_GamepadButton(e.gbutton.button);
            open = b == SDL_GAMEPAD_BUTTON_TOUCHPAD || b == SDL_GAMEPAD_BUTTON_MISC1 || // the touchpad / share
                   (b == SDL_GAMEPAD_BUTTON_LEFT_STICK && padHeld(pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK)) || // L3 + R3
                   (b == SDL_GAMEPAD_BUTTON_RIGHT_STICK && padHeld(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK));
        }
        if (!open || !photoAllowed()) return false;
        photoEnter();
        return true;
    }
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
        if (e.key.repeat) return true;
        if (e.key.scancode == SDL_SCANCODE_P || e.key.scancode == SDL_SCANCODE_ESCAPE) photoLeave();
        else if (e.key.scancode == SDL_SCANCODE_H) photo.hint = !photo.hint;
        else if (e.key.scancode == SDL_SCANCODE_RETURN || e.key.scancode == SDL_SCANCODE_KP_ENTER) photo.capture = true;
        return true;
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        return true;
    case SDL_EVENT_MOUSE_MOTION:
        if (SDL_GetWindowRelativeMouseMode(window)) {
            photoLook[0] += e.motion.xrel * kMouseLook;
            photoLook[1] -= e.motion.yrel * kMouseLook;
        }
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        photoWheel += e.wheel.y;
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT && SDL_GetWindowRelativeMouseMode(window)) photo.capture = true;
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        switch (SDL_GamepadButton(e.gbutton.button)) {
        case SDL_GAMEPAD_BUTTON_EAST:
        case SDL_GAMEPAD_BUTTON_TOUCHPAD:
        case SDL_GAMEPAD_BUTTON_MISC1: photoLeave(); break;
        case SDL_GAMEPAD_BUTTON_SOUTH: photo.capture = true; break;
        case SDL_GAMEPAD_BUTTON_NORTH: photo.hint = !photo.hint; break;
        default: break;
        }
        return true;
    default:
        return false; // the window's own events (quit, size, focus, devices) go on
    }
}

void App::photoCamera() {
    PhotoInput in;
    if (!ImGui::GetIO().WantCaptureKeyboard) {
        const bool* k = SDL_GetKeyboardState(nullptr);
        in.move[0] = float(k[SDL_SCANCODE_D]) - float(k[SDL_SCANCODE_A]);
        in.move[1] = float(k[SDL_SCANCODE_E]) - float(k[SDL_SCANCODE_Q]);
        in.move[2] = float(k[SDL_SCANCODE_W]) - float(k[SDL_SCANCODE_S]);
        in.roll = (float(k[SDL_SCANCODE_X]) - float(k[SDL_SCANCODE_Z])) * kRollRate * dt;
        in.fast = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
    }
    if (pad) {
        in.move[0] += stick(pad, SDL_GAMEPAD_AXIS_LEFTX);
        in.move[2] -= stick(pad, SDL_GAMEPAD_AXIS_LEFTY);
        in.move[1] += float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) - SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) / 32767.0f;
        in.look[0] += stick(pad, SDL_GAMEPAD_AXIS_RIGHTX) * kPadYaw * dt;
        in.look[1] -= stick(pad, SDL_GAMEPAD_AXIS_RIGHTY) * kPadPitch * dt;
        in.roll += (float(padHeld(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) - float(padHeld(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))) *
                   kRollRate * dt;
        in.zoom += (float(padHeld(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) - float(padHeld(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))) *
                   kZoomRate * dt;
        in.fast = in.fast || padHeld(pad, SDL_GAMEPAD_BUTTON_WEST);
    }
    for (int c = 0; c < 3; ++c) in.move[c] = std::clamp(in.move[c], -1.0f, 1.0f);
    in.look[0] += photoLook[0];
    in.look[1] += photoLook[1];
    in.zoom += photoWheel * kWheelZoom;
    photoLook[0] = photoLook[1] = 0.0f;
    photoWheel = 0.0f;
    photo.step(in, dt);
    cam = photo.cam;
    camSource = "photo";
}

void App::photoHint() {
    const float h = float(height), px = h * 0.024f, row = h * 0.036f, x = h * 0.04f, keyW = h * 0.17f;
    const ui::Color white = ui::rgba(236, 240, 246), dim = ui::rgba(170, 180, 196);
    auto word = [&](const char* key) {
        const std::string* w = vectorUi.word(key);
        return w ? *w : std::string();
    };
    if (photoSavedAt && SDL_GetTicksNS() - photoSavedAt < kSavedShownNs)
        vectorUi.textLeft(*renderer, canvas, word("photo.saved") + "  " + photoSavedName, x, h * 0.06f, px, white,
                          float(width) - 2.0f * x);
    if (!photo.hint) return;
    struct Row {
        std::string key;     // a key's name (keyboard)
        int pads[2];         // pad buttons (padButtonDown codes; 0 none)
        const char* action;  // its words
    };
    const bool keys = promptDevice() == PromptDevice::Keyboard;
    std::vector<Row> rows;
    if (keys)
        rows = {{"W A S D", {0, 0}, "photo.move"},   {word("photo.mouse"), {0, 0}, "photo.look"},
                {"E / Q", {0, 0}, "photo.updown"},   {word("photo.wheel"), {0, 0}, "photo.zoom"},
                {"Z / X", {0, 0}, "photo.roll"},     {"Shift", {0, 0}, "photo.fast"},
                {"Enter", {0, 0}, "photo.save"},     {"H", {0, 0}, "photo.hide"},
                {"P / Esc", {0, 0}, "photo.exit"}};
    else
        rows = {{word("photo.lstick"), {0, 0}, "photo.move"}, {word("photo.rstick"), {0, 0}, "photo.look"},
                {"", {1, 6}, "photo.updown"},                 {"", {100, 0}, "photo.zoom"},
                {"", {7, 8}, "photo.roll"},                   {"", {4, 0}, "photo.fast"},
                {"", {2, 0}, "photo.save"},                   {"", {5, 0}, "photo.hide"},
                {"", {3, 0}, "photo.exit"}};
    const float top = h - h * 0.05f - row * float(rows.size() + 1);
    canvas.roundRect({x * 0.5f, top - row * 0.9f}, {x + keyW + h * 0.24f, h - h * 0.05f + row * 0.2f}, h * 0.012f,
                     ui::rgba(8, 10, 18, 150));
    vectorUi.textLeft(*renderer, canvas, word("photo.title"), x, top, px * 1.15f, white, keyW + h * 0.22f, true);
    float y = top + row * 1.2f;
    for (const Row& r : rows) {
        if (!r.key.empty()) vectorUi.textLeft(*renderer, canvas, r.key, x, y, px, white, keyW - h * 0.01f, true);
        float bx = x;
        for (int code : r.pads)
            if (code) bx += vectorUi.padButton(*renderer, canvas, code, bx, y - px * 0.35f, row * 0.8f) + h * 0.006f;
        vectorUi.textLeft(*renderer, canvas, word(r.action), x + keyW, y, px, dim, h * 0.22f);
        y += row;
    }
}

void App::photoSave() {
    std::vector<uint8_t> px;
    renderer->draw(drawCam, &px, {}); // no overlay: the picture alone (this frame shows without the controls)
    const std::time_t t = std::time(nullptr);
    std::tm when{};
#ifdef _WIN32
    localtime_s(&when, &t);
#else
    localtime_r(&t, &when);
#endif
    const std::filesystem::path path = photoPath(photosDir, when);
    if (savePhoto(path.string(), width, height, px)) {
        photoSavedAt = SDL_GetTicksNS();
        photoSavedName = path.filename().string();
    }
    photo.capture = false;
}

} // namespace od::app
