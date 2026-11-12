# The big update (Herbert Costa)

A detailed list of what this update adds to OpenDash. Everything here is OpenDash code, text or OpenDash's own data;
no file of the game is included. The game's art that was redrawn in HD (the title words, the credits pages, the flags)
lives in a separate repository, [kurerb/opendash-extras](https://github.com/kurerb/opendash-extras), with a one-click
installer. OpenDash runs without it: where a picture is missing, the game's own pixel art is drawn, as before.

Contents:
1. Languages: text and voices apart, new text languages, translations, subtitles
2. Japanese, Korean and Chinese text in HD
3. The options menu in HD
4. Text size
5. The title screen: Extras, Quit, HD words
6. Extras: credits, gallery, sound test
7. The credits
8. HD panels of the status screen
9. Controls: L1 / R1, key configuration, button styles
10. Room transitions
11. Music and sound
12. Dialogue
13. The quiz show
14. HD CAPCOM logo, mission banners, selection hand (code only)
15. HUD pages in HD (code only)
16. Build and release
17. Fixes
18. Settings (`opendash.ini`)
19. Files

---

## 1. Languages

### Text and voices, each its own language
- Options > Language sets the **text** and the **voices** separately (for example, Japanese voices with English
  text). The voices' language sets the voice clips, the voice barks and the restored voices.
- Saved as `lang=` and `voice_lang=`; the F1 window has a combo for each; the world processes read both.
- `PlayerView::voiceLanguage` / `voicePack` / `setVoiceLanguage` (`app/play_session`).

### The language row
- Lines: **Text**, **Version**, **Voices**, **Subtitles**, **Text size** (and furigana in Japanese), in one wider
  window (it uses the widescreen), each language with its flag, and under the box a short description of the
  chosen version in its own colour.
- Left / right change a line, up / down move between lines, circle applies, cross goes back; the mouse works too.
- `engine/lang`: `languageBase` / `languageVersion` / `languageOf` turn a language into (base language, version) and
  back, so the Text line walks the languages and the Version line their versions.
- A version is only offered when its pack is in `lang_dir`.

### Text languages
| Code | Language | Made from |
|---|---|---|
| `jp` | Japanese | the PC version's own text |
| `en` | English | the US PlayStation script; the quiz corrected (see 13) |
| `ptbr` | Brazilian Portuguese | translated by people and revised for OpenDash |
| `ptjp`, `enjp` | Portuguese, English | translated from the Japanese script by AI, faithful to it (Japanese names, honorifics), with light human review (may contain some errors) |
| `ptjpus`, `enjpus` | Portuguese, English | the same translation with the US names and no honorifics |
| `es`, `fr`, `it`, `de` | Spanish, French, Italian, German | translated from the US English script by AI, with light human review (may contain some errors) |
| `esjp`, `frjp`, `itjp`, `dejp` | the same languages | translated from the Japanese script by AI, faithful, with light human review (may contain some errors) |
| `esjpus`, `frjpus`, `itjpus`, `dejpus` | the same languages | from the Japanese with the US names, no honorifics |
| `ko`, `zh` | Korean, Simplified Chinese | translated from the Japanese script by AI (English only for doubts), with light human review (may contain some errors) |

- Every translation except the Brazilian Portuguese one was made by AI with light human review (names, gender
  agreement, context, the official local names), so it may contain some errors. The language menu says so ("IA" /
  "AI" in the version's name and its description).
- The packs are not in this repository (they are derived from the game's script): OpenDash reads them from
  `lang/<code>/` next to the exe, and a language whose pack is missing is simply not offered. They are distributed
  separately at https://github.com/kurerb/opendash-extras.
- What a pack holds: every dialogue bank of every stage (`msg/<STEM>.bin`); the item names and descriptions and all
  menus (status, items, equipment, options, help, map, shop, development room, save / load, place names, Data's
  hints, the entry list, the quiz) in `exe/*.bin`; the subtitles (`legendas*.tsv`); the font changes the language needs.
- Latin packs (PS1 format): 11,912 messages per version. Window sizes, line counts and the choice hand's columns are
  recalculated from the translated text; a mission title stays on its own line; counters keep their width.
- European font: the accented letters (á é í ó ú à è ì ò ù â ê î ô û ä ö ü ß ç ñ œ « » ¿ ¡ ...) added to the PS1 font
  (glyphs 0x7b..0xa1) with their widths. PS1 glyph 0x7b is drawn as ü in HD text (Müller).
- Korean and Chinese (PC format): the PC's Japanese banks with the pack's glyphs (`charset.bin`: the code point of
  each glyph; `font.txt`: the vector font). Korean wraps at word boundaries, Chinese at any character (closing
  punctuation never starts a line). `engine/pack_charset`.
- Big banks (above 0x3c00) go to their own area, and the pack bank area is larger (`app/play_session`).
- OpenDash's own words in each language: `assets/ui/text/<code>.txt` (es, fr, it, de, jp, ko, zh added): every
  options row, help line, key list, music row, language row and the title menu.
- The Portuguese pack's name is "Português brasileiro".
- `uiLanguage()`: a language translated from the Japanese uses its base language's OpenDash words and, having no
  voices of its own, its base language's voices.

### Subtitles for the voiced lines with no text box (new: the original game has none)
- The original game shows no subtitles in its cutscenes. Now they have them, in every text language of this update,
  for the Japanese, English and Brazilian Portuguese voices.
- The cutscenes' voices with no message window now have subtitles at the bottom of the screen, from the text
  language's pack:
  - `lang/<text>/legendas.tsv` for the pack's own voices;
  - `legendas_<voice>.tsv` for the same lines timed to another language's voices;
  - `legendas_<voice>_direto.tsv` (when present) for lines translated from those voices (AI).
- Format: one line per row, `file<TAB>track<TAB>start<TAB>end<TAB>text` (`xa02_37.dat 3 0.00 2.40 ...`), `#`
  comments, furigana as `{漢字|かんじ}` (drawn small above the kanji).
- Timed by the clip's own playback position (`Mixer::streamSeconds`: frames mixed, pauses excluded), so a paused or
  stalled stream does not shift them.
- Never drawn for a voice a message window speaks. Lines are balanced (no lone word on the last row), "Mega Man" is
  never split, "…" is drawn as "...".
- Options > Language: Subtitles on / off and, with other voices, their source (the dub's lines or the ones translated
  from the voices). Saved as `subtitles=` and `subtitles_direct=`.
- About 1,588 lines per language. `app/subtitles`, `game/audio_logic` (`VoiceClipInfo`, `voiceClipSeconds`),
  `audio/mixer` (`streamSeconds`).

## 2. Japanese, Korean and Chinese text in HD
- With HD text on, the Japanese text (dialogue, windows, menus) is drawn from the full M+ Rounded 1c
  (`assets/fonts/MPLUSRounded1c-Medium.ttf`, the same OFL licence as the Latin subsets already there), each glyph
  centred in its cell, outlined in the game's colours.
- `engine/jp_charset`: glyph → Unicode tables for INIT.DAT's two Japanese fonts (834 + 804 glyphs), read off the
  fonts and checked glyph by glyph against the game's furigana. Glyphs with no Unicode form (the hands, empty cells)
  stay bitmaps. Drawn by `MessageUi::hdJpGlyph`.
- Korean and Chinese with Noto Sans KR / SC (each pack's `font.txt` names its font).
- Proportional ASCII inside Japanese text (numbers, Latin letters) without moving the choice hand.
- The vector UI's Japanese captions: `assets/ui/text/jp.txt` (the words of the PC's SUBSCN pages); with Japanese,
  the vector UI uses the full M+ for both weights.

## 3. The options menu in HD
- The options page is the game's own list in HD (Controller, View, Auto-aim, Vibration, Sound, Volume, Music, Screen,
  Language, Extras, Default, Back) with the values on their rails, as the game shows them.
- The windows over the menu are darker, so the page under them does not show through the words.
- The exe's grey screen-position grid and the four corner marks are no longer drawn (OpenDash has no screen position
  to set).
- **Music** window: Original / Re-arranged (the re-arranged music is not part of OpenDash: the row only shows when
  that music is installed), and the credits song: Original (Japanese PC) / US version. In the 9 languages.
- **Screen** window: anti-aliasing (DLSS, FSR, ...), resolution and display mode (window, borderless, full screen),
  with HD icons and the values in the menu's proportional font.
- The options page's frame corner is a vector picture when `assets/ui/options_junction.svg` is installed.
- The mouse picks the lines of every window.

## 4. Text size
- Large (the original) / Medium (default) / Small, for the dialogue and the menus (not the cutscene subtitles). In
  Options > Language; saved as `text_size=`.
- Text boxes never cut text: when a line does not fit, the box grows sideways (up to the screen's edge, widescreen
  included); only if that is not enough does the text get a little smaller.
- Lines the packs centred with spaces (crooked in the HD font) are left-aligned.

## 5. The title screen
- New rows after the game's four: **Extras** and **Quit** (Quit closes the app the normal way). They join upstream's
  Multiplayer row: each row only shows with its host hook (`TitleHost::lobby` / `extras` / `quit`), and with more
  rows they come closer together so the last one stays on screen. The cursor and the mouse follow them.
- Their words are pictures in the menu's yellow style (`ScreenPrim` labels `kLblTitleExtras` / `kLblTitleQuit`,
  `assets/ui/title/<language>/extras_*.svg` / `sair_*.svg`).
- HD title words (`TitleScreen::hdTitle`): each 4-bit sprite of the title is identified by a hash of its pixels and
  palette (`spriteHash`, FNV-1a); when `assets/ui/title/<language>/sprite_<hash>.svg` exists it is drawn instead of
  the pixel art, rasterized at the window's resolution. The language's own folder is searched first, so one sprite can
  show each language's word: the menu, the difficulty list, PRESS START, the credit lines.
- HD logo: `assets/ui/title/logo_*.png` (per language: Mega Man Legends 2 / Rockman Dash 2), fitted to the logo
  sprites' box and resized from its master each frame (sharp at any resolution), keeping the game's fade.
- `OD_DUMP_TITLE=<dir>` writes each sprite with no picture (its indices and palette), the tracer's input;
  `OD_DUMP_DIFFICULTY=1` counts every difficulty as cleared, so the difficulty list can be captured.

## 6. Extras
- **Extras** on the title menu opens the options' sub-screen straight on the Extras list (only the moving backdrop and
  the list); Back returns to the title. It is also a row of the title's options.
- The list: Credits, Sound test, Illustrations (by category), Books and manuals, English cast. Each category is an
  album read from `assets/ui/extras/` (none ships with OpenDash: the albums show only when their pictures are installed).
- While Extras is open the Flutter's music plays; the sound test silences it, and it comes back when you leave it.
- **Gallery:** left / right, L1 / R1 or the stick; a caption under each picture; PNG and JPG.
- **Sound test:** every music of the game (76), from the stage files' music parts; the arrows choose, circle plays,
  cross stops, cross again goes back. The bottom bar shows the buttons.

## 7. The credits
- The staff roll can be watched from the title's Extras / options, and the end of the game shows the same roll.
- HD cards over the darkened illustration, spread over the whole song, in order: the original staff, the US dub, the
  Brazilian fan dub, OpenDash, "presented by CAPCOM". The cast as character: Japanese / English / Brazilian
  Portuguese voice. Drawn from `assets/ui/credits/` when installed, else the game's own roll.

## 8. HD panels of the status screen
The panels' design follows a mockup by Doni Arts (thanks!); the drawing is OpenDash code (generated SVG).
- The status screen redrawn as vector art at the window's resolution (side panels, the place plate, the Zenny and
  time bars with their icons, the H / M / S tabs, the weapon plate), the green frame with its brass rails, the list
  frames and the help frames' rings. The controller buttons are as before. (`app/ui/vector_ui`: generated SVG.)
- The life gauge as a tube with its orb at the foot and the column of the four status slots; each life cell drawn in
  HD (`kLblGaugeCell`). The Zenny counter and the special letters (Greek) in the HD font.
- The Parts (equipment) screen: its tabs and the barrier badges (flame / light) in HD (`kLblBarrier`).
- The text size changes the letters only; the boxes keep their size.
- The sub-screens' backdrop can be an HD tile (`assets/ui/backdrop_tile.png`, rotated 45 degrees, scrolling as the
  original); without it, the original backdrop. (The tile is Doni Arts' art and is not included.)

## 9. Controls
- **L1 / R1** on foot with the modern controls work as on the PS1: the game turns Mega Man at its own rate, the stick
  is the PS1 d-pad while one is held (up forward, down back, the sides side-step), and the camera stays behind him by
  the original's own rule (exe 0x40e360, the same on the PS1 disc): at once while walking, 0x80 a tick while hanging
  or talking. Skating and lock-on keep their own behaviour.
- **Key configuration** in HD: an opaque ground behind the lists, the question in its own box, a d-pad prompt; the
  capture flow is: circle on an action, then the button (Esc or 5 s cancels); L3 / R3 can be bound; the menu softlock
  with raw devices is fixed (no pad button reached the menu). `game/key_config`, `app/input`.
- **Button style** for the gamepad prompts everywhere: PlayStation (default, the PS1's START / SELECT names), Xbox / PC,
  Steam Deck, Nintendo Switch. Set in Options (triangle on the pad column of the Controller screen), in F1 and as
  `button_style=`. Keyboard and mouse prompts are unchanged. `app/ui/prompts`.

## 10. Room transitions
- Leaving a room: the exit animation always plays to its end; a 0.4 s fade to black covers its last 0.4 s
  (`exitTicksLeft` predicts it); behind the black the rest of the exit, the wipe and the load's waits run up to 8 game
  ticks per frame (with no audio ticks); the new room fades in over 0.2 s from its first frame. Start skips the exit
  animation. The fast-forward is off in a world process, so co-op is not affected.
- The quiz show's slides (PS1 0x30 / 0x31, not ported: two seconds of black) are the same fade out / in.
- `app/play_session` (`fade`, `exitFastForward`, `exitSkip`), `app/app_frame`, `game/exit_transitions`
  (`exitTicksLeft`), `game/fade` (`setExitWipeTicks`), `game/room_triggers`, `content/calbania/st32_quiz`.

## 11. Music and sound
- When the next stage's music is the one already playing, it goes on instead of fading out and restarting
  (`audioBindRoom` compares each music slot's contents and stream; `keepMusicStream`).
- A stage's music starts at full volume, as on the PS1, instead of fading in.
- The PS1 music (the sampler's sequences, the disc-streamed tracks) 3 dB lower: it stood above the effects and the
  voices.
- PS1 sequence player: the key-offs at the loop end's tick are played before the jump. Before, those notes rang on and
  piled up every round (stuck, out-of-tune notes in about 45 tracks, such as ST4D, ST0D, ST36, ST37, ST38, ST48).
  `engine/ps1_music`.

## 12. Dialogue
- Cross shows the whole page at once and the voice goes on; the next page cross moves to also shows at once.
- Triangle held is the fast skip (cross held did that before), except where the text wait gives triangle a use of its
  own. Circle / Start are unchanged. `game/message`, `game/text_script`.

## 13. The quiz show (Calbania, ST32)
- Each pack can carry its own quiz (`exe/quiz.bin` + `exe/quiz_answers.bin`); the engine reads the answers from the
  pack.
- English: the US quiz with its errors fixed, 58 of 200 questions (two right answers or none, wrong facts,
  misspellings).
- Portuguese: localised for Brazil (110 translated, 51 adapted, 39 new). Korean and Chinese: 200 new questions each,
  in the spirit of the Japanese kanji quiz. Spanish, French, Italian, German: the US quiz translated.

## 14. HD CAPCOM logo, mission banners, selection hand (code; the pictures are not included)
- **CAPCOM logo:** the boot logo screen fills the whole window with the logo's white (16:9 included) and shows the
  logo from `assets/ui/capcom_logo.png` / `.svg` when present, keeping the game's fade. `TitleScreen::drawLogoArt`.
- **Mission banners:** "MISSION START / COMPLETE" drawn in HD in the text language, one piece per letter flying in
  exactly as the original sprites (same speed, path and clock), from `assets/ui/mission/<language>.png` + `.txt`
  when present. `app/ui/mission_art`.
- **Selection hand:** with `assets/ui/hand_fill.png` and `hand_edge.png` (the hand's shape as white masks), one HD
  hand replaces every pointing-hand cursor (dialogue choices, shop / development room / entry lists) in every
  language, painted in the window text's own colours and dimmed with its row. `MessageUi::hdHand`. (The hand is
  Herbert's own drawing; it is in the separate art repository.)
- Without those files the game's own graphics are drawn.

## 15. HUD pages in HD (code only)
- An HD image can replace a HUD page: `assets/ui/hud/hud_<tpage><clut>.png` (five hex digits, e.g. `hud_d7fd0.png`),
  the same 256 x 256 layout drawn larger (a square RGBA PNG of 256 px or more). `app/ui/hud_ui`.

## 16. Build and release
- `install()` rules: `cmake --install build --config Release --prefix <folder>` puts the exe, its runtime DLLs, the
  upscaler DLLs and `assets/` in a release folder (the v0.5.0 zip lacked `assets/`, so
  the HD text and the vector UI fell back to the pixel art).
- `OD_NO_VENDOR_SDKS` (CMake option, off by default): on Windows, build without the upscaler SDKs, as the Linux build.

## 17. Fixes
- Options > View no longer drops the game into the stage viewer: the PC's key-name table has 10 entries (8 = R2,
  9 = L2), not 8. `game/text_script`.
- The credits viewer is reached through the options host (`OptionsHost::creditsStart` / `creditsTick`), so `game/`
  does not call `content/` and the tests link.

## 18. Settings (`opendash.ini`)
| Key | Values |
|---|---|
| `voice_lang` | the voices' language apart from the text's (`jp`, `en`, `ptbr`, ...) |
| `subtitles` | `1` / `0`: the subtitles of the voiced lines with no text box |
| `subtitles_direct` | `1` / `0`: with other voices, the lines translated from them |
| `text_size` | `grande` / `medio` / `pequeno` |
| `button_style` | `playstation` / `xbox` / `steamdeck` / `switch` |
| `credits_music` | `jp` / `us` |
| `extended_music` | `1` / `0` (only with the re-arranged music installed) |

## 19. Files
New: `src/app/subtitles.*`, `src/app/music_choice.*`, `src/app/ui/mission_art.*`, `src/engine/jp_charset.*`,
`src/engine/pack_charset.*`, `assets/fonts/MPLUSRounded1c-Medium.ttf`, `assets/ui/text/{jp,es,fr,it,de,ko,zh}.txt`,
`docs/changes_herbert_costa.md`.
Changed: see the pull request's file list.
