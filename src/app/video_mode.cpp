#include "app/video_mode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace od::app {

namespace {

// the window sizes offered (16:9, 16:10, 4:3), those that fit the display
constexpr int kWindowSizes[][2] = {{960, 720},   {1024, 768},  {1280, 720},  {1280, 800},  {1280, 960},
                                   {1366, 768},  {1440, 1080}, {1600, 900},  {1680, 1050}, {1920, 1080},
                                   {1920, 1200}, {2560, 1080}, {2560, 1440}, {2560, 1600}, {3440, 1440},
                                   {3840, 2160}};

VideoMode modePixels(const SDL_DisplayMode& m) {
    const float s = m.pixel_density > 0.0f ? m.pixel_density : 1.0f;
    return {int(std::lround(float(m.w) * s)), int(std::lround(float(m.h) * s)), int(std::lround(m.refresh_rate))};
}

VideoMode desktopPixels(SDL_DisplayID d) {
    const SDL_DisplayMode* m = SDL_GetDesktopDisplayMode(d);
    return m ? modePixels(*m) : VideoMode{1920, 1080, 60};
}

bool parseMode(const std::string& s, VideoMode& out, bool withHz) {
    int w = 0, h = 0, hz = 0;
    const int n = withHz ? std::sscanf(s.c_str(), "%dx%d@%d", &w, &h, &hz) : std::sscanf(s.c_str(), "%dx%d", &w, &h);
    if (n != (withHz ? 3 : 2) || w <= 0 || h <= 0 || (withHz && hz <= 0)) return false;
    out = {w, h, withHz ? hz : 0};
    return true;
}

const char* displayName(DisplayMode m) {
    switch (m) {
    case DisplayMode::Borderless: return "borderless";
    case DisplayMode::Exclusive: return "exclusive";
    default: return "windowed";
    }
}

} // namespace

Video videoFromConfig(const od::Config& cfg, SDL_DisplayID d) {
    Video v;
    if (cfg.display == "borderless") v.display = DisplayMode::Borderless;
    else if (cfg.display == "exclusive") v.display = DisplayMode::Exclusive;
    else if (cfg.display != "windowed") SDL_Log("video: unknown display '%s', using windowed", cfg.display.c_str());
    if (!parseMode(cfg.window, v.window, false)) v.window = kDefaultWindow;
    if (!parseMode(cfg.fullscreenMode, v.exclusive, true)) v.exclusive = desktopPixels(d);
    return v;
}

void videoSave(const Video& v) {
    od::saveConfigValue("display", displayName(v.display));
    od::saveConfigValue("window", std::to_string(v.window.w) + "x" + std::to_string(v.window.h));
    od::saveConfigValue("fullscreen_mode", std::to_string(v.exclusive.w) + "x" + std::to_string(v.exclusive.h) + "@" +
                                               std::to_string(v.exclusive.hz));
}

std::vector<VideoMode> videoModes(SDL_Window* window, DisplayMode mode) {
    const SDL_DisplayID d = SDL_GetDisplayForWindow(window);
    std::vector<VideoMode> modes;
    if (mode == DisplayMode::Borderless) {
        const VideoMode desk = desktopPixels(d);
        modes.push_back({desk.w, desk.h, 0});
    } else if (mode == DisplayMode::Windowed) {
        // the client area that fits the display's usable area once the window's frame is added
        SDL_Rect usable{0, 0, 1920, 1080};
        SDL_GetDisplayUsableBounds(d, &usable);
        int top = 0, left = 0, bottom = 0, right = 0;
        SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);
        const float den = SDL_GetWindowPixelDensity(window);
        const int maxW = int(float(usable.w - left - right) * den), maxH = int(float(usable.h - top - bottom) * den);
        for (const auto& sz : kWindowSizes)
            if (sz[0] <= maxW && sz[1] <= maxH) modes.push_back({sz[0], sz[1], 0});
    } else {
        int n = 0;
        SDL_DisplayMode** list = SDL_GetFullscreenDisplayModes(d, &n);
        for (int i = 0; i < n; ++i) {
            const VideoMode m = modePixels(*list[i]);
            if (std::find(modes.begin(), modes.end(), m) == modes.end()) modes.push_back(m);
        }
        SDL_free(list);
        std::sort(modes.begin(), modes.end(), [](const VideoMode& a, const VideoMode& b) {
            return a.w * a.h != b.w * b.h ? a.w * a.h < b.w * b.h : a.w != b.w ? a.w < b.w : a.hz < b.hz;
        });
    }
    return modes;
}

VideoMode windowPixels(SDL_Window* window) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    return {w, h, 0};
}

Video videoApply(SDL_Window* window, const Video& v) {
    const SDL_DisplayID d = SDL_GetDisplayForWindow(window);
    Video got = v;
    switch (v.display) {
    case DisplayMode::Windowed: {
        SDL_SetWindowFullscreen(window, false);
        SDL_SyncWindow(window); // out of full screen first: SDL defers a size request made while that is pending
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) {
            SDL_RestoreWindow(window);
            SDL_SyncWindow(window);
        }
        const float den = SDL_GetWindowPixelDensity(window);
        SDL_SetWindowSize(window, int(std::lround(float(v.window.w) / den)), int(std::lround(float(v.window.h) / den)));
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(d), SDL_WINDOWPOS_CENTERED_DISPLAY(d));
        break;
    }
    case DisplayMode::Borderless:
        SDL_SetWindowFullscreenMode(window, nullptr); // full screen at the desktop's mode
        SDL_SetWindowFullscreen(window, true);
        break;
    case DisplayMode::Exclusive: {
        SDL_DisplayMode mode{};
        bool found = false;
        int n = 0;
        SDL_DisplayMode** list = SDL_GetFullscreenDisplayModes(d, &n);
        for (int i = 0; i < n && !found; ++i)
            if (modePixels(*list[i]) == v.exclusive) {
                mode = *list[i];
                found = true;
            }
        SDL_free(list);
        if (!found) { // a mode of another display (the window moved, a monitor changed): this one's closest
            const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(d);
            const float s = desk && desk->pixel_density > 0.0f ? desk->pixel_density : 1.0f;
            found = SDL_GetClosestFullscreenDisplayMode(d, int(std::lround(float(v.exclusive.w) / s)),
                                                        int(std::lround(float(v.exclusive.h) / s)), float(v.exclusive.hz),
                                                        true, &mode);
        }
        if (found) {
            SDL_SetWindowFullscreenMode(window, &mode);
            got.exclusive = modePixels(mode);
        } else { // the display lists no modes at all: its desktop mode
            SDL_SetWindowFullscreenMode(window, nullptr);
            got.exclusive = desktopPixels(d);
        }
        SDL_SetWindowFullscreen(window, true);
        break;
    }
    }
    SDL_SyncWindow(window);
    if (got.display == DisplayMode::Windowed) got.window = windowPixels(window);
    SDL_Log("video: %s, window %dx%d, exclusive %dx%d@%d, output %dx%d", displayName(got.display), got.window.w,
            got.window.h, got.exclusive.w, got.exclusive.h, got.exclusive.hz, windowPixels(window).w,
            windowPixels(window).h);
    return got;
}

} // namespace od::app
