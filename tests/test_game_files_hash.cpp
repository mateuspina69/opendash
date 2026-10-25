// The game files' hash (engine/game_files_hash) on a small fake installation in the temp directory: deterministic, a
// changed byte of a DAT or of the exe changes it, the exe alone is told apart, other files and the names' case do not
// count. Then once on the real installation (its time logged).
//   test_game_files_hash <dat dir> <dash2.exe>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "engine/game_files_hash.hpp"

namespace fs = std::filesystem;

namespace {
void writeFile(const fs::path& p, const std::vector<uint8_t>& b) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const fs::path root = fs::temp_directory_path() / "od_test_game_files_hash";
    fs::remove_all(root);
    fs::create_directories(root / "dat");
    const fs::path exe = root / "dash2.exe";
    std::vector<uint8_t> big(3 << 20); // over the hash's 1 MB chunk
    for (size_t i = 0; i < big.size(); ++i) big[i] = uint8_t(i * 31 + (i >> 9));
    writeFile(exe, {1, 2, 3, 4, 5});
    writeFile(root / "dat" / "INIT.DAT", big);
    writeFile(root / "dat" / "ST08.DAT", {9, 9, 9});
    const od::GameFilesHash a = od::gameFilesHash(root / "dat", exe);
    CHECK(a.exe != 0 && a.all != 0 && a.exe != a.all);
    const od::GameFilesHash a2 = od::gameFilesHash(root / "dat", exe);
    CHECK(a.exe == a2.exe && a.all == a2.all);

    writeFile(root / "dat" / "notes.txt", {7}); // not a .DAT
    CHECK(od::gameFilesHash(root / "dat", exe).all == a.all);

    big[(2 << 20) + 5] ^= 1; // one byte of a DAT
    writeFile(root / "dat" / "INIT.DAT", big);
    const od::GameFilesHash b = od::gameFilesHash(root / "dat", exe);
    CHECK(b.exe == a.exe && b.all != a.all);
    big[(2 << 20) + 5] ^= 1;
    writeFile(root / "dat" / "INIT.DAT", big);
    CHECK(od::gameFilesHash(root / "dat", exe).all == a.all);

    writeFile(root / "dat" / "ST09.dat", {0}); // one more DAT (lower-case extension counts)
    CHECK(od::gameFilesHash(root / "dat", exe).all != a.all);
    fs::remove(root / "dat" / "ST09.dat");

    writeFile(exe, {1, 2, 3, 4, 6}); // the exe
    const od::GameFilesHash c = od::gameFilesHash(root / "dat", exe);
    CHECK(c.exe != a.exe && c.all != a.all);

    bool threw = false;
    try {
        od::gameFilesHash(root / "dat", root / "missing.exe");
    } catch (const std::exception& e) {
        threw = true;
        std::printf("  missing exe: %s\n", e.what());
    }
    CHECK(threw);
    fs::remove_all(root);

    const auto t0 = std::chrono::steady_clock::now();
    const od::GameFilesHash real = od::gameFilesHash(argv[1], argv[2]);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  the installation: exe %016llx, all %016llx (%lld ms)\n", (unsigned long long)real.exe,
                (unsigned long long)real.all, (long long)ms);
    return testResult("test_game_files_hash");
}
