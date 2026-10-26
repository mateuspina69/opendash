// Disc-native music. Capcom's compact SEQ/VAB containers are decoded directly from the user's disc;
// neither the PC recordings nor a General MIDI soundfont are used.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace od {

// One self-contained ODSM packet per sequence, in the collection's order. The bank is the whole
// 0x05/0x0e block; the sequence collection is the whole 0x08/0x09/0x0f/0x10 block. Throws on bad data.
std::vector<std::vector<uint8_t>> ps1MusicTracks(std::span<const uint8_t> sequences,
                                               std::span<const uint8_t> bank);
bool isPs1Music(std::span<const uint8_t> bytes);

struct Ps1MusicInfo {
    uint64_t endFrame = 0, loopStartFrame = 0, loopEndFrame = 0;
    uint32_t events = 0, samples = 0;
    uint8_t loopRepeats = 0; // 127 = endless; otherwise the authored repeat count
    bool hasLoop = false;
};

// A portable 24-voice sampler, at 44100 Hz stereo. Owns its data. next() returns false only after
// a one-shot's final note releases; rewind() also resets controllers, envelopes and sample positions.
// Loop markers are honored only when loop=true. Throws std::runtime_error for a corrupt packet.
// Like the host mixer, resampling is linear; this is not a cycle-exact SPU emulator.
class Ps1MusicPlayer {
public:
    explicit Ps1MusicPlayer(std::span<const uint8_t> packet, bool loop);
    ~Ps1MusicPlayer();
    Ps1MusicPlayer(Ps1MusicPlayer&&) noexcept;
    Ps1MusicPlayer& operator=(Ps1MusicPlayer&&) noexcept;
    Ps1MusicPlayer(const Ps1MusicPlayer&) = delete;
    Ps1MusicPlayer& operator=(const Ps1MusicPlayer&) = delete;

    bool next(float& left, float& right);
    void rewind();
    const Ps1MusicInfo& info() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace od
