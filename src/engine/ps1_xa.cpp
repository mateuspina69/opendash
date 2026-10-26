#include "engine/ps1_xa.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>

namespace od {

std::vector<Ps1XaClip> ps1XaClips(const DiscFile& file, const std::function<RawSector(uint32_t)>& readSector) {
    std::map<uint8_t, Ps1XaClip> open;
    std::vector<Ps1XaClip> out;
    for (uint32_t i = 0; i < file.sectors(); ++i) {
        const RawSector sector = readSector(file.lba + i);
        const uint8_t channel = sector[17], submode = sector[18], coding = sector[19];
        if (!(submode & 0x04)) continue;
        auto [it, inserted] = open.try_emplace(channel, Ps1XaClip{channel, coding, {}});
        if (!inserted && it->second.coding != coding) throw std::runtime_error("PS1 XA: coding changes inside a clip");
        it->second.sectors.push_back(file.lba + i);
        if (submode & 0x80) {
            out.push_back(std::move(it->second));
            open.erase(it);
        }
    }
    for (auto& [channel, clip] : open) out.push_back(std::move(clip));
    std::sort(out.begin(), out.end(), [](const Ps1XaClip& a, const Ps1XaClip& b) {
        return a.channel != b.channel ? a.channel < b.channel : a.sectors.front() < b.sectors.front();
    });
    return out;
}

std::vector<Ps1XaClip> ps1XaClips(const DiscImage& disc, const DiscFile& file) {
    return ps1XaClips(file, [&](uint32_t lba) { return disc.rawSector(lba); });
}

uint16_t ps1XaTrack(std::span<const uint8_t, 8> descriptor, uint32_t fileLba, std::span<const Ps1XaClip> clips) {
    const uint32_t start = (uint32_t(descriptor[1]) << 16) | descriptor[2] | (uint32_t(descriptor[3]) << 8);
    const uint32_t end = (uint32_t(descriptor[5]) << 16) | descriptor[6] | (uint32_t(descriptor[7]) << 8);
    const uint8_t channel = descriptor[4] & 0x7f;
    if (end <= start) throw std::runtime_error("PS1 XA: empty or reversed voice range");
    size_t track = clips.size();
    for (size_t i = 0; i < clips.size(); ++i) {
        const Ps1XaClip& clip = clips[i];
        if (clip.channel != channel || clip.sectors.empty() || clip.sectors.front() < fileLba) continue;
        const uint32_t first = clip.sectors.front() - fileLba;
        if (first < start || first >= end) continue;
        if (clip.sectors.back() - fileLba >= end)
            throw std::runtime_error("PS1 XA: voice range cuts a clip short");
        if (track != clips.size()) throw std::runtime_error("PS1 XA: voice range contains several clips");
        track = i;
    }
    if (track == clips.size() || track > std::numeric_limits<uint16_t>::max())
        throw std::runtime_error("PS1 XA: voice range has no clip");
    return uint16_t(track);
}

} // namespace od
