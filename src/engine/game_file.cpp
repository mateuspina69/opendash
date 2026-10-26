#include "engine/game_file.hpp"

#include <algorithm>
#include <stdexcept>

#include "engine/dat.hpp"
#include "engine/ps1_container.hpp"
#include "engine/ps1_music.hpp"
#include "engine/ps1_sfx.hpp"

namespace od {

namespace {

constexpr uint32_t kPs1RamBase = 0x80000000u; // KSEG0: a PS1 block's load address -> the PC TOC's arg

VramRect pcRect(std::span<const uint8_t> sub, bool clut, const std::string& file) {
    // {u32 size, s16 x, y, w, h, pixels} (Maybe_UploadClut / Maybe_LoadImageToVram 0x401630)
    if (sub.size() < 0xc) throw std::runtime_error(file + ": VRAM sub-block shorter than its rect");
    VramRect r;
    r.x = int16_t(rd16(sub.data() + 4));
    r.y = int16_t(rd16(sub.data() + 6));
    r.w = int16_t(rd16(sub.data() + 8));
    r.h = int16_t(rd16(sub.data() + 10));
    r.clut = clut;
    const size_t n = r.w > 0 && r.h > 0 ? std::min(sub.size() - 0xc, size_t(r.w) * size_t(r.h) * 2) : 0;
    r.pixels.assign(sub.begin() + 0xc, sub.begin() + 0xc + std::ptrdiff_t(n));
    return r;
}

} // namespace

const FilePart* GameFile::first(FilePart::Kind kind) const {
    for (const FilePart& p : parts)
        if (p.kind == kind) return &p;
    return nullptr;
}

const FilePart& GameFile::require(FilePart::Kind kind) const {
    if (const FilePart* p = first(kind)) return *p;
    static const char* const kNames[] = {"code",     "model bank", "geometry",  "VRAM",        "PC message bank",
                                         "PC font",  "WAV SFX bank", "MP3 music", "PS1 message bank", "PS1 block", "PS1 music"};
    throw std::runtime_error(name + ": no " + kNames[int(kind)] + " part");
}

GameFile pcGameFile(std::string name, std::span<const uint8_t> dat) {
    GameFile f;
    f.name = std::move(name);
    using K = FilePart::Kind;
    for (const DatSegment& s : readContainer(dat)) {
        const std::span<const uint8_t> seg = dat.subspan(s.fileOffset, s.size);
        FilePart p;
        p.arg = s.arg;
        p.extra = s.extra;
        switch (s.type) {
        case 0: p.kind = K::Code; break;
        case 1: p.kind = K::Models; break;
        case 2: p.kind = K::Geometry; break;
        case 3: p.kind = K::Vram; p.rects.push_back(pcRect(seg, true, f.name)); break;
        case 4: { // the CLUT's sub-block, then the page's at the first word's offset
            p.kind = K::Vram;
            const uint32_t page = seg.size() >= 4 ? rd32(seg.data()) : 0;
            if (page > seg.size()) throw std::runtime_error(f.name + ": page sub-block past its segment");
            p.rects.push_back(pcRect(seg.first(page), true, f.name));
            p.rects.push_back(pcRect(seg.subspan(page), false, f.name));
            break;
        }
        case 5: p.kind = K::Vram; p.rects.push_back(pcRect(seg, false, f.name)); break;
        case 6: p.kind = K::PcMessages; break;
        case 7: p.kind = K::PcFont; break;
        case 8: p.kind = K::WavSfxBank; break;
        case 9: p.kind = K::Mp3Music; break;
        default: throw std::runtime_error(f.name + ": segment type " + std::to_string(s.type) + " has no loader");
        }
        if (p.kind != K::Vram) p.bytes.assign(seg.begin(), seg.end());
        f.parts.push_back(std::move(p));
    }
    return f;
}

GameFile ps1GameFile(std::string name, std::span<const uint8_t> bin) {
    GameFile f;
    f.name = std::move(name);
    using K = FilePart::Kind;
    const auto blocks = ps1Blocks(bin);
    std::vector<bool> musicBanks(blocks.size(), false);
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        const Ps1Block& b = blocks[bi];
        if (b.type == 8 || b.type == 9 || b.type == 15 || b.type == 16) {
            // The sequence precedes its bank on the disc; other blocks may sit between them.
            const uint32_t bankType = b.type < 10 ? 5u : 14u;
            auto bank = std::find_if(blocks.begin() + bi + 1, blocks.end(), [=](const Ps1Block& x) {
                return x.type == bankType;
            });
            if (bank == blocks.end()) throw std::runtime_error(f.name + ": music sequence has no instrument bank");
            musicBanks[size_t(bank - blocks.begin())] = true;
            const auto seqBytes = bin.subspan(b.offset, size_t(b.sectors) * kPs1Sector);
            const auto bankBytes = bin.subspan(bank->offset, size_t(bank->sectors) * kPs1Sector);
            for (auto& packet : ps1MusicTracks(seqBytes, bankBytes)) {
                const Ps1MusicPlayer player(packet, false);
                FilePart music;
                music.kind = K::Ps1Music;
                music.arg = player.info().hasLoop ? 1 : 0;
                music.bytes = std::move(packet);
                f.parts.push_back(std::move(music));
            }
            continue;
        }
        FilePart p;
        switch (b.type) {
        case kPs1SoundBank: {
            if (musicBanks[bi]) continue;
            const auto whole = bin.subspan(b.offset, size_t(b.sectors) * kPs1Sector);
            const uint32_t key = ps1SfxBankKey(whole);
            p.kind = K::WavSfxBank;
            p.arg = key & 0xffff;
            p.extra = key >> 16;
            p.bytes = ps1SfxBank(whole);
            break;
        }
        case kPs1Code:
            p.kind = K::Code;
            p.arg = b.header[3] - kPs1RamBase;
            p.bytes = ps1Payload(bin, b);
            break;
        case kPs1Models:
        case kPs1PackedModels: p.kind = K::Models; p.bytes = ps1Payload(bin, b); break;
        case kPs1PackedGeometry: p.kind = K::Geometry; p.bytes = ps1Payload(bin, b); break;
        case kPs1Messages: p.kind = K::Ps1Messages; p.bytes = ps1Payload(bin, b); break;
        case kPs1Image:
        case kPs1PackedImage: {
            p.kind = K::Vram;
            std::vector<Ps1Upload> ups = ps1Image(bin, b);
            const bool hasClut = (b.header[4] & 0xffff) != 0 && (b.header[4] >> 16) != 0;
            for (size_t i = 0; i < ups.size(); ++i)
                p.rects.push_back({int16_t(ups[i].x), int16_t(ups[i].y), int16_t(ups[i].w), int16_t(ups[i].h),
                                   hasClut && i == 0, std::move(ups[i].pixels)});
            break;
        }
        default: {
            p.kind = K::Ps1Block;
            p.arg = b.type;
            const auto whole = bin.subspan(b.offset, size_t(b.sectors) * kPs1Sector);
            p.bytes.assign(whole.begin(), whole.end());
        }
        }
        f.parts.push_back(std::move(p));
    }
    return f;
}

} // namespace od
