// Which bytes of dash2.exe OpenDash actually depends on (measurement for dropping the exe: every such byte needs a
// source on the PS1 disc or an OpenDash implementation). Off unless enabled (--exe-coverage PATH or
// OPENDASH_EXE_COVERAGE=PATH); then one flag byte per image byte:
//   kFileRead  a byte from the exe file the data source read (engine/pc_data_source; the tests' exe image too): a
//              real dependency on the file's contents
//   kBssRead   the same in a section's zero-filled tail: needs no file, only the zero (informative)
//   kTextScan  .text bytes a scan of the code reads to find table pointers (the tests' stage table check)
// At exit the flags are OR-ed into PATH (created if missing), so sequential runs (a playthrough chain) accumulate
// one union. File: "ODXC" u32 version=1, u32 image lo VA, u32 size, then size flag bytes.
#pragma once

#include <cstdint>
#include <filesystem>

namespace od::exe_coverage {

enum : uint8_t { kFileRead = 1, kBssRead = 2, kTextScan = 4 };

extern bool g_enabled;
inline bool enabled() { return g_enabled; }

// Before the first read of the exe: arms the tracker and registers the dump at exit.
void enable(const std::filesystem::path& out);
void mark(uint32_t va, uint32_t n, uint8_t flag);

} // namespace od::exe_coverage
