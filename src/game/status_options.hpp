// The status sub-screen's options page (page 6, exe 0x5edb70; SUBSCN06; the title's options run it too): its rows
// (OpenDash's layout of them: the furigana row only in Japanese, the language row before "defaults"), the two-choice
// rows (camera, vibration, furigana, mono), the lock-on, the volume, the video row, the defaults and the language rows,
// the inline values and the overlays; the key configuration
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
//   - OpenDash's language row (state 0xc, modelled on the video row's overlay)
//   - the panel's "To Title" row (game/status_title) asks the host for the game's soft reset after a yes
struct OptionsHost {
    std::function<int()> language;          // 0 Japanese, 1 English, 2 Portuguese
    std::function<int()> languageCount;     // the text languages (od::lang::kLanguageCount)
    std::function<void(int)> setLanguage;   // apply + persist (the text's language, an od::lang::Language)
    // language() above is the language of OpenDash's own texts (0 / 1 / 2); the text's language as the language row
    // picks it may also be a pack translated from the Japanese (od::lang::Language 3, 4 ...): its value, whether its
    // pack is installed, the row's name of a language (UTF-8; Japanese is drawn in the PC glyphs), the voice languages
    std::function<int()> textLanguage;
    std::function<bool(int)> textLanguageOffered;
    std::function<const char*(int)> languageName;
    std::function<int()> voiceLanguageCount;
    // what a text language is (UTF-8, in OpenDash's own texts' language; nullptr = none): shown under the row
    std::function<const char*(int)> languageDescription;
    // OpenDash's own word `key` in the UI language (assets/ui/text/<code>.txt), nullptr = none: the language menu's
    // labels for the languages without a table here (Korean, Chinese: PC glyphs; Spanish..German: Latin)
    std::function<const char*(const char*)> uiWord;
    std::function<int()> voiceLanguage;     // the voices' language (0 / 1 / 2 as language), apart from the text's
    std::function<void(int)> setVoiceLanguage; // apply + persist
    // the subtitles of the voiced lines without a window (app/subtitles): on / off, and with other voices the dub's
    // lines (0) or the text translated straight from those voices by AI (1); each apply + persist
    std::function<bool()> subtitles;
    std::function<void(bool)> setSubtitles;
    std::function<int()> subtitleSource;
    std::function<void(int)> setSubtitleSource;
    // OpenDash's music row (state 0xd, after the language row; none without these): 0 the original music, 1 the
    // extended (app/music_choice: takes effect at the next music load)
    std::function<int()> music;
    std::function<void(int)> setMusic;      // apply + persist
    // the staff roll's music (OpenDash, a second line of the music row): 0 the Japanese PC version's song, 1 the US PS1
    // one; apply + persist (the next staff roll plays it)
    std::function<int()> creditsMusic;
    std::function<void(int)> setCreditsMusic;
    // the text size (OpenDash): 0 Grande (the original), 1 Médio (default), 2 Pequeno; apply + persist
    std::function<int()> buttonStyle;       // OpenDash: the gamepad prompts' style, 0 PlayStation 1 Xbox/PC 2 Steam Deck 3 Switch
    std::function<void(int)> setButtonStyle; // apply + persist
    // OpenDash (Herbert): the gallery (the title options' Galeria row): how many pictures, open (its music) / close
    // (the title's back), the picture shown
    // the sound test (assets/ui/extras/soundtest.txt): how many songs, a song's name, play one (-1: stop, the
    // title's song back)
    std::function<int()> soundTestCount;
    std::function<const char*(int)> soundTestTitle;
    std::function<void(int)> soundTestPlay;
    std::function<void()> creditsStart;          // the staff roll on its own (content/late_game creditsViewStart)
    std::function<bool(bool skip)> creditsTick;  // one frame of it: false = over
    std::function<int()> groupCount;             // the Extras' groups (assets/ui/extras/groups.txt)
    std::function<const char*(int)> groupTitle;  // a group's title in the UI's language
    std::function<int(int)> albumGroup;          // an album's group (-1: none)
    std::function<void(bool)> extrasMusic;       // the Flutter's arranged music on (the Extras) / off
    std::function<int()> albumCount;            // the Extras' albums
    std::function<const char*(int)> albumTitle; // an album's title in the UI's language
    std::function<void(int)> galleryAlbum;      // the album the gallery shows
    std::function<int()> galleryCount;
    std::function<void(bool)> galleryOpen;
    std::function<void(int)> galleryShow;
    std::function<int()> textSize;
    std::function<void(int)> setTextSize;
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
    std::function<void()> returnToTitle; // the panel's "To Title" row (game/status_title): the game's soft reset (game/title)
    std::function<void(uint32_t type, uint8_t param)> fade; // exe 0x53f6b0 (the title's options)
};
OptionsHost& optionsHost();
// OpenDash: the next options page opens straight on its Extras list and leaves to the title (the title's Extras row)
void optionsOpenExtras();
// OpenDash: the Extras' sound test is up (its bar: circle Play, cross Stop -- ScreenPrim kLblMenuBar arg 1)
bool optionsSoundTestOn();

// exe 0x5edb70: page 6 one frame (the state's handler, the pad's kind, the values, the page)
void optionsPage(StatusScreen& s);
// the source's options texture has the PC's pictures (the pad, the keyboard, the mouse, the buttons' and the video
// row's icons: tpage 8); without them (the PS1's) OpenDash draws its own where it needs them
bool optionsPictures();
// the options page's overlay (the screen dimmed and lined, the box of `rows` lines, the cursor bar on line s.step, the
// corner brackets; `icons`: the video row's pictures where the source has them): the questions of the sub-screen use it
void optionsOverlay(const StatusScreen& s, int rows, bool icons);
// OpenDash: the mouse on an overlay's lines (its rows base + line): onto a line the cursor (+3) moves there with the
// move sound; true = the mouse moved onto one this step (the d-pad's move is not taken)
bool optionsOverlayPointer(StatusScreen& s, int rows, int base = 0);
// the source's own message headers are the PS1's (their window columns are in 2 px units): its options page has the
// PS1's nine rows
bool optionsSourceHeadersPs1();
// the inline value table k of the rows (OpenDash's copies, moved with their row: 0 the key configuration's columns)
uint32_t optionsValueTable(size_t k);

} // namespace od::port
