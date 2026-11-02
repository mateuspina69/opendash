// OpenDash's texture pack: larger images of the PS1's texture pages, made offline from the pages the game dumps
// (tools/upscale_pages.py, docs/texture_pack.md). A page is found by what it shows: its image is
//   <XXH3-64 of the page as Vram::decode makes it, 16 lowercase hex digits>.png
// (any folder depth), (256 * scale)^2 RGBA, alpha 0 / Vram::kStpAlpha / 255 as the PS1 page's texels. A page with no
// image is the PS1 page enlarged, nearest.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/vram.hpp"

namespace od {

class TexturePack {
public:
    // Indexes the folder's page images; throws std::runtime_error when it holds none. scale: the first image's
    // (name order) width over 256; an image of another size is no image of its page.
    explicit TexturePack(const std::filesystem::path& dir);

    int scale() const { return scale_; }
    size_t size() const { return images_.size(); }

    // the page's name (pageName) and its image, or the PS1 page enlarged; *images: 1 when the image drew it
    std::vector<uint8_t> decode(const std::vector<uint8_t>& ps1, int* images = nullptr) const;

    // a 256 x 256 RGBA8 slice enlarged (nearest) by `scale`
    static std::vector<uint8_t> enlarge(const std::vector<uint8_t>& rgba, int scale);

private:
    // values by key, the least recently used dropped past `budget` bytes (thread safe)
    template <class V> class Lru {
    public:
        explicit Lru(size_t budget) : budget_(budget) {}
        std::shared_ptr<const V> get(uint64_t key) {
            std::lock_guard lock(lock_);
            const auto it = map_.find(key);
            if (it == map_.end()) return nullptr;
            order_.splice(order_.begin(), order_, it->second.second);
            return it->second.first;
        }
        void put(uint64_t key, std::shared_ptr<const V> v, size_t bytes) {
            std::lock_guard lock(lock_);
            if (map_.contains(key)) return;
            order_.push_front(key);
            map_[key] = {std::move(v), order_.begin()};
            sizes_[key] = bytes;
            bytes_ += bytes;
            while (bytes_ > budget_ && order_.size() > 1) {
                const uint64_t old = order_.back();
                order_.pop_back();
                bytes_ -= sizes_[old];
                sizes_.erase(old);
                map_.erase(old);
            }
        }

    private:
        std::mutex lock_;
        std::list<uint64_t> order_;
        std::unordered_map<uint64_t, std::pair<std::shared_ptr<const V>, std::list<uint64_t>::iterator>> map_;
        std::unordered_map<uint64_t, size_t> sizes_;
        size_t bytes_ = 0, budget_;
    };

    std::unordered_map<uint64_t, std::filesystem::path> images_; // by page name
    int scale_ = 1;
    // the images read: a room entered again, an animated page's frames met again, without reading them
    mutable Lru<std::vector<uint8_t>> cache_{size_t(512) << 20};
};

// A page's name: the XXH3-64 of its 256 x 256 RGBA8 decode
uint64_t pageName(const std::vector<uint8_t>& ps1);

// The bound pack (the app binds the one opendash.ini's texture_pack= names; none: the PS1 pages as they are)
void bindTexturePack(std::shared_ptr<const TexturePack> pack);
const TexturePack* texturePack();
// The folder the pages with no image are written to (opendash.ini's texture_dump=; empty: none), each once, as
// <name>.png at 256 x 256: the upscaler's input
void bindPageDump(std::filesystem::path dir);

// The edge of the renderer's texture slices: 256 times the bound pack's scale
int sliceEdge();
// A texture page as a slice of sliceEdge(): the bound pack's image of it (1 in *images), or Vram::decode enlarged
std::vector<uint8_t> pageSlice(const Vram& vram, uint16_t tpage, uint16_t clut, bool stp, int* images = nullptr);
// The same from the page as Vram::decode made it, through `pack` (none: the page as it is)
std::vector<uint8_t> pageSliceOf(const std::vector<uint8_t>& ps1, const TexturePack* pack, int* images = nullptr);

// Pages made off the frame's thread: an animated page's frame (its image read and decoded, about 30 ms at 4x) is made
// by worker threads, so a frame never waits on a file
class PageLoader {
public:
    explicit PageLoader(int threads);
    ~PageLoader();
    // queues the page (Vram::decode) under its name (pageName), made through the pack bound now; a name queued, being
    // made or made and not taken yet is not queued again
    void request(uint64_t name, std::vector<uint8_t> ps1);
    // the pages made since the last call: their names and pageSliceOf
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> takeReady();

private:
    struct Job {
        uint64_t name;
        std::vector<uint8_t> ps1;
        std::shared_ptr<const TexturePack> pack;
    };
    void run();

    std::mutex lock_;
    std::condition_variable wake_;
    std::deque<Job> queue_;
    std::unordered_set<uint64_t> pending_; // queued, being made or ready
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> ready_;
    bool stop_ = false;
    std::vector<std::thread> threads_;
};
// The app's loader (two threads, started at the first call)
PageLoader& pageLoader();
// A 256 x 256 RGBA8 picture that is no VRAM page (the effects' gradients) as a slice of sliceEdge()
std::vector<uint8_t> pictureSlice(const std::vector<uint8_t>& rgba);

} // namespace od
