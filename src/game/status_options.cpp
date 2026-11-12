// The status sub-screen's options page (see status_options.hpp), one function per exe function; OpenDash's rows
// (the video, the language) on the exe's.
#include "game/status_options.hpp"
#include "game/loaded_data.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <span>

#include "engine/data_source.hpp"
#include "engine/jp_charset.hpp"
#include "engine/pack_charset.hpp"
#include "engine/lang.hpp"
#include "engine/screen_prim.hpp"
#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/fade.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/input_bindings.hpp"
#include "game/key_config.hpp"
#include "game/language_pack.hpp"
#include "game/menu_art.hpp"
#include "game/menu_banks.hpp"
#include "game/menu_draw.hpp"
#include "game/message.hpp"
#include "game/menu_help.hpp"
#include "game/menu_pointer.hpp"
#include "game/message_state.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/status_controls.hpp"
#include "game/status_screen.hpp"
#include "game/text_script.hpp"

namespace od::port {

using namespace menu_draw;

namespace {

// +1 of the page (the handler table 0x91b7f8; OpenDash's 0xc)
enum class OptionsState : uint8_t {
    SetUp = 0, Rows = 1, Leave = 2, Keys = 3, Camera = 4, LockOn = 5, Vibration = 6, Furigana = 7, Mono = 8, Volume = 9,
    Video = 0xa, Defaults = 0xb, Language = 0xc, Music = 0xd, TextSize = 0x10, ButtonStyle = 0x11, Credits = 0x12, Gallery = 0x13, Extras = 0x14, SoundTest = 0x15, // (0xe, 0xf: the groups' rows)
};
constexpr uint8_t kLangState = uint8_t(OptionsState::Language);  // OpenDash: the language row's state
constexpr uint8_t kMusicState = uint8_t(OptionsState::Music);    // OpenDash: the music row's state (original / extended)
// +2 of the set-up / the leave: their steps
enum class OptionsStep : uint8_t { Prepare = 0, WaitFade = 1, Done = 2 };
// +2 of a row's own screen: set up, the cursor, back; the defaults' question (Ask), its yes (Apply)
enum class RowStep : uint8_t { Enter = 0, Cursor = 1, Back = 2, Apply = 3, Ask = 4 };

// OpenDash's region of the page's copies (the panel's entry and the page's rows pointer point at them while the
// sub-screen runs): the root and level-2 entries (+0), the captions (+0x100), the values (+0x200), the rows' help bank
// (+0x600), the key configuration's Latin banks (+0xa00 / +0xc00)
constexpr uint32_t kOpt = 0x7c9f1000;
constexpr uint32_t kOptBytes = 0x1000;
constexpr uint32_t kOptCaptions = kOpt + 0x100, kOptValues = kOpt + 0x200, kOptValueSize = 0x40, kOptHelp = kOpt + 0x600,
                   kOptLatinKeys = kOpt + 0xa00, kOptLatinLists = kOpt + 0xc00;
// the page's copies (OpenDash's region, the game's built data at kOpt): the root entry, the level-2 entries of the
// states 3..0xc, the captions' rows table, the value tables, the help banks
struct OptionsRegion {
    StateArray<uint8_t, kOptBytes> bytes{};
};
OptionsRegion& optionsRegion() {
    static OptionsRegion s;
    static const bool registered = (registerBuiltData(kOpt, std::span<uint8_t>(s.bytes.data(), s.bytes.size())), true);
    (void)registered;
    return s;
}
// the copies' writes (into the region)
void storeRowArt(uint32_t at, const RowArt& e) {
    storeData(at, e.x);
    storeData(at + 2, e.y);
    storeData(at + 4, e.uv);
    storeData(at + 6, e.clut);
    storeData(at + 8, e.w);
    storeData(at + 0xa, e.h);
}
void storeRowArtWord(uint32_t at, size_t i, uint32_t v) { storeData(at + uint32_t(i) * 4, v); }
uint32_t rowArtAt(uint32_t table, size_t i) { return table + RowTable::kRows + uint32_t(i) * RowArt::kBytes; }
// the exe's tables the copies come from: the level-2 entries, the captions' table, the value tables (moved with
// their row), the rows' states / the lock-on's and the volume's sub-rows
constexpr TableId kValueTables[9] = {
    TableId::StatusOptionsKeyConfigValues,  TableId::StatusOptionsCameraValues,    TableId::StatusOptionsLockOnFirstValues,
    TableId::StatusOptionsLockOnSecondValues, TableId::StatusOptionsVibrationValues, TableId::StatusOptionsFuriganaValues,
    TableId::StatusOptionsMonoValues,       TableId::StatusOptionsLockOnLabels,    TableId::StatusOptionsVolumeLabels};
constexpr uint8_t kValueRows[9] = {0, 1, 2, 2, 3, 4, 5, 2, 6}; // the exe row each value table belongs to
const DataTable<ScalarTable<uint8_t, 3>> lockOnRows{TableId::StatusOptionsLockOnRows};
const DataTable<ScalarTable<int8_t, 3>> lockOnModes{TableId::StatusOptionsLockOnModes};
const DataTable<ScalarTable<uint8_t, 3>> volumeRows{TableId::StatusOptionsVolumeRows};
// the value tables' indices (optionsValueTable): the key configuration, the camera, the lock-on's two buttons' modes,
// the vibration, the furigana, the mono, the lock-on's labels, the volume's labels
constexpr size_t kValueCamera = 1, kValueLockFirst = 2, kValueLockSecond = 3, kValueVibration = 4,
                 kValueFurigana = 5, kValueMono = 6, kValueLockLabels = 7, kValueVolumeLabels = 8;
// the help banks of the exe (the rows' help, the defaults' question, the volume's level help)
constexpr int16_t kVolumeLevelHelp = 2;
constexpr int kOptionsPage = 3; // the page tables' index
constexpr uint32_t kCaptionX = 0x13, kCaptionY = 0x26, kCaptionSize = 0x100030; // OpenDash's captions: 48 x 16
constexpr uint16_t kFrameBase = 0xae;                                              // the frame's height of 10 rows
// the choice tables' answers (the cursor's row -> kChoiceOff / kChoiceOn)
constexpr uint8_t kChoiceOff = 3, kChoiceOn = 4;
// the vibration's test rumble (exe 0x55cd10(0xa01, 0x41efa))
constexpr uint32_t kTestRumbleA = 0xa01, kTestRumbleB = 0x41efa;
// the fades the title's options ask for
constexpr uint8_t kOptionsFadeIn = kFadeInRequest | 2, kOptionsFadeOut = kFadeOutRequest | 2;
// the volume: 17 steps of 8 (0x7f the top)
constexpr int kVolumeStep = 8;
// the pad kinds the pad type gives (+0x2a)
constexpr uint8_t kPadTypeDigital = 0, kPadTypeAnalogA = 4, kPadTypeAnalogB = 7, kPadTypeKeep = 0xfe;

// the art: the junction's pieces, the rails, the values' cluts, the volume boxes, the overlay's corners
constexpr uint32_t kJunctionLeftArt = 0x7f122000, kJunctionMiddleArt = 0x7f122010, kJunctionRightArt = 0x7f12c000;
constexpr uint32_t kRailLeftArt = 0x7f129080, kRailRightArt = 0x7f129090;
constexpr uint32_t kVolumeBoxArt = 0x7f919000, kVolumeLevelArt = 0x7f919140;
constexpr uint32_t kVolumeSetBoxArt = 0x7f909000, kVolumeSetLevelArt = 0x7f909140;
constexpr uint32_t kVideoIconArts[3] = {0x3fdcc488, 0x3fdcc456, 0x3fdcc424};
constexpr uint32_t kCornerArts[4] = {0x7f1090c0, 0x7f1090d0, 0x7f1090e0, 0x7f1090f0};
constexpr uint16_t kOptMode = 7;
constexpr uint16_t kValueClut = 0x7f50, kLabelClut = 0x7f90, kRailClut = 0x7f12;
constexpr uint32_t kCursorBarColour = 0x101080;

// OpenDash's cells of the options texture (tpage 7, 48 x 16, see game/menu_art) as the rows' texture words
constexpr uint16_t kCaptionClut = 0x7f12;
constexpr uint32_t cellArt(menuart::Cell c) { return uint32_t(kCaptionClut) << 16 | uint32_t(c.v) << 8 | uint32_t(c.u); }
constexpr uint32_t kLangCaption = cellArt(menuart::kCellLanguage), kVideoCaption = cellArt(menuart::kCellVideo);

// the PC glyph codes of OpenDash's Japanese texts (font slot 0: 0x80.. hiragana)
constexpr uint16_t kLangHelpJp[] = {0xba, 0xad, 0xbb, 0xac, 0x83, 0xa6, 0xc8, 0x9e, 0x8c}; // げんごをえらびます

const char* const kLangHelpLatin[3] = {"", "Choose the game language.", "Escolha o idioma do jogo."};

// the music row (no free cell of the options texture for its caption: its caption is text in the game's font, drawn
// over the row each frame): おんがく / Music / Música; its help おんがくをえらびます; its lines もとのまま / ながいばん
constexpr uint16_t kMusicCaptionJp[] = {0x84, 0xad, 0xb7, 0x87, 0};
const char* const kMusicCaptionLatin[3] = {"", "Music", "M\xc3\xbasica"};
// its key in the vector UI's words (row.<page>.<uv>: no art of its own, a uv no caption has)
constexpr uint32_t kMusicCaptionUv = 0xfff0;
// (Herbert: the second choice is the extended, re-arranged versions -- pre-alpha, only a few songs have one so far;
// never "Japanese version"): オリジナルか、ながくアレンジしたきょくをえらびます (PC glyphs, jpGlyphString)
constexpr const char* kMusicHelpJpText =
    "\xe3\x82\xaa\xe3\x83\xaa\xe3\x82\xb8\xe3\x83\x8a\xe3\x83\xab\xe3\x81\x8b\xe3\x80\x81\xe3\x81\xaa\xe3\x81\x8c"
    "\xe3\x81\x8f\xe3\x82\xa2\xe3\x83\xac\xe3\x83\xb3\xe3\x82\xb8\xe3\x81\x97\xe3\x81\x9f\xe3\x81\x8d\xe3\x82\x87"
    "\xe3\x81\x8f\xe3\x82\x92\xe3\x81\x88\xe3\x82\x89\xe3\x81\xb3\xe3\x81\xbe\xe3\x81\x99";
const char* const kMusicHelpLatin[3] = {
    "", "Original, or extended and re-arranged versions (pre-alpha: only a few songs).",
    "Original, ou estendidas e rearranjadas (pr\xc3\xa9-alfa: poucas m\xc3\xbasicas)."};
std::vector<uint16_t> jpGlyphString(std::string_view t); // (below, with the music row)
// the music row's values in Japanese (UTF-8): オリジナル / アレンジ
constexpr const char* kMusicJpOriginal = "\xe3\x82\xaa\xe3\x83\xaa\xe3\x82\xb8\xe3\x83\x8a\xe3\x83\xab";
constexpr const char* kMusicJpArrange = "\xe3\x82\xa2\xe3\x83\xac\xe3\x83\xb3\xe3\x82\xb8";

// OpenDash (Herbert): the page's rows in groups -- the root lists the groups (Controls, Audio, Language & text,
// Video), Defaults and Close; a group lists its rows and a Back row (the close row's art) to the root. A group's row and
// the Back row are never states the page runs (optionsRows takes them); the text size row is OpenDash's own overlay
enum class OptGroup : uint8_t { Controls, Audio, LangText, Video, Count };
constexpr uint8_t kGroupState = 0xe;     // a group's row at the root (OptRow::arg = the group)
constexpr uint8_t kGroupBackState = 0xf; // a group's Back row
constexpr uint8_t kTextSizeState = 0x10; // the text size row (Grande / Médio / Pequeno)
int gGroup = -1;                         // the group shown (-1: the root)
// the text captions' keys in the vector UI's words (row.6.<uv>, as the music row's 0xfff0): the groups', the text size's
constexpr uint32_t kGroupCaptionUv = 0xfff1, kTextSizeCaptionUv = 0xfff6;
// their captions, helps and the text size's values: Japanese (UTF-8, as PC glyphs), English, Portuguese; the UI
// language's own words first (row.6.<uv> / help.group.<g> / help.groupback / help.textsize / textsize.*)
const char* const kGroupCaption[3][4] = {
    {"\xe3\x81\x9d\xe3\x81\x86\xe3\x81\x95", "\xe3\x82\xb5\xe3\x82\xa6\xe3\x83\xb3\xe3\x83\x89",
     "\xe3\x81\x92\xe3\x82\x93\xe3\x81\x94\xe3\x83\xbb\xe3\x82\x82\xe3\x81\x98", "\xe3\x81\x8c\xe3\x82\x81\xe3\x82\x93"},
    {"Controls", "Audio", "Language & text", "Video"},
    {"Controles", "\xc3\x81udio", "Idioma e texto", "V\xc3\xad" "deo"}};
const char* const kGroupHelp[3][4] = {
    {"\xe3\x83\x9c\xe3\x82\xbf\xe3\x83\xb3\xe3\x80\x81\xe3\x81\x97\xe3\x81\xa6\xe3\x82\x93\xe3\x80\x81\xe3\x83\xad\xe3"
     "\x83\x83\xe3\x82\xaf\xe3\x82\xaa\xe3\x83\xb3\xe3\x80\x81\xe3\x81\x97\xe3\x82\x93\xe3\x81\xa9\xe3\x81\x86\xe3\x81"
     "\xae\xe3\x81\x9b\xe3\x81\xa3\xe3\x81\xa6\xe3\x81\x84",
     "\xe3\x82\xb9\xe3\x83\x86\xe3\x83\xac\xe3\x82\xaa\xe3\x80\x81\xe3\x81\x8a\xe3\x82\x93\xe3\x82\x8a\xe3\x82\x87\xe3"
     "\x81\x86\xe3\x80\x81\xe3\x81\x8a\xe3\x82\x93\xe3\x81\x8c\xe3\x81\x8f\xe3\x81\xae\xe3\x81\x9b\xe3\x81\xa3\xe3\x81"
     "\xa6\xe3\x81\x84",
     "\xe3\x81\x92\xe3\x82\x93\xe3\x81\x94\xe3\x80\x81\xe3\x83\x9c\xe3\x82\xa4\xe3\x82\xb9\xe3\x80\x81\xe3\x81\x98\xe3"
     "\x81\xbe\xe3\x81\x8f\xe3\x80\x81\xe3\x82\x82\xe3\x81\x98\xe3\x81\xae\xe3\x81\x8a\xe3\x81\x8a\xe3\x81\x8d\xe3\x81"
     "\x95\xe3\x81\xae\xe3\x81\x9b\xe3\x81\xa3\xe3\x81\xa6\xe3\x81\x84",
     "\xe3\x81\x8c\xe3\x82\x81\xe3\x82\x93\xe3\x81\xae\xe3\x81\x9b\xe3\x81\xa3\xe3\x81\xa6\xe3\x81\x84"},
    {"Buttons, camera, auto lock-on and vibration.", "Stereo or mono, volumes and music.",
     "Text and voice language, subtitles and text size.", "Anti-aliasing, resolution and window mode."},
    {"Bot\xc3\xb5" "es, vis\xc3\xa3" "o, mira autom\xc3\xa1tica e vibra\xc3\xa7\xc3\xa3o.",
     "Est\xc3\xa9reo ou mono, volumes e m\xc3\xbasica.",
     "Idioma do texto e das vozes, legendas e tamanho do texto.",
     "Anti-aliasing, resolu\xc3\xa7\xc3\xa3o e modo de tela."}};
const char* const kGroupBackHelp[3] = {
    "\xe3\x81\xbe\xe3\x81\x88\xe3\x81\xae\xe3\x83\xaa\xe3\x82\xb9\xe3\x83\x88\xe3\x81\xab\xe3\x82\x82\xe3\x81\xa9\xe3"
    "\x82\x8a\xe3\x81\xbe\xe3\x81\x99",
    "Back to the options list.", "Volta para a lista de op\xc3\xa7\xc3\xb5" "es."};
const char* const kTextSizeCaption[3] = {"\xe3\x82\x82\xe3\x81\x98\xe3\x82\xb5\xe3\x82\xa4\xe3\x82\xba", "Text size",
                                         "Tamanho"};
const char* const kTextSizeHelp[3] = {
    "\xe3\x82\xa6\xe3\x82\xa4\xe3\x83\xb3\xe3\x83\x89\xe3\x82\xa6\xe3\x81\xa8\xe3\x83\xa1\xe3\x83\x8b\xe3\x83\xa5\xe3"
    "\x83\xbc\xe3\x81\xae\xe3\x82\x82\xe3\x81\x98\xe3\x81\xae\xe3\x81\x8a\xe3\x81\x8a\xe3\x81\x8d\xe3\x81\x95\xe3\x82"
    "\x92\xe3\x81\x88\xe3\x82\x89\xe3\x81\xb3\xe3\x81\xbe\xe3\x81\x99",
    "The size of the text in the windows and menus.", "O tamanho do texto das janelas e dos menus."};
const char* const kTextSizeRow[3] = {
    "\xe3\x82\x82\xe3\x81\x98\xe3\x81\xae\xe3\x81\x8a\xe3\x81\x8a\xe3\x81\x8d\xe3\x81\x95", "Text size",
    "Tamanho do texto"};
const char* const kTextSizeValue[3][3] = {
    {"\xe3\x81\x8a\xe3\x81\x8a\xe3\x81\x8d\xe3\x81\x84", "\xe3\x81\xb5\xe3\x81\xa4\xe3\x81\x86",
     "\xe3\x81\xa1\xe3\x81\x84\xe3\x81\x95\xe3\x81\x84"},
    {"Large", "Medium", "Small"},
    {"Grande", "M\xc3\xa9" "dio", "Pequeno"}};
// the credits row (Herbert: the staff roll without finishing the game; the title's options only)
constexpr uint8_t kCreditsState = 0x12;
constexpr uint32_t kCreditsCaptionUv = 0xfff8;
const char* const kCreditsCaption[3] = {"\xe3\x82\xb9\xe3\x82\xbf\xe3\x83\x83\xe3\x83\x95", "Credits", "Cr\xc3\xa9" "ditos"};
const char* const kCreditsHelp[3] = {
    "\xe3\x82\xb9\xe3\x82\xbf\xe3\x83\x83\xe3\x83\x95\xe3\x83\xad\xe3\x83\xbc\xe3\x83\xab\xe3\x82\x92\xe3\x81\xbf\xe3"
    "\x81\xbe\xe3\x81\x99",
    "Watch the game's credits.", "Assista aos cr\xc3\xa9" "ditos do jogo."};
// the gallery row (Herbert: the credits' illustrations as an extra; the title's options only)
constexpr uint8_t kGalleryState = 0x13;
// the Extras row (the title's options): its own list -- the credits, the gallery (more to come)
constexpr uint8_t kExtrasState = 0x14;
constexpr uint8_t kSoundTestState = 0x15; // (the Extras' sound test)
constexpr uint32_t kExtrasCaptionUv = 0xfffa;
const char* const kExtrasCaption[3] = {"\xe3\x81\x8a\xe3\x81\xbe\xe3\x81\x91", "Extras", "Extras"};
const char* const kExtrasHelp[3] = {
    "\xe3\x82\xb9\xe3\x82\xbf\xe3\x83\x83\xe3\x83\x95\xe3\x83\xad\xe3\x83\xbc\xe3\x83\xab\xe3\x82\x84\xe3\x82\xae\xe3"
    "\x83\xa3\xe3\x83\xa9\xe3\x83\xaa\xe3\x83\xbc",
    "The credits, the gallery and more.", "Os cr\xc3\xa9" "ditos, a galeria e mais."};
// the rows the values panel holds (the game's page: its first seven rows have their values on rails)
constexpr size_t kPanelRows = 7;
constexpr uint32_t kGalleryCaptionUv = 0xfff9;
const char* const kGalleryCaption[3] = {"\xe3\x82\xae\xe3\x83\xa3\xe3\x83\xa9\xe3\x83\xaa\xe3\x83\xbc", "Gallery", "Galeria"};
const char* const kGalleryHelp[3] = {
    "\xe3\x82\xa4\xe3\x83\xa9\xe3\x82\xb9\xe3\x83\x88\xe3\x82\x92\xe3\x81\xbf\xe3\x81\xbe\xe3\x81\x99",
    "See the game's illustrations.", "Veja as ilustra\xc3\xa7\xc3\xb5" "es do jogo."};
// the buttons' style row (the main session's prompts: OptionsHost::buttonStyle)
constexpr uint8_t kButtonStyleState = 0x11;
constexpr uint32_t kButtonStyleCaptionUv = 0xfff7;
const char* const kButtonStyleCaption[3] = {"\xe3\x83\x9c\xe3\x82\xbf\xe3\x83\xb3", "Button style", "Estilo"};
const char* const kButtonStyleHelp[3] = {
    "\xe3\x83\x9c\xe3\x82\xbf\xe3\x83\xb3\xe3\x81\xae\xe3\x82\xa2\xe3\x82\xa4\xe3\x82\xb3\xe3\x83\xb3\xe3"
    "\x82\x92\xe3\x81\x88\xe3\x82\x89\xe3\x81\xb3\xe3\x81\xbe\xe3\x81\x99",
    "The gamepad button icons shown on screen.", "Os \xc3\xad" "cones dos bot\xc3\xb5" "es do controle nas telas."};
const char* const kButtonStyleRow[3] = {
    "\xe3\x83\x9c\xe3\x82\xbf\xe3\x83\xb3\xe3\x81\xae\xe3\x81\xb2\xe3\x82\x87\xe3\x81\x86\xe3\x81\x98",
    "Button style", "Estilo dos bot\xc3\xb5" "es"};
const char* const kButtonStyleValue[3][4] = {
    {"PlayStation", "Xbox\xe3\x83\xbb" "PC", "Steam Deck", "Nintendo Switch"},
    {"PlayStation", "Xbox / PC", "Steam Deck", "Nintendo Switch"},
    {"PlayStation", "Xbox / PC", "Steam Deck", "Nintendo Switch"}};

// OpenDash's texts for the rows the PS1 page lacks, in the packs' languages (EN, PT-BR)
const char* const kVideoHelpLatin[3] = {"", "Set the anti-aliasing, the screen size and the window mode.",
                                        "Ajuste o anti-aliasing, o tamanho da tela e o modo de janela."};

const char* const kKeyHelpLatin[3][3] = {
    {"", "", ""},
    {"Change the gamepad button settings.", "Change the keyboard key settings.", "Change the mouse button settings."},
    {"Altere os bot\xc3\xb5" "es do controle.", "Altere as teclas do teclado.", "Altere os bot\xc3\xb5" "es do mouse."}};

// the key configuration's windows are OpenDash's own messages (a u16 page header, the window's fb 06 op, then the text):
// window 0's help (the window's columns / lines as the PC's page has them) and window 2's action lists, one per column
constexpr uint8_t kKeyHelpHeader[10] = {0x00, 0x00, 0xfb, 0x06, 0x00, 0x68, 0x00, 0xa1, 0x10, 0x02};
constexpr uint8_t kKeyListsHeader[3][13] = {{0x00, 0x00, 0xfb, 0x09, 0x00, 0xfb, 0x06, 0x00, 0x1c, 0x00, 0x3e, 0x17, 0x05},
                                            {0x00, 0x00, 0xfb, 0x09, 0x00, 0xfb, 0x06, 0x00, 0x1c, 0x00, 0x3e, 0x17, 0x05},
                                            {0x00, 0x00, 0xfb, 0x09, 0x00, 0xfb, 0x06, 0x00, 0x1c, 0x00, 0x5e, 0x17, 0x02}};

// window 2's action lists by column ('|': the right column), as the JP ones (0x8c2c30)
const char* const kKeyListsLatin[3][3][6] = {
    {{nullptr}, {nullptr}, {nullptr}},
    {{"Move|Buster", "Turn left|Special attack", "Turn right|Action, OK", "Lock-on|Change view", "Jump, cancel", nullptr},
     {"Forward|Turn left", "Back|Turn right", "Left|Special attack", "Right|Jump", "Action", nullptr},
     {"Buster|Lock-on", nullptr}},
    // (the pad's left column ends at its button boxes, x 0x82: "Girar a esquerda" is 100 px of the PS1 font)
    {{"Mover|Buster", "Girar \xc3\xa0 esq.|Ataque especial", "Girar \xc3\xa0 dir.|A\xc3\xa7\xc3\xa3o, confirmar",
      "Travar mira|Mudar vis\xc3\xa3o", "Pular, cancelar", nullptr},
     {"Frente|Girar \xc3\xa0 esquerda", "Tr\xc3\xa1s|Girar \xc3\xa0 direita", "Esquerda|Ataque especial",
      "Direita|Pular", "A\xc3\xa7\xc3\xa3o", nullptr},
     {"Buster|Travar mira", nullptr}}};

// The source's own page, by exe row (0..9: keys, camera, lock-on, vibration, furigana, mono, volume, video, defaults,
// close): the row of the source's tables (captions, entries, values) that means the same, -1 where it has none. The
// PC's page has ten rows in that order; the PS1's nine are controller, view, lock-on, vibration, sound, volume, screen
// position, defaults, close (its screen position is not OpenDash's video row: that one has art of its own)
constexpr int8_t kPcRows[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
constexpr int8_t kPs1Rows[10] = {0, 1, 2, 3, -1, 4, 5, -1, 7, 8};
const int8_t* sourceRows() {
    const int rows = RowTable(stateAddress(TableId::StatusOptionsCaptionRows)).count;
    if (rows == 10) return kPcRows;
    if (rows == 9) return kPs1Rows;
    throw DataSourceError("options page: a source page of " + std::to_string(rows) + " rows");
}
// OpenDash's cursor entries of the states that are its own (the key configuration's three columns, the video and language
// rows' overlays)
constexpr uint32_t kKeysEntryHead = 3u | 1u << 8 | 3u << 24, kOverlayEntryHead = 3u | 1u << 8;

// One of the page's rows (OpenDash's layout)
struct OptRow {
    uint8_t state, exe; // exe: the row in the exe's tables (0..9), kNoExeRow = OpenDash's
    uint8_t arg = 0;    // a group's row: the group
};
constexpr uint8_t kNoExeRow = 0xff;
std::vector<OptRow> gRows;
uint32_t gVal[9];          // the value tables (their copies, moved with their row)
int gVolY = 0;             // the volume row's y (its bars)
std::vector<int> gLines;   // the rows whose value is on the row (exe 0x5eeda0's underlines: exe rows 0..6)
OptionsHost gHost;
bool gExtrasMode = false;   // the options page opened on the Extras (the title's Extras row)
bool gExtrasMusic = false;  // the Flutter's music on
int gExtrasGroup = -1;      // the group open (-1: the root)
int gExtrasLine[2] = {0, 0}; // the cursor: the root's, the group's
void extrasMusic(bool on) {
    if (on != gExtrasMusic && gHost.extrasMusic) gHost.extrasMusic(on);
    gExtrasMusic = on;
}
// back on the Extras list from one of its lines
void backToExtras(StatusScreen& s) {
    s.state = kExtrasState;
    s.sub = uint8_t(RowStep::Cursor);
    s.step = uint8_t(gExtrasLine[gExtrasGroup < 0 ? 0 : 1]);
}
OptionsHost::Video gVideoEdit, gVideoShown; // the video row: the settings being edited, the ones applied
// the UI language's own words for OpenDash's texts (assets/ui/text/<uiTextCode>.txt: help.*, music.*, keylist.*,
// musicrow.*, musicval.*): the languages after Portuguese have no column in these tables; nullptr = the table's
const char* ownWord(const char* key) { return gHost.uiWord ? gHost.uiWord(key) : nullptr; }
// the same word as the PC glyphs of a pack in the PC's format (Korean, Chinese: their charset has them); empty if none
std::vector<uint16_t> ownPcWord(const char* key) {
    const char* w = lang::packGlyphsActive() ? ownWord(key) : nullptr;
    return w ? lang::pcEncode(w) : std::vector<uint16_t>();
}

// the key (row.6.<uv>) of a row whose caption is text (the music, text size and group rows), 0 = its art
uint32_t textCaptionUv(const OptRow& r) {
    if (r.state == kMusicState) return kMusicCaptionUv;
    if (r.state == kTextSizeState) return kTextSizeCaptionUv;
    if (r.state == kButtonStyleState) return kButtonStyleCaptionUv;
    if (r.state == kCreditsState) return kCreditsCaptionUv;
    if (r.state == kGalleryState) return kGalleryCaptionUv;
    if (r.state == kExtrasState) return kExtrasCaptionUv;
    if (r.state == kGroupState) return kGroupCaptionUv + r.arg;
    return 0;
}
// the table's help of a group's row, a Back row, the text size row (the language 0 JP / 1 EN / 2 PT); null = another row
const char* groupHelp(const OptRow& r, int lang) {
    const int l = std::clamp(lang, 0, 2);
    if (r.state == kGroupState) return kGroupHelp[l][std::min<int>(r.arg, 3)];
    if (r.state == kGroupBackState) return kGroupBackHelp[l];
    if (r.state == kTextSizeState) return kTextSizeHelp[l];
    if (r.state == kButtonStyleState) return kButtonStyleHelp[l];
    if (r.state == kCreditsState) return kCreditsHelp[l];
    if (r.state == kGalleryState) return kGalleryHelp[l];
    if (r.state == kExtrasState) return kExtrasHelp[l];
    return nullptr;
}
// the caption text of a text-captioned row in the language (UTF-8; JP: as PC glyphs)
const char* textCaption(const OptRow& r, int lang) {
    const int l = std::clamp(lang, 0, 2);
    if (r.state == kMusicState) return kMusicCaptionLatin[l];
    if (r.state == kTextSizeState) return kTextSizeCaption[l];
    if (r.state == kButtonStyleState) return kButtonStyleCaption[l];
    if (r.state == kCreditsState) return kCreditsCaption[l];
    if (r.state == kGalleryState) return kGalleryCaption[l];
    if (r.state == kExtrasState) return kExtrasCaption[l];
    if (r.state == kGroupState) return kGroupCaption[l][std::min<int>(r.arg, 3)];
    return "";
}

// OpenDash: the rows of the language into the copies
void optLayout() {
    const int lang = gHost.language ? gHost.language() : 0;
    const bool jp = lang == 0;
    gRows.clear();
    // the root: the groups, Defaults, Close; a group: its rows, Back (OptGroup)
    auto exeRow = [&](uint8_t e) { gRows.push_back({uint8_t(e < 9 ? e + 3 : 2), e}); };
    // OpenDash (Herbert): the game's page as it was -- one list, each row with its values on its rail -- with OpenDash's
    // rows among the game's: the music (with the sound), the screen, the language (its window: the text, the voices,
    // the subtitles, the text size, the furigana), the credits (the title's options only); the buttons' style is the
    // key configuration's. (The groups, OptGroup, are not shown: gGroup stays -1.)
    gGroup = -1;
    for (uint8_t e = 0; e < 4; ++e) exeRow(e); // keys, camera, lock-on, vibration
    exeRow(5);                                  // stereo / mono
    exeRow(6);                                  // the volumes
    if (gHost.music) gRows.push_back({kMusicState, kNoExeRow});
    exeRow(7); // the screen
    gRows.push_back({kLangState, kNoExeRow});
    // (the Extras: the title's own menu row, Herbert -- not a row here)
    exeRow(8); // defaults
    exeRow(9); // close
    (void)jp;
    const uint32_t n = uint32_t(gRows.size());
    optionsRegion(); // the region (built data at kOpt)
    // the root {0, 0, 0, rows, the help bank} and the level-2 entries of the states 3..0xc (0xc as 0xa: its own cursor)
    CursorEntry root;
    root.setHead(n << 24);
    root.link = kOptHelp;
    storeCursorEntry(kOpt, root);
    const int8_t* rows = sourceRows();
    for (uint32_t c = 3; c <= kSoundTestState; ++c) {
        if (c == kGroupState || c == kGroupBackState) continue; // (never run: optionsRows takes them)
        CursorEntry entry;
        const int source = c >= kLangState || c == uint32_t(OptionsState::Video) ? -1 : rows[c - 3];
        if (c == uint32_t(OptionsState::Keys)) entry.setHead(kKeysEntryHead);
        else if (c >= uint32_t(OptionsState::Video) && c != uint32_t(OptionsState::Defaults)) entry.setHead(kOverlayEntryHead);
        else if (source >= 0) // the source's entry of the row that means this state (its state is the row + 3)
            entry = cursorEntryAt(stateAddress(TableId::StatusOptionsLevelTwoEntries) + uint32_t(source + 3) * 8 - 0x10);
        else continue; // a row the source has none of (it is not on the page)
        storeCursorEntry(kOpt + 8 + (c - 3) * 8, entry);
    }
    CursorEntry page = pageCursorEntry(uint32_t(StatusPage::Options));
    page.link = kOpt;
    storeCursorEntry(pageCursorEntryAddress(uint32_t(StatusPage::Options)), page);
    // the captions (the page's rows table: the exe's header, OpenDash's rows) and the frame
    const RowTable captionsFrom(stateAddress(TableId::StatusOptionsCaptionRows));
    for (size_t k = 0; k < RowTable::kHeaderWords; ++k) storeData(kOptCaptions + uint32_t(k) * 4, captionsFrom.word(k));
    storeData(kOptCaptions, int16_t(n)); // the count
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t row = rowArtAt(kOptCaptions, i);
        if (gRows[i].exe == kNoExeRow || rows[gRows[i].exe] < 0) { // OpenDash's art
            storeData(row, int16_t(kCaptionX));
            storeRowArtWord(row, 1, gRows[i].exe == kNoExeRow ? kLangCaption : kVideoCaption);
            // the music, text size and group rows: no art (their captions are drawn as text, optionsValuesDraw)
            storeRowArtWord(row, 2, textCaptionUv(gRows[i]) != 0 ? 0u : kCaptionSize);
        } else {
            storeRowArt(row, captionsFrom.row(size_t(rows[gRows[i].exe])));
        }
        storeData(row + 2, int16_t(kCaptionY + i * 0x10)); // y
    }
    StatusPageTables& pages = statusPageTables();
    pages.rows[kOptionsPage] = kOptCaptions;
    pages.frameHeights[kOptionsPage] = uint16_t(kFrameBase + (n - 10) * 0x10);
    // the value tables, moved with their row
    gLines.clear();
    for (uint32_t i = 0; i < n; ++i)
        if (i < kPanelRows && (gRows[i].exe <= 6 || gRows[i].state == kMusicState)) gLines.push_back(int(i));
    for (int k = 0; k < 9; ++k) {
        int row = -1;
        for (uint32_t i = 0; i < n; ++i)
            if (gRows[i].exe == kValueRows[k]) row = int(i);
        gVal[k] = kOptValues + uint32_t(k) * kOptValueSize;
        if (row < 0) { // a hidden row (or one the source has none of): no values (the count)
            storeData(gVal[k], int16_t(0));
            continue;
        }
        const int rowY = int(kCaptionY) + row * 0x10;
        if (k == 0 && !dataSource().hasTable({TableId::StatusOptionsKeyConfigValues, 0})) {
            // the source has no picture of the key configuration's devices: OpenDash's cells (game/menu_art), the pad,
            // the keys and the mouse in a row
            storeData(gVal[k], uint32_t(3) | uint32_t(3) << 16); // the count, the level byte's offset
            storeData(gVal[k] + 4, uint32_t(0));
            storeData(gVal[k] + 8, uint32_t(0));
            static constexpr int kKeyX[3] = {0x88, 0xb8, 0xe8};
            for (size_t i = 0; i < 3; ++i) {
                const menuart::Cell c = menuart::kCellKeys[i];
                storeRowArt(rowArtAt(gVal[k], i), {{}, int16_t(kKeyX[i]), int16_t(rowY), uint16_t(c.u | c.v << 8),
                                                   kCaptionClut, menuart::kCellWidth, menuart::kCellHeight});
            }
            continue;
        }
        const RowTable from(stateAddress(kValueTables[k]));
        for (size_t b = 0; b < RowTable::kCopyWords; ++b) storeData(gVal[k] + uint32_t(b) * 4, from.word(b));
        // the table's rows keep their place in the source's row: moved with the row's caption
        const int dy = rowY - captionsFrom.row(size_t(rows[kValueRows[k]])).y;
        const RowTable copy(gVal[k]);
        for (int i = 0; i < copy.count && i < 3; ++i)
            storeData(rowArtAt(gVal[k], size_t(i)) + 2, int16_t(copy.row(size_t(i)).y + dy));
        if (k == 8) gVolY = rowY;
    }
    // the rows' help: the bank of the root entry -- the exe's (Japanese) or the pack's PS1 one, whose nine rows are
    // the PS1 page's (controller, view, lock-on, vibration, sound, volume, screen position, defaults, close: the PC
    // rows map onto them; the video and language rows have OpenDash's texts) -- into the copies' help bank
    const bool pack = ps1Text() && langBank(stateAddress(TableId::MenuBankOptionsHelp)) != stateAddress(TableId::MenuBankOptionsHelp);
    const int8_t* helpRows = pack ? kPs1Rows : rows; // the pack's bank is the PS1's, else the source's own
    const uint32_t src = langBank(stateAddress(TableId::MenuBankOptionsHelp));
    // the PS1 scripts' fb 06 cols are 2 px units (game/message): the JP header's 12 px columns x 6 for a pack's text
    // (the source's own headers are the PS1's already where its page has the PS1's nine rows)
    HelpWriter w(kOpt, jp, rows == kPs1Rows);
    const uint32_t help = kOptHelp - kOpt;
    w.at = help + n * 2;
    for (uint32_t i = 0; i < n; ++i) {
        w.setOffset(help, i, w.at - help);
        const uint8_t e = gRows[i].exe;
        if (e != kNoExeRow && helpRows[e] >= 0 && gRows[i].state != kGroupBackState) {
            w.copy(src, helpRows[e]);
            continue;
        }
        w.header(stateAddress(TableId::MenuBankOptionsHelp), 0);
        const bool music = gRows[i].state == kMusicState;
        // OpenDash's groups, their Back row, the text size: their own helps (the table's, the UI language's first)
        if (const char* own = groupHelp(gRows[i], lang)) {
            const std::string k = gRows[i].state == kGroupState ? "help.group." + std::to_string(gRows[i].arg)
                                  : gRows[i].state == kGroupBackState ? std::string("help.groupback")
                                  : gRows[i].state == kButtonStyleState ? std::string("help.buttonstyle")
                                  : gRows[i].state == kCreditsState     ? std::string("help.credits")
                                  : gRows[i].state == kGalleryState     ? std::string("help.gallery")
                                  : gRows[i].state == kExtrasState      ? std::string("help.extras")
                                                                        : std::string("help.textsize");
            const std::vector<uint16_t> pc = jp ? ownPcWord(k.c_str()) : std::vector<uint16_t>();
            if (!pc.empty()) {
                w.glyphs(pc.data(), uint32_t(pc.size()));
            } else if (jp) {
                std::vector<uint16_t> g = jpGlyphString(own);
                g.pop_back(); // (its 0 end)
                w.glyphs(g.data(), uint32_t(g.size()));
            } else {
                const char* word = ownWord(k.c_str());
                w.wrapped(word ? word : own, w.at - 1);
            }
            w.end();
            continue;
        }
        const char* key = e == 7 ? "help.video" : music ? "help.music" : "help.language";
        const std::vector<uint16_t> pc = jp ? ownPcWord(key) : std::vector<uint16_t>();
        const char* own = jp ? nullptr : ownWord(key);
        if (!pc.empty()) w.glyphs(pc.data(), uint32_t(pc.size()));
        else if (jp && music) {
            static const std::vector<uint16_t> help = [] {
                std::vector<uint16_t> g = jpGlyphString(kMusicHelpJpText);
                g.pop_back(); // (its 0 end)
                return g;
            }();
            w.glyphs(help.data(), uint32_t(help.size()));
        }
        else if (jp) w.glyphs(kLangHelpJp, uint32_t(std::size(kLangHelpJp)));
        else w.wrapped(own ? own : e == 7 ? kVideoHelpLatin[lang] : music ? kMusicHelpLatin[lang] : kLangHelpLatin[lang],
                       w.at - 1);
        w.end();
    }
    setKeyConfigBanks(0, 0);
    if (!jp) {
        const uint32_t keys = kOptLatinKeys - kOpt;
        w.at = keys + 3 * 2;
        for (int k = 0; k < 3; ++k) {
            w.setOffset(keys, uint32_t(k), w.at - keys);
            const uint32_t from = w.at;
            for (const uint8_t byte : kKeyHelpHeader) w.out.byte(w.at++) = byte;
            w.ps1Columns(from, w.at);
            const std::string key = "help.key." + std::to_string(k);
            const char* own = ownWord(key.c_str());
            w.wrapped(own ? own : kKeyHelpLatin[lang][k], w.at - 1);
            w.end();
        }
        const uint32_t lists = kOptLatinLists - kOpt;
        w.at = lists + 3 * 2;
        for (int k = 0; k < 3; ++k) {
            w.setOffset(lists, uint32_t(k), w.at - lists);
            const uint32_t from = w.at;
            for (const uint8_t byte : kKeyListsHeader[k]) w.out.byte(w.at++) = byte;
            w.ps1Columns(from, w.at);
            // the UI language's own lists (keylist.<k>.<n>), else the table's
            const char* lines[6] = {};
            for (int n = 0; n < 6; ++n) {
                const std::string key = "keylist." + std::to_string(k) + "." + std::to_string(n);
                lines[n] = ownWord(key.c_str());
                if (!lines[n]) break;
            }
            if (!lines[0])
                for (int n = 0; n < 6; ++n) lines[n] = kKeyListsLatin[lang][k][n];
            for (const char* line : lines) {
                if (!line) break;
                const std::string l = line;
                const size_t tab = l.find('|');
                for (const uint8_t g : lang::ps1Encode(l.substr(0, tab))) w.out.byte(w.at++) = g;
                if (tab != std::string::npos) {
                    w.out.byte(w.at++) = 0xfb;
                    w.out.byte(w.at++) = 0x2e;
                    w.out.byte(w.at++) = 0xc0 | 11;
                    for (const uint8_t g : lang::ps1Encode(l.substr(tab + 1))) w.out.byte(w.at++) = g;
                }
                w.out.byte(w.at++) = 0xfc;
            }
            w.out.byte(w.at - 1) = 0xff;
        }
        setKeyConfigBanks(kOptLatinKeys, kOptLatinLists);
    }
}

// exe 0x5eeda0: the page's inline values -- the corner pieces, the values frame, the underlines of the rows with a
// value, the value sprites (the chosen value clut 0x7f50, the others 0x7f51; the labels 0x7f90), the volume bars
void optionsValuesDraw(const StatusScreen& s) {
    if (!drawing()) return;
    GameOptions& o = gameOptions();
    const uint8_t cur[7] = {o.controllerLayout, o.lookDirection, o.autoLockBuster, o.autoLockSpecial,
                            o.vibrationOff, uint8_t(1 - o.ruby), o.mono};
    size_t c = otOpen(kOtFrames);
    otDrawMode(c, kOptMode);
    artBegin(kVecOptJunction);
    otSprite(c, 0x10, 0x94, kJunctionLeftArt, 0x20, 0x10);
    otSprite(c, 0x30, 0x94, kJunctionMiddleArt, 0x10, 0x10);
    otSprite(c, 0x40, 0x94, kJunctionRightArt, 0x20, 0x10);
    artEnd();
    frameDraw(0x10, 0x1f, 0x120, 0x7d, kFrameArrow);
    c = otOpen(kOtRows);
    otDrawMode(c, kOptMode);
    for (const int r : gLines) {
        const int y = 0x2c + r * 0x10;
        labelNext(kLblOptRail);
        otQuad(c, {0x4c, 0x118, 0x4c, 0x118}, {y, y, y + 9, y + 9}, kNeutral, kPolyFT4, {0x88, 0x90, 0x88, 0x90},
               {0x90, 0x90, 0x98, 0x98}, kRailClut, kOptMode);
        labelNext(kLblNone2);
        otSprite(c, 0x44, y, kRailLeftArt, 8, 8);
        labelNext(kLblNone2);
        otSprite(c, 0x118, y, kRailRightArt, 8, 8);
    }
    for (uint32_t k = 0; k < 9; ++k) {
        const RowTable t(gVal[k]);
        for (int i = 0; i < t.count; ++i) {
            const RowArt e = t.row(size_t(i));
            const uint32_t clut = k < 7 ? kValueClut + uint32_t(i != cur[k]) : kLabelClut;
            labelNext(kLblOptValue, uint16_t(k < 7 ? (i == cur[k] ? 1 : 0) : 2), e.uv);
            otSprite(c, e.x, e.y, clut << 16 | e.uv, e.w, e.h);
        }
    }
    // OpenDash's rows' values on their rails, as the game's rows show theirs (Herbert: the page full of what is set)
    for (size_t i = 0; i < gRows.size(); ++i) {
        const int lang = std::clamp(gHost.language ? gHost.language() : 0, 0, 2);
        const int16_t y = int16_t(kCaptionY + i * 0x10);
        auto box = [&](int x, int w, const char* text, int kind) {
            labelNext(kLblOptText, uint16_t(kind));
            ScreenPrim& t = otPut(c, ScreenPrim::Text);
            t.x = t.ax = int16_t(x);
            t.y = y;
            t.w = int16_t(w);
            t.h = 0x10;
            t.utf8 = text;
        };
        static std::string screen, langName;
        if (gRows[i].state == kMusicState) { // original / rearranged, the chosen one lit (as stereo / mono)
            const int m = gHost.music ? gHost.music() : 0;
            for (int v = 0; v < 2; ++v) {
                const std::string key = "musicval.0." + std::to_string(v);
                static std::string vals[2];
                const char* own = ownWord(key.c_str());
                static const char* const kVal[3][2] = {{kMusicJpOriginal, kMusicJpArrange},
                                                       {"Original", "Rearranged"}, {"Original", "Rearranjada"}};
                vals[v] = own ? own : kVal[lang][v];
                if (vals[v].size() > 4 && vals[v].rfind("< ", 0) == 0) vals[v] = vals[v].substr(2, vals[v].size() - 4);
                box(v == 0 ? 0x88 : 0xd8, 0x40, vals[v].c_str(), v == m ? 1 : 0);
            }
        } else if (i >= kPanelRows) { // (under the values panel: the help's box is there, no values)
        } else if (gRows[i].exe == 7 && gHost.video) { // the screen: its size
            const OptionsHost::Video v = gHost.video();
            const OptionsHost::VideoMode& m = v.display == OptionsHost::DisplayMode::Exclusive ? v.exclusive : v.window;
            screen = m.w > 0 ? std::to_string(m.w) + " \xc3\x97 " + std::to_string(m.h) : std::string();
            if (!screen.empty()) box(0xb8, 0x60, screen.c_str(), 1);
        } else if (gRows[i].state == kLangState) { // the language: its flag and its name
            const int l = gHost.textLanguage ? gHost.textLanguage() : 0;
            const char* name = gHost.languageName ? gHost.languageName(l) : nullptr;
            langName = name ? name : "";
            if (const size_t nl = langName.find('\n'); nl != std::string::npos) langName.resize(nl);
            labelNext(kLblFlag, uint16_t(lang::languageBase(lang::Language(l))));
            ScreenPrim& f = otPut(c, ScreenPrim::Text);
            f.x = f.ax = 0x84;
            f.y = int16_t(y + 4);
            f.w = 0x14;
            f.h = 8;
            if (!langName.empty()) box(0x9c, 0x7c, langName.c_str(), 1);
        }
    }
    // the text captions (the music, text size and group rows: no cell of the texture is free for them)
    static std::vector<uint16_t> pcCaption[16];
    for (size_t i = 0; i < gRows.size(); ++i) {
        const uint32_t uv = textCaptionUv(gRows[i]);
        if (uv == 0) continue;
        const int lang = gHost.language ? gHost.language() : 0;
        // the vector UI draws it as the other rows (its bar, the row's state: as rowsDraw), the word row.6.<uv>
        const RowTable tbl(kOptCaptions);
        uint32_t pc = tbl.levelOffset != 0 ? uint32_t(int32_t(tbl.levelOffset)) : 0;
        for (uint32_t m = tbl.skipLevels; m != 0 && int8_t(s.level(pc)) > 2; --m) ++pc;
        const int level = int8_t(s.level(pc));
        const uint16_t state = level < int(kFirstChildState) ? (s.cursor == int(i) ? 1 : 0)
                               : level - int(kFirstChildState) == int(i) ? 2 : 3;
        labelNext(kLblPageRow, uint16_t(i | state << 8), uint32_t(StatusPage::Options) << 16 | uv);
        ScreenPrim& t = otPut(c, ScreenPrim::Text);
        // at the caption's box (the other rows' place: the vector UI's bar), the text's place without it
        t.x = t.ax = int16_t(kCaptionX);
        t.y = int16_t(kCaptionY + i * 0x10);
        t.w = kCaptionSize & 0xffff;
        t.h = int16_t(kCaptionSize >> 16);
        std::vector<uint16_t>& g = pcCaption[std::min<size_t>(i, 15)];
        const bool music = gRows[i].state == kMusicState;
        const std::string key = music ? std::string("music.caption") : "row.6." + [&] {
            char b[8];
            std::snprintf(b, sizeof b, "%04x", unsigned(uv));
            return std::string(b);
        }();
        g = lang == 0 ? ownPcWord(key.c_str()) : std::vector<uint16_t>();
        if (!g.empty()) g.push_back(0);
        else if (lang == 0 && music) g.assign(std::begin(kMusicCaptionJp), std::end(kMusicCaptionJp));
        else if (lang == 0) g = jpGlyphString(textCaption(gRows[i], 0));
        const char* own = lang == 0 ? nullptr : ownWord(key.c_str());
        if (!g.empty()) t.pc = g.data();
        else t.utf8 = own ? own : textCaption(gRows[i], lang);
    }
    // the volume boxes: only with the volume row on the page
    bool volumeRow = false;
    for (const OptRow& r : gRows) volumeRow = volumeRow || r.exe == 6;
    if (!volumeRow) return;
    const int vy = gVolY;
    SoundMasters& volume = soundMasters();
    labelNext(kLblVolBox);
    otSprite(c, 0x78, vy, kVolumeBoxArt, 0x40, 0x10);
    labelNext(kLblVolBox);
    otSprite(c, 0xd8, vy, kVolumeBoxArt, 0x40, 0x10);
    labelNext(kLblVolLevel);
    otSprite(c, 0x78, vy + 1, kVolumeLevelArt, ((volume.bgm + 7) >> 3) * 3 + 8, 0xe);
    labelNext(kLblVolLevel);
    otSprite(c, 0xd8, vy + 1, kVolumeLevelArt, ((volume.sfx + 7) >> 3) * 3 + 8, 0xe);
}
// exe 0x5ef210(block, x, y): the volume bar being set (clut 0x7f90 frame, the level from the cursor)
void volumeBarDraw(const StatusScreen& s, int x, int y) {
    if (!drawing()) return;
    const size_t c = otOpen(kOtRows);
    otDrawMode(c, kOptMode);
    labelNext(kLblVolBox, 1);
    otSprite(c, x, y, kVolumeSetBoxArt, 0x40, 0x10);
    labelNext(kLblVolLevel, 1);
    otSprite(c, x, y + 1, kVolumeSetLevelArt, s.cursor * 3 + 8, 0xe);
    // OpenDash: the bar's 17 levels are the mouse's rows (the level's cursor row: game/status_controls menuCursorStep)
    for (int k = 0; k <= 0x10; ++k) {
        const int x0 = k == 0 ? x - 4 : x + 3 * k + 6, x1 = k == 0x10 ? x + 0x44 : x + 3 * k + 9;
        otHitRow(c, x0, y - 2, x1 - x0, 0x14, k);
    }
}
// exe 0x5edce0 (state 0): set up; the title fades in first
void optionsSetUp(StatusScreen& s) {
    const OptionsStep c = OptionsStep(s.sub);
    if (c == OptionsStep::Prepare) {
        cursorReset(s);
        s.testRumble = 0;
        gGroup = -1; // (the root first)
        optLayout();
        if (gameState().frontEnd == kFrontEndTitle) {
            if (gHost.fade) gHost.fade(kOptionsFadeIn, 0);
            s.sub = uint8_t(OptionsStep::WaitFade);
            return;
        }
        s.sub = uint8_t(OptionsStep::Done);
    } else if (c == OptionsStep::WaitFade) {
        if (fadeState().busy != 0) return;
        s.sub = uint8_t(OptionsStep::Done);
        return;
    } else if (c != OptionsStep::Done) {
        return;
    }
    s.switching = 0;
    s.state = gExtrasMode ? kExtrasState : uint8_t(OptionsState::Rows);
    s.sub = 0;
}
// exe 0x5edd40 (state 1): the rows; the vibration row needs a pad that vibrates (exe 0x409f80)
// OpenDash: the groups -- a group's row opens it (its rows, the cursor on the first), its Back row and cross go back to
// the root (the cursor on that group's row)
void showGroup(StatusScreen& s, int group, int cursor) {
    gGroup = group;
    optLayout();
    s.cursorKept = uint8_t(std::clamp(cursor, 0, int(gRows.size()) - 1));
    s.cursor = int8_t(s.cursorKept);
    s.cursorStep = 0;
}
void optionsRows(StatusScreen& s) {
    if (gGroup >= 0 && (menuPadPressed() & kPadCross)) { // a group: cross is its Back row
        soundPlayFlatForced(uint16_t(UiSound::Cancel));
        showGroup(s, -1, gGroup);
        return;
    }
    menuCursorStep(s, 1);
    if (menuPadPressed() & kPadConfirm) {
        const int r = s.cursor;
        if (r < 0 || r >= int(gRows.size())) return;
        const OptRow row = gRows[size_t(r)];
        const uint8_t st = row.state;
        if (st == kGroupState) {
            showGroup(s, row.arg, 0);
            return;
        }
        if (st == kGroupBackState) {
            showGroup(s, -1, gGroup);
            return;
        }
        if (st == uint8_t(OptionsState::Vibration) && !(gHost.rumble && gHost.rumble())) return;
        s.state = st;
    }
}
// exe 0x5edd90 (state 2): leave -- the panel's cursor on its "options" row (the title: a fade, then the page switch
// that closes the sub-screen)
void optionsLeave(StatusScreen& s) {
    const bool title = gameState().frontEnd == kFrontEndTitle;
    if (!title) s.sub = uint8_t(OptionsStep::Done);
    const OptionsStep c = OptionsStep(s.sub);
    if (c == OptionsStep::Prepare) {
        if (gHost.fade) gHost.fade(kOptionsFadeOut, 0);
        s.sub = uint8_t(uint8_t(c) + 1u);
    } else if (c == OptionsStep::WaitFade) {
        if (fadeState().busy == 0) s.sub = uint8_t(OptionsStep::Done);
    } else if (c == OptionsStep::Done) {
        const uint8_t page = s.page;
        s.page = uint8_t(StatusPage::Panel);
        s.state = 0;
        s.cursorKept = uint8_t(page - 3u);
        s.sub = 0;
        if (gameState().frontEnd == kFrontEndTitle) { // the title: the page switch to the closing page
            s.switching = 1;
            s.switchRow = 4;
            s.switchPage = uint8_t(StatusPage::Closing);
            s.switchState = 0;
            s.switchCountdown = 2;
        }
    }
}
// the cursor on the row of OpenDash's state `state` (exe 0x5e96f0 put it on state - 3)
void cursorOnRow(StatusScreen& s, uint8_t state) {
    for (size_t i = 0; i < gRows.size(); ++i)
        if (gRows[i].state == state) s.cursorKept = uint8_t(i);
}
// back from one of the game's rows: the cursor on that row of OpenDash's list (Herbert: Som came back on Volume --
// levelBack keeps the exe's row, state - 3, and the list has no furigana row outside Japanese)
void rowBack(StatusScreen& s) {
    const uint8_t st = uint8_t(s.level(1));
    levelBack(s, 2);
    cursorOnRow(s, st);
}
// a two-choice row (states 4 / 6 / 7 / 8: exe 0x5ee070 / 0x5ee280 / 0x5ee350 / 0x5ee3f0): the option set while the
// cursor moves (the choice table's kChoiceOff / kChoiceOn), circle back to the rows; the vibration's off -> on runs a
// test rumble
void optionChoice(StatusScreen& s, uint8_t& option, bool inverted, bool vibration, uint32_t valueTable,
                  uint32_t choiceTable) {
    switch (RowStep(s.sub)) {
    case RowStep::Enter: {
        cursorReset(s);
        s.sub = uint8_t(RowStep::Cursor);
        const uint8_t v = inverted ? uint8_t(option == 0) : uint8_t(option);
        s.cursorKept = v;
        s.cursor = int8_t(v);
        break;
    }
    case RowStep::Cursor: {
        menuCursorStep(s, 2);
        const DataArray<uint8_t> choices(choiceTable);
        const uint8_t t = choices[size_t(uint32_t(int32_t(s.cursor)))];
        if (t == kChoiceOff) {
            if (vibration) { // vibration off -> on: a test rumble (exe 0x55cd10), the dual-shock flag
                if (option == 1) {
                    option = 0;
                    padVibrate(kTestRumbleA, kTestRumbleB);
                    if (s.padKind == kPadKindAnalog) s.testRumble = 1;
                }
            } else {
                option = inverted ? 1 : 0;
            }
        } else if (t == kChoiceOn) {
            option = inverted ? 0 : 1;
        }
        if (menuPadPressed() & kPadConfirm) s.sub = uint8_t(RowStep::Back);
        break;
    }
    case RowStep::Back:
        rowBack(s);
        break;
    default:
        break;
    }
    rowsDraw(s, valueTable);
    s.cursor = int8_t(s.cursorKept);
}
// state 5 (exe 0x5ee110: [0x91b838 + +2 * 4], then the labels): the lock-on row -- exe 0x5ee140 picks the button (the
// special weapon's / the buster's), exe 0x5ee1a0 its mode (manual / auto)
void optionsLockOn(StatusScreen& s) {
    const uint8_t st = s.sub;
    GameOptions& o = gameOptions();
    if (st <= uint8_t(RowStep::Back)) { // exe 0x5ee140
        if (st == uint8_t(RowStep::Enter)) {
            cursorReset(s);
            s.sub = uint8_t(RowStep::Cursor);
        } else if (st == uint8_t(RowStep::Cursor)) {
            menuCursorStep(s, 2);
            if (menuPadPressed() & kPadConfirm) s.sub = lockOnRows[size_t(int32_t(s.cursor))];
        } else {
            rowBack(s);
        }
    } else { // exe 0x5ee1a0
        uint8_t& option = st == kChoiceOff ? o.autoLockBuster : o.autoLockSpecial;
        const uint8_t s3 = s.step;
        if (s3 == uint8_t(RowStep::Enter)) {
            cursorReset(s);
            s.step = uint8_t(RowStep::Cursor);
            s.cursorKept = option;
            s.cursor = int8_t(option);
        } else if (s3 == uint8_t(RowStep::Cursor)) {
            menuCursorStep(s, 3);
            const int8_t t = lockOnModes[size_t(int32_t(s.cursor))];
            if (t > 2 && t < 5) option = uint8_t(t - 3);
            if (menuPadPressed() & kPadConfirm) {
                s.sub = uint8_t(RowStep::Back);
                s.step = 0;
            }
        } else if (s3 == uint8_t(RowStep::Back)) {
            levelBack(s, 3);
        }
        rowsDraw(s, st == kChoiceOff ? gVal[kValueLockFirst] : gVal[kValueLockSecond]);
        s.cursor = int8_t(s.cursorKept);
    }
    rowsDraw(s, gVal[kValueLockLabels]);
    s.cursor = int8_t(s.cursorKept);
}
// state 9 (exe 0x5ee490: [0x91b860 + +2 * 4], then the labels): the volume row -- exe 0x5ee4c0 picks the music / the
// effects, exe 0x5ee520 its level (17 steps of 8: the music's applied to the streams)
void optionsVolume(StatusScreen& s) {
    const uint8_t st = s.sub;
    SoundMasters& volume = soundMasters();
    if (st <= uint8_t(RowStep::Back)) { // exe 0x5ee4c0
        if (st == uint8_t(RowStep::Enter)) {
            cursorReset(s);
            s.sub = uint8_t(RowStep::Cursor);
        } else if (st == uint8_t(RowStep::Cursor)) {
            menuCursorStep(s, 2);
            if (menuPadPressed() & kPadConfirm) s.sub = volumeRows[size_t(int32_t(s.cursor))];
        } else {
            rowBack(s);
        }
    } else { // exe 0x5ee520
        const bool music = st == kChoiceOff;
        const uint8_t s3 = s.step;
        if (s3 == uint8_t(RowStep::Enter)) {
            cursorReset(s);
            s.step = uint8_t(RowStep::Cursor);
            const int8_t v = int8_t((int16_t(music ? volume.bgm : volume.sfx) + 7) >> 3);
            s.cursorKept = uint8_t(v);
            s.cursor = v;
        } else if (s3 == uint8_t(RowStep::Cursor)) {
            menuCursorStep(s, 3);
            menuWindowOpen(0, stateAddress(TableId::MenuBankOptionsVolume), kVolumeLevelHelp);
            int v = int(int16_t(s.cursor)) << 3;
            if (v >= 0x80) v = kSoundMasterFull;
            else if (v < 0) v = 0;
            if (music) {
                volume.bgm = int8_t(v);
                soundApplyBgmMaster();
            } else {
                volume.sfx = int8_t(v);
            }
            if (menuPadPressed() & kPadConfirm) {
                s.sub = uint8_t(RowStep::Back);
                s.step = 0;
            }
        } else if (s3 == uint8_t(RowStep::Back)) {
            levelBack(s, 3);
        }
        volumeBarDraw(s, music ? 0x78 : 0xd8, gVolY);
        s.cursor = int8_t(s.cursorKept);
    }
    rowsDraw(s, gVal[kValueVolumeLabels]);
    s.cursor = int8_t(s.cursorKept);
}
// the overlay of the video row (and of OpenDash's rows): the screen dimmed, the box, the cursor bar on the
// line, the corner brackets (exe 0x5ee620's packets)
// OpenDash: the dimming runs past the 320 px screen (a widescreen window shows the sides too)
constexpr int kWideMargin = 0x200;
// OpenDash: the Extras' lists -- the box's padding above / below the rows; a row's words: their middle is the string's
// y + 6 (MessageUi::menuText scales them about it), so they stay centred on the bar at every text size
constexpr int kListPad = 6;
int listRowTextY(int top, int step, int k) { return top + kListPad + k * step + step / 2 - 6; }
// top: the box's y (the exe's 0x48; OpenDash's language row starts higher), extra: px more under the rows (its notes)
void overlayFrame(const StatusScreen& s, int rows, bool icons, int top = 0x48, int extra = 0, int step = 0x20,
                  int boxX = 0x28, int boxW = 0xf4, bool centred = false, int hitBase = 0) {
    if (!drawing()) return;
    if (centred) { // OpenDash (the Extras' lists): the rows centred in the box (kListPad above and below), the bar across
                   // the box on the cursor's row, the words centred on it (listRowTextY) at any text size
        const size_t c = otOpen(kOtOverlay);
        otDrawMode(c, kOptMode);
        otTile(c, -kWideMargin, 0, kScreenWidth + 2 * kWideMargin, kScreenHeight, 0, kTileSemi);
        otTile(c, int16_t(boxX), int16_t(top), int16_t(boxW), int16_t(rows * step + 2 * kListPad), 0, kTileSemi);
        otTile(c, -kWideMargin, 0, kScreenWidth + 2 * kWideMargin, kScreenHeight, 0, kTileSemi); // (darker: as the
        otTile(c, int16_t(boxX), int16_t(top), int16_t(boxW), int16_t(rows * step + 2 * kListPad), 0, kTileSemi); // rest)
        const int cy = top + kListPad + int8_t(s.step) * step + step / 2;
        otTile(c, int16_t(boxX + 4), int16_t(cy - (step - 2) / 2), int16_t(boxW - 8), int16_t(step - 2), kCursorBarColour,
               kTileSemi);
        for (int k = 0; k < rows; ++k) // OpenDash: the lines are the mouse's rows (hitBase + line)
            otHitRow(c, int16_t(boxX), int16_t(top + kListPad + k * step), int16_t(boxW), int16_t(step), hitBase + k);
        return;
    }
    const size_t c = otOpen(kOtOverlay);
    otDrawMode(c, kOptMode);
    otTile(c, -kWideMargin, 0, kScreenWidth + 2 * kWideMargin, kScreenHeight, 0, kTileSemi);
    // (the exe's grid of the screen-position row is not drawn: OpenDash has no screen position to set)
    otTile(c, int16_t(boxX), int16_t(top), int16_t(boxW), int16_t(rows * step + extra), 0, kTileSemi);
    // OpenDash (Herbert: every window over the menu as dark as the language's -- the page under it does not show
    // through the words): the dimming and the box once more
    otTile(c, -kWideMargin, 0, kScreenWidth + 2 * kWideMargin, kScreenHeight, 0, kTileSemi);
    otTile(c, int16_t(boxX), int16_t(top), int16_t(boxW), int16_t(rows * step + extra), 0, kTileSemi);
    // (the cursor's bar: the values' column to the box's right edge less 8)
    const int barX = boxX == 0x28 ? 0x4e : boxX + 0x7a, barW = boxX == 0x28 ? 0xc8 : boxX + boxW - 8 - barX;
    otTile(c, int16_t(barX), int16_t(int8_t(s.step) * step + top + 8), int16_t(barW), 0x10, kCursorBarColour, kTileSemi);
    for (int k = 0; k < rows; ++k) // OpenDash: the lines are the mouse's rows (hitBase + line)
        otHitRow(c, int16_t(boxX), int16_t(top + k * step), int16_t(boxW), int16_t(step), hitBase + k);
    if (icons && optionsPictures())
        for (int k = 0; k < 3; ++k) {
            labelNext(kLblVideoIcon, uint16_t(k)); // (OpenDash: drawn in HD as the line's meaning, app/ui/vector_ui)
            otSprite(c, 0x2c, 0x4c + k * 0x20, kVideoIconArts[k], 0x19, 0x19, kNeutral, kSpriteHalf);
        }
    // (the exe's four corner brackets -- kCornerArts, the CRT's screen-position marks -- are not drawn: OpenDash has
    // no screen position to set)
}
// state 0xa: OpenDash's video row (the exe's place for it, its overlay and icons) -- three lines: the anti-aliasing,
// the resolution (the window's size / the exclusive full screen's mode; borderless shows the desktop's, fixed), the
// display mode (window / borderless / exclusive); left / right change the line's value, circle applies (and persists)
// what changed, cross back. The line is +3.
void optionsVideo(StatusScreen& s) {
    using DM = OptionsHost::DisplayMode;
    const uint8_t st = s.sub;
    if (st == uint8_t(RowStep::Enter)) {
        if (gHost.video) gVideoEdit = gHost.video();
        gVideoShown = gVideoEdit;
        cursorReset(s);
        s.sub = uint8_t(RowStep::Cursor);
        s.step = 0;
    } else if (st == uint8_t(RowStep::Cursor)) {
        uint8_t p = uint8_t(menuDirections() | (menuPadPressed() & ~(kPadUp | kPadDown | kPadLeft | kPadRight)));
        // OpenDash: the mouse -- onto a line moves there, a click on a line steps its value and applies it
        MenuPointer& pt = menuPointer();
        bool clickApply = false;
        optionsOverlayPointer(s, 3, kValueCell);
        if (pt.click) {
            pt.click = false;
            if (pt.row == kValueCell + int8_t(s.step)) {
                p = kPadRight;
                clickApply = true;
            }
        }
        if (p & (kPadLeft | kPadRight)) {
            soundPlayFlat(UiSound::Cursor);
            const int step = (p & kPadLeft) ? -1 : 1;
            const int8_t line = int8_t(s.step);
            if (line == 0) {
                const int n = gHost.aaCount ? gHost.aaCount() : 1;
                gVideoEdit.aa = n > 0 ? (gVideoEdit.aa + step + n) % n : 0;
            } else if (line == 1 && gVideoEdit.display != DM::Borderless && gHost.videoModes) {
                const std::vector<OptionsHost::VideoMode> modes = gHost.videoModes(gVideoEdit.display);
                OptionsHost::VideoMode& cur =
                    gVideoEdit.display == DM::Windowed ? gVideoEdit.window : gVideoEdit.exclusive;
                const int n = int(modes.size());
                if (n > 0) {
                    const auto it = std::find(modes.begin(), modes.end(), cur);
                    // a size not in the list (a window resized by hand): the step starts from the list's ends
                    const int i = it != modes.end() ? int(it - modes.begin()) : (step > 0 ? -1 : n);
                    cur = modes[size_t((i + step + n) % n)];
                }
            } else if (line == 2) {
                gVideoEdit.display = DM((int(gVideoEdit.display) + step + 3) % 3);
            }
            if (clickApply && !(gVideoEdit == gVideoShown) && gHost.videoApply) {
                gHost.videoApply(gVideoEdit);
                if (gHost.video) gVideoEdit = gHost.video();
                gVideoShown = gVideoEdit;
            }
        } else if (p & kPadUp) {
            soundPlayFlat(UiSound::Cursor);
            int8_t l = int8_t(int8_t(s.step) - 1);
            if (l < 0) l = 2;
            s.step = uint8_t(l);
        } else if (p & kPadDown) {
            soundPlayFlat(UiSound::Cursor);
            int8_t l = int8_t(int8_t(s.step) + 1);
            if (l > 2) l = 0;
            s.step = uint8_t(l);
        } else if (menuPadPressed() & kPadCross) {
            soundPlayFlat(UiSound::Cancel);
            s.sub = uint8_t(RowStep::Back);
        } else if (menuPadPressed() & kPadConfirm) {
            soundPlayFlat(UiSound::Confirm);
            if (!(gVideoEdit == gVideoShown) && gHost.videoApply) {
                gHost.videoApply(gVideoEdit);
                if (gHost.video) gVideoEdit = gHost.video(); // what the display gave (the closest mode)
                gVideoShown = gVideoEdit;
            }
        }
    } else if (st == uint8_t(RowStep::Back)) {
        rowBack(s);
    }
    const bool pt = gHost.language && gHost.language() == 2;
    std::string aa = gHost.aaName ? gHost.aaName(gVideoEdit.aa) : "";
    for (char& c : aa) c = char(std::toupper(uint8_t(c)));
    while (asciiTextWidth(aa) > 0xb4 && !aa.empty()) aa.pop_back();
    OptionsHost::VideoMode res = gVideoEdit.display == DM::Windowed ? gVideoEdit.window : gVideoEdit.exclusive;
    if (gVideoEdit.display == DM::Borderless && gHost.videoModes) {
        const std::vector<OptionsHost::VideoMode> desk = gHost.videoModes(DM::Borderless);
        res = desk.empty() ? OptionsHost::VideoMode{} : desk.front();
    }
    char mode[48];
    if (res.hz > 0) std::snprintf(mode, sizeof mode, "%dx%d %dHZ", res.w, res.h, res.hz);
    else std::snprintf(mode, sizeof mode, "%dx%d", res.w, res.h);
    static constexpr const char* kDisplayEn[] = {"Window", "Borderless", "Full screen"};
    static constexpr const char* kDisplayPt[] = {"Janela", "Sem borda", "Tela cheia"};
    // (OpenDash, Herbert: the values in the overlays' own proportional font, "< >" as the other rows -- not the PS1
    // ASCII cells, one letter apart)
    if (drawing()) {
        static std::string vals[3];
        vals[0] = "< " + aa + " >";
        std::string m = mode;
        if (const size_t xpos = m.find('x'); xpos != std::string::npos) m.replace(xpos, 1, " x ");
        vals[1] = "< " + m + " >";
        vals[2] = std::string("< ") + (pt ? kDisplayPt : kDisplayEn)[int(gVideoEdit.display)] + " >";
        const size_t c = otOpen(kOtOverlay);
        for (int k = 0; k < 3; ++k) {
            ScreenPrim& t = otPut(c, ScreenPrim::Text);
            t.x = t.ax = 0x58;
            t.y = int16_t(0x52 + k * 0x20);
            t.utf8 = vals[k].c_str();
        }
    }
    overlayFrame(s, 3, true, 0x48, 0, 0x20, 0x28, 0xf4, false, kValueCell);
    s.cursor = int8_t(s.cursorKept);
}
// state 0xb (exe 0x5eec80): the defaults -- the question (window 0), then (yes) the settings and the bindings back to
// their defaults
void optionsDefaults(StatusScreen& s) {
    switch (RowStep(s.sub)) {
    case RowStep::Enter:
        cursorReset(s);
        s.sub = uint8_t(RowStep::Cursor);
        break;
    case RowStep::Cursor:
        menuWindowOpen(0, stateAddress(TableId::MenuBankOptionsDefaults), 0);
        s.sub = uint8_t(RowStep::Ask);
        [[fallthrough]];
    case RowStep::Ask:
        if (windowStatus(0) == 0) {
            const uint32_t a = messageSystem().answerOf(0);
            if (a == 1) s.sub = uint8_t(RowStep::Apply);
            else if (a == 0 || a == 2) s.sub = uint8_t(RowStep::Back);
        }
        break;
    case RowStep::Apply: {
        GameOptions& o = gameOptions();
        o.controllerLayout = 0;
        o.controlOption = 0;
        o.lookDirection = 0;
        o.autoLockBuster = 0;
        o.autoLockSpecial = 0;
        o.vibrationOff = 0;
        o.ruby = 1;
        o.mono = 0;
        copyDefaultButtonMap(player()); // exe 0x55cbb0
        soundMasters().bgm = kSoundMasterFull;
        soundMasters().sfx = kSoundMasterFull;
        inputConfigDefaults();
        if (gHost.bindingsChanged) gHost.bindingsChanged();
        [[fallthrough]];
    }
    case RowStep::Back:
        rowBack(s);
        break;
    default:
        break;
    }
    s.cursor = int8_t(s.cursorKept);
}
// state 0xc (OpenDash): the language row, on the video row's overlay -- the text's language, the voices' (each its own:
// e.g. Japanese voices with Portuguese text), the subtitles of the voiced lines without a window (on / off) and, with
// other voices, which text they show (the dub's lines / translated straight from those voices by AI); up / down pick
// the line, left / right change it, circle applies (and persists) what changed, cross back. The line is +3.
int gEditLang[6] = {0, 0, 1, 0, 1, 1}; // the text's, the voices' language, the subtitles, their source, the text size,
                                       // the furigana being edited
constexpr int kLangTop = 0x20; // the language box starts higher than the exe's 0x48 (its notes go under the rows)
int gLangNoteLines = 0;   // the note lines drawn under the rows (the box grows by them)
// OpenDash (Herbert): the text is a language and a version of it -- the rows Text (Japanese / English / Portuguese),
// Version (only when that language has more than one: the release's or the fandub, faithful to the Japanese, faithful
// with the US names; what the picked one is goes under the box), Voices, Subtitles (the cutscenes' lines on / off)
// OpenDash (Herbert, the page back as the game's): the text size and (Japanese text) the furigana are lines here too
enum class LangRow : uint8_t { Text, Version, Voices, Subtitles, TextSize, Furigana };
constexpr int kLangRowsMax = 6;
// the language window wider than the game's boxes (Herbert: the widescreen has room): its box, its values' column
constexpr int kLangBoxX = 0x10, kLangBoxW = 0x124, kLangValueX = 0x98;
// the lines' spacing: the rows and the most note lines (3 / 4) fit above the hints (y 0xca), the gap between them
// kept (Herbert) -- whether the note is up or not, so the rows never move
constexpr int kLangNoteGap = 8;
int langStep(int lines) {
    const int notes = lines > 4 ? 3 : 4;
    const int room = 0xca - kLangTop - (notes * 0x0e + 8 + kLangNoteGap);
    return std::clamp(room / std::max(1, lines), 0x12, 0x20);
}
bool langOffered(lang::Language l) {
    return l != lang::Language::Count && (!gHost.textLanguageOffered || gHost.textLanguageOffered(int(l)));
}
int versionsOffered(int base) {
    int n = 0;
    for (int v = 0; v < lang::kLanguageVersions; ++v) n += langOffered(lang::languageOf(base, v)) ? 1 : 0;
    return n;
}
int langRows(LangRow out[kLangRowsMax], bool voices, bool subs) {
    int n = 0;
    out[n++] = LangRow::Text;
    if (versionsOffered(lang::languageBase(lang::Language(gEditLang[0]))) > 1) out[n++] = LangRow::Version;
    if (voices) out[n++] = LangRow::Voices;
    if (subs) out[n++] = LangRow::Subtitles;
    if (gHost.textSize && gHost.setTextSize) out[n++] = LangRow::TextSize;
    if (lang::languageBase(lang::Language(gEditLang[0])) == 0 && sourceRows()[4] >= 0) out[n++] = LangRow::Furigana;
    return n;
}
void optionsLanguage(StatusScreen& s) {
    const bool voices = bool(gHost.voiceLanguage) && bool(gHost.setVoiceLanguage);
    const bool subs = voices && gHost.subtitles && gHost.setSubtitles;
    LangRow rows[kLangRowsMax];
    int lines = langRows(rows, voices, subs);
    const uint8_t st = s.sub;
    if (st == uint8_t(RowStep::Enter)) {
        cursorReset(s);
        s.sub = uint8_t(RowStep::Cursor);
        s.step = 0;
        gEditLang[0] = gHost.textLanguage ? gHost.textLanguage() : gHost.language ? gHost.language() : 0;
        gEditLang[1] = voices ? gHost.voiceLanguage() : gEditLang[0];
        gEditLang[2] = subs ? int(gHost.subtitles()) : 1;
        gEditLang[4] = gHost.textSize ? std::clamp(gHost.textSize(), 0, 2) : 1;
        gEditLang[5] = gameOptions().ruby ? 1 : 0;
        lines = langRows(rows, voices, subs);
    } else if (st == uint8_t(RowStep::Cursor)) {
        const uint8_t p = optionsOverlayPointer(s, lines) ? 0 : uint8_t(menuPadPressed()); // OpenDash: the mouse's line
        const int row = std::min<int>(int8_t(s.step), lines - 1);
        if (p & (kPadLeft | kPadRight)) {
            soundPlayFlat(UiSound::Cursor);
            const int dir = (p & kPadLeft) ? -1 : 1;
            const lang::Language now = lang::Language(gEditLang[0]);
            const int base = lang::languageBase(now), version = lang::languageVersion(now);
            switch (rows[row]) {
            case LangRow::Text: // the next language with a version here: its version 0, else its first (Herbert: the
                                // release's / the one translated from the English is every language's default)
                for (int k = 1; k <= lang::kLanguageBases; ++k) {
                    const int b = (base + dir * k + lang::kLanguageBases * 2) % lang::kLanguageBases;
                    if (versionsOffered(b) == 0) continue;
                    lang::Language l = lang::languageOf(b, 0);
                    for (int v = 0; !langOffered(l) && v < lang::kLanguageVersions; ++v) l = lang::languageOf(b, v);
                    gEditLang[0] = int(l);
                    break;
                }
                break;
            case LangRow::Version:
                for (int k = 1; k <= lang::kLanguageVersions; ++k) {
                    const lang::Language l =
                        lang::languageOf(base, (version + dir * k + lang::kLanguageVersions * 2) % lang::kLanguageVersions);
                    if (langOffered(l)) {
                        gEditLang[0] = int(l);
                        break;
                    }
                }
                break;
            case LangRow::Voices: {
                const int count = gHost.voiceLanguageCount ? gHost.voiceLanguageCount() : 1;
                gEditLang[1] = (gEditLang[1] + dir + count) % count;
                break;
            }
            case LangRow::Subtitles: gEditLang[2] = 1 - gEditLang[2]; break;
            case LangRow::TextSize: gEditLang[4] = (gEditLang[4] + dir + 3) % 3; break;
            case LangRow::Furigana: gEditLang[5] = 1 - gEditLang[5]; break;
            }
            lines = langRows(rows, voices, subs);
            if (int8_t(s.step) >= lines) s.step = uint8_t(lines - 1);
        } else if (p & kPadUp) {
            soundPlayFlat(UiSound::Cursor);
            s.step = uint8_t((int8_t(s.step) + lines - 1) % lines);
        } else if (p & kPadDown) {
            soundPlayFlat(UiSound::Cursor);
            s.step = uint8_t((int8_t(s.step) + 1) % lines);
        } else if (menuPadPressed() & kPadCross) {
            soundPlayFlat(UiSound::Cancel);
            s.sub = uint8_t(RowStep::Back);
        } else if (menuPadPressed() & kPadConfirm) {
            soundPlayFlat(UiSound::Confirm);
            if (voices && gEditLang[1] != gHost.voiceLanguage()) gHost.setVoiceLanguage(gEditLang[1]);
            if (subs && (gEditLang[2] != 0) != gHost.subtitles()) gHost.setSubtitles(gEditLang[2] != 0);
            if (gHost.textSize && gHost.setTextSize && gEditLang[4] != gHost.textSize()) gHost.setTextSize(gEditLang[4]);
            gameOptions().ruby = uint8_t(gEditLang[5]);
            const int textNow = gHost.textLanguage ? gHost.textLanguage() : gHost.language ? gHost.language() : 0;
            if (gEditLang[0] != textNow && gHost.setLanguage) {
                gHost.setLanguage(gEditLang[0]);
                optLayout(); // the rows of the new language: the cursor stays on this row
                cursorOnRow(s, kLangState);
            }
            s.sub = uint8_t(RowStep::Back);
        }
    } else if (st == uint8_t(RowStep::Back)) {
        levelBack(s, 2);
        cursorOnRow(s, kLangState);
    }
    if (drawing()) {
        // the PC glyph codes of the Japanese words (font slot 0: hiragana from 0x80, Latin at their ASCII codes)
        static constexpr uint16_t kJp[] = {0x95, 0x9d, 0xad, 0xbb, 0};                    // nihongo
        static constexpr uint16_t kLabelsJp[4][5] = {{0xa2, 0xbd, 0},                     // moji
                                                     {0x99, 0xad, 0},                     // han
                                                     {0x89, 0x83, 0},                     // koe
                                                     {0xbd, 0x9e, 0x87, 0}};              // jimaku
        static constexpr uint16_t kOnOffJp[2][3] = {{0x94, 0x8b, 0}, {0x80, 0xa7, 0}};    // nashi / ari
        static const char* const kLabels[3][6] = {
            {"", "", "", "", "", ""}, {"Text", "Version", "Voices", "Subtitles", "Text size", "Furigana"},
            {"Texto", "Vers\xc3\xa3o", "Vozes", "Legendas", "Tamanho do texto", "Furigana"}};
        // the new lines' Japanese labels (UTF-8, as PC glyphs): もじのおおきさ, ふりがな
        static const char* const kLabelsJpText[2] = {
            "\xe3\x82\x82\xe3\x81\x98\xe3\x81\xae\xe3\x81\x8a\xe3\x81\x8a\xe3\x81\x8d\xe3\x81\x95",
            "\xe3\x81\xb5\xe3\x82\x8a\xe3\x81\x8c\xe3\x81\xaa"};
        static std::vector<uint16_t> jpNew[2];
        static const char* const kOnOff[3][2] = {{"", ""}, {"Off", "On"}, {"N\xc3\xa3o", "Sim"}};
        static const char* const kBaseNames[3] = {"", "English", "Portugu\xc3\xaas (BR)"};
        // the versions' names (the Japanese menu: the English ones): 0 the release's, 1 the fandub, 2 faithful to the
        // Japanese, 3 the same with the US names, 4 translated from the US English (the European languages' version 0)
        static const char* const kVersions[3][5] = {
            {"Official (PS1)", "From English (fans)", "From Japanese (AI)", "Japanese, US names (AI)",
             "From US English (AI)"},
            {"Official (PS1)", "From English (fans)", "From Japanese (AI)", "Japanese, US names (AI)",
             "From US English (AI)"},
            {"Oficial (PS1)", "Do ingl\xc3\xaas (por f\xc3\xa3s)", "Do japon\xc3\xaas (IA)", "Japon\xc3\xaas, nomes EUA (IA)",
             "Do ingl\xc3\xaas dos EUA (IA)"}};
        const int lang = gHost.language ? gHost.language() : 0;
        static std::string values[kLangRowsMax]; // the primitives keep the pointers until the frame is drawn
        // the UI language's own words for the rows (assets/ui/text: langrow.0..3, onoff.0/1): the PC glyphs of the
        // packs in the PC's format (their charset has them), the Latin text of the Latin ones after Portuguese
        static std::vector<uint16_t> pcWords[2 * kLangRowsMax];
        auto word = [&](const char* key) -> const char* { return gHost.uiWord ? gHost.uiWord(key) : nullptr; };
        auto pcWord = [&](int slot, const char* key) -> const uint16_t* {
            const char* w = lang == 0 && lang::packGlyphsActive() ? word(key) : nullptr;
            if (!w) return nullptr;
            pcWords[slot] = lang::pcEncode(w);
            if (pcWords[slot].empty()) return nullptr;
            pcWords[slot].push_back(0);
            return pcWords[slot].data();
        };
        static const char* const kRowKeys[6] = {"langrow.0", "langrow.1", "langrow.2", "langrow.3", "textsize.row",
                                                "langrow.5"};
        const int step = langStep(lines);
        for (int k = 0; k < lines; ++k) {
            const size_t c = otOpen(kOtOverlay);
            const int kind = int(rows[k]);
            ScreenPrim& label = otPut(c, ScreenPrim::Text);
            label.x = label.ax = kLangBoxX + 0x0c;
            label.y = int16_t(kLangTop + 0x0a + k * step);
            if (lang == 0) {
                const uint16_t* own = pcWord(k, kRowKeys[kind]);
                if (!own && kind >= 4) {
                    jpNew[kind - 4] = jpGlyphString(kLabelsJpText[kind - 4]);
                    own = jpNew[kind - 4].data();
                }
                label.pc = own ? own : kLabelsJp[kind];
            } else {
                const char* own = word(kRowKeys[kind]);
                label.utf8 = own ? own : kLabels[lang][kind];
            }
            ScreenPrim& t = otPut(c, ScreenPrim::Text);
            t.x = t.ax = kLangValueX;
            t.y = int16_t(kLangTop + 0x0a + k * step);
            const lang::Language text = lang::Language(gEditLang[0]);
            int base = -1;
            switch (rows[k]) {
            case LangRow::Text: base = lang::languageBase(text); break;
            case LangRow::Voices: base = gEditLang[1]; break;
            case LangRow::Version: {
                const int v = lang::languageVersion(text);
                const int b = lang::languageBase(text);
                const int name = v == 0 ? (b == 1 ? 0 : b == 2 ? 1 : 4) : v + 1;
                static const char* const kVersionKeys[5] = {"langver.0", "langver.1", "langver.2", "langver.3",
                                                            "langver.4"};
                const char* own = lang != 0 ? word(kVersionKeys[name]) : nullptr; // the UI language's own (es.txt)
                values[k] = std::string("< ") + (own ? own : kVersions[lang][name]) + " >";
                t.utf8 = values[k].c_str();
                break;
            }
            case LangRow::Subtitles:
            case LangRow::Furigana: {
                const bool on = (rows[k] == LangRow::Subtitles ? gEditLang[2] : gEditLang[5]) != 0;
                if (lang == 0) {
                    const uint16_t* own = pcWord(kLangRowsMax + k, on ? "onoff.1" : "onoff.0");
                    t.pc = own ? own : kOnOffJp[on];
                } else {
                    const char* own = word(on ? "onoff.1" : "onoff.0");
                    values[k] = std::string("< ") + (own ? own : kOnOff[lang][on]) + " >";
                    t.utf8 = values[k].c_str();
                }
                break;
            }
            case LangRow::TextSize: {
                const std::string key = "textsize." + std::to_string(gEditLang[4]);
                if (lang == 0) {
                    const uint16_t* own = pcWord(kLangRowsMax + k, key.c_str());
                    if (!own) {
                        pcWords[kLangRowsMax + k] = jpGlyphString(kTextSizeValue[0][gEditLang[4]]);
                        own = pcWords[kLangRowsMax + k].data();
                    }
                    t.pc = own;
                } else {
                    const char* own = word(key.c_str());
                    values[k] = std::string("< ") + (own ? own : kTextSizeValue[std::clamp(lang, 0, 2)][gEditLang[4]]) + " >";
                    t.utf8 = values[k].c_str();
                }
                break;
            }
            }
            if (base == 0) {
                t.pc = kJp;
            } else if (base >= 7 && base <= 10) { // Korean / Chinese / HK / Russian: their own names (pack_charset's fixed glyphs)
                t.pc = base == 7 ? lang::kKoreanName : base == 8 ? lang::kChineseName : base == 9 ? lang::kChineseHKName : lang::kRussianName;
            } else if (base > 0) {
                const char* name = base <= 2 ? kBaseNames[base] : lang::menuName(lang::languageOf(base, 0));
                values[k] = std::string("< ") + name + " >";
                t.utf8 = values[k].c_str();
            }
            // OpenDash (Herbert): the language's flag left of its name (the vector UI's; without it, only the name)
            if (base >= 0 && (rows[k] == LangRow::Text || rows[k] == LangRow::Voices)) {
                const int16_t fy = int16_t(kLangTop + 0x0a + k * step + 2);
                labelNext(kLblFlag, uint16_t(base));
                ScreenPrim& f = otPut(c, ScreenPrim::Text);
                f.x = f.ax = kLangValueX - 3 - 20;
                f.y = fy;
                f.w = 20;
                f.h = 8;
            }
        }
        // OpenDash: what the text language being picked is (which voices it suits), wrapped under the box
        gLangNoteLines = 0;
        const LangRow onRow = rows[std::min<int>(int8_t(s.step), lines - 1)];
        if (const char* d = (onRow == LangRow::Text || onRow == LangRow::Version) && gHost.languageDescription
                                ? gHost.languageDescription(gEditLang[0]) : nullptr) {
            static std::vector<std::string> latin;
            static std::vector<std::vector<uint16_t>> pc;
            latin.clear();
            pc.clear();
            const std::string_view text(d);
            constexpr int kWidth = 0x110, kJpPerLine = kWidth / 12;
            const int kMaxLines = lines > 4 ? 3 : 4;
            std::vector<std::string_view> words;
            for (size_t i = 0; i < text.size();) {
                const size_t j = text.find(' ', i);
                words.push_back(text.substr(i, j == std::string_view::npos ? std::string_view::npos : j - i));
                if (j == std::string_view::npos) break;
                i = j + 1;
            }
            if (lang == 0) { // the PC glyphs, 12 px each (a word = up to a space)
                std::vector<uint16_t> line;
                for (const std::string_view w : words) {
                    std::vector<uint16_t> g;
                    for (size_t i = 0; i < w.size();) {
                        const auto* u = reinterpret_cast<const unsigned char*>(w.data() + i);
                        uint32_t cp = u[0];
                        size_t n = 1;
                        if ((u[0] & 0xe0) == 0xc0) cp = (u[0] & 0x1fu) << 6 | (u[1] & 0x3fu), n = 2;
                        else if ((u[0] & 0xf0) == 0xe0) cp = (u[0] & 0x0fu) << 12 | (u[1] & 0x3fu) << 6 | (u[2] & 0x3fu), n = 3;
                        i += n;
                        if (const int k = lang::pcGlyphOf(cp); k >= 0) g.push_back(uint16_t(k));
                    }
                    if (!line.empty() && int(line.size() + 1 + g.size()) > kJpPerLine) {
                        pc.push_back(line);
                        line.clear();
                    }
                    if (!line.empty()) line.push_back(0x20);
                    line.insert(line.end(), g.begin(), g.end());
                }
                if (!line.empty()) pc.push_back(line);
                for (auto& l : pc) l.push_back(0);
            } else { // the pack font's widths
                auto width = [](std::string_view w) {
                    int px = 0;
                    for (const uint8_t g : lang::ps1Encode(w)) px += ps1Advance(g);
                    return px;
                };
                std::string line;
                for (const std::string_view w : words) {
                    if (!line.empty() && width(line + " " + std::string(w)) > kWidth) {
                        latin.push_back(line);
                        line.clear();
                    }
                    if (!line.empty()) line += ' ';
                    line += w;
                }
                if (!line.empty()) latin.push_back(line);
            }
            const size_t n = std::min<size_t>(kMaxLines, lang == 0 ? pc.size() : latin.size());
            gLangNoteLines = int(n);
            for (size_t k = 0; k < n; ++k) {
                const size_t c = otOpen(kOtOverlay);
                ScreenPrim& t = otPut(c, ScreenPrim::Text);
                // (Herbert: a gap between the lines and the note, the note in red -- not read as one more line)
                t.x = t.ax = kLangBoxX + 0x0c; // (the rows' labels' x)
                t.y = int16_t(kLangTop + lines * langStep(lines) + 4 + kLangNoteGap + int(k) * 0x0e);
                t.colour = 0x40407c; // (0xBBGGRR, 0x80 = 1.0: a light red)
                if (lang == 0) t.pc = pc[k].data();
                else t.utf8 = latin[k].c_str();
            }
        }
    }
    overlayFrame(s, lines, false, kLangTop, gLangNoteLines * 0x0e + 8 + (gLangNoteLines ? kLangNoteGap : 0),
                 langStep(lines), kLangBoxX, kLangBoxW);
    s.cursor = int8_t(s.cursorKept);
}
// the PC glyphs of a UTF-8 Japanese string (engine/jp_charset; the characters the PC font lacks dropped), 0-terminated
std::vector<uint16_t> jpGlyphString(std::string_view t) {
    std::vector<uint16_t> g;
    for (size_t i = 0; i < t.size();) {
        const auto* u = reinterpret_cast<const unsigned char*>(t.data() + i);
        uint32_t cp = u[0];
        size_t n = 1;
        if ((u[0] & 0xe0) == 0xc0) cp = (u[0] & 0x1fu) << 6 | (u[1] & 0x3fu), n = 2;
        else if ((u[0] & 0xf0) == 0xe0) cp = (u[0] & 0x0fu) << 12 | (u[1] & 0x3fu) << 6 | (u[2] & 0x3fu), n = 3;
        i += n;
        if (const int k = lang::jpGlyphOf(cp); k >= 0) g.push_back(uint16_t(k));
    }
    g.push_back(0);
    return g;
}
// state 0xd (OpenDash): the music row, on the language row's overlay -- two lines: the music (original / extended)
// and, with the host's hooks, the staff roll's music (the Japanese PC version's song / the US PS1 one); up / down pick
// the line, left / right change it, circle applies (and persists: the next music load plays it), cross back
int gEditMusic[2] = {0, 0};
void optionsMusic(StatusScreen& s) {
    const bool credits = bool(gHost.creditsMusic) && bool(gHost.setCreditsMusic);
    const int lines = credits ? 2 : 1;
    const uint8_t st = s.sub;
    if (st == uint8_t(RowStep::Enter)) {
        cursorReset(s);
        s.sub = uint8_t(RowStep::Cursor);
        s.step = 0;
        gEditMusic[0] = gHost.music ? gHost.music() : 1;
        gEditMusic[1] = credits ? gHost.creditsMusic() : 0;
    } else if (st == uint8_t(RowStep::Cursor)) {
        const uint8_t p = uint8_t(menuPadPressed());
        if (p & (kPadLeft | kPadRight)) {
            soundPlayFlat(UiSound::Cursor);
            int& v = gEditMusic[std::min<int>(int8_t(s.step), lines - 1)];
            v = 1 - v;
        } else if (p & (kPadUp | kPadDown)) {
            soundPlayFlat(UiSound::Cursor);
            s.step = uint8_t((int8_t(s.step) + 1) % lines);
        } else if (menuPadPressed() & kPadCross) {
            soundPlayFlat(UiSound::Cancel);
            s.sub = uint8_t(RowStep::Back);
        } else if (menuPadPressed() & kPadConfirm) {
            soundPlayFlat(UiSound::Confirm);
            if (gHost.setMusic && (!gHost.music || gEditMusic[0] != gHost.music())) gHost.setMusic(gEditMusic[0]);
            if (credits && gEditMusic[1] != gHost.creditsMusic()) gHost.setCreditsMusic(gEditMusic[1]);
            s.sub = uint8_t(RowStep::Back);
        }
    } else if (st == uint8_t(RowStep::Back)) {
        levelBack(s, 2);
        cursorOnRow(s, kMusicState);
    }
    if (drawing()) {
        const int lang = gHost.language ? gHost.language() : 0;
        // the lines' labels and values: Japanese (PC glyphs), English, Portuguese
        static const char* const kLabel[3][2] = {{"\xe3\x81\x8a\xe3\x82\x93\xe3\x81\x8c\xe3\x81\x8f",   // おんがく
                                                  "\xe3\x82\xa8\xe3\x83\xb3\xe3\x83\x87\xe3\x82\xa3\xe3\x83\xb3\xe3\x82\xb0"}, // エンディング
                                                 {"Music", "Credits"},
                                                 {"M\xc3\xbasica", "Cr\xc3\xa9" "ditos"}};
        static const char* const kValue[3][2][2] = {
            {{"\xe3\x82\xaa\xe3\x83\xaa\xe3\x82\xb8\xe3\x83\x8a\xe3\x83\xab", // オリジナル
              "\xe3\x82\xa2\xe3\x83\xac\xe3\x83\xb3\xe3\x82\xb8"},                 // アレンジ
             {"\xe3\x82\xaa\xe3\x83\xaa\xe3\x82\xb8\xe3\x83\x8a\xe3\x83\xab", // オリジナル (Herbert: never "Japanese")
              "\xe3\x81\x82\xe3\x82\x81\xe3\x82\x8a\xe3\x81\x8b\xe3\x81\xb0\xe3\x82\x93"}}, // あめりかばん
            {{"< Original >", "< Rearranged >"}, {"< Original >", "< US version >"}},
            {{"< Original >", "< Rearranjada >"}, {"< Original >", "< Americana >"}}};
        static std::vector<uint16_t> jp[2][2];
        for (int k = 0; k < lines; ++k) {
            const size_t c = otOpen(kOtOverlay);
            ScreenPrim& label = otPut(c, ScreenPrim::Text);
            label.x = label.ax = 0x30;
            label.y = int16_t(0x52 + k * 0x20);
            ScreenPrim& t = otPut(c, ScreenPrim::Text);
            t.x = t.ax = 0x78;
            t.y = int16_t(0x52 + k * 0x20);
            const int v = gEditMusic[k];
            // the UI language's own words (musicrow.<k>, musicval.<k>.<v>; the value already in its "< >")
            const std::string rowKey = "musicrow." + std::to_string(k);
            const std::string valKey = "musicval." + std::to_string(k) + "." + std::to_string(v);
            if (lang == 0) {
                const std::vector<uint16_t> ownLabel = ownPcWord(rowKey.c_str()), ownValue = ownPcWord(valKey.c_str());
                jp[k][0] = ownLabel.empty() ? jpGlyphString(kLabel[0][k]) : ownLabel;
                jp[k][1] = ownValue.empty() ? jpGlyphString(kValue[0][k][v]) : ownValue;
                if (!ownLabel.empty()) jp[k][0].push_back(0);
                if (!ownValue.empty()) jp[k][1].push_back(0);
                label.pc = jp[k][0].data();
                t.pc = jp[k][1].data();
            } else {
                const char* ownLabel = ownWord(rowKey.c_str());
                const char* ownValue = ownWord(valKey.c_str());
                label.utf8 = ownLabel ? ownLabel : kLabel[lang][k];
                t.utf8 = ownValue ? ownValue : kValue[lang][k][v];
            }
        }
    }
    overlayFrame(s, lines, false);
    s.cursor = int8_t(s.cursorKept);
}
// OpenDash (Herbert): a row of one value cycled in place, on the music row's overlay -- the text size (state 0x10:
// Grande (the original) / Médio (default) / Pequeno) and the buttons' style (state 0x11: PlayStation / Xbox, PC / Steam
// Deck / Nintendo Switch); left / right change it at once (the windows / prompts show it while choosing), circle keeps
// it (persisted), cross puts back what it was. The UI language's words: <key>.row, <key>.<v> (the value without its
// "< >"), else the tables (JP / EN / PT)
struct CycleRow {
    uint8_t state;
    int count;
    const std::function<int()>* get;
    const std::function<void(int)>* set;
    const char* key;
    const char* const* row;   // [3]
    const char* const* value; // [3][count]: value[l * count + v]
};
int gEditCycle = 0, gCycleWas = 0;
void optionsCycle(StatusScreen& s, const CycleRow& r) {
    const uint8_t st = s.sub;
    auto apply = [&](int v) {
        if (*r.set) (*r.set)(v);
    };
    if (st == uint8_t(RowStep::Enter)) {
        cursorReset(s);
        s.sub = uint8_t(RowStep::Cursor);
        s.step = 0;
        gEditCycle = gCycleWas = *r.get ? std::clamp((*r.get)(), 0, r.count - 1) : 0;
    } else if (st == uint8_t(RowStep::Cursor)) {
        const uint8_t p = uint8_t(menuPadPressed());
        if (p & (kPadLeft | kPadRight)) {
            soundPlayFlat(UiSound::Cursor);
            gEditCycle = (gEditCycle + ((p & kPadRight) ? 1 : r.count - 1)) % r.count;
            apply(gEditCycle);
        } else if (menuPadPressed() & kPadCross) {
            soundPlayFlat(UiSound::Cancel);
            if (gEditCycle != gCycleWas) apply(gCycleWas);
            s.sub = uint8_t(RowStep::Back);
        } else if (menuPadPressed() & kPadConfirm) {
            soundPlayFlat(UiSound::Confirm);
            apply(gEditCycle);
            s.sub = uint8_t(RowStep::Back);
        }
    } else if (st == uint8_t(RowStep::Back)) {
        levelBack(s, 2);
        cursorOnRow(s, r.state);
    }
    if (drawing()) {
        const int lang = gHost.language ? gHost.language() : 0;
        const int l = std::clamp(lang, 0, 2);
        static std::vector<uint16_t> jp[2];
        const size_t c = otOpen(kOtOverlay);
        ScreenPrim& label = otPut(c, ScreenPrim::Text);
        label.x = label.ax = 0x30;
        label.y = 0x52;
        ScreenPrim& t = otPut(c, ScreenPrim::Text);
        t.x = t.ax = 0x98;
        t.y = 0x52;
        const std::string rowKey = std::string(r.key) + ".row";
        const std::string valKey = std::string(r.key) + "." + std::to_string(gEditCycle);
        static std::string value;
        if (lang == 0) {
            const std::vector<uint16_t> ownLabel = ownPcWord(rowKey.c_str()), ownValue = ownPcWord(valKey.c_str());
            jp[0] = ownLabel.empty() ? jpGlyphString(r.row[0]) : ownLabel;
            jp[1] = ownValue.empty() ? jpGlyphString(r.value[gEditCycle]) : ownValue;
            if (!ownLabel.empty()) jp[0].push_back(0);
            if (!ownValue.empty()) jp[1].push_back(0);
            label.pc = jp[0].data();
            t.pc = jp[1].data();
        } else {
            const char* ownLabel = ownWord(rowKey.c_str());
            const char* ownValue = ownWord(valKey.c_str());
            label.utf8 = ownLabel ? ownLabel : r.row[l];
            value = std::string("< ") + (ownValue ? ownValue : r.value[l * r.count + gEditCycle]) + " >";
            t.utf8 = value.c_str();
        }
    }
    overlayFrame(s, 1, false);
    s.cursor = int8_t(s.cursorKept);
}
void optionsTextSize(StatusScreen& s) {
    optionsCycle(s, {kTextSizeState, 3, &gHost.textSize, &gHost.setTextSize, "textsize", kTextSizeRow, &kTextSizeValue[0][0]});
}
// state 0x12 (OpenDash, Herbert): the credits -- the staff roll on its own (content/late_game creditsView*), over
// the page; circle, start or cross cut it short; back on the row when it ends
void optionsCredits(StatusScreen& s) {
    const uint8_t st = s.sub;
    if (st == uint8_t(RowStep::Enter)) {
        if (gHost.creditsStart) gHost.creditsStart(); // (the host's: game/ links no content/)
        s.sub = uint8_t(RowStep::Cursor);
    } else if (st == uint8_t(RowStep::Cursor)) {
        if (!gHost.creditsTick || !gHost.creditsTick((menuPadPressed() & (kPadConfirm | kPadCross)) != 0))
            s.sub = uint8_t(RowStep::Back);
    } else if (st == uint8_t(RowStep::Back)) {
        backToExtras(s);
        extrasMusic(true);
    }
    s.cursor = int8_t(s.cursorKept);
}
// state 0x13 (OpenDash, Herbert): the gallery -- the credits' illustrations one by one (left / right, the stick, L1
// / R1 change it), the Flutter's music while it is open; cross (or circle) back on the row
int gGalleryIndex = 0;
void optionsGallery(StatusScreen& s) {
    const uint8_t st = s.sub;
    const int n = gHost.galleryCount ? gHost.galleryCount() : 0;
    if (st == uint8_t(RowStep::Enter)) {
        gGalleryIndex = 0;
        if (gHost.galleryOpen) gHost.galleryOpen(true);
        if (gHost.galleryShow) gHost.galleryShow(gGalleryIndex);
        s.sub = uint8_t(RowStep::Cursor);
    } else if (st == uint8_t(RowStep::Cursor)) {
        const uint16_t p = menuPadPressed();
        const int dir = (p & (kPadLeft | kPadL1)) ? -1 : (p & (kPadRight | kPadR1)) ? 1 : 0;
        if (dir != 0 && n > 0) {
            soundPlayFlat(UiSound::Cursor);
            gGalleryIndex = (gGalleryIndex + dir + n) % n;
            if (gHost.galleryShow) gHost.galleryShow(gGalleryIndex);
        } else if (p & (kPadCross | kPadConfirm)) {
            soundPlayFlat(UiSound::Cancel);
            if (gHost.galleryOpen) gHost.galleryOpen(false);
            s.sub = uint8_t(RowStep::Back);
        }
    } else if (st == uint8_t(RowStep::Back)) {
        backToExtras(s);
    }
    s.cursor = int8_t(s.cursorKept);
}
// state 0x14 (OpenDash, Herbert): the Extras -- a list (the root: the credits, the sound test, the groups, Voltar;
// a group: its albums, Voltar; a group of one album shows it straight); up / down, circle opens, cross (or Voltar) one
// level up, the cursor kept per level. The Flutter's arranged music plays in it (the host's extrasMusic); its lines
// (the credits, the sound test, an album) come back to it. Opened from the title's Extras row (optionsOpenExtras) it
// leaves to the title
void optionsExtras(StatusScreen& s) {
    enum Kind { Credits, Sound, Group, Album, Back };
    struct Line {
        Kind kind;
        int arg;
    };
    // this level's lines
    std::vector<Line> lines;
    const int albums = gHost.albumCount ? gHost.albumCount() : 0;
    const int groups = gHost.groupCount ? gHost.groupCount() : 0;
    auto groupAlbums = [&](int g) {
        std::vector<int> a;
        for (int k = 0; k < albums; ++k)
            if (gHost.albumGroup && gHost.albumGroup(k) == g) a.push_back(k);
        return a;
    };
    if (gExtrasGroup < 0) {
        lines.push_back({Credits, 0});
        if (gHost.soundTestCount && gHost.soundTestCount() > 0) lines.push_back({Sound, 0});
        for (int g = 0; g < groups; ++g) {
            const std::vector<int> a = groupAlbums(g);
            if (a.size() == 1) lines.push_back({Album, a[0]});
            else if (!a.empty()) lines.push_back({Group, g});
        }
        for (int k = 0; k < albums; ++k) // (an album of no group)
            if (!gHost.albumGroup || gHost.albumGroup(k) < 0) lines.push_back({Album, k});
    } else {
        for (const int k : groupAlbums(gExtrasGroup)) lines.push_back({Album, k});
    }
    lines.push_back({Back, 0});
    const int n = int(lines.size());
    const int level = gExtrasGroup < 0 ? 0 : 1;
    // the box centred between the screen's top and the hints (y 0x10 .. 0xd0), the rows 0x12 apart (closer when many)
    constexpr int kBoxX = 0x28, kBoxW = 0xf4;
    const int kStep = std::min(0x12, (0xc0 - 2 * kListPad) / std::max(1, n));
    const int kTop = std::max(0x10, 0x70 - (n * kStep + 2 * kListPad) / 2);
    auto up = [&] {
        if (gExtrasGroup >= 0) { // the group's list: back to the root, on the group's line
            gExtrasGroup = -1;
            s.step = uint8_t(gExtrasLine[0]);
            return;
        }
        extrasMusic(false);
        if (gExtrasMode) { // opened from the title: back to it
            gExtrasMode = false;
            s.state = uint8_t(OptionsState::Leave);
            s.sub = uint8_t(OptionsStep::Prepare);
            return;
        }
        s.sub = uint8_t(RowStep::Back);
    };
    const uint8_t st = s.sub;
    if (st == uint8_t(RowStep::Enter)) {
        cursorReset(s);
        s.sub = uint8_t(RowStep::Cursor);
        gExtrasGroup = -1;
        gExtrasLine[0] = gExtrasLine[1] = 0;
        s.step = 0;
        extrasMusic(true);
    } else if (st == uint8_t(RowStep::Cursor)) {
        const uint8_t p = uint8_t(menuPadPressed());
        int& cur = gExtrasLine[level];
        cur = std::clamp<int>(int8_t(s.step), 0, n - 1);
        if (p & (kPadUp | kPadDown)) {
            soundPlayFlat(UiSound::Cursor);
            cur = (cur + ((p & kPadDown) ? 1 : n - 1)) % n;
            s.step = uint8_t(cur);
        } else if (menuPadPressed() & kPadCross) {
            soundPlayFlat(UiSound::Cancel);
            up();
            return;
        } else if (menuPadPressed() & kPadConfirm) {
            const Line ln = lines[size_t(cur)];
            soundPlayFlat(ln.kind == Back ? UiSound::Cancel : UiSound::Confirm);
            switch (ln.kind) {
            case Back: up(); return;
            case Group:
                gExtrasGroup = ln.arg;
                gExtrasLine[1] = 0;
                s.step = 0;
                break;
            case Credits:
                extrasMusic(false); // (the credits' own song)
                s.state = kCreditsState;
                s.sub = uint8_t(RowStep::Enter);
                return;
            case Sound:
                extrasMusic(false); // (silence: the sound test's songs)
                s.state = kSoundTestState;
                s.sub = uint8_t(RowStep::Enter);
                return;
            case Album:
                if (gHost.galleryAlbum) gHost.galleryAlbum(ln.arg);
                s.state = kGalleryState;
                s.sub = uint8_t(RowStep::Enter);
                return;
            }
        }
    } else if (st == uint8_t(RowStep::Back)) {
        levelBack(s, 2);
        cursorOnRow(s, kExtrasState);
    }
    if (drawing()) {
        const int lang = gHost.language ? gHost.language() : 0;
        const int l = std::clamp(lang, 0, 2);
        static std::vector<std::vector<uint16_t>> jp;
        jp.resize(size_t(n));
        static const char* const kCredits[3] = {"\xe3\x82\xb9\xe3\x82\xbf\xe3\x83\x83\xe3\x83\x95", "Credits",
                                                "Cr\xc3\xa9" "ditos"};
        static const char* const kSound[3] = {"\xe3\x82\xb5\xe3\x82\xa6\xe3\x83\xb3\xe3\x83\x89\xe3\x83\x86\xe3\x82\xb9"
                                              "\xe3\x83\x88", "Sound test", "Teste de som"};
        static const char* const kBack[3] = {"\xe3\x82\x82\xe3\x81\xa9\xe3\x82\x8b", "Back", "Voltar"};
        static std::string more[32];
        for (int k = 0; k < n; ++k) {
            const Line ln = lines[size_t(k)];
            const size_t c = otOpen(kOtOverlay);
            ScreenPrim& t = otPut(c, ScreenPrim::Text);
            t.x = t.ax = kBoxX + 0x10;
            t.y = int16_t(listRowTextY(kTop, kStep, k));
            const char* text = "";
            switch (ln.kind) {
            case Credits: text = lang != 0 && ownWord("row.6.fff8") ? ownWord("row.6.fff8") : kCredits[l]; break;
            case Sound: text = kSound[l]; break;
            case Back: text = kBack[l]; break;
            case Group:
                more[k & 31] = std::string(gHost.groupTitle ? gHost.groupTitle(ln.arg) : "") + "  >";
                text = more[k & 31].c_str();
                break;
            case Album: text = gHost.albumTitle ? gHost.albumTitle(ln.arg) : ""; break;
            }
            if (lang == 0) {
                const std::vector<uint16_t> own = lang::packGlyphsActive() ? lang::pcEncode(text) : std::vector<uint16_t>();
                jp[size_t(k)] = !own.empty() ? own : jpGlyphString(text);
                if (!own.empty()) jp[size_t(k)].push_back(0);
                t.pc = jp[size_t(k)].data();
            } else {
                t.utf8 = text;
            }
        }
    }
    overlayFrame(s, n, false, kTop, 0, kStep, kBoxX, kBoxW, true);
    s.cursor = int8_t(s.cursorKept);
}
// state 0x15 (OpenDash, Herbert): the sound test -- a song's name and number on the music row's overlay; left /
// right (L1 / R1) pick the previous / next song (nothing plays), circle plays the picked one, cross stops it -- and,
// stopped already, goes back to the Extras (their music again). The bar under it: circle Play, cross Stop
int gSoundIndex = 0;
int gSoundPlaying = -1; // the song playing (-1: none)
bool gSoundTestOn = false;
void optionsSoundTest(StatusScreen& s) {
    const int n = gHost.soundTestCount ? gHost.soundTestCount() : 0;
    auto play = [&](int k) {
        if (gHost.soundTestPlay) gHost.soundTestPlay(k);
        gSoundPlaying = k >= 0 ? k : -1;
    };
    const uint8_t st = s.sub;
    if (st == uint8_t(RowStep::Enter)) {
        cursorReset(s);
        s.sub = uint8_t(RowStep::Cursor);
        s.step = 0;
        gSoundIndex = std::clamp(gSoundIndex, 0, std::max(0, n - 1));
        play(-2); // (Herbert: silence on entering; a song plays when one is picked)
        gSoundTestOn = true;
    } else if (st == uint8_t(RowStep::Cursor)) {
        gSoundTestOn = true;
        const uint16_t p = menuPadPressed();
        const int dir = (p & (kPadLeft | kPadL1)) ? -1 : (p & (kPadRight | kPadR1)) ? 1 : 0;
        if (dir != 0 && n > 0) {
            soundPlayFlat(UiSound::Cursor);
            gSoundIndex = (gSoundIndex + dir + n) % n;
        } else if (p & kPadCross) {
            if (gSoundPlaying >= 0) { // playing: stopped
                play(-2);
            } else { // stopped already: back to the Extras (their music comes back)
                soundPlayFlat(UiSound::Cancel);
                gSoundTestOn = false;
                s.sub = uint8_t(RowStep::Back);
            }
        } else if ((p & kPadConfirm) && n > 0) {
            play(gSoundIndex); // (-2: stopped, the title's song not yet)
        }
    } else if (st == uint8_t(RowStep::Back)) {
        gSoundTestOn = false;
        backToExtras(s);
        extrasMusic(true);
    }
    if (drawing() && n > 0) {
        static std::string value;
        char num[16];
        std::snprintf(num, sizeof num, "%02d / %02d", gSoundIndex + 1, n);
        const char* name = gHost.soundTestTitle ? gHost.soundTestTitle(gSoundIndex) : "";
        value = std::string("< ") + num + "  " + name + " >";
        const size_t c = otOpen(kOtOverlay);
        ScreenPrim& label = otPut(c, ScreenPrim::Text);
        label.x = label.ax = 0x30;
        label.y = 0x52;
        static const char* const kSoundWord[3] = {"Sound test", "Sound test", "Teste de som"}; // (jp: the Latin words)
        const int l = std::clamp(gHost.language ? gHost.language() : 1, 0, 2);
        label.utf8 = kSoundWord[l];
        ScreenPrim& t = otPut(c, ScreenPrim::Text);
        t.x = t.ax = 0x78;
        t.y = 0x52;
        t.utf8 = value.c_str();
    }
    overlayFrame(s, 1, false);
    s.cursor = int8_t(s.cursorKept);
}
void optionsButtonStyle(StatusScreen& s) {
    optionsCycle(s, {kButtonStyleState, 4, &gHost.buttonStyle, &gHost.setButtonStyle, "buttonstyle", kButtonStyleRow,
                     &kButtonStyleValue[0][0]});
}
} // namespace

OptionsHost& optionsHost() { return gHost; }

void optionsOpenExtras() { gExtrasMode = true; }

bool optionsSoundTestOn() { return gSoundTestOn; }

void optionsOverlay(const StatusScreen& s, int rows, bool icons) { overlayFrame(s, rows, icons); }

bool optionsOverlayPointer(StatusScreen& s, int rows, int base) {
    MenuPointer& pt = menuPointer();
    if (!pt.moved || pt.row < base || pt.row >= base + rows) return false;
    pt.moved = false;
    if (pt.row - base != int8_t(s.step)) {
        s.step = uint8_t(pt.row - base);
        soundPlayFlat(UiSound::Cursor);
    }
    return true;
}

bool optionsSourceHeadersPs1() { return sourceRows() == kPs1Rows; }

bool optionsPictures() { return dataSource().hasTable({TableId::KeyConfigPadButtonArts, 0}); }

uint32_t optionsValueTable(size_t k) { return gVal[k]; }

void optionsPage(StatusScreen& s) {
    if (gRows.empty()) optLayout();
    GameOptions& o = gameOptions();
    switch (OptionsState(s.state)) {
    case OptionsState::SetUp: optionsSetUp(s); break;
    case OptionsState::Rows: optionsRows(s); break;
    case OptionsState::Leave: optionsLeave(s); break;
    case OptionsState::Keys: keyConfigState(s); break;
    case OptionsState::Camera: optionChoice(s, o.lookDirection, false, false, gVal[kValueCamera], stateAddress(TableId::StatusOptionsCameraChoice)); break;
    case OptionsState::LockOn: optionsLockOn(s); break;
    case OptionsState::Vibration:
        optionChoice(s, o.vibrationOff, false, true, gVal[kValueVibration], stateAddress(TableId::StatusOptionsVibrationChoice));
        break;
    case OptionsState::Furigana: optionChoice(s, o.ruby, true, false, gVal[kValueFurigana], stateAddress(TableId::StatusOptionsFuriganaChoice)); break;
    case OptionsState::Mono: optionChoice(s, o.mono, false, false, gVal[kValueMono], stateAddress(TableId::StatusOptionsMonoChoice)); break;
    case OptionsState::Volume: optionsVolume(s); break;
    case OptionsState::Video: optionsVideo(s); break;
    case OptionsState::Defaults: optionsDefaults(s); break;
    case OptionsState::Language: optionsLanguage(s); break;
    case OptionsState::Music: optionsMusic(s); break;
    case OptionsState::TextSize: optionsTextSize(s); break;
    case OptionsState::ButtonStyle: optionsButtonStyle(s); break;
    case OptionsState::Credits: optionsCredits(s); break;
    case OptionsState::Gallery: optionsGallery(s); break;
    case OptionsState::Extras: optionsExtras(s); break;
    case OptionsState::SoundTest: optionsSoundTest(s); break;
    default: break;
    }
    switch (pad0().typeByte()) {
    case kPadTypeDigital: s.padKind = kPadKindDigital; break;
    case kPadTypeAnalogA:
    case kPadTypeAnalogB: s.padKind = kPadKindAnalog; break;
    case kPadTypeKeep: break;
    default: s.padKind = kPadKindOther; break;
    }
    if (!gExtrasMode) { // (the Extras from the title: only the backdrop and the list)
        optionsValuesDraw(s);
        pageDraw(s, kOptionsPage);
    }
    s.cursor = int8_t(s.cursorKept);
}

} // namespace od::port
