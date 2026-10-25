// The disc image reader (engine/disc_image) over the user's PS1 image: the cue sheet's tracks, the sectors' headers,
// the ISO9660 tree, every form 1 file read whole and every streamed file's sectors form 2.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "check.hpp"
#include "engine/disc_image.hpp"

int main(int argc, char** argv) {
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
