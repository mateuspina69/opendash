// Shared XA clip indexing: the language pack's track numbers and disc voice descriptors use this same order.
#pragma once

#include <functional>
#include <span>
#include <vector>

#include "engine/disc_image.hpp"

namespace od {

struct Ps1XaClip {
    uint8_t channel = 0, coding = 0;
    std::vector<uint32_t> sectors; // absolute LBAs, in playback order
};

// Audio sectors per channel, split at EOF; incomplete final clips are kept. Sorted by (channel, first LBA).
std::vector<Ps1XaClip> ps1XaClips(const DiscFile& file, const std::function<RawSector(uint32_t)>& readSector);
std::vector<Ps1XaClip> ps1XaClips(const DiscImage& disc, const DiscFile& file);

// Eight-byte PS1 descriptor -> track in ps1XaClips order. Seek starts are aligned to an interleave boundary,
// preceding the channel's first sector; bit 7 of the channel is the game's low-rate playback flag.
uint16_t ps1XaTrack(std::span<const uint8_t, 8> descriptor, uint32_t fileLba, std::span<const Ps1XaClip> clips);

} // namespace od
