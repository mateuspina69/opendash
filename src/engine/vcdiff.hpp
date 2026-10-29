// A file as a VCDIFF patch (RFC 3284: xdelta3, Delta Patcher) makes it from its source, read in place: the target's
// bytes are made on demand, window by window (each window builds its slice of the target from the source's segment,
// or from the target's earlier bytes, and its own data), so a fan patch over a 600 MB disc track is never written out.
// Read: the default code table, no secondary compression (the header's VCD_DECOMPRESS / VCD_CODETABLE throw), the
// application header skipped, xdelta3's VCD_ADLER32 window checksum verified.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <list>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace od {

// Throws std::runtime_error ("vcdiff: ...") on a patch it cannot read, a window that does not build its declared size
// or checksum, or a read past the target.
class VcdiffTarget {
public:
    VcdiffTarget(const std::filesystem::path& source, const std::filesystem::path& patch);
    VcdiffTarget(const VcdiffTarget&) = delete;
    VcdiffTarget& operator=(const VcdiffTarget&) = delete;

    uint64_t size() const { return size_; }
    // out.size() bytes of the target from `offset`. Thread safe.
    void read(uint64_t offset, std::span<uint8_t> out) const;

private:
    struct Window {
        uint8_t indicator = 0;
        uint64_t segmentLength = 0, segmentPosition = 0; // in the source (VCD_SOURCE) or the target (VCD_TARGET)
        uint64_t target = 0, length = 0;                 // the slice of the target it builds
        uint64_t sections = 0;                           // the patch offset of its data, then instructions, addresses
        uint64_t dataLength = 0, instructionLength = 0, addressLength = 0;
        uint32_t adler = 0;
    };
    const std::vector<uint8_t>& built(size_t window) const;
    std::vector<uint8_t> build(const Window& w) const;
    void readLocked(uint64_t offset, std::span<uint8_t> out) const;

    mutable std::ifstream source_, patch_;
    std::filesystem::path patchPath_;
    std::vector<Window> windows_;
    uint64_t size_ = 0;
    mutable std::recursive_mutex lock_;
    mutable std::list<std::pair<size_t, std::vector<uint8_t>>> cache_; // the windows built last, the latest first
};

} // namespace od
