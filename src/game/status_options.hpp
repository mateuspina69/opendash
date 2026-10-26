// The status sub-screen's options page (page 6, exe 0x5edb70; SUBSCN06; the title's options run it too): its rows
// (OpenDash's layout of them: the furigana row only in Japanese, the language row before "defaults", the "back to the
// title" row in play), the two-choice rows (camera, vibration, furigana, mono), the lock-on, the volume, the video row,
// the defaults, the language and the back-to-the-title rows, the inline values and the overlays; the key configuration
// (state 3) is game/key_config. What the exe asks of its PC layer is answered by OpenDash's own settings (OptionsHost).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace od::port {

struct StatusScreen;

// Page 6's questions to the host:
//   - the key configuration (state 3) edits the binding tables (game/input_bindings) that OpenDash's input reads; the
//     raw devices while a binding is captured
//   - the video row (state 0xa, the exe's place for it) is OpenDash's: the anti-aliasing, the resolution, the display
//     mode (window / borderless / exclusive full screen), on OpenDash's settings, not the exe's video globals
//   - OpenDash's rows: the language (state 0xc, modelled on the video row's overlay), back to the title (state 0xd,
//     in play only: the soft reset after a yes / no)
struct OptionsHost {
    std::function<int()> language;          // 0 Japanese, 1 English, 2 Portuguese
    std::function<int()> languageCount;
    std::function<void(int)> setLanguage;   // apply + persist
    std::function<bool()> rumble;           // exe 0x409f80: a pad that can vibrate
    std::function<int()> padPressed;        // exe 0x40a0f0: pad button 1..10 down now, 0 = none
    std::function<int()> keyPressed;        // exe 0x40a090: a nameable key (DirectInput code) down now
    std::function<int()> mousePressed;      // exe 0x40a150: mouse button 1..2 down now
    std::function<void()> bindingsChanged;  // persist the binding tables
    enum class DisplayMode : uint8_t { Windowed, Borderless, Exclusive };
    struct VideoMode {
        int w = 0, h = 0; // pixels
        int hz = 0;       // the exclusive modes' refresh rate (rounded), 0 for the window / borderless sizes
        bool operator==(const VideoMode&) const = default;
    };
    struct Video {
        int aa = 0; // index into the anti-aliasing list (aaCount / aaName)
        DisplayMode display = DisplayMode::Windowed;
        VideoMode window;    // the window's size (Windowed)
        VideoMode exclusive; // the display mode of the exclusive full screen
        bool operator==(const Video&) const = default;
    };
    std::function<int()> aaCount;
    std::function<std::string(int)> aaName;
    std::function<Video()> video; // the current settings (the window's live size)
    // what the resolution line offers: Windowed the window sizes that fit the display, Borderless the desktop's size
    // (the one choice: the render resolution is the anti-aliasing quality's), Exclusive the display's modes
    std::function<std::vector<VideoMode>(DisplayMode)> videoModes;
    std::function<void(const Video&)> videoApply; // apply + persist
    std::function<void()> returnToTitle; // OpenDash's "back to the title" row: the game's soft reset (game/title)
    std::function<void(uint32_t type, uint8_t param)> fade; // exe 0x53f6b0 (the title's options)
};
OptionsHost& optionsHost();

// exe 0x5edb70: page 6 one frame (the state's handler, the pad's kind, the values, the page)
void optionsPage(StatusScreen& s);
// the source's options texture has the PC's pictures (the pad, the keyboard, the mouse, the buttons' and the video
// row's icons: tpage 8); without them (the PS1's) OpenDash draws its own where it needs them
bool optionsPictures();
// the inline value table k of the rows (OpenDash's copies, moved with their row: 0 the key configuration's columns)
uint32_t optionsValueTable(size_t k);

} // namespace od::port
