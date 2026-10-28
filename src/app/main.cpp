// OpenDash M1: fixed 30 Hz simulation loop + input + a full room (scenery + floor) rendered in 3D
// with a free-fly camera.
// Usage: opendash.exe [--stage 08] [--sub N] [--cam x y z yaw pitch] [--shot out.bmp] [--menu 1]
//   F1: stage / sub-room browser (PageUp/PageDown stage, [ ] sub-room)
//   --sub: sub-room (sub-grid) to show, default 0; -1 = every sub-room overlaid (debug)
//   (stage in hex; --shot renders a few frames, saves the image and exits)
//   --audio 0: no sound device (the sound logic still runs silently; --shot runs never open one)
//   --audio-log 1: log every sound request / voice start and the mixer peak level once per second
//   --host [port]: netplay (app/netplay, docs/netplay.md): host a session on UDP port (default 27460; ini net=host,
//     net_port=)
//   --join ip:port: netplay: join the host there (ini net=join, net_join=)
//   --net-name NAME / --net-key KEY: this player's name (default the Windows user) / the session key (ini net_name=,
//     net_key=; the host refuses a different key)
//   --net-sim <latency ms> <jitter ms> <loss %>: test instrument: this end's half of a path with that one-way
//     latency, jitter and loss (the same on both ends: round trip 2 * latency)
//   --ghost-mirror <delay ticks> <dx> <dz>: dev / test instrument of the ghost view (app/netplay): a ghost slot plays
//     the local player's state of `delay` logic ticks ago, moved by (dx, dz) game units, through the same smoothing
//     buffer as a network peer
//   --ghost-look <costume> <variant> <weapon>: with --ghost-mirror, that ghost's look (Megaman body PL00P costume /
//     variant, special weapon > 2 = its PL00R arm) instead of the local player's
//   OD_TRACE_THROW=1 (environment, dev): the call stack of every C++ throw on stderr (named with a build that has
//     symbols, RelWithDebInfo)
#include "app/app.hpp"
#include "app/log_file.hpp"
#include "app/ui/prompts.hpp"

#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>

#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/game_state.hpp"
#include "content/registry.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>
#pragma comment(lib, "dbghelp.lib")
namespace {
// OD_TRACE_THROW: a vectored handler runs before the unwind, so it sees the thrower's frames
LONG CALLBACK traceThrow(EXCEPTION_POINTERS* e) {
    if (e->ExceptionRecord->ExceptionCode != 0xE06D7363) return EXCEPTION_CONTINUE_SEARCH; // a C++ throw
    void* frames[24];
    const USHORT n = CaptureStackBackTrace(0, 24, frames, nullptr);
    HANDLE p = GetCurrentProcess();
    alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
    for (USHORT i = 0; i < n; ++i) {
        auto* s = reinterpret_cast<SYMBOL_INFO*>(buf);
        s->SizeOfStruct = sizeof(SYMBOL_INFO);
        s->MaxNameLen = 255;
        DWORD64 d = 0;
        if (SymFromAddr(p, DWORD64(frames[i]), &d, s))
            std::fprintf(stderr, "      %s+0x%llx\n", s->Name, (unsigned long long)d);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace
#endif

namespace od::app {

int App::run(int argc, char** argv) {
    parseArgs(argc, argv);
    if (const int rc = init(); rc >= 0) return rc;
    while (running) frame();
    shutdown();
    return scriptFailed ? 1 : 0;
}

void App::frame() {
    if (scripted && (!playerView.playError.empty() || !titleScreen.error.empty())) {
        SDL_Log("scripted run failed: %s", !playerView.playError.empty() ? playerView.playError.c_str()
                                                                       : titleScreen.error.c_str());
        scriptFailed = true;
        running = false;
        return;
    }
    if (scripted && ++scriptedFrames > scriptedFrameLimit) {
        SDL_Log("scripted run: %llu frames without an end (limit), quitting", (unsigned long long)scriptedFrameLimit);
        running = false;
        scriptFailed = true;
    }
    handleEvents();
    now = SDL_GetTicksNS();
    // --shot: a fixed clock (half a sim step per frame) so images are reproducible
    const uint64_t step = ptSpeed > 0 && !playScript.empty() ? kSimStepNs * uint64_t(ptSpeed)
                          : shotPath.empty() ? now - last : kSimStepNs / 2;
    dt = float(step) * 1e-9f;
    last = now;

    if (paced()) { // the title / game frames run when the exe's pacer has them due (app/game_pacer)
        accumulator = 0;
        pacer.tick(step);
        while (paced() && pacer.next(od::port::display().pace)) simStep(1);
    } else { // the viewer: fixed 30 Hz steps
        accumulator += step;
        while (accumulator >= kSimStepNs) {
            simStep(0);
            accumulator -= kSimStepNs;
        }
    }
    rumble();
    updateCamera();
    updateScene();
    drawOverlays();
    if (showMenu) drawDebugMenu();
    present();
}

// The game's motors (game/player/player_input: what its vibration tick hands 0x409f90) on the gamepad: the small one
// is on / off (the PS1 pad's), the high-frequency motor at full; the large one's level on the low-frequency motor.
// Off while the game's frames do not run (the viewer, a pause, the window in the background); sent when it
// changes and again every half second (each send lasts a second, so a stalled loop stops it)
void App::rumble() {
    const uint16_t frame = od::port::gameState().frame;
    if (frame != rumbleFrame) {
        rumbleFrame = frame;
        rumbleFrameAt = now;
    }
    od::port::PadMotors m;
    if (paced() && windowFocused && now - rumbleFrameAt < 200'000'000ull) m = od::port::padMotors();
    if (!pad) return;
    const bool changed = m.small != rumbleSent.small || m.large != rumbleSent.large;
    if (!changed && ((m.small == 0 && m.large == 0) || now - rumbleSentAt < 500'000'000ull)) return;
    SDL_RumbleGamepad(pad, uint16_t(m.large * 257), m.small ? 0xffff : 0, 1000);
    rumbleSent = m;
    rumbleSentAt = now;
}

void App::handleEvents() {
    for (SDL_Event e; SDL_PollEvent(&e);) {
        ImGui_ImplSDL3_ProcessEvent(&e);
        notePromptEvent(e); // the device the button prompts show
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureMouse) menuMouse(e); // the mouse on the sub-screens
        if (e.type == SDL_EVENT_QUIT) running = false;
        if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) windowFocused = false;
        if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) windowFocused = true;
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_ESCAPE &&
            !io.WantTextInput) {
            // Esc opens the status sub-screen in the field (Start) and backs out of the sub-screens (cancel: a level
            // at a time, the last one closes them); it never quits (the window's close button does)
            if (playerView.play || titleScreen.active) {
                clickPress = (subScreenOpen() || titleScreen.active) ? 0x4000 : 0x8;
                clickDelay = 1;
            }
        }
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && !io.WantTextInput) {
            if ((e.key.scancode == SDL_SCANCODE_RETURN || e.key.scancode == SDL_SCANCODE_KP_ENTER) &&
                (e.key.mod & SDL_KMOD_ALT)) { // Alt+Enter: the window <-> the last full screen (borderless at first)
                Video v = currentVideo();
                v.display = v.display == DisplayMode::Windowed ? lastFullscreen : DisplayMode::Windowed;
                setVideo(v);
            }
            if (e.key.scancode == SDL_SCANCODE_F1) {
                showMenu = !showMenu;
                if (!showMenu) escReleased = false; // back to play: capture the mouse again
            }
            if (e.key.scancode == SDL_SCANCODE_F2) {
                if (playerView.play) playerView.leavePlay();
                else {
                    leaveTitle();
                    playerView.enterPlay(current);
                }
            }
            if (e.key.scancode == SDL_SCANCODE_PAGEDOWN) stepStage(+1);
            if (e.key.scancode == SDL_SCANCODE_PAGEUP) stepStage(-1);
            if (e.key.scancode == SDL_SCANCODE_RIGHTBRACKET) reload(stage, (sub + 1) % subCount(stage));
            if (e.key.scancode == SDL_SCANCODE_LEFTBRACKET)
                reload(stage, (sub + subCount(stage) - 1) % subCount(stage));
        }
        if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) {
            pad = SDL_OpenGamepad(e.gdevice.which);
            if (pad) {
                SDL_Log("gamepad: %s", SDL_GetGamepadName(pad));
                SDL_SetGamepadLED(pad, 0x20, 0x60, 0xff); // DualSense / DS4 light bar: Megaman blue
            }
        }
        if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad && e.gdevice.which == SDL_GetGamepadID(pad)) {
            SDL_CloseGamepad(pad);
            pad = nullptr;
            rumbleSent = {};
        }
        if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            width = e.window.data1;
            height = e.window.data2;
            renderer->resize(width, height);
        }
        // free camera (viewer): the right button looks around while held; play mode captures the mouse
        if (!playerView.play && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT &&
            !io.WantCaptureMouse)
            SDL_SetWindowRelativeMouseMode(window, true);
        if (!playerView.play && e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT)
            SDL_SetWindowRelativeMouseMode(window, false);
        if (playerView.play && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !io.WantCaptureMouse) {
            // a click in the window captures the mouse again; that click does not fire
            if (!SDL_GetWindowRelativeMouseMode(window) && e.button.button == SDL_BUTTON_LEFT && !pointerScreen())
                captureClick = true;
            escReleased = false;
        }
        if (e.type == SDL_EVENT_MOUSE_MOTION && SDL_GetWindowRelativeMouseMode(window)) {
            if (playerView.play) {
                // modern: the mouse orbits OpenDash's camera; classic: the game camera has no free look
                if (playerView.modern) playerView.orbit(e.motion.xrel * 0.004f, e.motion.yrel * 0.004f);
            } else {
                cam.yaw += e.motion.xrel * 0.004f;
                cam.pitch = std::clamp(cam.pitch - e.motion.yrel * 0.004f, -1.5f, 1.5f);
            }
        }
    }
    { // play mode keeps the mouse captured (relative mode) unless the F1 menu is open, the window lost the
      // focus or Esc released it; scripted --shot runs never capture
        // a sub-screen (status, shop, lists) shows the pointer: the mouse picks its rows
        const bool capture = playerView.play && !showMenu && windowFocused && !escReleased && shotPath.empty() &&
                             !pointerScreen();
        if (capture && !SDL_GetWindowRelativeMouseMode(window)) {
            SDL_SetWindowRelativeMouseMode(window, true);
            playCapture = true;
        } else if (!capture && playCapture) { // only release what play mode captured (not the viewer's look)
            SDL_SetWindowRelativeMouseMode(window, false);
            playCapture = false;
        }
    }
}

void App::shutdown() {
    netplay.stop(); // Bye to the other players
    // the mixer's SDL audio stream (and its callback thread) must go before SDL_Quit: SDL_Quit frees the audio
    // streams, and ~Mixer destroying its stream afterwards (at the end of main) crashed the exit (0xC0000005)
    od::port::setSoundBackend(nullptr);
    playerView.mixer.reset();
    if (!scripted && windowArgW == 0 && video.display == DisplayMode::Windowed) { // the size it was resized to
        video.window = windowPixels(window);
        videoSave(video);
    }
    renderer->imguiShutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    renderer.reset();
    if (pad) SDL_CloseGamepad(pad);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

} // namespace od::app

int main(int argc, char** argv) {
#ifdef _WIN32
    if (std::getenv("OD_TRACE_THROW")) {
        SymInitialize(GetCurrentProcess(), nullptr, TRUE);
        AddVectoredExceptionHandler(1, traceThrow);
    }
#endif
    od::app::startLogFile(); // opendash.log next to the executable, however the game was started
    od::content::registerAll(); // the game code dispatches into the content through its registry
    od::app::App app;
    return app.run(argc, argv);
}
