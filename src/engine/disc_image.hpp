// The PS1 disc, read from the user's image (a .cue sheet and its raw 2352-byte .bin tracks): the tracks, any sector
// as the drive returns it, and the ISO9660 file system of the data track (with the CD-XA attributes that mark the
// interleaved audio / movie files). The PS1 data source (engine/data_source, not written yet) reads the game only
// through this.
//
// Sectors are addressed by LBA (logical block address: 0 = the data track's first sector, MSF 00:02:00); a raw
// sector is the full 2352 bytes (sync, header, subheader, user data, EDC / ECC). A mode 2 form 1 sector carries 2048
// bytes of data, a form 2 sector (XA audio, STR video) 2324.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "engine/vcdiff.hpp"

namespace od {

inline constexpr uint32_t kRawSectorBytes = 2352;
inline constexpr uint32_t kForm1Bytes = 2048;
inline constexpr uint32_t kForm2Bytes = 2324;

using RawSector = std::array<uint8_t, kRawSectorBytes>;

struct DiscTrack {
    enum class Kind { Mode1, Mode2, Audio };
    int number = 0;
    Kind kind = Kind::Mode2;
    uint32_t lba = 0;     // INDEX 01
    uint32_t sectors = 0; // INDEX 01 up to the next track's pregap (or the end of the disc)
};

// CD-XA attributes of a directory record (big endian in the record's system use area)
inline constexpr uint16_t kXaForm1 = 0x0800;
inline constexpr uint16_t kXaForm2 = 0x1000;
inline constexpr uint16_t kXaInterleaved = 0x2000;
inline constexpr uint16_t kXaCdda = 0x4000;
inline constexpr uint16_t kXaDirectory = 0x8000;

struct DiscFile {
    std::string path;        // "DAT/ST4BT.BIN": upper case, '/' separated, without the ";1" version
    uint32_t lba = 0;        // first sector
    uint32_t size = 0;       // bytes as the directory records them (form 1 bytes: 2048 per sector)
    uint16_t xa = 0;         // CD-XA attributes (0: no XA entry)
    uint32_t sectors() const { return (size + kForm1Bytes - 1) / kForm1Bytes; }
    // a file of form 2 / interleaved sectors (XA audio, STR video) or a CD-DA track (its size counts 2048 bytes a
    // sector): read it sector by sector (DiscImage::rawSector)
    bool streamed() const { return (xa & (kXaForm2 | kXaInterleaved | kXaCdda)) != 0; }
    bool cdda() const { return (xa & kXaCdda) != 0; }
};

// Throws std::runtime_error ("disc: ...") on a cue sheet it cannot read, a missing track file, a sector whose
// header does not match its address, or a file system that is not ISO9660.
class DiscImage {
public:
    explicit DiscImage(const std::filesystem::path& cue);
    // the disc with its first track file made by a VCDIFF patch of the cue's (engine/vcdiff: a fan translation over
    // track 1), read in place; vcdiff errors throw as they are
    DiscImage(const std::filesystem::path& cue, const std::filesystem::path& firstTrackPatch);
    ~DiscImage();
    DiscImage(const DiscImage&) = delete;
    DiscImage& operator=(const DiscImage&) = delete;

    const std::vector<DiscTrack>& tracks() const { return tracks_; }
    // the disc's sectors: the end of the last track
    uint32_t sectorCount() const;

    // the sector as stored (audio sectors: 588 stereo 16-bit samples)
    RawSector rawSector(uint32_t lba) const;
    // the user data of `count` data sectors from `lba` (mode 1 or mode 2 form 1; a form 2 sector throws)
    std::vector<uint8_t> dataSectors(uint32_t lba, uint32_t count) const;

    // every file of the data track's file system, in directory order (directories not listed)
    const std::vector<DiscFile>& files() const { return files_; }
    // the file at `path` ("DAT/ST4BT.BIN", any case, with or without ";1"); nullptr when the disc has none
    const DiscFile* find(std::string_view path) const;
    // the bytes of a form 1 file (a streamed file throws: its sectors are not form 1)
    std::vector<uint8_t> file(std::string_view path) const;

private:
    struct TrackFile {
        std::filesystem::path path;
        mutable std::ifstream stream;
        std::unique_ptr<VcdiffTarget> patched; // the file as the patch makes it (the stream unused)
        uint32_t sectors = 0;
    };
    // a run of disc sectors stored contiguously in one track file (a cue PREGAP splits a file into two runs)
    struct Extent {
        uint32_t lba = 0, sectors = 0;
        size_t file = 0;
        uint32_t fileSector = 0;
    };

    void parseCue(const std::filesystem::path& cue, const std::filesystem::path& firstTrackPatch);
    void readFileSystem();
    void readDirectory(const std::string& prefix, uint32_t lba, uint32_t size, int depth);

    std::vector<std::unique_ptr<TrackFile>> trackFiles_;
    std::vector<Extent> extents_;
    std::vector<DiscTrack> tracks_;
    std::vector<DiscFile> files_;
    mutable std::mutex readLock_; // the track files' streams: one reader at a time
};

} // namespace od
