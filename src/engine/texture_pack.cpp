#include "texture_pack.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

#define XXH_INLINE_ALL
#include <xxhash.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
// static: the app links its own copy (app/ui/hd_art)
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#pragma warning(push, 0)
#include <stb_image.h>
#include <stb_image_write.h>
#pragma warning(pop)

namespace od {

namespace {

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// "<16 lowercase hex digits>.png": a page's name
bool nameOf(const std::filesystem::path& p, uint64_t& name) {
    if (p.extension() != ".png") return false;
    const std::string stem = p.stem().string();
    if (stem.size() != 16) return false;
    name = 0;
    for (const char c : stem) {
        const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0) return false;
        name = name << 4 | uint64_t(d);
    }
    return true;
}

std::string hex(uint64_t name) {
    char s[17];
    std::snprintf(s, sizeof(s), "%016llx", static_cast<unsigned long long>(name));
    return s;
}

} // namespace

uint64_t pageName(const std::vector<uint8_t>& ps1) { return XXH3_64bits(ps1.data(), ps1.size()); }

TexturePack::TexturePack(const std::filesystem::path& dir) {
    std::map<std::filesystem::path, uint64_t> found; // name order
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        uint64_t name = 0;
        if (e.is_regular_file() && nameOf(e.path(), name)) found.emplace(e.path(), name);
    }
    if (found.empty()) throw std::runtime_error("no page images in " + dir.string());
    for (const auto& [path, name] : found) images_.emplace(name, path);
    const std::vector<uint8_t> bytes = readFile(found.begin()->first);
    int w = 0, h = 0, comp = 0;
    if (!stbi_info_from_memory(bytes.data(), int(bytes.size()), &w, &h, &comp) || w != h || w % 256 || w > 256 * 16)
        throw std::runtime_error("not a page image: " + found.begin()->first.string());
    scale_ = w / 256;
}

std::vector<uint8_t> TexturePack::enlarge(const std::vector<uint8_t>& rgba, int scale) {
    if (scale == 1) return rgba;
    // the renderer filters a pack's pages: a transparent texel beside a drawn one takes its colour, not its black
    std::vector<uint8_t> filled = rgba;
    for (int pass = 0; pass < 2; ++pass) {
        const std::vector<uint8_t> from = filled;
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x) {
                uint8_t* t = &filled[(size_t(y) * 256 + size_t(x)) * 4];
                if (t[3]) continue;
                int sum[3] = {0, 0, 0}, n = 0;
                for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                    if (x + dx < 0 || x + dx > 255 || y + dy < 0 || y + dy > 255) continue;
                    const uint8_t* s = &from[(size_t(y + dy) * 256 + size_t(x + dx)) * 4];
                    if (!s[3] && !(s[0] | s[1] | s[2])) continue; // transparent and not filled yet
                    for (int c = 0; c < 3; ++c) sum[c] += s[c];
                    ++n;
                }
                if (n)
                    for (int c = 0; c < 3; ++c) t[c] = uint8_t(sum[c] / n);
            }
    }
    const size_t edge = size_t(256) * size_t(scale);
    std::vector<uint8_t> out(edge * edge * 4);
    for (size_t y = 0; y < edge; ++y) {
        const uint8_t* src = filled.data() + (y / size_t(scale)) * 256 * 4;
        uint8_t* dst = out.data() + y * edge * 4;
        for (size_t x = 0; x < edge; ++x) std::memcpy(dst + x * 4, src + (x / size_t(scale)) * 4, 4);
    }
    return out;
}

std::vector<uint8_t> TexturePack::decode(const std::vector<uint8_t>& ps1, int* images) const {
    if (images) *images = 0;
    const uint64_t name = pageName(ps1);
    if (const auto img = cache_.get(name)) {
        if (images) *images = 1;
        return *img;
    }
    const auto it = images_.find(name);
    if (it == images_.end()) return enlarge(ps1, scale_);
    const std::vector<uint8_t> bytes = readFile(it->second);
    const int edge = 256 * scale_;
    int w = 0, h = 0, comp = 0;
    stbi_uc* px = bytes.empty() ? nullptr : stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &comp, 4);
    if (!px || w != edge || h != edge) {
        if (px) stbi_image_free(px);
        return enlarge(ps1, scale_);
    }
    auto img = std::make_shared<std::vector<uint8_t>>(px, px + size_t(edge) * size_t(edge) * 4);
    stbi_image_free(px);
    cache_.put(name, img, img->size());
    if (images) *images = 1;
    return *img;
}

namespace {
std::shared_ptr<const TexturePack> g_pack;
std::filesystem::path g_dump;
std::mutex g_dumpLock;
std::unordered_set<uint64_t> g_dumped;

void dump(const std::vector<uint8_t>& ps1) {
    const uint64_t name = pageName(ps1);
    {
        std::lock_guard lock(g_dumpLock);
        if (!g_dumped.insert(name).second) return;
    }
    const std::filesystem::path p = g_dump / (hex(name) + ".png");
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) stbi_write_png(p.string().c_str(), 256, 256, 4, ps1.data(), 256 * 4);
}
} // namespace

void bindTexturePack(std::shared_ptr<const TexturePack> pack) { g_pack = std::move(pack); }
const TexturePack* texturePack() { return g_pack.get(); }

void bindPageDump(std::filesystem::path dir) {
    std::error_code ec;
    if (!dir.empty()) std::filesystem::create_directories(dir, ec);
    g_dump = std::move(dir);
}
const std::filesystem::path& pageDumpDir() { return g_dump; }

int sliceEdge() { return g_pack ? 256 * g_pack->scale() : 256; }

std::vector<uint8_t> pageSliceOf(const std::vector<uint8_t>& ps1, const TexturePack* pack, int* images) {
    int drawn = 0;
    if (!pack) {
        if (!g_dump.empty()) dump(ps1);
        if (images) *images = 0;
        return ps1;
    }
    std::vector<uint8_t> out = pack->decode(ps1, &drawn);
    if (!drawn && !g_dump.empty()) dump(ps1);
    if (images) *images = drawn;
    return out;
}

std::vector<uint8_t> pageSlice(const Vram& vram, uint16_t tpage, uint16_t clut, bool stp, int* images) {
    return pageSliceOf(vram.decode(tpage, clut, stp), g_pack.get(), images);
}

PageLoader::PageLoader(int threads) {
    for (int i = 0; i < threads; ++i) threads_.emplace_back([this] { run(); });
}

PageLoader::~PageLoader() {
    {
        std::lock_guard lock(lock_);
        stop_ = true;
    }
    wake_.notify_all();
    for (std::thread& t : threads_) t.join();
}

void PageLoader::request(uint64_t name, std::vector<uint8_t> ps1) {
    {
        std::lock_guard lock(lock_);
        if (!pending_.insert(name).second) return;
        queue_.push_back({name, std::move(ps1), g_pack});
    }
    wake_.notify_one();
}

std::vector<std::pair<uint64_t, std::vector<uint8_t>>> PageLoader::takeReady() {
    std::lock_guard lock(lock_);
    for (const auto& r : ready_) pending_.erase(r.first);
    return std::exchange(ready_, {});
}

void PageLoader::waitReady() {
    std::unique_lock lock(lock_);
    made_.wait(lock, [this] { return !ready_.empty() || pending_.empty(); });
}

void PageLoader::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(lock_);
            wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        std::vector<uint8_t> img = pageSliceOf(job.ps1, job.pack.get());
        appendPageMips(img, job.pack ? 256 * job.pack->scale() : 256);
        {
            std::lock_guard lock(lock_);
            ready_.emplace_back(job.name, std::move(img));
        }
        made_.notify_all();
    }
}

PageLoader& pageLoader() {
    static PageLoader loader(std::max(2, int(std::thread::hardware_concurrency()) / 2));
    return loader;
}

uint32_t pageMipLevels(int edge) {
    uint32_t levels = 1;
    while ((edge >> levels) >= 256) ++levels;
    return levels;
}

void appendPageMips(std::vector<uint8_t>& rgba, int edge) {
    const uint32_t levels = pageMipLevels(edge);
    size_t total = 0;
    for (uint32_t level = 0; level < levels; ++level) total += size_t(edge >> level) * size_t(edge >> level) * 4;
    size_t from = 0, to = rgba.size();
    rgba.resize(total);
    for (uint32_t level = 1; level < levels; ++level) {
        const size_t size = size_t(edge >> level), row = size * 2 * 4;
        for (size_t y = 0; y < size; ++y) {
            const uint8_t* a = rgba.data() + from + y * 2 * row;
            const uint8_t* b = a + row;
            uint8_t* out = rgba.data() + to + y * size * 4;
            for (size_t x = 0; x < size * 4; x += 4, a += 8, b += 8)
                for (size_t c = 0; c < 4; ++c) out[x + c] = uint8_t((a[c] + a[c + 4] + b[c] + b[c + 4] + 2) >> 2);
        }
        from = to;
        to += size * size * 4;
    }
}

std::vector<uint8_t> pictureSlice(const std::vector<uint8_t>& rgba) {
    return g_pack ? TexturePack::enlarge(rgba, g_pack->scale()) : rgba;
}

} // namespace od
