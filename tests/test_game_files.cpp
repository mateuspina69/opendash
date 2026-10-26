// Startup requirements depend on the selected data source. A disc-backed run needs no PC installation.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "check.hpp"
#include "app/game_files.hpp"
#include "engine/disc_image.hpp"
#include "engine/game_files_hash.hpp"

namespace fs = std::filesystem;

namespace {
void put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = uint8_t(value >> (8 * i));
}

// A tiny synthetic ISO in raw sectors: readable directories/CNF, an executable and INIT. No game bytes.
fs::path makeDisc(const fs::path& root) {
    std::vector<std::vector<uint8_t>> sectors(26, std::vector<uint8_t>(2048));
    auto record = [&](std::vector<uint8_t>& bytes, size_t at, const std::string& name,
                      uint32_t lba, uint32_t size, bool directory = false) {
        const size_t length = (33 + name.size() + 1) & ~size_t(1);
        bytes[at] = uint8_t(length);
        put32(bytes, at + 2, lba);
        put32(bytes, at + 10, size);
        bytes[at + 25] = directory ? 2 : 0;
        bytes[at + 32] = uint8_t(name.size());
        std::copy(name.begin(), name.end(), bytes.begin() + at + 33);
        return at + length;
    };
    sectors[16][0] = 1;
    std::copy_n("CD001", 5, sectors[16].begin() + 1);
    sectors[16][129] = 8; // logical block size 2048
    record(sectors[16], 156, std::string(1, '\0'), 20, 2048, true);
    const std::string cnf = "BOOT = cdrom:\\SLUS_011.40;1\n";
    size_t at = record(sectors[20], 0, "SYSTEM.CNF;1", 22, uint32_t(cnf.size()));
    at = record(sectors[20], at, "SLUS_011.40;1", 23, 4096);
    record(sectors[20], at, "COMMON", 21, 2048, true);
    record(sectors[21], 0, "INIT.BIN;1", 25, 2048);
    std::copy(cnf.begin(), cnf.end(), sectors[22].begin());
    const fs::path bin = root / "synthetic.bin", cue = root / "synthetic.cue";
    std::ofstream image(bin, std::ios::binary);
    auto bcd = [](unsigned n) { return uint8_t((n / 10) * 16 + n % 10); };
    for (size_t i = 0; i < sectors.size(); ++i) {
        std::vector<uint8_t> raw(2352);
        std::fill(raw.begin() + 1, raw.begin() + 11, 0xff);
        const unsigned msf = unsigned(i) + 150;
        raw[12] = bcd(msf / 4500);
        raw[13] = bcd(msf / 75 % 60);
        raw[14] = bcd(msf % 75);
        raw[15] = 2;
        std::copy(sectors[i].begin(), sectors[i].end(), raw.begin() + 24);
        image.write(reinterpret_cast<const char*>(raw.data()), std::streamsize(raw.size()));
    }
    std::ofstream(cue) << "FILE \"synthetic.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";
    return cue;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const fs::path root = fs::temp_directory_path() /
        ("od_test_game_files_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "dat");
    od::Config cfg;
    cfg.datDir = root / "dat";
    cfg.exePath = root / "dash2.exe";
    cfg.disc = argv[1];

    // A disc configured only for language packs must not bypass the PC requirements.
    CHECK(!od::app::gameFilesFound(cfg));
    CHECK(!od::app::ensureGameFiles(cfg, false));
    cfg.data = "disc";
    CHECK(od::app::gameFilesFound(cfg));
    CHECK(od::app::ensureGameFiles(cfg, false));
    CHECK(cfg.datDir == root / "dat" && cfg.exePath == root / "dash2.exe");

    cfg.disc.clear();
    CHECK(!od::app::gameFilesFound(cfg));
    CHECK(!od::app::ensureGameFiles(cfg, false));
    cfg.disc = root / "missing.cue";
    CHECK(!od::app::gameFilesFound(cfg));
    // An existing cue with missing tracks is not a usable installation.
    cfg.disc = root / "broken.cue";
    std::ofstream(cfg.disc) << "FILE \"missing.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";
    CHECK(!od::app::gameFilesFound(cfg));
    CHECK(!od::app::ensureGameFiles(cfg, false));

    cfg.disc = makeDisc(root);
    CHECK(!od::app::gameFilesFound(cfg)); // directory entry exists, executable header is corrupt
    const fs::path synthetic = root / "synthetic.bin";
    {
        std::fstream image(synthetic, std::ios::binary | std::ios::in | std::ios::out);
        image.seekp(23 * 2352 + 24);
        image.write("PS-X EXE", 8);
        std::vector<uint8_t> size(4);
        put32(size, 0, 0x1000); // declared text does not fit the executable's 4096 bytes
        image.seekp(23 * 2352 + 24 + 0x1c);
        image.write(reinterpret_cast<const char*>(size.data()), 4);
    }
    CHECK(!od::app::gameFilesFound(cfg));
    {
        std::fstream image(synthetic, std::ios::binary | std::ios::in | std::ios::out);
        const char size[4] = {0, 8, 0, 0}; // 0x800-byte text fits after the 0x800-byte header
        image.seekp(23 * 2352 + 24 + 0x1c);
        image.write(size, 4);
    }
    CHECK(od::app::gameFilesFound(cfg));
    const auto originalHash = od::gameFilesHash(od::DiscImage(cfg.disc));
    CHECK(originalHash.exe != 0 && originalHash.all != 0 && originalHash.exe != originalHash.all);
    CHECK(od::gameFilesHash(od::DiscImage(cfg.disc)).all == originalHash.all);
    const fs::path alias = root / "renamed.cue";
    std::ofstream(alias) << "REM same content under another cue name\nFILE \"synthetic.bin\" BINARY\n"
                           "  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";
    CHECK(od::gameFilesHash(od::DiscImage(alias)).all == originalHash.all);
    {
        std::fstream image(synthetic, std::ios::binary | std::ios::in | std::ios::out);
        image.seekp(25 * 2352 + 24);
        image.put('\1'); // INIT changes; the executable's identity stays the same
    }
    const auto changedData = od::gameFilesHash(od::DiscImage(cfg.disc));
    CHECK(changedData.exe == originalHash.exe && changedData.all != originalHash.all);
    {
        std::fstream image(synthetic, std::ios::binary | std::ios::in | std::ios::out);
        image.seekp(24 * 2352 + 24);
        image.put('\1'); // text byte of the boot executable
    }
    const auto changedExe = od::gameFilesHash(od::DiscImage(cfg.disc));
    CHECK(changedExe.exe != changedData.exe && changedExe.all != changedData.all);
    fs::resize_file(synthetic, 25 * 2352); // directories and boot are intact, INIT's sector is missing
    CHECK(!od::app::gameFilesFound(cfg));
    CHECK(!od::app::ensureGameFiles(cfg, false));

    cfg.data = "pc";
    std::ofstream(cfg.datDir / "INIT.DAT").put('\0');
    std::ofstream(cfg.exePath).put('\0');
    CHECK(!od::app::gameFilesFound(cfg)); // voices are required on PC
    fs::create_directories(root / "snd");
    CHECK(od::app::gameFilesFound(cfg));
    CHECK(od::app::ensureGameFiles(cfg, false));
    od::Config picked;
    CHECK(od::app::gameFilesAt(root, picked));
    CHECK(picked.datDir == cfg.datDir && picked.exePath == cfg.exePath);
    CHECK(od::app::gameFilesAt(root / "dat", picked));
    fs::remove_all(root);
    return testResult("test_game_files");
}
