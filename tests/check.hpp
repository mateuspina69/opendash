// Minimal test helpers (no framework dependency). A test binary returns non-zero on failure.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <exception>

inline int g_failures = 0;

// an exception no test catches: its message on stderr before the abort (the abort alone loses the buffered output)
inline const bool g_reportUncaught = (std::set_terminate([] {
    std::fflush(stdout);
    try {
        if (const std::exception_ptr e = std::current_exception()) std::rethrow_exception(e);
        std::fprintf(stderr, "terminate without an exception\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "uncaught exception: %s\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "uncaught exception of an unknown type\n");
    }
    std::abort();
}), true);

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                                \
        }                                                                                \
    } while (0)

inline int testResult(const char* name) {
    std::printf("%s: %s\n", name, g_failures ? "FAILED" : "ok");
    return g_failures ? 1 : 0;
}
