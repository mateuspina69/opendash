// engine/texture_pack: a synthetic pack over a synthetic VRAM page -- a page found by its decode's name, the page with
// no image enlarged, the pack's scale, the dump of the pages the pack lacks, and the slices made by the loader's threads
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "check.hpp"
#include "engine/texture_pack.hpp"

namespace fs = std::filesystem;

namespace {

constexpr uint16_t kTpage = 1;                     // 4-bit page at (64, 0)
constexpr uint16_t kClut = uint16_t(480 << 6);     // CLUT at (0, 480)
constexpr uint16_t kClut2 = uint16_t(481 << 6);    // another at (0, 481)

std::string hex(uint64_t v) {
    char s[17];
    std::snprintf(s, sizeof(s), "%016llx", static_cast<unsigned long long>(v));
    return s;
}

const uint8_t* at(const std::vector<uint8_t>& s, int edge, int x, int y) { return &s[(size_t(y) * edge + size_t(x)) * 4]; }

} // namespace

int main() {
    od::Vram vram;
    std::vector<uint8_t> words(64 * 256 * 2);
    for (size_t i = 0; i < words.size(); ++i) words[i] = uint8_t(i * 7 + 3);
    vram.loadImage(64, 0, 64, 256, words);
    std::vector<uint8_t> clut(2 * 32);
    for (size_t i = 0; i < clut.size(); i += 2) clut[i] = uint8_t(0x21 * (i / 2 + 1)), clut[i + 1] = i < 32 ? 0x10 : 0x30;
    vram.loadImage(0, 480, 16, 2, clut);

    const fs::path root = fs::temp_directory_path() / "od_test_texture_pack";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "pack" / "sub");

    // the first page's image at scale 2, one colour, its first texel transparent; in a sub folder
    const std::vector<uint8_t> ps1 = vram.decode(kTpage, kClut, true);
    std::vector<uint8_t> img(512 * 512 * 4);
    for (size_t i = 0; i < img.size(); i += 4) img[i] = 200, img[i + 1] = 100, img[i + 2] = 50, img[i + 3] = 255;
    img[3] = 0;
    stbi_write_png((root / "pack" / "sub" / (hex(od::pageName(ps1)) + ".png")).string().c_str(), 512, 512, 4, img.data(), 512 * 4);
    // not a page name: not indexed
    stbi_write_png((root / "pack" / "picture.png").string().c_str(), 512, 512, 4, img.data(), 512 * 4);

    od::bindTexturePack(std::make_shared<const od::TexturePack>(root / "pack"));
    CHECK(od::texturePack()->scale() == 2);
    CHECK(od::texturePack()->size() == 1);
    CHECK(od::sliceEdge() == 512);

    int images = -1;
    const std::vector<uint8_t> found = od::pageSlice(vram, kTpage, kClut, true, &images);
    CHECK(images == 1);
    CHECK(found == img);
    CHECK(od::pageSlice(vram, kTpage, kClut, true, &images) == img); // again, from the cache
    CHECK(images == 1);

    // the other CLUT: no image, the PS1 page enlarged (nearest), and dumped once
    od::bindPageDump(root / "dump");
    const std::vector<uint8_t> other = vram.decode(kTpage, kClut2, true);
    const std::vector<uint8_t> enlarged = od::pageSlice(vram, kTpage, kClut2, true, &images);
    CHECK(images == 0);
    CHECK(enlarged.size() == size_t(512) * 512 * 4);
    for (int y = 0; y < 512; y += 37)
        for (int x = 0; x < 512; x += 29) CHECK(std::equal(at(enlarged, 512, x, y), at(enlarged, 512, x, y) + 4, at(other, 256, x / 2, y / 2)));
    const fs::path dumped = root / "dump" / (hex(od::pageName(other)) + ".png");
    CHECK(fs::exists(dumped));
    CHECK(!fs::exists(root / "dump" / (hex(od::pageName(ps1)) + ".png"))); // the pack has it
    size_t dumps = 0;
    for (const auto& e : fs::directory_iterator(root / "dump")) dumps += e.is_regular_file();
    CHECK(dumps == 1);

    // a page whose VRAM changed is another page
    const uint8_t two[2] = {0xff, 0xff};
    vram.loadImage(64 + 3, 6, 1, 1, two);
    od::pageSlice(vram, kTpage, kClut, true, &images);
    CHECK(images == 0);

    // the loader's threads make the same slices; a name asked twice is made once
    {
        od::PageLoader loader(2);
        loader.request(od::pageName(ps1), ps1);
        loader.request(od::pageName(ps1), ps1);
        loader.request(od::pageName(other), other);
        std::vector<std::pair<uint64_t, std::vector<uint8_t>>> made;
        for (int i = 0; i < 500 && made.size() < 2; ++i) {
            for (auto& m : loader.takeReady()) made.push_back(std::move(m));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        for (auto& m : loader.takeReady()) made.push_back(std::move(m));
        CHECK(made.size() == 2);
        CHECK(od::pageMipLevels(512) == 2 && od::pageMipLevels(256) == 1);
        for (const auto& [name, slice] : made) { // the slice, then its level of 256 x 256
            const std::vector<uint8_t>& want = name == od::pageName(ps1) ? img : enlarged;
            CHECK(slice.size() == want.size() + size_t(256) * 256 * 4 && std::equal(want.begin(), want.end(), slice.begin()));
        }
    }

    od::bindPageDump({});
    od::bindTexturePack(nullptr);

    // no page image: refused
    fs::create_directories(root / "empty");
    bool threw = false;
    try {
        od::TexturePack none(root / "empty");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    fs::remove_all(root, ec);
    return testResult("test_texture_pack");
}
