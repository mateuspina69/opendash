// The PS1 disc's game files (engine/game_file over engine/disc_image) vs the PC dat/ (engine/game_file), as
// docs/ps1_disc.md records them: every PC code (with its load address) / models / geometry part has a byte-identical PS1 part but the
// five version differences named below, and the PC's VRAM uploads come back from the disc's image blocks but the
// counted PC-only art.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <tuple>

#include "check.hpp"
#include "engine/dat.hpp"
#include "engine/disc_image.hpp"
#include "engine/game_file.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const od::DiscImage disc(argv[1]);
    const fs::path datDir = argv[2];

    // the PC segments with no byte-identical PS1 block ("file#segment")
    const std::set<std::string> kVersionDiffers = {"ST37#1", "ST08#0", "ST0B#0", "ST26T#66", "ST56#0"};
    std::set<std::string> differs;
    size_t files = 0, segments = 0, uploads = 0, identicalUploads = 0, images = 0;
    for (const auto& entry : fs::directory_iterator(datDir)) {
        const std::string base = entry.path().stem().string();
        const od::DiscFile* f = disc.find("DAT/" + base + ".BIN");
        if (!f) f = disc.find("COMMON/" + base + ".BIN");
        if (!f) continue; // PC only (docs/ps1_disc.md)
        ++files;
        const od::GameFile ps1 = od::ps1GameFile(base + ".DAT", disc.file(f->path));
        // the code / models / geometry payloads (code with its load address) and the VRAM uploads
        using K = od::FilePart::Kind;
        std::set<std::tuple<K, uint32_t, std::vector<uint8_t>>> payloads;
        std::map<std::pair<std::array<int16_t, 4>, std::vector<uint8_t>>, int> ps1Uploads;
        for (const od::FilePart& p : ps1.parts) {
            if (p.kind == K::Code || p.kind == K::Models || p.kind == K::Geometry)
                payloads.insert({p.kind, p.kind == K::Code ? p.arg : 0, p.bytes});
            else if (p.kind == K::Vram) {
                ++images;
                for (const od::VramRect& r : p.rects) ++ps1Uploads[{{r.x, r.y, r.w, r.h}, r.pixels}];
            }
        }

        const od::GameFile pc = od::pcGameFile(entry.path().filename().string(), od::readFile(entry.path()));
        for (size_t i = 0; i < pc.parts.size(); ++i) {
            const od::FilePart& p = pc.parts[i];
            if (p.kind == K::Code || p.kind == K::Models || p.kind == K::Geometry) {
                ++segments;
                if (!payloads.contains({p.kind, p.kind == K::Code ? p.arg : 0, p.bytes}))
                    differs.insert(base + "#" + std::to_string(i));
            } else if (p.kind == K::Vram) {
                for (const od::VramRect& r : p.rects) {
                    ++uploads;
                    const auto it = ps1Uploads.find({{r.x, r.y, r.w, r.h}, r.pixels});
                    if (it != ps1Uploads.end() && it->second > 0) {
                        --it->second;
                        ++identicalUploads;
                    }
                }
            }
        }
    }
    for (const auto& d : differs)
        if (!kVersionDiffers.contains(d)) std::fprintf(stderr, "%s: no identical PS1 block\n", d.c_str());
    for (const auto& d : kVersionDiffers)
        if (!differs.contains(d)) std::fprintf(stderr, "%s: now identical (update docs/ps1_disc.md)\n", d.c_str());
    CHECK(differs == kVersionDiffers);
    std::printf("%zu files: %zu code / models / geometry segments (%zu differ), %zu image blocks, %zu / %zu VRAM "
                "uploads identical\n",
                files, segments, differs.size(), images, identicalUploads, uploads);
    CHECK(files == 331);
    CHECK(segments == 371);
    CHECK(uploads == 7374 && identicalUploads == 7249);
    return testResult("test_ps1_files");
}
