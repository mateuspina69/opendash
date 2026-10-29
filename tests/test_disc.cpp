// The disc image reader (engine/disc_image) over the user's PS1 image: the cue sheet's tracks, the sectors' headers,
// the ISO9660 tree, every form 1 file read whole and every streamed file's sectors form 2. First the VCDIFF reader a
// patched disc reads its track through (engine/vcdiff), on a patch made by hand.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "check.hpp"
#include "engine/disc_image.hpp"
#include "engine/vcdiff.hpp"

namespace {

uint32_t adler32(const std::string& d) {
    uint32_t a = 1, b = 0;
    for (const char c : d) {
        a = (a + uint8_t(c)) % 65521;
        b = (b + a) % 65521;
    }
    return b << 16 | a;
}

// source "abcdefghij"; window 1 (VCD_SOURCE, the whole source): ADD "XY", COPY 5 from source 0 (mode self), COPY 8
// from its own target 2 (mode here: overlaps the bytes it makes), RUN 3 'Z'; window 2 (VCD_TARGET, the target's first
// 4 bytes, with xdelta3's checksum): COPY 4 from 0 twice
void vcdiffByHand() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "opendash_test_vcdiff";
    fs::create_directories(dir);
    const fs::path source = dir / "source.bin", patch = dir / "patch.vcdiff";
    std::ofstream(source, std::ios::binary) << "abcdefghij";
    const std::string want = "XYabcdeabcdeabcZZZ", want2 = "XYabXYab";
    auto window = [](std::vector<uint8_t> head, size_t target, std::vector<uint8_t> data, std::vector<uint8_t> inst,
                     std::vector<uint8_t> addr, const uint32_t* adler) {
        std::vector<uint8_t> delta = {uint8_t(target), 0, uint8_t(data.size()), uint8_t(inst.size()),
                                      uint8_t(addr.size())};
        if (adler)
            for (int k = 3; k >= 0; --k) delta.push_back(uint8_t(*adler >> (8 * k)));
        for (const auto* sec : {&data, &inst, &addr}) delta.insert(delta.end(), sec->begin(), sec->end());
        head.push_back(uint8_t(delta.size()));
        head.insert(head.end(), delta.begin(), delta.end());
        return head;
    };
    auto write = [&](uint32_t adler2) {
        std::vector<uint8_t> p = {0xd6, 0xc3, 0xc4, 0x00, 0x04, 3, 'a', 'p', 'p'}; // with an application header
        const auto w1 = window({0x01, 10, 0}, want.size(), {'X', 'Y', 'Z'}, {3, 21, 40, 0, 3}, {0, 5}, nullptr);
        const auto w2 = window({0x06, 4, 0}, want2.size(), {}, {20, 20}, {0, 0}, &adler2);
        p.insert(p.end(), w1.begin(), w1.end());
        p.insert(p.end(), w2.begin(), w2.end());
        std::ofstream(patch, std::ios::binary).write(reinterpret_cast<const char*>(p.data()), std::streamsize(p.size()));
    };
    auto readAll = [](const od::VcdiffTarget& t, uint64_t at, size_t n) {
        std::vector<uint8_t> out(n);
        t.read(at, out);
        return std::string(out.begin(), out.end());
    };
    write(adler32(want2));
    {
        const od::VcdiffTarget t(source, patch);
        CHECK(t.size() == want.size() + want2.size());
        CHECK(readAll(t, 0, size_t(t.size())) == want + want2);
        CHECK(readAll(t, 16, 6) == "ZZXYab"); // across the windows
        bool past = false;
        try {
            readAll(t, t.size() - 2, 3);
        } catch (const std::runtime_error&) {
            past = true;
        }
        CHECK(past);
    }
    write(adler32(want2) ^ 1);
    {
        const od::VcdiffTarget t(source, patch);
        CHECK(readAll(t, 0, 4) == "XYab");
        bool bad = false;
        try {
            readAll(t, want.size(), 1);
        } catch (const std::runtime_error&) {
            bad = true;
        }
        CHECK(bad); // the second window's checksum
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

} // namespace

int main(int argc, char** argv) {
    vcdiffByHand();
    if (argc < 2) return 2;
    const od::DiscImage disc(argv[1]);

    const auto& tracks = disc.tracks();
    CHECK(!tracks.empty());
    CHECK(tracks[0].number == 1 && tracks[0].kind != od::DiscTrack::Kind::Audio && tracks[0].lba == 0);
    for (size_t i = 1; i < tracks.size(); ++i) CHECK(tracks[i].lba >= tracks[i - 1].lba + tracks[i - 1].sectors);
    for (const auto& t : tracks)
        std::printf("track %02d %s lba %u sectors %u\n", t.number,
                    t.kind == od::DiscTrack::Kind::Audio ? "audio" : "data", t.lba, t.sectors);

    // the boot file names the executable the disc boots
    const auto cnf = disc.file("SYSTEM.CNF");
    const std::string cnfText(cnf.begin(), cnf.end());
    CHECK(cnfText.find("BOOT") != std::string::npos);
    const size_t cdrom = cnfText.find("cdrom:");
    CHECK(cdrom != std::string::npos);
    if (cdrom != std::string::npos) {
        std::string boot = cnfText.substr(cdrom + 6);
        boot = boot.substr(0, boot.find_first_of(";\r\n"));
        const od::DiscFile* exe = disc.find(boot);
        CHECK(exe != nullptr);
        if (exe) {
            const auto bytes = disc.file(exe->path);
            CHECK(bytes.size() >= 0x800 && std::memcmp(bytes.data(), "PS-X EXE", 8) == 0);
            std::printf("boot %s (%u bytes)\n", exe->path.c_str(), exe->size);
        }
    }
    CHECK(disc.find("system.cnf;1") == disc.find("SYSTEM.CNF"));
    CHECK(disc.find("NO/SUCH.BIN") == nullptr);

    size_t form1 = 0, streamed = 0, bytes = 0;
    const bool list = argc > 2 && std::strcmp(argv[2], "--list") == 0; // the file table on stdout
    for (const auto& f : disc.files()) {
        if (list) std::printf("%-24s lba %7u size %10u xa %04x\n", f.path.c_str(), f.lba, f.size, f.xa);
        // a file lies inside one track: a CD-DA file inside an audio track, every other inside the data track
        const auto in = std::find_if(tracks.begin(), tracks.end(), [&](const od::DiscTrack& t) {
            return f.lba >= t.lba && f.lba + f.sectors() <= t.lba + t.sectors;
        });
        if (in == tracks.end() || (in->kind == od::DiscTrack::Kind::Audio) != f.cdda()) {
            std::fprintf(stderr, "%s (lba %u, %u sectors) is not inside a %s track\n", f.path.c_str(), f.lba,
                         f.sectors(), f.cdda() ? "audio" : "data");
            ++g_failures;
            continue;
        }
        if (f.cdda()) {
            ++streamed;
            disc.rawSector(f.lba + f.sectors() - 1); // stored to its end
        } else if (f.streamed()) {
            ++streamed;
            for (uint32_t s = 0; s < f.sectors(); ++s) {
                const od::RawSector raw = disc.rawSector(f.lba + s);
                CHECK(raw[15] == 2);
            }
        } else {
            ++form1;
            bytes += disc.file(f.path).size();
        }
    }
    std::printf("%zu files: %zu form 1 (%zu bytes), %zu streamed\n", disc.files().size(), form1, bytes, streamed);
    CHECK(form1 > 0);

    return testResult("test_disc");
}
