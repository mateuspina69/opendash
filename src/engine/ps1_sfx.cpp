#include "engine/ps1_sfx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

#include "engine/dat.hpp"
#include "engine/ps1_container.hpp"

namespace od {

namespace {

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error("PS1 sound bank: " + what); }

constexpr int kFilters[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

struct Record {
    uint32_t addr = 0;
    uint8_t volume = 0, note = 0, fine = 0, key = 0;
    uint16_t sample = 0;
    uint16_t freq() const { return uint16_t(uint32_t(44100.0 * std::pow(2.0, (int(key) - int(note) + fine / 128.0) / 12.0))); }
};

struct Sample {
    std::vector<uint8_t> wav; // RIFF / WAVE PCM16 mono 22050
    int32_t loop = -1;        // loop byte offset in the PCM, -1 = one-shot
};

struct Entry {
    uint8_t sample = 0, volume = 0;
    uint16_t freq = 0;
};

std::vector<uint8_t> wav(std::span<const int16_t> pcm) {
    const uint32_t bytes = uint32_t(pcm.size() * 2);
    std::vector<uint8_t> out(44 + bytes);
    auto u32 = [&](size_t o, uint32_t v) { for (int k = 0; k < 4; ++k) out[o + k] = uint8_t(v >> (8 * k)); };
    auto u16 = [&](size_t o, uint16_t v) { out[o] = uint8_t(v); out[o + 1] = uint8_t(v >> 8); };
    std::memcpy(out.data(), "RIFF", 4);
    u32(4, 36 + bytes);
    std::memcpy(out.data() + 8, "WAVEfmt ", 8);
    u32(16, 16);
    u16(20, 1);       // PCM
    u16(22, 1);       // mono
    u32(24, 22050);
    u32(28, 44100);   // byte rate as the PC's banks have it
    u16(32, 2);
    u16(34, 16);
    std::memcpy(out.data() + 36, "data", 4);
    u32(40, bytes);
    std::memcpy(out.data() + 44, pcm.data(), bytes);
    return out;
}

// the PC bank: u32 table size (= 4 + entries), 8-byte entries {u8 sample, u8 volume, u16 freq, u32 handle}, then at
// the table size the sample table {u32 offset (from the table), s32 loop} with index 0 unused, then the RIFF files
struct PcBank {
    std::vector<Entry> entries;
    std::map<int, Sample> samples; // 1-based
};
PcBank parsePc(std::span<const uint8_t> b) {
    PcBank pc;
    if (b.size() < 4) fail("PC bank too short");
    const uint32_t tab = rd32(b.data());
    if (tab < 4 || tab > b.size() || (tab - 4) % 8) fail("PC bank table size");
    for (uint32_t o = 4; o < tab; o += 8) pc.entries.push_back({b[o], b[o + 1], rd16(b.data() + o + 2)});
    if (tab + 16 > b.size()) return pc;
    const uint32_t first = rd32(b.data() + tab + 8); // sample 1's offset = the sample table's size
    const uint32_t n = first / 8;
    for (uint32_t k = 1; k < n && tab + 8 * k + 8 <= b.size(); ++k) {
        const uint32_t ofs = rd32(b.data() + tab + 8 * k);
        const int32_t loop = int32_t(rd32(b.data() + tab + 8 * k + 4));
        const size_t at = size_t(tab) + ofs;
        if (at + 8 > b.size() || std::memcmp(b.data() + at, "RIFF", 4) != 0) continue;
        const size_t len = size_t(rd32(b.data() + at + 4)) + 8;
        if (at + len > b.size()) continue;
        pc.samples[int(k)] = {std::vector<uint8_t>(b.begin() + at, b.begin() + at + len), loop};
    }
    return pc;
}

// the same PCM up to the PC decoder's rounding (<= 16)
bool close(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size() || a.size() < 44) return false;
    for (size_t o = 44; o + 1 < a.size(); o += 2)
        if (std::abs(int(int16_t(rd16(a.data() + o))) - int(int16_t(rd16(b.data() + o)))) > 16) return false;
    return true;
}

} // namespace

std::vector<int16_t> decodeSpuAdpcm(std::span<const uint8_t> raw) {
    std::vector<int16_t> out;
    out.reserve(raw.size() / 16 * 28);
    int h1 = 0, h2 = 0;
    for (size_t o = 0; o + 16 <= raw.size(); o += 16) {
        const int shift = raw[o] & 15, flt = std::min((raw[o] >> 4) & 7, 4);
        const int f0 = kFilters[flt][0], f1 = kFilters[flt][1];
        for (int i = 0; i < 28; ++i) {
            const uint8_t b = raw[o + 2 + i / 2];
            int n = (i & 1) ? (b >> 4) : (b & 15);
            if (n >= 8) n -= 16;
            int s = shift <= 12 ? (n << 12) >> shift : 0;
            s += (h1 * f0 + h2 * f1 + 32) >> 6;
            s = std::clamp(s, -32768, 32767);
            out.push_back(int16_t(s));
            h2 = h1;
            h1 = s;
        }
    }
    return out;
}

uint32_t ps1SfxBankKey(std::span<const uint8_t> block) {
    if (block.size() < kPs1BlockHeader) fail("block too short");
    return rd32(block.data() + 0x0c);
}

std::vector<uint8_t> ps1SfxBank(std::span<const uint8_t> blk) {
    if (blk.size() < kPs1BlockHeader + 0x20 || rd32(blk.data()) != kPs1SoundBank) fail("invalid native block");
    const uint32_t base = rd32(blk.data() + 0x10), tab = rd32(blk.data() + 0x14);
    const size_t header = size_t(rd16(blk.data() + 0x1c)) * 8;
    if (header > tab || size_t(kPs1BlockHeader) + tab > blk.size()) fail("native table past block");
    const size_t count = (tab - header) / 20;
    const size_t body = (kPs1BlockHeader + size_t(tab) + kPs1Sector - 1) / kPs1Sector * kPs1Sector + 0x10;
    // The existing encoder accepts an entry layout as its template. Build that layout from the disc's tone slots,
    // rather than inheriting PC-specific disabled entries or substituting PC samples.
    const uint32_t tableSize = uint32_t(4 + count * 8);
    std::vector<uint8_t> layout(tableSize, 0);
    for (unsigned i = 0; i < 4; ++i) layout[i] = uint8_t(tableSize >> (8 * i));
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* p = blk.data() + kPs1BlockHeader + header + i * 20;
        const Record rec{rd32(p), p[6], p[8], p[9], p[0xa], rd16(p + 0x12)};
        if (!rec.volume || rec.sample == 255) continue;
        if (rec.sample > 254 || rec.addr < base) fail("native tone has an invalid sample");
        const uint64_t start = uint64_t(body) + rec.addr - base;
        if (start + 16 > blk.size()) fail("native tone sample past block");
        size_t end = size_t(start);
        while (end + 16 <= blk.size() && !(blk[end + 1] & 1)) end += 16;
        if (end + 16 > blk.size()) fail("native sample has no end frame");
        const size_t at = 4 + i * 8;
        layout[at] = uint8_t(rec.sample + 1);
        layout[at + 1] = rec.volume;
        const uint16_t freq = rec.freq();
        layout[at + 2] = uint8_t(freq);
        layout[at + 3] = uint8_t(freq >> 8);
    }
    auto bank = ps1SfxBank(blk, layout);
    return bank.empty() ? layout : bank;
}

std::vector<uint8_t> ps1SfxBank(std::span<const uint8_t> blk, std::span<const uint8_t> pcBank) {
    if (blk.size() < kPs1BlockHeader + 0x20) fail("block too short");
    const uint32_t base = rd32(blk.data() + 0x10), tab = rd32(blk.data() + 0x14);
    const uint16_t nhead = rd16(blk.data() + 0x1c);
    if (kPs1BlockHeader + size_t(tab) > blk.size()) fail("table past the block");
    const uint8_t* p = blk.data() + kPs1BlockHeader;
    std::vector<Record> records;
    for (int64_t r = int64_t(nhead) * 8; r < int64_t(tab) - 19; r += 20)
        records.push_back({rd32(p + r), p[r + 6], p[r + 8], p[r + 9], p[r + 0xa], rd16(p + r + 0x12)});
    const size_t body = (kPs1BlockHeader + tab + kPs1Sector - 1) / kPs1Sector * kPs1Sector + 0x10;
    std::map<int, Sample> ps1; // by PS1 sample index
    for (const Record& rec : records) {
        if (ps1.count(rec.sample)) continue;
        const int64_t start = int64_t(body) + int64_t(rec.addr) - int64_t(base);
        if (start < int64_t(body) || size_t(start) + 16 > blk.size()) continue;
        size_t end = size_t(start);
        std::optional<size_t> loop;
        while (end + 16 <= blk.size() && !(blk[end + 1] & 1)) {
            if ((blk[end + 1] & 4) && end != size_t(start)) loop = end;
            end += 16;
        }
        if (end + 16 > blk.size()) continue; // no end frame inside the block
        if (!(blk[end + 1] & 2)) loop.reset(); // an end without repeat: one-shot
        else if (!loop) loop = size_t(start);
        const auto pcm = decodeSpuAdpcm(blk.subspan(size_t(start), end + 16 - size_t(start)));
        ps1[rec.sample] = {wav(pcm), loop ? int32_t((*loop - size_t(start)) / 16 * 56) : -1};
    }

    const PcBank pc = parsePc(pcBank);
    std::vector<Entry> entries;
    int nSamples = 1;
    for (size_t i = 0; i < pc.entries.size(); ++i) {
        const Entry& e = pc.entries[i];
        const Record* r = i < records.size() ? &records[i] : nullptr;
        if (!r || e.sample == 0 || (e.volume == 0 && r->volume == 0)) entries.push_back(e);
        else entries.push_back({uint8_t(r->sample + 1), r->volume, r->freq()});
        nSamples = std::max(nSamples, int(entries.back().sample));
    }
    std::vector<Sample> samples; // 1..nSamples
    for (int k = 1; k <= nSamples; ++k) {
        if (const auto it = ps1.find(k - 1); it != ps1.end()) samples.push_back(it->second);
        else if (const auto f = pc.samples.find(k); f != pc.samples.end()) samples.push_back(f->second);
        else samples.push_back({wav(std::vector<int16_t>(28, 0)), -1});
    }

    // the PC's bank stays where this one equals it
    bool same = entries.size() == pc.entries.size();
    for (size_t i = 0; same && i < entries.size(); ++i)
        same = entries[i].sample == pc.entries[i].sample && entries[i].volume == pc.entries[i].volume &&
               entries[i].freq == pc.entries[i].freq;
    for (size_t i = 0; same && i < entries.size(); ++i) {
        const int k = entries[i].sample;
        if (k == 0 || entries[i].volume == 0) continue;
        const auto f = pc.samples.find(k);
        same = f != pc.samples.end() && k <= nSamples && close(samples[size_t(k - 1)].wav, f->second.wav) &&
               samples[size_t(k - 1)].loop == f->second.loop;
    }
    if (same) return {};

    std::vector<uint8_t> out(4);
    const uint32_t tableSize = uint32_t(4 + entries.size() * 8);
    for (int k = 0; k < 4; ++k) out[size_t(k)] = uint8_t(tableSize >> (8 * k));
    for (const Entry& e : entries) out.insert(out.end(), {e.sample, e.volume, uint8_t(e.freq), uint8_t(e.freq >> 8), 0, 0, 0, 0});
    const size_t table = out.size(), tableBytes = 8 * size_t(nSamples + 1);
    out.resize(table + tableBytes, 0);
    for (int k = 1; k <= nSamples; ++k) {
        const Sample& s = samples[size_t(k - 1)];
        const uint32_t ofs = uint32_t(out.size() - table);
        for (int b = 0; b < 4; ++b) {
            out[table + 8 * size_t(k) + size_t(b)] = uint8_t(ofs >> (8 * b));
            out[table + 8 * size_t(k) + 4 + size_t(b)] = uint8_t(uint32_t(s.loop) >> (8 * b));
        }
        out.insert(out.end(), s.wav.begin(), s.wav.end());
    }
    return out;
}

} // namespace od
