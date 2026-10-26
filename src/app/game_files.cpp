// The player's copy of the game (see game_files.hpp).
#include "app/game_files.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstring>
#include <exception>
#include <string>

#include "engine/dat.hpp"
#include "engine/disc_image.hpp"

namespace od::app {

namespace {

bool isFile(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
}

// the install's files: dat/INIT.DAT, dash2.exe and snd/ next to dat/ (the voice clips: the scripts that wait on one,
// as the attract's first step, never go on without them)
bool complete(const std::filesystem::path& dat, const std::filesystem::path& exe) {
    std::error_code ec;
    return isFile(dat / "INIT.DAT") && isFile(exe) && std::filesystem::is_directory(dat.parent_path() / "snd", ec);
}

bool discComplete(const std::filesystem::path& cue) {
    if (!isFile(cue)) return false;
    try {
        const od::DiscImage disc(cue); // also checks that the cue's track files can be opened
        const auto cnf = disc.file("SYSTEM.CNF");
        if (std::string(cnf.begin(), cnf.end()).find("SLUS_011.40") == std::string::npos) return false;
        const auto exe = disc.file("SLUS_011.40");
        if (exe.size() < 0x800 || std::memcmp(exe.data(), "PS-X EXE", 8) != 0 ||
            uint64_t(0x800) + od::rd32(exe.data() + 0x1c) > exe.size()) return false;
        // Directory entries can survive a truncated track. Read the required startup payloads before accepting it.
        return !disc.file("COMMON/INIT.BIN").empty();
    } catch (const std::exception& e) {
        SDL_Log("game files: unusable PS1 disc: %s", e.what());
        return false;
    }
}

// The system's file / folder picker (SDL: the portal / zenity on Linux, the shell's on Windows).
std::filesystem::path pickPath(const std::filesystem::path& start, bool disc) {
    struct Pick {
        std::atomic<bool> done = false;
        std::string path;
    } pick;
    const std::string startText = start.string();
    const auto callback = [](void* userdata, const char* const* files, int) {
        auto* p = static_cast<Pick*>(userdata);
        if (!files) SDL_Log("game files: the file picker failed (%s)", SDL_GetError());
        else if (*files) p->path = *files;
        p->done = true;
    };
    if (disc) {
        static const SDL_DialogFileFilter filter{"PS1 disc image", "cue"};
        SDL_ShowOpenFileDialog(callback, &pick, nullptr, &filter, 1,
                               startText.empty() ? nullptr : startText.c_str(), false);
    } else {
        SDL_ShowOpenFolderDialog(callback, &pick, nullptr, startText.empty() ? nullptr : startText.c_str(), false);
    }
    while (!pick.done) SDL_WaitEventTimeout(nullptr, 50); // the callback comes from the event loop
    return std::filesystem::path(std::u8string(pick.path.begin(), pick.path.end()));
}

// 0: pick the selected source's files, 1: quit
int askBox(const char* text, bool disc) {
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, disc ? "Choose disc image..." : "Choose folder..."},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "Quit"},
    };
    const SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION, nullptr, "OpenDash", text, 2, buttons, nullptr};
    int chosen = 1;
    if (!SDL_ShowMessageBox(&box, &chosen)) {
        SDL_Log("game files: no message box (%s)", SDL_GetError());
        return 1;
    }
    return chosen;
}

} // namespace

bool gameFilesFound(const od::Config& cfg) {
    return cfg.data == "disc" ? discComplete(cfg.disc) : complete(cfg.datDir, cfg.exePath);
}

bool gameFilesAt(const std::filesystem::path& folder, od::Config& cfg) {
    for (const auto& [dat, exe] : {std::pair{folder / "dat", folder / "dash2.exe"},
                                   std::pair{folder, folder.parent_path() / "dash2.exe"}}) {
        if (complete(dat, exe)) {
            cfg.datDir = dat;
            cfg.exePath = exe;
            return true;
        }
    }
    return false;
}

bool ensureGameFiles(od::Config& cfg, bool ask) {
    if (gameFilesFound(cfg)) return true;
    if (cfg.data == "disc") {
        SDL_Log("game files: no usable USA PS1 disc image at %s", cfg.disc.string().c_str());
        if (!ask) return false;
        const char* text = "OpenDash could not read your Mega Man Legends 2 USA PS1 disc image.\n\n"
                           "Choose its .cue file. Keep the .bin track files beside it, with the names in the cue sheet.";
        std::filesystem::path start = cfg.disc;
        while (askBox(text, true) == 0) {
            const std::filesystem::path cue = pickPath(start, true);
            if (cue.empty()) continue;
            if (discComplete(cue)) {
                cfg.disc = cue;
                od::saveConfigValue("disc", cfg.disc.generic_string());
                return true;
            }
            start = cue;
        }
        return false;
    }
    SDL_Log("game files: no dat/INIT.DAT + snd/ + dash2.exe at %s / %s", cfg.datDir.string().c_str(),
            cfg.exePath.string().c_str());
    if (!ask) return false;
    const char* text = "OpenDash runs on the files of your own PC copy of Mega Man Legends 2, and did not find them.\n\n"
                       "Choose the game's folder: the one with dash2.exe and the dat and snd folders. It is "
                       "remembered in opendash.ini next to OpenDash.";
    std::filesystem::path start = cfg.datDir.parent_path();
    while (askBox(text, false) == 0) {
        const std::filesystem::path folder = pickPath(start, false);
        if (folder.empty()) continue; // cancelled: ask again
        if (gameFilesAt(folder, cfg)) {
            od::saveConfigValue("dat_dir", cfg.datDir.generic_string());
            od::saveConfigValue("exe", cfg.exePath.generic_string());
            SDL_Log("game files: %s, %s (saved to %s)", cfg.datDir.string().c_str(), cfg.exePath.string().c_str(),
                    od::configPath().string().c_str());
            return true;
        }
        start = folder;
        text = "That folder does not have the game's files (dash2.exe, dat/INIT.DAT and the snd folder).\n\n"
               "Choose the folder of your Mega Man Legends 2 PC installation: the one with dash2.exe and the dat and "
               "snd folders.";
    }
    return false;
}

} // namespace od::app
