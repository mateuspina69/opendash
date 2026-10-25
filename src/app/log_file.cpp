#include "app/log_file.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>

#include <cstdio>
#include <string>

namespace od::app {

namespace {

std::FILE* g_file = nullptr;
SDL_LogOutputFunction g_console = nullptr;
void* g_consoleData = nullptr;

void writeLine(void* data, int category, SDL_LogPriority priority, const char* message) {
    (void)data;
    if (g_console) g_console(g_consoleData, category, priority, message);
    if (!g_file) return;
    std::fputs(message, g_file);
    std::fputc('\n', g_file);
    std::fflush(g_file); // a crash keeps every line written before it
}

} // namespace

void startLogFile(const std::filesystem::path& file) {
    if (g_file) return;
    const char* base = SDL_GetBasePath();
    const std::filesystem::path chosen = file.empty() ? std::filesystem::path(base ? base : "") / "opendash.log" : file;
    const std::string path = chosen.string();
#ifdef _WIN32
    g_file = _wfopen(chosen.c_str(), L"w");
#else
    g_file = std::fopen(path.c_str(), "w");
#endif
    SDL_GetLogOutputFunction(&g_console, &g_consoleData);
    SDL_SetLogOutputFunction(writeLine, nullptr);
    if (!g_file) SDL_Log("log: cannot write %s", path.c_str());
}

} // namespace od::app
