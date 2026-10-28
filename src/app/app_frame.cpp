#include "app/app.hpp"
#include "app/dat_cache.hpp"
#include "app/screenshot.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <cstdlib>
#include <set>
#include <sstream>

#include "app/sky.hpp"
#include "content/late_game/st54_clear_screen.hpp"
#include "engine/dat.hpp"
#include "engine/data_source.hpp"
#include "game/vectors.hpp"
#include "engine/lang.hpp"
#include "engine/screen_prim.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actors.hpp"
#include "game/draw_commands.hpp"
#include "app/ui/choice_mouse.hpp"
#include "game/choice_pointer.hpp"
#include "game/menu_draw.hpp"
#include "game/menu_pointer.hpp"
#include "game/message.hpp"
#include "game/camera_state.hpp"
#include "game/fade.hpp"
#include "game/walker.hpp"
#include "render/render_logic.hpp"
#include <imgui_impl_sdl3.h>
#include "game/player/player_state.hpp"
#include "content/intro/fire_mission.hpp"
#include "content/nino/ruin_effects.hpp"
#include "content/nino/screen_ripple.hpp"
#include "game/display.hpp"
#include "game/door_actor.hpp"
#include "game/event_state.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/message_state.hpp"
#include "game/room_scripts.hpp"
#include "game/script_state.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"


namespace od::app {

bool App::pointerScreen() const { return subScreenOpen() || (playerView.play && od::port::choiceWindow()); }

bool App::subScreenOpen() const {
    return playerView.play && (!playerView.menuPrims.empty() || !playerView.shopPrims.empty() ||
                               !playerView.devPrims.empty() || !playerView.entryPrims.empty());
}

int App::menuRowAt(float x, float y) const {
    const float s = float(height) / 240.0f, ox = (float(width) - 320.0f * s) * 0.5f;
    const float px = (x - ox) / s, py = y / s; // the 320 x 240 screen
    int row = -1;
    for (const auto* prims : {&playerView.menuPrims, &playerView.shopPrims, &playerView.devPrims, &playerView.entryPrims})
        for (const od::port::ScreenPrim& p : *prims)
            if (p.row >= 0 && px >= p.x && px < p.x + p.w && py >= p.y && py < p.y + p.h) row = p.row; // the topmost
    return row;
}

void App::menuMouse(const SDL_Event& e) {
    if (titleScreen.active && !playerView.play && titleMouse(e)) return; // the title's menus
    if (playerView.play && !SDL_GetWindowRelativeMouseMode(window) &&
        choiceMouse(e, SDL_GetWindowPixelDensity(window), float(height) / 240.0f,
                    (float(width) - 320.0f * float(height) / 240.0f) * 0.5f, hoverRow, clickPress, clickDelay))
        return; // a message window's choice (app/ui/choice_mouse)
    if (!subScreenOpen() || SDL_GetWindowRelativeMouseMode(window)) {
        hoverRow = -1;
        return;
    }
    const float density = SDL_GetWindowPixelDensity(window);
    if (e.type == SDL_EVENT_MOUSE_MOTION) {
        const int row = menuRowAt(e.motion.x * density, e.motion.y * density);
        if (row >= 0 && row != hoverRow) { // onto a new row: the cursor follows
            od::port::menuPointer().row = row;
            od::port::menuPointer().moved = true;
        }
        hoverRow = row;
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        const int row = menuRowAt(e.button.x * density, e.button.y * density);
        if (e.button.button == SDL_BUTTON_LEFT && row >= 0) { // onto the row, then its confirm
            od::port::menuPointer().row = row;
            od::port::menuPointer().moved = true;
            if (row >= 0x100) { // a scrolling list's cell: the list confirms once its cursor is there
                od::port::menuPointer().click = true;
            } else {
                clickPress = 0x2000;
                clickDelay = 2;
            }
        } else if (e.button.button == SDL_BUTTON_RIGHT) { // back
            clickPress = 0x4000;
            clickDelay = 1;
        }
    } else if (e.type == SDL_EVENT_MOUSE_WHEEL && e.wheel.y != 0.0f) { // the wheel: up / down
        clickPress = e.wheel.y > 0.0f ? 0x10 : 0x40;
        clickDelay = 1;
    }
}

bool App::titleMouse(const SDL_Event& e) {
    const float density = SDL_GetWindowPixelDensity(window);
    auto at = [&](float x, float y) {
        return titleScreen.rowAt(float(width), float(height), x * density, y * density);
    };
    if (e.type == SDL_EVENT_MOUSE_MOTION) {
        const int row = at(e.motion.x, e.motion.y);
        if (row >= 0 && row != hoverRow) od::port::menuPointer() = {row, true, false};
        hoverRow = row;
        return true;
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        const int row = at(e.button.x, e.button.y);
        if (e.button.button == SDL_BUTTON_LEFT && row >= 0) { // onto the row, then its confirm
            const bool cell = row >= od::port::kListCell; // a list's cell: the list confirms once it is there
            od::port::menuPointer() = {row, true, cell};
            if (!cell) {
                clickPress = 0x2000;
                clickDelay = 2;
            }
        } else if (e.button.button == SDL_BUTTON_LEFT &&
                   std::none_of(titleScreen.prims.begin(), titleScreen.prims.end(),
                                [](const od::port::ScreenPrim& p) { return p.row >= 0; })) {
            clickPress = 0x2000; // a screen without rows ("press enter", the logos): a click is the confirm
            clickDelay = 1;
        } else if (e.button.button == SDL_BUTTON_RIGHT) {
            clickPress = 0x4000;
            clickDelay = 1;
        }
        return true;
    }
    return false;
}

void App::simStep(int gameFrames) {
    const bool captured = playerView.play && SDL_GetWindowRelativeMouseMode(window);
    const SDL_MouseButtonFlags mouse = SDL_GetMouseState(nullptr, nullptr);
    const bool aimHeld = captured && (mouse & SDL_BUTTON_RMASK);
    if (!(mouse & SDL_BUTTON_LMASK)) captureClick = false;
    const bool fireHeld = captured && (mouse & SDL_BUTTON_LMASK) && !captureClick;
    const uint32_t held = pollButtons(pad, playerView.play || titleScreen.active, aimHeld, fireHeld);
    input.pressed = held & ~input.held;
    input.held = held;
    pollStick(pad, input);
    if ((input.held & kStart) && !playerView.play && !titleScreen.active && !titleScreen.inGame) running = false;
    paceFrames = gameFrames; // the title task / the game loop (paced by the exe's pacer, app/game_pacer)
    bool netStepped = false; // netplay's step ran this sim step (after the tick loop in play)
    if (titleScreen.active) { // the main task / title task: logical buttons -> PS1 pad bits
        static constexpr std::pair<uint32_t, uint16_t> kPs1t[] = {
            {kUp, 0x10}, {kRight, 0x20}, {kDown, 0x40}, {kLeft, 0x80}, {kCross, 0x4000}, {kSquare, 0x8000},
            {kCircle, 0x2000}, {kTriangle, 0x1000}, {kL1, 0x400}, {kR1, 0x800}, {kStart, 0x8},
            {kL2, 0x100}, {kR2, 0x200}, {kSelect, 0x1}};
        uint16_t ps1 = 0;
        for (auto [bit, b] : kPs1t)
            if (input.held & bit) ps1 |= b;
        if (showMenu) ps1 = 0; // the F1 window has the keyboard
        if (clickPress && --clickDelay <= 0) { // Esc on the title screens: cancel for one step
            ps1 |= clickPress;
            clickPress = 0;
        }
        if (playScriptPos < playScript.size()) ps1 = playScript[playScriptPos++];
        titleStep(ps1);
    }
    ++animTick; // viewer animation clock
    playerView.tick();
    if (!playerView.play && !titleScreen.active && playScriptPos < playScript.size() &&
        playScript[playScriptPos] != kPtCalm && playScript[playScriptPos] != kPtMash)
        ++playScriptPos; // keep scripts moving (a playthrough wait marker waits for play)
    if (playerView.play) { // logical buttons -> PS1 pad bits
        static constexpr std::pair<uint32_t, uint16_t> kPs1[] = {
            {kUp, 0x10}, {kRight, 0x20}, {kDown, 0x40}, {kLeft, 0x80}, {kCross, 0x4000}, {kSquare, 0x8000},
            {kCircle, 0x2000}, {kTriangle, 0x1000}, {kL1, 0x400}, {kR1, 0x800}, {kStart, 0x8},
            {kL2, 0x100}, {kR2, 0x200}, {kSelect, 0x1}};
        uint16_t ps1 = 0;
        for (auto [bit, b] : kPs1)
            if (input.held & bit) ps1 |= b;
        if (clickPress && --clickDelay <= 0) { // a menu click / Esc: its press for one step (the game sees the edge)
            if (pointerScreen() || clickPress == 0x8) ps1 |= clickPress;
            clickPress = 0;
        }
        bool aimApproach = false, aimBack = false;
        uint16_t aimSide = 0;
        uint16_t aimPad = 0;
        botStep(aimApproach, aimBack, aimSide, aimPad); // the dev bots of --playscript (app/playthrough)
        if (const auto tp = playScriptTeleports.find(playScriptPos); tp != playScriptTeleports.end()) {
            od::port::Player& pl = od::port::player();
            od::port::Fixed16* const pos[3] = {&pl.pos.x, &pl.pos.y, &pl.pos.z};
            od::port::Fixed16* const prev[3] = {&pl.prevPos.x, &pl.prevPos.y, &pl.prevPos.z};
            for (uint32_t k = 0; k < 3; ++k) {
                *pos[k] = int32_t(tp->second[k] * 65536);
                *prev[k] = int32_t(tp->second[k] * 65536);
            }
            pl.rotation.y = int16_t(tp->second[3] & 0xfff);
            pl.prevRotation.y = int16_t(tp->second[3] & 0xfff);
            SDL_Log("playscript teleport at %zu: %d %d %d yaw %d", playScriptPos, tp->second[0], tp->second[1],
                    tp->second[2], tp->second[3]);
            playScriptTeleports.erase(tp);
        }
        if (playScriptPos < playScript.size()) ps1 = ptNextPad();
        if (aimApproach) ps1 |= 0x10;
        if (aimBack) ps1 |= 0x40;
        ps1 |= aimSide;
        ps1 |= aimPad;
        float lx = (float(input.stickX) - 128.0f) / 127.0f, ly = (float(input.stickY) - 128.0f) / 127.0f;
        if (std::abs(lx) < 0.2f && std::abs(ly) < 0.2f) { // no stick: arrows / d-pad as a direction
            lx = float(((ps1 & 0x20) != 0) - ((ps1 & 0x80) != 0));
            ly = float(((ps1 & 0x40) != 0) - ((ps1 & 0x10) != 0));
        }
        static uint64_t paceTicks = 0, paceSteps = 0; // logic ticks per sim step, logged every 150 steps
        if (titleScreen.inGame) { // a run the title started: the game's pace (app/game_pacer)
            for (int f = paceFrames; f > 0 && playerView.play; --f, ++paceTicks)
            {
                if (godArg) od::port::player().hitPoints = od::port::player().maxHitPoints;
                playerView.playTick(ps1, input.stickX, input.stickY, lx, ly);
                netplay.afterTick(); // between ticks: the local state for the network / the mirror (reads only)
            }
        } else {
            if (godArg) od::port::player().hitPoints = od::port::player().maxHitPoints;
            playerView.playTick(ps1, input.stickX, input.stickY, lx, ly);
            netplay.afterTick(); // between ticks: the local state for the network / the mirror (reads only)
            ++paceTicks;
        }
        if (++paceSteps % 150 == 0) {
            SDL_Log("pacecheck: %llu logic ticks in %llu sim steps (30 Hz), pace byte %u, mode %u, title-run %d",
                    (unsigned long long)paceTicks, (unsigned long long)paceSteps, unsigned(od::port::display().pace),
                    unsigned(od::port::display().mode), int(titleScreen.inGame));
            paceTicks = paceSteps = 0;
        }
        titleResume();
        if (playerView.assetsDirty) { // a script loaded DATs (textures / a model bank): the room rebuilt in place
            playerView.assetsDirty = false;
            try {
                g_inStageSwitch = true; // the same room: the stage's files, not a new selection
                RoomMesh mesh = loadRoom(current.stage, current.sub);
                g_inStageSwitch = false;
                seeThrough.roomLoaded(mesh.tris, mesh.semiStart[3] ? mesh.semiStart[0] : mesh.tris.size(), mesh.slices,
                                      mesh.sliceCount);
                modelClearance.roomLoaded(mesh.tris, mesh.semiStart[3] ? mesh.semiStart[0] : mesh.tris.size());
                if (!renderer->upload(mesh.tris, mesh.slices, mesh.sliceCount, mesh.semiStart)) SDL_Log("upload failed");
                mesh.camTris = std::move(current.camTris);
                mesh.tris.clear();
                mesh.slices.clear();
                current = std::move(mesh);
                playerView.snap = true;
            } catch (const std::exception& e) {
                SDL_Log("asset refresh failed: %s", e.what());
            }
        }
        // the ghosts, after the tick loop and the room rebuild (never inside a tick): each posed in its own files, the
        // local ones only read; a new look's slices go straight to the renderer (no room rebuild)
        netplay.step(SDL_GetTicksNS(), ghosts); // send, receive, the slots from their buffers
        netStepped = true;
        const SliceSink sink{[&](const std::vector<uint8_t>& rgba, int count) { return renderer->appendSlices(rgba, count); },
                             [&](int slice, const std::vector<uint8_t>& rgba) { renderer->updateSlice(slice, rgba); }};
        ghosts.update({g_roomVramGen, current.sliceCount, &g_roomVram, current.shift},
                      od::port::loadedDataFingerprint(loadedDataIdentity()),
                      [](const std::string& dat) -> const od::GameFile& { return datCache().file(dat); }, sink);
        playscriptLog(); // --playscript trace / timeline / end (app/playthrough)
    }
    if (!netStepped) netplay.step(SDL_GetTicksNS(), ghosts); // outside play the session still runs (no ghost drawn)
    ++simTicks;
    // the room script sets its door locks on its first tick; a scripted --clear-flag run must clear after that
    if (simTicks == 2 && playerView.play && (!flagArgs.empty() || !flagClearArgs.empty())) applyFlags();
}

void App::updateCamera() {
    // free-fly debug camera: WASD + Q/E, Shift = fast, right mouse = look
    const bool* k = SDL_GetKeyboardState(nullptr);
    const bool typing = ImGui::GetIO().WantCaptureKeyboard;
    const float speed = (k[SDL_SCANCODE_LSHIFT] ? 12000.0f : 3000.0f) * dt;
    const float fx = std::sin(cam.yaw), fz = std::cos(cam.yaw);
    float mv[3] = {0, 0, 0};
    if (k[SDL_SCANCODE_W]) { mv[0] += fx; mv[2] += fz; }
    if (k[SDL_SCANCODE_S]) { mv[0] -= fx; mv[2] -= fz; }
    if (k[SDL_SCANCODE_D]) { mv[0] += fz; mv[2] -= fx; }
    if (k[SDL_SCANCODE_A]) { mv[0] -= fz; mv[2] += fx; }
    if (k[SDL_SCANCODE_E]) mv[1] += 1;
    if (k[SDL_SCANCODE_Q]) mv[1] -= 1;
    if (!typing && !playerView.play)
        for (int c = 0; c < 3; ++c) cam.eye[c] += mv[c] * speed;
    // logic runs at 30 Hz; frames in between show the blend of the last two ticks (one tick of latency)
    alpha = std::clamp(float(accumulator) / float(kSimStepNs), 0.0f, 1.0f);
    if (playerView.play) { // blend over the logic tick's own period (pace 3 under water: 20 / s, not every 30 Hz step)
        alpha = titleScreen.inGame ? pacer.phase() // the game frame's real interval (app/game_pacer)
                                   : std::clamp(float(SDL_GetTicksNS() - playerView.logicNs) / float(kSimStepNs), 0.0f, 1.0f);
    }
    if (playerView.play && playerView.modern) { // right stick orbits (per frame, any frame rate)
        float rx = 0.0f, ry = 0.0f;
        if (pad) {
            rx = float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX)) / 32767.0f;
            ry = float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY)) / 32767.0f;
            if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK)) playerView.orbitBehind();
        }
        auto dz = [](float v) { return std::abs(v) < 0.15f ? 0.0f : (v - std::copysign(0.15f, v)) / 0.85f; };
        playerView.orbit(dz(rx) * 2.6f * dt, dz(ry) * 1.6f * dt);
        if (orbitSpin != 0.0f) playerView.orbit(orbitSpin * dt, 0.0f);
        if (orbitTilt != 0.0f) playerView.orbit(0.0f, orbitTilt * dt);
    }
    camSource = "orbit";
    if (playerView.orbitCamera(current, cam, alpha, dt)) {
        if (playerView.lockCamShown) camSource = "lock";
    } else {
        camSource = "game";
        if (!playerView.gameCamera(current, cam, alpha)) {
            cam.fovY = 1.0f; // free camera
            cam.roll = 0.0f;
            camSource = "free";
        }
    }
    // the game's near plane in play (exe 0x40d020 / 0x40d0c0: focal 0x180 / 5 = 76 view units, the view in the world
    // scaled by the game's current coordinate shift 0x6d00b5, which the camera scripts raise for close shots: 76 >>
    // that shift world units, here in the room's units): a camera pressed against a wall sees past it as on the original
    // instead of a wall-sized polygon on the lens
    cam.nearZ = playerView.play ? float(0x180 / 5) * float(1 << current.shift) /
                                      float(1 << (od::port::stage().coordinateShift & 0x1f))
                                : 8.0f;
    if (playerView.logLock && playerView.play && (playerView.lockedOn() || playerView.lockBlend > 0.0f)) {
        // scripted: camera source per frame + the player -> target axis yaw it converges to (axis -1 = none)
        float axis = -1.0f;
        if (const uint32_t t = playerView.lockTarget) {
            const float dx = playerView.tgtCur[0] - playerView.posCur[0], dz = playerView.tgtCur[2] - playerView.posCur[2];
            axis = std::fmod(std::atan2(dx, dz) + 6.2831853f, 6.2831853f);
            (void)t;
        }
        SDL_Log("lockcam frame tick %llu: %s yaw %.4f axis %.4f pitch %.4f blend %.2f eye %.0f %.0f %.0f fov %.3f dist %.2f back %.0f",
                (unsigned long long)simTicks, camSource, std::fmod(std::fmod(cam.yaw, 6.2831853f) + 6.2831853f, 6.2831853f),
                axis, cam.pitch, playerView.lockBlend, cam.eye[0], cam.eye[1], cam.eye[2], cam.fovY, playerView.lockDist,
                playerView.lockBack);
    }
}

// the endless-track travel frame (PlayerView::renderOffset): game-coordinate geometry drawn at + the offset
static void travelOffset(const float r[3], std::vector<od::RenderVertex>& v) {
    if (r[0] == 0.0f && r[1] == 0.0f && r[2] == 0.0f) return;
    for (od::RenderVertex& x : v)
        for (int c = 0; c < 3; ++c) x.pos[c] += r[c];
}

// OpenDash's remaster precipitation (render/renderer.hpp Precipitation) from the game's weather after the tick
// (game/weather_draw conditions). The towns' snow: as dense as its live particles, drifting with its wind (game units a
// tick, y down) at kSnowScale of its speed -- snow's pace (~0.5 m/s; the wind at full speed, ST08 / ST09's 8 a tick,
// fell like rain) -- never slower than kSnowFall. A blizzard layer over the scene: as thick as its grey (an additive
// layer: 0x40 = full; ST10 / ST11 0x30), gusting by its grey's swing, blowing as fast as its x scroll (1/16 PS1 pixel a
// tick: kBlowPerScroll game units a second each; ST10 / ST11 320: ~29 m/s, a gale -- the layer crosses the screen in
// half a second) while the snow sinks at kBlowFall: it flies sideways.
// The layer scrolls the screen at the camera's yaw and so gives no direction in the world: the snow's wind gives it
// when the stage also snows, else it blows along world +x / -x by its scroll's sign
static od::Precipitation precipitationOf(const od::weather::Conditions& w, float unit, float seconds,
                                         const App::PrecipTuning& tune) {
    constexpr float kTicksPerSecond = 30.0f, kSnowScale = 0.35f, kSnowFall = 70.0f, kBlowPerScroll = 13.5f,
                    kBlowFall = 200.0f;
    od::Precipitation p;
    p.unit = unit;
    p.seconds = seconds;
    float wind[3] = {0.0f, -kSnowFall, 0.0f};
    if (w.snowStatics) {
        p.snow = std::min(1.0f, float(w.flakes) / float(od::port::kSnowParticles * w.snowStatics));
        const float k = kTicksPerSecond * kSnowScale;
        wind[0] = float(w.wind[0]) * k * tune.drift;
        wind[1] = std::min(-float(w.wind[1]) * k, -kSnowFall) * tune.fall;
        wind[2] = float(w.wind[2]) * k * tune.drift;
    }
    if (w.blizzards) {
        const float grey = float(w.speed.grey), swing = float(w.speed.swing);
        p.blizzard = std::clamp(grey / 64.0f, 0.25f, 1.0f);
        p.gust = std::clamp(swing / std::max(grey, 1.0f), 0.0f, 0.8f);
        const float blow = std::fabs(float(w.speed.scrollX)) * kBlowPerScroll * tune.blow;
        const float h = std::sqrt(wind[0] * wind[0] + wind[2] * wind[2]);
        const float dir[2] = {h > 1e-3f ? wind[0] / h : (w.speed.scrollX < 0 ? -1.0f : 1.0f), h > 1e-3f ? wind[2] / h : 0.0f};
        wind[0] = dir[0] * std::max(blow, h);
        wind[2] = dir[1] * std::max(blow, h);
        wind[1] = std::min(wind[1], -kBlowFall * tune.fall);
    }
    for (int c = 0; c < 3; ++c) p.wind[c] = wind[c] * unit;
    p.density = tune.density;
    p.size = tune.size;
    p.brightness = tune.brightness;
    p.swirl = tune.swirl;
    p.flutter = tune.flutter;
    p.reach = tune.reach;
    p.streak = tune.streak;
    p.chaos = tune.chaos;
    return p;
}

void App::updateScene() {
    if (current.depthCue && fogMode == od::FogMode::Classic) renderer->setFog(fogFor(current, fogMode, fogDistance, fogSource)); // the game's far colour as it goes
    const float* travel = playerView.renderOffset;
    drawCam = cam;
    for (int c = 0; c < 3; ++c) drawCam.eye[c] += travel[c];
    {
        const float aspect = float(width) / float(std::max(height, 1));
        std::vector<od::RenderVertex> skyV;
        if (current.travelMapSlice[0] >= 0) { // ST01: the travel map's 2D layer (no sky in that stage)
            if (playerView.play) buildTravelMap(current, simTicks, aspect, skyV);
        } else if (!current.sky.empty() || !current.skyGrad.empty())
            skyV = skyDome ? buildSkyDome(current, cam, simTicks) : buildSky(current, cam, simTicks, aspect);
        if (playerView.play && playerView.roomStage == current.stage && playerView.roomSub == current.sub)
            appendStormSky(current, playerView.storm, cam, aspect, skyDome, skyV);
        const bool skyScreen = skyV.empty() || !skyDome || current.travelMapSlice[0] >= 0;
        if (!skyScreen) travelOffset(travel, skyV); // the dome around the camera
        renderer->uploadSky(skyV, skyScreen, current.travelMapSlice[0] < 0);
    }
    // the room UV animation (0x418350 / 0x4183d0 rewrite room faces' UV pairs): changed faces -> GPU geometry
    current.uvAnim.apply(playerView.play && playerView.roomStage == current.stage && playerView.roomSub == current.sub,
                         PlayerView::kRoomBlock, *renderer,
                         [&](uint32_t i) { return i < current.segQuads.size() ? &current.segQuads[i].v : nullptr; });
    const size_t poseKey = playerView.play ? size_t(playerView.logicTicks) : animTick; // play: per logic tick
    if (poseKey != shownTick) { // NPCs + Megaman: rebuild once per tick, keep the previous one
        auto actorsLive = playerView.liveActors(current);
        std::vector<od::DynamicObject> dyn;
        for (od::DynamicObject& o : actorsLive) dyn.push_back(std::move(o));
        playerView.viewAspect = float(width) / float(std::max(height, 1));
        // the 2D screens past the 320 x 240 frame (the status sub-screen's backdrop / tabs, game/menu_draw)
        od::port::setScreenExtra(std::max(0, int(std::ceil((240.0f * playerView.viewAspect - 320.0f) * 0.5f))));
        for (od::DynamicObject& o : playerView.liveEffects(current, cam)) dyn.push_back(std::move(o));
        // per-tick room parts (switchable corridor segments, door leaves, door actors): one object; when
        // the set of drawn segments changes its vertex count changes and that tick is not blended
        size_t segVerts = 0;
        std::vector<od::DynamicObject> poolModels;
        std::array<std::vector<od::RenderVertex>, 3> segSemi;
        auto live = playerView.liveScene(current, &segVerts, &poolModels, &segSemi);
        for (od::DynamicObject& o : poolModels) dyn.push_back(std::move(o));
        if (current.trackAxis >= 0 && segVerts > 0) { // endless track: the segments are scenery (tiled below)
            dyn.push_back({od::objectId(od::kKindLive, 1), {live.begin(), live.begin() + std::ptrdiff_t(segVerts)}});
            live.erase(live.begin(), live.begin() + std::ptrdiff_t(segVerts));
        }
        // the semi-transparent segment quads: one blended object per PS1 mode (live ids 2..4)
        for (uint32_t m = 0; m < 3; ++m)
            if (!segSemi[m].empty())
                dyn.push_back({od::objectId(od::kKindLive, 2 + m), std::move(segSemi[m]), uint8_t(m + 1)});
        if (!live.empty()) dyn.push_back({od::objectId(od::kKindLive, 0), std::move(live)});
        dyn.push_back({od::objectId(od::kKindPlayer, 0), playerView.build(current)});
        if (playerView.play) { // the player drawn out of the walls it sinks into (app/model_clearance)
            float pivot[3];
            playerView.pivotAt(current, pivot);
            modelClearance.tick(dyn, od::objectId(od::kKindPlayer, 0), pivot, clearance && playerView.freeView(current));
        }
        if (playerView.play) playerView.measureBodies(current, dyn); // the lock camera's framing (app/cameras)
        { // the see-through: the dynamic scenery's pieces, which pieces hide the characters this tick
            std::vector<od::SeeThroughBody> bodies;
            if (dissolve) playerView.seeThroughBodies(current, dyn, bodies);
            seeThrough.tick(dyn, cam.eye, bodies);
        }
        if (playerView.play) ghosts.appendDynamic(dyn, g_roomVramGen); // objectId(kKindPlayer, 1 + slot)
        if (current.trackAxis >= 0) { // endless track: the travel frame (see travelOffset)
            const int ax = current.trackAxis;
            const float period = current.trackPeriod;
            const int base = int(std::lround(travel[ax] / period));
            std::vector<od::DynamicObject> placed;
            for (od::DynamicObject& o : dyn) {
                const uint32_t seg = o.id & 0xffffffu; // live ids 1 (opaque) / 2..4 (semi): the room's segments
                if ((o.id >> 24) != od::kKindLive || seg < 1 || seg > 4) { // everything but the room's segments
                    travelOffset(travel, o.tris); // the actors / effects / Megaman ride the travel frame
                    placed.push_back(std::move(o));
                    continue;
                }
                // the room's switchable segments are scenery: every tile around the action, at absolute places
                // (periodic like the strip, so a wrap moves nothing); the id carries the tile base, so a wrap
                // starts them without a blend instead of sliding them one period
                od::DynamicObject t{od::objectId(od::kKindLive, seg | (uint32_t(base & 0xff) << 16)), {}};
                t.layout = o.layout;
                t.blend = o.blend;
                for (int j = -4; j <= 4; ++j)
                    for (od::RenderVertex v : o.tris) {
                        v.pos[ax] += float(base + j) * period;
                        t.tris.push_back(v);
                    }
                placed.push_back(std::move(t));
            }
            dyn = std::move(placed);
        }
        if (playerView.snap) {
            dynCur = dyn; // no blending across a discontinuity
            renderer->resetHistory();
        }
        if (std::getenv("OD_TRAVELLOG")) { // dev: the largest per-tick jump of an object / the drawn camera
            static float lastEye[3] = {};
            float worst = 0.0f;
            uint32_t worstId = 0;
            for (const od::DynamicObject& n : dyn)
                for (const od::DynamicObject& o : dynCur)
                    if (o.id == n.id && o.tris.size() == n.tris.size() && !n.tris.empty()) {
                        float d2 = 0.0f;
                        for (int c = 0; c < 3; ++c) {
                            const float d = n.tris[0].pos[c] - o.tris[0].pos[c];
                            d2 += d * d;
                        }
                        if (d2 > worst) { worst = d2; worstId = n.id; }
                    }
            float e2 = 0.0f;
            for (int c = 0; c < 3; ++c) {
                const float d = drawCam.eye[c] - lastEye[c];
                e2 += d * d;
                lastEye[c] = drawCam.eye[c];
            }
            SDL_Log("travel tick %zu: object jump %.0f (id %08x), camera jump %.0f, offset %.0f %.0f", poseKey,
                    std::sqrt(worst), worstId, std::sqrt(e2), travel[0], travel[2]);
            { // objects that vanished this tick / came back (a one-tick hole is a visible flicker)
                std::set<uint32_t> now, before;
                for (const od::DynamicObject& n : dyn) now.insert(n.id);
                for (const od::DynamicObject& o : dynCur) before.insert(o.id);
                static std::set<uint32_t> gone; // gone last tick
                for (uint32_t id : before)
                    if (!now.count(id)) SDL_Log("travel tick %zu: object %08x gone", poseKey, id);
                for (uint32_t id : now)
                    if (!before.count(id) && gone.count(id)) SDL_Log("travel tick %zu: object %08x BACK after 1 tick", poseKey, id);
                gone.clear();
                for (uint32_t id : before)
                    if (!now.count(id)) gone.insert(id);
            }
            for (const od::DynamicObject& n : dyn) // an object that lost its blend this tick (vertex count changed)
                for (const od::DynamicObject& o : dynCur) {
                    if (o.id == n.id && (n.id >> 24) == od::kKindEffect && !n.tris.empty() && !o.tris.empty()) {
                        // the aligned pairs' largest move (what the blend now interpolates across)
                        std::vector<float> a(o.tris.size() * 3), b(n.tris.size() * 3);
                        for (size_t i = 0; i < o.tris.size(); ++i)
                            for (int c = 0; c < 3; ++c) a[i * 3 + size_t(c)] = o.tris[i].pos[c];
                        for (size_t i = 0; i < n.tris.size(); ++i)
                            for (int c = 0; c < 3; ++c) b[i * 3 + size_t(c)] = n.tris[i].pos[c];
                        const size_t unit = od::effectUnit(o.tris.size(), n.tris.size());
                        const long k = od::effectShift(a.data(), o.tris.size(), b.data(), n.tris.size(), unit) * long(unit);
                        float pm = 0.0f;
                        for (long i = 0; i < long(n.tris.size()); ++i) {
                            const long j = i + k;
                            if (j < 0 || j >= long(o.tris.size())) continue;
                            float d2 = 0.0f;
                            for (int c = 0; c < 3; ++c) d2 += (b[size_t(i) * 3 + size_t(c)] - a[size_t(j) * 3 + size_t(c)]) * (b[size_t(i) * 3 + size_t(c)] - a[size_t(j) * 3 + size_t(c)]);
                            pm = std::max(pm, d2);
                        }
                        SDL_Log("travel tick %zu: effect %08x %zu -> %zu vertices, shift %ld, aligned move %.0f", poseKey,
                                n.id, o.tris.size(), n.tris.size(), k, std::sqrt(pm));
                    }
                    if (o.id == n.id && o.tris.size() != n.tris.size()) {
                        float pm = 0.0f; // the common prefix's largest per-vertex move
                        for (size_t i = 0; i < std::min(o.tris.size(), n.tris.size()); ++i) {
                            float d2 = 0.0f;
                            for (int c = 0; c < 3; ++c) d2 += (n.tris[i].pos[c] - o.tris[i].pos[c]) * (n.tris[i].pos[c] - o.tris[i].pos[c]);
                            pm = std::max(pm, d2);
                        }
                        SDL_Log("travel tick %zu: no blend for %08x (%zu -> %zu vertices), prefix move %.0f", poseKey, n.id,
                                o.tris.size(), n.tris.size(), std::sqrt(pm));
                    }
                }
        }
        dynPrev = std::move(dynCur);
        dynCur = std::move(dyn);
        playerView.snap = false;
        shownTick = poseKey;
    }
    // per actor: blend the last two ticks when the actor kept its vertex count (render_logic.hpp)
    { // per actor: blend the last two ticks; the screen overlays follow this frame's camera
        auto dynFrame = od::interpolateObjects(dynPrev, dynCur, alpha);
        // near-camera fade of the effects (smoke puffs, sparks, rings): a particle closer to the eye than kFar shrinks
        // toward its own centre, gone at kNear -- the train fight's big smoke puffs crossed the camera and filled /
        // popped the whole screen (the PS1 camera never got that close to them)
        if (playerView.play) {
            constexpr float kNear = 150.0f, kFar = 500.0f;
            const float sc = float(1 << current.shift);
            for (od::DynamicObject& o : dynFrame) {
                if ((o.id >> 24) != od::kKindEffect) continue;
                const size_t unit = o.tris.size() % 6 == 0 ? 6 : 3;
                for (size_t q = 0; q + unit <= o.tris.size(); q += unit) {
                    float c[3] = {0.0f, 0.0f, 0.0f};
                    for (size_t v = 0; v < unit; ++v)
                        for (int a = 0; a < 3; ++a) c[a] += o.tris[q + v].pos[a] / float(unit);
                    float d2 = 0.0f;
                    for (int a = 0; a < 3; ++a) d2 += (c[a] - drawCam.eye[a]) * (c[a] - drawCam.eye[a]);
                    const float d = std::sqrt(d2) / sc;
                    if (d >= kFar) continue;
                    const float f = std::clamp((d - kNear) / (kFar - kNear), 0.0f, 1.0f);
                    const float k = f * f * (3.0f - 2.0f * f);
                    for (size_t v = 0; v < unit; ++v)
                        for (int a = 0; a < 3; ++a) o.tris[q + v].pos[a] = c[a] + (o.tris[q + v].pos[a] - c[a]) * k;
                }
            }
        }
        for (od::DynamicObject& o : playerView.screenOverlays(current, cam, weatherRemaster)) {
            travelOffset(travel, o.tris);
            dynFrame.push_back(std::move(o));
        }
        for (od::DynamicObject& o : playerView.reticleOverlay(current, cam)) {
            travelOffset(travel, o.tris);
            dynFrame.push_back(std::move(o));
        }
        { // the see-through (app/see_through): the open pieces this frame, the player's hole around the model as drawn
            std::vector<od::SeeThroughBody> bodies;
            std::vector<od::SeeThroughPiece> pieces;
            if (dissolve) {
                playerView.seeThroughBodies(current, dynFrame, bodies);
                seeThrough.frame(dt, pieces);
            }
            renderer->setSeeThrough(bodies, pieces, dissolve && seeThroughDebug);
        }
        renderer->uploadDynamic(dynFrame);
    }
    playerView.vramMovesApply(current, *renderer);
    playerView.clutAnimationSync(current, *renderer);
    { // the screen ripples (pool a0 class 0x438750 variant 0, e.g. the ST18 machine's pulse of script 0x3b, 0x52b180):
        // ring radius +0x18 .. +0x18 + (+0x1a >> 8) around the screen point +0x10 / +0x12, shifted by s8 +0xe;
        // none while 0x6d000a (the exe skips them then)
        std::vector<od::ScreenRipple> rip;
        const od::port::DisplayState& display = od::port::display();
        for (uint32_t i = 0; playerView.play && display.drawingOff == 0 && i < 64; ++i) {
            const uint32_t a = od::port::kEffectPool + i * 0x28;
            od::RoutineRef h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Effects);
            if (!h) h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::EffectsLate);
            const od::port::EffectActor& fx = od::port::effectActorAt(a);
            if (!h.is(od::port::kScreenRippleClass) || fx.variant != od::port::kGrowingRippleVariant || fx.state == 0)
                continue;
            const od::port::GrowingRippleFields& ripple = od::port::classState<od::port::GrowingRippleFields>(od::port::effectActorAt(a));
            od::ScreenRipple r;
            r.cx = float(fx.pos.x);
            r.cy = float(fx.pos.y);
            r.r0 = float(ripple.radius);
            r.r1 = r.r0 + float(ripple.phase >> 8);
            r.shift = float(ripple.shift);
            rip.push_back(r);
        }
        renderer->setScreenRipples(rip);
        // the flooded rooms' screen wave (pool a0 class 0x431e90 variants 0 / 1 / 5, spawned with the water by the
        // ruin's room handlers, colour +0xc = 0x706060): an empty ret on PC, the PS1 / PSP draw (PSP BOOT.BIN
        // 0x179d94) shifts every screen row by sin(((Y - phase) & 127) >> 1) >> (12 - strength) and tints the
        // picture; strength = block +0x18 + 8 (1 variant 0, 2 others), variant 5 s8 +0xf; phase + 1 a game tick
        float waveAmp = 0.0f, tint[3] = {1.0f, 1.0f, 1.0f};
        for (uint32_t i = 0; playerView.play && display.drawingOff == 0 && i < 64; ++i) {
            const uint32_t a = od::port::kEffectPool + i * 0x28;
            od::RoutineRef h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Effects);
            if (!h) h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::EffectsLate);
            const od::port::EffectActor& fx = od::port::effectActorAt(a);
            const uint8_t v = fx.variant;
            if (!h.is(od::port::kRuinVariantEffectClass) || fx.state != uint8_t(od::port::VariantEffectState::Run) ||
                (v != 0 && v != 1 && v != od::port::kScreenWaveVariant))
                continue;
            const auto* const wave = od::port::heldClassStateOf<od::port::VariantEffectFields>(fx);
            if (!wave) continue; // (not run yet)
            const int strength = v == od::port::kScreenWaveVariant ? int(wave->waveStrength) : int(wave->kind);
            waveAmp = float(4096 >> std::clamp(12 - strength, 0, 12));
            for (int k = 0; k < 3; ++k) tint[k] = float(wave->tint[size_t(k)]) / 128.0f;
            break;
        }
        // remaster: also whenever the player is in a flooded room (0xa71dde = 1, pace 3), with the ruin's effect
        // values (variant 1: strength 2, colour 0x706060) when its effect actor is missing
        const od::port::Player& pl = od::port::player();
        if (waterRemaster && waveAmp == 0.0f && playerView.play && pl.inWater == 1 &&
            display.pace == 3 && display.drawingOff == 0) {
            waveAmp = float(4096 >> 10);
            tint[0] = tint[1] = 0x60 / 128.0f;
            tint[2] = 0x70 / 128.0f;
        }
        // the same class makes the lava rooms' heat shimmer (area 6: variant 0, colour 0x7070c0; freed when the
        // lava is switched off): water while the player is in water (0xa71dde) or the flooded rooms' pace 3 is
        // on -- the field mode clears 0xa71dde every tick and the door transitions leave it at 0 (the water rooms
        // then got the lava's look), the pace byte stays
        const bool heat = !((pl.inWater == 1) || display.pace == 3);
        renderer->setScreenWave(waveAmp, std::fmod(float(playerView.logicTicks) + alpha, 128.0f), tint, waterRemaster,
                                float(std::fmod(double(SDL_GetTicksNS()) * 1e-9, 2400.0)), heat);
    }
    { // the remaster precipitation (none in classic: the game's quads, PlayerView::screenOverlays), on the game's clock
        od::Precipitation p;
        if (playerView.play && weatherRemaster && od::port::display().drawingOff == 0) {
            const od::weather::Conditions w = od::weather::conditions();
            p = precipitationOf(w, float(1 << current.shift), float((double(playerView.logicTicks) + alpha) / 30.0),
                                w.blizzards ? blizzardTuning : snowTuning);
            if (w.snowStatics != weatherLog.snowStatics || w.blizzards != weatherLog.blizzards ||
                w.flakes / 8 != weatherLog.flakes / 8) {
                SDL_Log("precipitation: snow %u statics %u flakes wind %d %d %d | blizzard %u grey %u swing %u scroll %d %d"
                        " -> snow %.2f blizzard %.2f gust %.2f wind %.0f %.0f %.0f",
                        w.snowStatics, w.flakes, int(w.wind[0]), int(w.wind[1]), int(w.wind[2]), w.blizzards,
                        unsigned(w.speed.grey), unsigned(w.speed.swing), int(w.speed.scrollX), int(w.speed.scrollY),
                        p.snow, p.blizzard, p.gust, p.wind[0], p.wind[1], p.wind[2]);
                weatherLog = w;
            }
        }
        renderer->setPrecipitation(p);
    }
}

void App::drawOverlays() {
    renderer->imguiNewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    canvas.begin(float(width), float(height));
    menuPainter.art.enabled = titleScreen.painter.art.enabled = vectorUi.enabled = messageUi.vectorLines = hdArtArg;
    menuPainter.vector = titleScreen.painter.vector = &vectorUi;
    vectorUi.setLanguage(od::lang::code(playerView.language));
    if (playerView.play) {
        if (!messageUi.tried) messageUi.load(*renderer);
        if (!messageUi.hdTried) messageUi.loadHd(*renderer);
        {
            messageUi.usePack(*renderer, playerView.pack.get(), playerView.labelPack());
            playerView.menuVram.setArtPack(playerView.pack);
        }
        if (!hudUi.tried || hudUi.vramGen != g_roomVramGen) hudUi.load(*renderer); // a new room's VRAM
        if (!playerView.minimapPrims.empty()) { // the minimap (game/minimap): the HUD's entry +0x78, linked after it
            if (!playerView.menuVram.ready) playerView.menuVram.reset();
            // its tiles and markers sample the field's pages
            if (!playerView.menuVram.fieldBase || playerView.menuVram.baseGen != g_roomVramGen)
                playerView.menuVram.setFieldBase(g_roomVram);
            const float s = float(height) / 240.0f;
            const ui::Vec2 o((float(width) - 320.0f * s) * 0.5f, 0.0f);
            menuPainter.draw(*renderer, playerView.menuVram, canvas, playerView.minimapPrims, s, o,
                             [&](const od::port::ScreenPrim& p) {
                                 messageUi.menuText(canvas, p, s, o);
                             });
        }
        if (!titleScreen.demoShown) // the demo hides the HUD elements (FUN_00542460: +0 &= ~2)
            hudUi.draw(canvas, playerView.hud.draw(), float(width), float(height));
        hudUi.draw(canvas, playerView.overlayPrims, float(width), float(height));
        if (!playerView.menuPrims.empty()) { // the status sub-screen (game/status_screen), under its windows
            const float s = float(height) / 240.0f;
            const ui::Vec2 o((float(width) - 320.0f * s) * 0.5f, 0.0f);
            menuPainter.draw(*renderer, playerView.menuVram, canvas, playerView.menuPrims, s, o,
                             [&](const od::port::ScreenPrim& p) {
                                 messageUi.menuText(canvas, p, s, o);
                             }, 320.0f, 240.0f, o.x / s);
        }
        // the shop's list (FUN_005d2cf0) / the development room's list and stats (content/yosyonke/dev_room): OT +0x7c,
        // between the windows of layer 3 and the ones above them
        const bool lists = !playerView.shopPrims.empty() || !playerView.devPrims.empty() || !playerView.entryPrims.empty();
        messageUi.draw(canvas, float(width), float(height), lists ? 0 : -1);
        if (lists) {
            if (!playerView.menuVram.ready) playerView.menuVram.reset();
            // the development room's weapon model (content/yosyonke/dev_room devStatsDraw) is skinned by the field's pages
            if (!playerView.devPrims.empty() &&
                (!playerView.menuVram.fieldBase || playerView.menuVram.baseGen != g_roomVramGen))
                playerView.menuVram.setFieldBase(g_roomVram);
            const float s = float(height) / 240.0f;
            const ui::Vec2 o((float(width) - 320.0f * s) * 0.5f, 0.0f);
            for (const auto* prims : {&playerView.shopPrims, &playerView.devPrims, &playerView.entryPrims})
                if (!prims->empty())
                    menuPainter.draw(*renderer, playerView.menuVram, canvas, *prims, s, o,
                                     [&](const od::port::ScreenPrim& p) {
                                         messageUi.menuText(canvas, p, s, o);
                                     });
            messageUi.draw(canvas, float(width), float(height), 1);
        }
        if (!playerView.quizPrims.empty()) { // the ST32 quiz's mark / counter (OT +0x78): over the windows
            if (!playerView.menuVram.ready) playerView.menuVram.reset();
            const float s = float(height) / 240.0f;
            const ui::Vec2 o((float(width) - 320.0f * s) * 0.5f, 0.0f);
            menuPainter.draw(*renderer, playerView.menuVram, canvas, playerView.quizPrims, s, o,
                             [&](const od::port::ScreenPrim& p) {
                                 messageUi.menuText(canvas, p, s, o);
                             });
        }
        // script 0x76's game-clear screen (content/late_game/st54_clear_screen: FUN_005e7fe0(1) backdrop + the clear-time sprites)
        if (const auto& clear = od::port::clearScreenPrims(); !clear.empty()) {
            if (!playerView.menuVram.ready) playerView.menuVram.reset();
            const float s = float(height) / 240.0f;
            const ui::Vec2 o((float(width) - 320.0f * s) * 0.5f, 0.0f);
            menuPainter.draw(*renderer, playerView.menuVram, canvas, clear, s, o,
                             [&](const od::port::ScreenPrim& p) {
                                 messageUi.menuText(canvas, p, s, o);
                             });
        }
    }
    if (titleScreen.active) { // the 2D screens of the main task / title task + the load menu's windows
        {
            messageUi.usePack(*renderer, playerView.pack.get(), playerView.labelPack());
            titleScreen.mv.setArtPack(playerView.pack);
        }
        titleScreen.draw(*renderer, canvas, float(width), float(height),
                         [&](const od::port::ScreenPrim& p, float s, ui::Vec2 o) {
                             messageUi.menuText(canvas, p, s, o);
                         });
        if (!messageUi.tried) messageUi.load(*renderer);
        if (!messageUi.hdTried) messageUi.loadHd(*renderer);
        messageUi.draw(canvas, float(width), float(height));
        if (const float f = od::port::fadeLevel(playerView.fadeTask); f > 0.0f)
            canvas.rectFilled(ui::Vec2(0, 0), ui::Vec2(float(width), float(height)),
                                                          ui::rgba(0, 0, 0, int(f * 255.0f)));
    }
    if (playerView.play) titleScreen.drawDemo(*renderer, canvas, float(width), float(height));
    // FUN_005f3150's full-screen tiles (od::dc::screenTint: script tints, flashes), approximated like the fade:
    // abr 1 / 3 (B + F, B + F / 4) a white veil of the colour's level, abr 2 (B - F) a black one, abr 0 half the
    // colour, opaque without the semi bit
    if (playerView.play)
        for (const uint32_t t : od::dc::screenTints()) {
            const int r = int(t & 0xff), g = int((t >> 8) & 0xff), b = int((t >> 16) & 0xff), k = (r + g + b) / 3;
            const int abr = int((t >> 29) & 3);
            ui::Color col = ui::rgba(r, g, b, 255);
            if (t & 0x2000000u) {
                if (abr == 0) col = ui::rgba(r, g, b, 128);
                else if (abr == 2) col = ui::rgba(0, 0, 0, k);
                else col = ui::rgba(255, 255, 255, abr == 3 ? k / 4 : k);
            }
            canvas.rectFilled(ui::Vec2(0, 0), ui::Vec2(float(width), float(height)), col);
        }
    if (const float f = playerView.fade(); f > 0.0f) { // the screen fade: to black, or additive (white flashes)
        const int c = playerView.fadeWhite() ? 255 : 0;
        canvas.rectFilled(ui::Vec2(0, 0), ui::Vec2(float(width), float(height)),
                                                      ui::rgba(c, c, c, int(f * 255.0f)));
    }
    messageUi.flushHd(*renderer); // the glyphs this frame's text rasterized
    vectorUi.flush(*renderer);
}

void App::present() {
    ImGui::Render();
    auto overlay = [&] { // the game's 2D layer, then the F1 panels over it
        renderer->renderCanvas(canvas);
        renderer->renderImGui(ImGui::GetDrawData());
    };

    if (!shotPath.empty() && resizeTest && frames >= 1 && frames <= 4) { // --resize-test (robustness)
        static const int kSizes[4][2] = {{640, 360}, {1, 1}, {1, 1}, {1280, 720}}; // 1x1 held two frames
        width = kSizes[frames - 1][0];
        height = kSizes[frames - 1][1];
        SDL_SetWindowSize(window, width, height);
        renderer->resize(width, height);
        if (frames == 2) renderer->resetHistory();
    }
    if (scriptEnded) running = false; // the end of the frame, as the shot below
    if (!shotPath.empty() && ++frames >= shotFrames && playScriptPos >= playScript.size()) {
        if (playerView.play) {
            playerView.logCheck(current);
            for (uint32_t i = 0; i < 24; ++i) { // live animated actors (actor system)
                const uint32_t a = od::port::kAnimatedPool + i * 0x16c;
                const od::port::AnimatedActor& act = od::port::animatedActorAt(a);
                if (!(act.flags & od::port::kActorLive) || !playerView.actorLive(a)) continue;
                SDL_Log("actor slot %u state %d/%d/%d anim %d pos %d %d %d yaw %d", i, int(act.state), int(act.subState),
                        int(act.step), int(act.anim), int(act.pos.x.whole), int(act.pos.y.whole), int(act.pos.z.whole),
                        int(act.rotation.y));
            }
            const od::port::Player& pl = od::port::player();
            SDL_Log("lock-on target slot %u actor %08x flags d3 %02x 144 %04x", unsigned(pl.lockTarget),
                    unsigned(od::port::slotIdentityWord(pl.lockedActor)), unsigned(pl.lockFlags), unsigned(pl.lockRequest));
            const od::port::EventState& event = od::port::eventState();
            const od::port::CameraState& camera = od::port::cameraState();
            SDL_Log("event %d mode %d state %d window0 %08x windows1..4 %08x %08x %08x %08x camera mode %d/%d busy %d",
                    int(event.active), int(event.mode), int(event.state), unsigned(od::port::messageWindow(0).flags),
                    unsigned(od::port::messageWindow(1).flags), unsigned(od::port::messageWindow(2).flags),
                    unsigned(od::port::messageWindow(3).flags), unsigned(od::port::messageWindow(4).flags),
                    int(camera.mode()), int(camera.blendStep()), int(uint8_t(camera.blendRequest)));
            for (uint32_t i = 0; i < 64; ++i) { // door actors (actor system)
                const uint32_t a = od::port::kStaticPool + i * 0xcc;
                const od::port::StaticActor& door = od::port::staticActorAt(a);
                if (!(door.flags & od::port::kActorLive) || door.type != od::port::kDoorActorType) continue;
                SDL_Log("door slot %u state %d live %d pos %d %d %d request %02x kind %02x flags700 %02x", i,
                        int(door.state), playerView.actorLive(a), int(door.pos.x.whole), int(door.pos.y.whole),
                        int(door.pos.z.whole), unsigned(od::port::transitionRequest().state),
                        unsigned(od::port::transitionRequest().kind), unsigned(od::port::storyFlags().bytes[0xe0]));
            }
        }
        std::vector<uint8_t> px;
        cam.fovY = 2.0f * std::atan(std::tan(cam.fovY * 0.5f) / shotZoom);
        renderer->draw(drawCam, &px, overlay);
        saveShot(shotPath, width, height, px);
        running = false;
    } else if (!shotPath.empty() && shotEvery > 0 && playerView.play && simTicks >= nextEveryShot &&
               simTicks >= shotFrom) {
        // --shot-every: a numbered shot + what was drawn with which camera (transition evidence)
        nextEveryShot = (simTicks / uint64_t(shotEvery) + 1) * uint64_t(shotEvery);
        const od::port::Player& pl = od::port::player();
        const od::port::GameState& game = od::port::gameState();
        const od::port::FadeState& fadeBlock = od::port::fadeState();
        const od::port::DisplayState& display = od::port::display();
        SDL_Log("shotevery tick %llu: cam %s eye %.0f %.0f %.0f yaw %.3f pitch %.3f | drawn ST%02X/%d game ST%02X/%d "
                "mode %u/%u cammode %u event %u fade %.2f (68 %u 69 %u 6e %u mask %u ft %u) | player %d %d %d yaw %d state %u/%u | script %02x/%u "
                "mission %u left %u out %u | weapon slots %d/%d saved %d lock %u | water %u pace %u skip %u",
                (unsigned long long)simTicks, camSource, cam.eye[0], cam.eye[1], cam.eye[2], cam.yaw, cam.pitch,
                current.stage, current.sub, unsigned(game.stage), int(game.room), unsigned(game.mode),
                unsigned(game.modeStep), unsigned(od::port::cameraState().mode()), unsigned(od::port::eventState().active),
                playerView.fade(), unsigned(fadeBlock.covered), unsigned(uint8_t(fadeBlock.busy)),
                unsigned(uint16_t(fadeBlock.level)), unsigned(display.maskTicks), unsigned(fadeBlock.type),
                int(pl.pos.x.whole), int(pl.pos.y.whole), int(pl.pos.z.whole), int(pl.rotation.y) & 0xfff,
                unsigned(pl.state), unsigned(pl.subState), unsigned(od::port::scriptState().id),
                unsigned(od::port::scriptState().state), unsigned(od::port::roomScriptState().handlerStep),
                unsigned(od::port::fireMission().firesLeft), unsigned(od::port::fireMission().firesOut),
                int(pl.activeSpecial), int(pl.activeBuster), int(pl.specialWeapon), unsigned(pl.lockTarget),
                unsigned(pl.inWater), unsigned(display.pace), unsigned(display.drawingOff));
        std::vector<uint8_t> px;
        od::Camera shotCam = drawCam;
        if (shotOrbit != 0.0f) { // dev: the camera turned around the player's chest (bone 0), looking at it
            const float sc = playerView.liveShiftScale(current);
            const od::port::ShortVec3& bp = od::port::player().bonePoints[od::port::kBodyBone];
            const float pv[3] = {float(bp.x) * sc, -float(bp.y) * sc, float(bp.z) * sc};
            const float dx = cam.eye[0] - pv[0], dz = cam.eye[2] - pv[2], c = std::cos(shotOrbit),
                        sn = std::sin(shotOrbit);
            shotCam.eye[0] = pv[0] + dx * c + dz * sn;
            shotCam.eye[2] = pv[2] - dx * sn + dz * c;
            shotCam.yaw = cam.yaw + shotOrbit;
        }
        renderer->draw(shotCam, &px, overlay);
        std::string path = shotPath;
        if (const auto dot = path.rfind('.'); dot != std::string::npos) path.resize(dot);
        char suffix[24];
        std::snprintf(suffix, sizeof suffix, "_t%04llu.jpg", (unsigned long long)simTicks);
        path += suffix;
        saveShot(path, width, height, px);
    } else {
        renderer->draw(drawCam, nullptr, overlay);
    }

    ++fpsFrames;
    if (now - titleAt >= 1'000'000'000ull) {
        const double fps = double(fpsFrames) * 1e9 / double(now - titleAt);
        if (fpsLog) SDL_Log("fps %.1f (%.2f ms/frame)", fps, 1000.0 / fps);
        fpsFrames = 0;
        char title[160];
        std::snprintf(title, sizeof title, "OpenDash (M1) ST%02X sub %d - %.0f fps - F1 cenarios - cam %.0f %.0f %.0f",
                      stage, sub, fps, cam.eye[0], cam.eye[1], cam.eye[2]);
        SDL_SetWindowTitle(window, title);
        titleAt = now;
    }
}

} // namespace od::app
