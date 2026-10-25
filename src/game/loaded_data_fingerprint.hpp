// The fingerprint of the loaded data a ghost's pose reads besides its own look files (spec "Facts that shaped the
// review round"): which stage files were loaded (the story flags pick them: ST53 loads other files on flag 0x5a0)
// and which alternate model banks the room scripts mapped (0x754800 / 0x76c800). Two players in the same (stage, sub)
// can hold different data; a ghost is shown only when its fingerprint equals the local one (app/ghost_view), so the
// stage animation tables (ids >= 0x80, [+0x298]) and the script tables (ids 0x60..0x7f) it is posed with are the
// remote player's own.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace od::port {

// What the local game loaded, by file name (the host's loader keeps these: app/room_mesh g_stageFilesGeo / Tex,
// g_altBankDat)
struct LoadedDataIdentity {
    std::string stageGeometry, stageTexture; // the last stage load's ST##.DAT / ST##T.DAT (or the story-state files)
    std::array<std::string, 2> altBanks;     // the DAT mapped in each type-1 bank buffer by a script ("" = none)
};

// 64-bit FNV-1a over the names, each followed by a 0 byte (so "AB" + "C" differs from "A" + "BC")
uint64_t loadedDataFingerprint(const LoadedDataIdentity& id);

} // namespace od::port
