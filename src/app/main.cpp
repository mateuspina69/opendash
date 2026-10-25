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
#include "app/app.hpp"
#include "app/log_file.hpp"
#include "app/ui/prompts.hpp"
#include "engine/config.hpp"

#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <string_view>

#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "content/registry.hpp"

namespace od::app {

int App::run(int argc, char** argv) {
    parseArgs(argc, argv);
    if (const int rc = init(); rc >= 0) return rc;
    while (running) frame();
    shutdown();
    return 0;
}

void App::frame() {
    if (scripted && ++scriptedFrames > scriptedFrameLimit) {
        SDL_Log("scripted run: %llu frames without an end (limit), quitting", (unsigned long long)scriptedFrameLimit);
        running = false;
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
    updateCamera();
    updateScene();
    drawOverlays();
    drawNetOverlay();
    if (showMenu) drawDebugMenu();
    present();
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
    // --config <file>: that ini instead of opendash.ini (its log= names the log file)
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string_view(argv[i]) == "--config") od::setConfigPath(argv[i + 1]);
    if (const char* base = SDL_GetBasePath()) od::setConfigDir(base);
    od::app::startLogFile(od::loadConfig().log); // log=, else opendash.log next to the executable
    od::content::registerAll(); // the game code dispatches into the content through its registry
    od::app::App app;
    return app.run(argc, argv);
}
