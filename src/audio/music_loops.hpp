// Seamless music loops for the PC MP3 tracks (DAT segment type 9). The PC restarts every MP3 from 0 at its end
// (Xaudio stream, loop flag), replaying the intro and the MP3 encoder delay / padding silence. The PS1 / PSP
// version plays sequences (PS1 blocks 8 / 9 / 0xf / 0x10) whose CC99 = 20 / 30 markers loop the body only.
// Tracks the PS1 markers do not fit loop the whole recorded master without the MP3 silence.
#pragma once

#include <cstdint>
#include <span>

namespace od::audio {

struct MusicLoop {
    uint64_t hash;   // FNV-1a 64 of the MP3 segment bytes
    uint32_t frames; // decoded length (sanity check against dr_mp3)
    uint32_t start, end;
};

const MusicLoop* findMusicLoop(std::span<const uint8_t> mp3);

} // namespace od::audio
