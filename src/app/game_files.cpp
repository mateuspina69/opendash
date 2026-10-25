// The player's copy of the game (see game_files.hpp).
#include "app/game_files.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <string>

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

// the system's folder picker (SDL: the portal / zenity on Linux, the shell's on Windows); empty when cancelled
std::filesystem::path pickFolder(const std::filesystem::path& start) {
    struct Pick {
        std::atomic<bool> done = false;
        std::string path;
    } pick;
    const std::string startText = start.string();
    SDL_ShowOpenFolderDialog(
        [](void* userdata, const char* const* files, int) {
            auto* p = static_cast<Pick*>(userdata);
            if (!files) SDL_Log("game files: the folder picker failed (%s)", SDL_GetError());
            else if (*files) p->path = *files;
            p->done = true;
        },
        &pick, nullptr, startText.empty() ? nullptr : startText.c_str(), false);
    while (!pick.done) SDL_WaitEventTimeout(nullptr, 50); // the callback comes from the event loop
    return pick.path;
}

// 0: pick the folder, 1: quit
int askBox(const char* text) {
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Choose folder..."},
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

bool gameFilesFound(const od::Config& cfg) { return complete(cfg.datDir, cfg.exePath); }

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
    SDL_Log("game files: no dat/INIT.DAT + snd/ + dash2.exe at %s / %s", cfg.datDir.string().c_str(),
            cfg.exePath.string().c_str());
    if (!ask) return false;
    const char* text = "OpenDash runs on the files of your own PC copy of Mega Man Legends 2, and did not find them.\n\n"
                       "Choose the game's folder: the one with dash2.exe and the dat and snd folders. It is "
                       "remembered in opendash.ini next to OpenDash.";
    std::filesystem::path start = cfg.datDir.parent_path();
    while (askBox(text) == 0) {
        const std::filesystem::path folder = pickFolder(start);
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
