#if defined(_WIN32) && !defined(NDEBUG)
#define OD_CRASH_REPORT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "app/log_file.hpp"
#ifndef NDEBUG
#include "game/actors.hpp"
#endif

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

#ifdef OD_CRASH_REPORT
namespace {
// a crash says where it was, in the log: offsets into the executable
LONG WINAPI logCrash(EXCEPTION_POINTERS* e) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    SDL_Log("crash: code %08lx at exe+%llx, address %llx", e->ExceptionRecord->ExceptionCode,
            (unsigned long long)(reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress) - base),
            (unsigned long long)e->ExceptionRecord->ExceptionInformation[1]);
    SDL_Log("crash: the last actor class updated: %s", od::routineName(od::port::updatingActorClass()));
    void* frames[24];
    const USHORT n = CaptureStackBackTrace(0, 24, frames, nullptr);
    for (USHORT k = 0; k < n; ++k) SDL_Log("crash: frame exe+%llx", (unsigned long long)(reinterpret_cast<uintptr_t>(frames[k]) - base));
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace
#endif

void startLogFile(const char* fileName) {
#ifdef OD_CRASH_REPORT
    SetUnhandledExceptionFilter(logCrash);
#endif
    if (g_file) return;
    const char* base = SDL_GetBasePath();
    const std::string path = std::string(base ? base : "") + fileName;
    g_file = std::fopen(path.c_str(), "w");
    SDL_GetLogOutputFunction(&g_console, &g_consoleData);
    SDL_SetLogOutputFunction(writeLine, nullptr);
    if (!g_file) SDL_Log("log: cannot write %s", path.c_str());
}

} // namespace od::app
