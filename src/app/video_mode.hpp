// OpenDash's display settings on SDL: the window, the borderless full screen (the desktop's mode) and the exclusive
// full screen (a display mode of its own), the resolutions each one offers, their opendash.ini keys (display= /
// window= / fullscreen_mode=). Sizes are pixels: the process is per-monitor DPI aware (SDL 3's default), the window's
// logical size is converted through its pixel density.
#pragma once

#include <SDL3/SDL.h>

#include <vector>

#include "engine/config.hpp"
#include "game/status_options.hpp"

namespace od::app {

using Video = od::port::OptionsHost::Video;
using VideoMode = od::port::OptionsHost::VideoMode;
using DisplayMode = od::port::OptionsHost::DisplayMode;

constexpr VideoMode kDefaultWindow{1280, 720, 0}; // no window= yet; the scripted runs always (their shots' size)

// the settings opendash.ini holds (Video::aa is the caller's: aa=); unset / unreadable keys: a 1280 x 720 window, the
// exclusive mode = display d's desktop mode
Video videoFromConfig(const od::Config& cfg, SDL_DisplayID d);
void videoSave(const Video& v);

// what the options page's resolution line offers for display mode `mode` on the window's display
std::vector<VideoMode> videoModes(SDL_Window* window, DisplayMode mode);

// puts the window in v's display mode at its resolution (the window centred on its display; the exclusive mode the
// display's exact one, else the closest) and returns what it got: the window's pixel size, the mode it runs
Video videoApply(SDL_Window* window, const Video& v);

// the window's size in pixels
VideoMode windowPixels(SDL_Window* window);

} // namespace od::app
