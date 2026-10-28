#pragma once

#include <filesystem>
#include <string>

namespace od {

struct Config {
    std::filesystem::path datDir;
    std::filesystem::path exePath; // dash2.exe (per-stage tables); default <datDir>/../dash2.exe
    std::string aa = "auto";          // aa= auto|none|dlss|fsr|xess|nis
    std::string aaQuality = "native"; // aa_quality= native|quality|balanced|performance
    std::filesystem::path langDir; // lang_dir= / OPENDASH_LANG_DIR: language packs, default <exe dir>/lang
    std::string lang = "jp";       // lang= jp|en|ptbr / OPENDASH_LANG (--lang overrides)
    std::filesystem::path disc;    // disc= / OPENDASH_DISC: the PS1 disc image (.cue); the English pack is read off it
    std::string data = "pc";       // data= pc|disc / OPENDASH_DATA: the game's files and tables from the PC install or
                                   // the disc image (engine/ps1_data_source)
    bool restoredVoices = true;    // restored_voices= 0|1: the pack's AI-restored voices (snd_restored) when present
    // story_save= the save whose story state / event flags free play (F2, the stage browser) uses: a slot number
    // (DASH2-NN.DAT next to dash2.exe, read only), a path, or nenhum / none (story 0). Unset: DASH2-00 if present.
    std::string storySave;
    // the options page (game/status_options, page 6) persists these: the display (display= windowed|borderless|exclusive,
    // window= WxH the window's size in pixels, fullscreen_mode= WxH@Hz the exclusive full screen's display mode) and
    // the bindings of the key configuration (keys= / pad= / mouse=: code:ps1bit hex pairs, comma separated)
    std::string display = "windowed";
    std::string window;
    std::string fullscreenMode;
    bool vsync = true; // vsync= 0|1 (F1 Video, --vsync): presents wait for the display's refresh
    std::string keys, pad, mouse;
    // fog= remaster|classic|off (F1 Visual), fog_distance= scale of the remaster fog's distance
    std::string fog = "remaster";
    std::string fogSource = "opendash"; // a room with no fog range of its own (the disc's rooms): "ps1" none, "opendash" ours
    float fogDistance = 1.0f;
    // water= remaster|classic (F1 Video): the flooded rooms' underwater look (OpenDash's or the PS1 / PSP screen wave)
    std::string water = "remaster";
    // weather= remaster|classic (F1 Video): the snow / blizzard (OpenDash's, in the world, or the game's screen quads)
    std::string weather = "remaster";
    // dissolve= 0|1 (F1 Visual): the scenery between the camera and the characters in focus (the player, the lock-on
    // target) dissolves around them, Baldur's Gate style
    bool dissolve = true;
    // clearance= 0|1 (F1 Visual): the player's model is drawn out of the walls it sinks into (app/model_clearance)
    bool clearance = true;
    // netplay (app/netplay, docs/netplay.md): net= off|host|join, net_port= the host's UDP port (unset / 0: 27460;
    // a client binds any free port), net_join= the host's ip:port, net_name= this player's name, net_key= the session key (the host refuses
    // a player whose key differs; only its hash travels). --host / --join / --net-name / --net-key override them.
    std::string net = "off";
    int netPort = 0;
    std::string netJoin;
    std::string netName;
    std::string netKey;
};

// opendash.ini's folder: the executable's (the app sets it before loadConfig); an opendash.ini in the working directory
// still wins. Unset: the working directory.
void setConfigDir(const std::filesystem::path& dir);
const std::filesystem::path& configPath();

// Resolution order: OPENDASH_DAT_DIR env var, then `dat_dir=` in opendash.ini,
// then the default install path. exePath: OPENDASH_EXE, `exe=`, default next to dat/. No game data ships with OpenDash.
Config loadConfig();

// The environment variable `name`, empty when unset
std::string envValue(const char* name);

// Sets `key=value` in opendash.ini (replaces the key's line or appends it; other lines kept). Settings the player
// changes in the app (lang=) persist this way.
void saveConfigValue(const std::string& key, const std::string& value);

} // namespace od
