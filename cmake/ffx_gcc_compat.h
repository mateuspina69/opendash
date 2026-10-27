// Force-included into the FidelityFX SDK's sources outside Windows (cmake/fidelityfx_vk.cmake): the MSVC CRT
// functions and the headers the SDK takes from <windows.h> there.
#pragma once

#include <bit>
#include <cmath>
#include <codecvt>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <locale>

#include <FidelityFX/host/ffx_util.h> // FFX_UNUSED (ffx_message.cpp uses it without the include)

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

inline int wcscpy_s(wchar_t* dst, size_t n, const wchar_t* src) {
    if (!dst || n == 0) return 1;
    std::wcsncpy(dst, src ? src : L"", n - 1);
    dst[n - 1] = 0;
    return 0;
}
template <size_t N> inline int wcscpy_s(wchar_t (&dst)[N], const wchar_t* src) { return wcscpy_s(dst, N, src); }

inline int strcpy_s(char* dst, size_t n, const char* src) {
    if (!dst || n == 0) return 1;
    std::strncpy(dst, src ? src : "", n - 1);
    dst[n - 1] = 0;
    return 0;
}
template <size_t N> inline int strcpy_s(char (&dst)[N], const char* src) { return strcpy_s(dst, N, src); }

inline int sprintf_s(char* dst, size_t n, const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    const int r = std::vsnprintf(dst, n, fmt, a);
    va_end(a);
    return r;
}
