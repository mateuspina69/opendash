#include "disc_image.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>

namespace od {

namespace {

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error("disc: " + what); }

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint8_t bcd(uint32_t v) { return uint8_t((v / 10) << 4 | (v % 10)); }

// the cue sheet's words: a quoted file name is one word
std::vector<std::string> words(const std::string& line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        if (std::isspace(uint8_t(line[i]))) { ++i; continue; }
        if (line[i] == '"') {
            const size_t end = line.find('"', i + 1);
            if (end == std::string::npos) fail("unterminated quote in the cue sheet: " + line);
            out.push_back(line.substr(i + 1, end - i - 1));
            i = end + 1;
        } else {
            size_t end = i;
            while (end < line.size() && !std::isspace(uint8_t(line[end]))) ++end;
            out.push_back(line.substr(i, end - i));
            i = end;
        }
    }
    return out;
}

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

// mm:ss:ff (75 frames a second) -> sectors
uint32_t msfFrames(const std::string& msf) {
    uint32_t part[3] = {};
    size_t p = 0;
    for (int i = 0; i < 3; ++i) {
        const size_t start = p;
        while (p < msf.size() && std::isdigit(uint8_t(msf[p]))) part[i] = part[i] * 10 + uint32_t(msf[p++] - '0');
        if (p == start || p - start > 3 || (i < 2 ? p >= msf.size() || msf[p++] != ':' : p != msf.size()))
            fail("bad MSF time in the cue sheet: " + msf);
    }
    if (part[1] >= 60 || part[2] >= 75) fail("bad MSF time in the cue sheet: " + msf);
    return (part[0] * 60 + part[1]) * 75 + part[2];
}

// "dat\\st4bt.bin;1" -> "DAT/ST4BT.BIN"
std::string normalPath(std::string_view path) {
    std::string s = upper(std::string(path));
    std::replace(s.begin(), s.end(), '\\', '/');
    while (!s.empty() && s.front() == '/') s.erase(s.begin());
    if (const size_t v = s.find(';'); v != std::string::npos) s.resize(v);
    return s;
}

} // namespace

DiscImage::DiscImage(const std::filesystem::path& cue) {
    parseCue(cue);
    readFileSystem();
}

DiscImage::~DiscImage() = default;

void DiscImage::parseCue(const std::filesystem::path& cue) {
    std::ifstream in(cue);
    if (!in) fail("cannot open " + cue.string());

    struct Pending {
        DiscTrack track;
        uint32_t pregap = 0;
        std::optional<uint32_t> regionStart; // INDEX 00, or the start of the PREGAP
        bool hasIndex1 = false;
    };
    std::vector<Pending> parsed;
    uint32_t runLba = 0;               // the disc LBA after everything placed so far
    uint32_t pieceLba = 0, pieceFrame = 0; // the open run of the current file: its LBA and its first file sector
    auto closeFile = [&] {
        if (trackFiles_.empty()) return;
        const uint32_t sectors = trackFiles_.back()->sectors;
        if (sectors > pieceFrame)
            extents_.push_back({pieceLba, sectors - pieceFrame, trackFiles_.size() - 1, pieceFrame});
        runLba = pieceLba + (sectors - pieceFrame);
    };

    std::string line;
    while (std::getline(in, line)) {
        const auto w = words(line);
        if (w.empty()) continue;
        const std::string cmd = upper(w[0]);
        if (cmd == "FILE") {
            if (w.size() < 3 || upper(w[2]) != "BINARY") fail("only BINARY track files are read: " + line);
            closeFile();
            auto tf = std::make_unique<TrackFile>();
            tf->path = cue.parent_path() / std::filesystem::path(std::u8string(w[1].begin(), w[1].end()));
            tf->stream.open(tf->path, std::ios::binary);
            if (!tf->stream) fail("cannot open the track file " + tf->path.string());
            const auto bytes = std::filesystem::file_size(tf->path);
            if (bytes % kRawSectorBytes != 0) fail(tf->path.string() + " is not made of 2352-byte sectors");
            tf->sectors = uint32_t(bytes / kRawSectorBytes);
            trackFiles_.push_back(std::move(tf));
            pieceLba = runLba;
            pieceFrame = 0;
        } else if (cmd == "TRACK") {
            if (trackFiles_.empty() || w.size() < 3) fail("TRACK before FILE: " + line);
            Pending p;
            p.track.number = std::stoi(w[1]);
            const std::string mode = upper(w[2]);
            if (mode == "MODE1/2352") p.track.kind = DiscTrack::Kind::Mode1;
            else if (mode == "MODE2/2352") p.track.kind = DiscTrack::Kind::Mode2;
            else if (mode == "AUDIO") p.track.kind = DiscTrack::Kind::Audio;
            else fail("only raw 2352-byte tracks are read (" + w[2] + ")");
            parsed.push_back(p);
        } else if (cmd == "PREGAP") {
            if (parsed.empty() || w.size() < 2) fail("PREGAP outside a track: " + line);
            parsed.back().pregap = msfFrames(w[1]);
        } else if (cmd == "POSTGAP") {
            fail("POSTGAP is not read: " + line);
        } else if (cmd == "INDEX") {
            if (parsed.empty() || w.size() < 3) fail("INDEX outside a track: " + line);
            Pending& p = parsed.back();
            const int index = std::stoi(w[1]);
            const uint32_t frame = msfFrames(w[2]);
            if (frame < pieceFrame || frame > trackFiles_.back()->sectors) fail("INDEX past its file: " + line);
            if (index == 0) {
                if (p.pregap != 0) fail("a track with both PREGAP and INDEX 00: " + line);
                p.regionStart = pieceLba + (frame - pieceFrame);
            } else if (index == 1) {
                if (p.pregap != 0) { // a gap the file does not store: it splits the file's sectors in two runs
                    if (frame > pieceFrame) extents_.push_back({pieceLba, frame - pieceFrame, trackFiles_.size() - 1, pieceFrame});
                    const uint32_t gapLba = pieceLba + (frame - pieceFrame);
                    p.regionStart = gapLba;
                    pieceLba = gapLba + p.pregap;
                    pieceFrame = frame;
                }
                p.track.lba = pieceLba + (frame - pieceFrame);
                if (!p.regionStart) p.regionStart = p.track.lba;
                p.hasIndex1 = true;
            }
        }
        // REM, CATALOG, PERFORMER, TITLE, FLAGS, ISRC, SONGWRITER: nothing the reader needs
    }
    closeFile();

    if (parsed.empty()) fail(cue.string() + " lists no track");
    for (size_t i = 0; i < parsed.size(); ++i) {
        if (!parsed[i].hasIndex1) fail("track " + std::to_string(parsed[i].track.number) + " has no INDEX 01");
        DiscTrack t = parsed[i].track;
        const uint32_t end = i + 1 < parsed.size() ? *parsed[i + 1].regionStart : runLba;
        if (end < t.lba) fail("track " + std::to_string(t.number) + " ends before it starts");
        t.sectors = end - t.lba;
        tracks_.push_back(t);
    }
    if (tracks_.front().kind == DiscTrack::Kind::Audio || tracks_.front().lba != 0)
        fail("the first track is not a data track at LBA 0");
}

uint32_t DiscImage::sectorCount() const { return tracks_.back().lba + tracks_.back().sectors; }

RawSector DiscImage::rawSector(uint32_t lba) const {
    const auto it = std::find_if(extents_.begin(), extents_.end(),
                                 [&](const Extent& e) { return lba >= e.lba && lba - e.lba < e.sectors; });
    if (it == extents_.end()) fail("sector " + std::to_string(lba) + " is not stored in the image");
    RawSector out;
    const TrackFile& tf = *trackFiles_[it->file];
    std::lock_guard lock(readLock_);
    tf.stream.clear();
    tf.stream.seekg(std::streamoff(it->fileSector + (lba - it->lba)) * kRawSectorBytes);
    if (!tf.stream.read(reinterpret_cast<char*>(out.data()), out.size()))
        fail("cannot read sector " + std::to_string(lba) + " of " + tf.path.string());
    return out;
}

std::vector<uint8_t> DiscImage::dataSectors(uint32_t lba, uint32_t count) const {
    static constexpr uint8_t kSync[12] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
    std::vector<uint8_t> out;
    out.reserve(size_t(count) * kForm1Bytes);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t at = lba + i;
        const RawSector s = rawSector(at);
        const uint32_t msf = at + 150; // the 2-second lead-in before LBA 0
        if (!std::equal(std::begin(kSync), std::end(kSync), s.begin()) || s[12] != bcd(msf / 4500) ||
            s[13] != bcd(msf / 75 % 60) || s[14] != bcd(msf % 75))
            fail("sector " + std::to_string(at) + " is not the data sector its address names");
        size_t data = 0;
        if (s[15] == 1) data = 16;
        else if (s[15] == 2) {
            if (s[18] & 0x20) fail("sector " + std::to_string(at) + " is mode 2 form 2 (stream data)");
            data = 24;
        } else fail("sector " + std::to_string(at) + " has mode " + std::to_string(s[15]));
        out.insert(out.end(), s.begin() + data, s.begin() + data + kForm1Bytes);
    }
    return out;
}

void DiscImage::readFileSystem() {
    const auto pvd = dataSectors(16, 1);
    if (pvd[0] != 1 || std::string_view(reinterpret_cast<const char*>(pvd.data() + 1), 5) != "CD001")
        fail("no ISO9660 primary volume descriptor at sector 16");
    if ((pvd[128] | pvd[129] << 8) != int(kForm1Bytes)) fail("the volume's block size is not 2048");
    const uint8_t* root = pvd.data() + 156;
    readDirectory("", le32(root + 2), le32(root + 10), 0);
}

void DiscImage::readDirectory(const std::string& prefix, uint32_t lba, uint32_t size, int depth) {
    if (depth > 8) fail("directories nested past ISO9660's 8 levels under " + prefix);
    const uint32_t sectors = (size + kForm1Bytes - 1) / kForm1Bytes;
    const auto dir = dataSectors(lba, sectors);
    for (uint32_t sector = 0; sector < sectors; ++sector) {
        const uint8_t* base = dir.data() + size_t(sector) * kForm1Bytes;
        for (uint32_t off = 0; off < kForm1Bytes;) {
            const uint8_t* r = base + off;
            const uint8_t len = r[0];
            if (len == 0) break; // records never cross a sector: the rest of this one is padding
            if (len < 34 || off + len > kForm1Bytes) fail("bad directory record under " + prefix);
            const uint8_t nameLen = r[32];
            if (33u + nameLen > len) fail("bad directory record name under " + prefix);
            off += len;
            if (nameLen == 1 && (r[33] == 0 || r[33] == 1)) continue; // "." and ".."
            std::string name = normalPath(std::string_view(reinterpret_cast<const char*>(r + 33), nameLen));
            if (!name.empty() && name.back() == '.') name.pop_back(); // "README." -> "README"
            const std::string path = prefix + name;
            const uint32_t extent = le32(r + 2), bytes = le32(r + 10);
            uint16_t xa = 0;
            const uint32_t su = 33u + nameLen + (nameLen % 2 == 0 ? 1u : 0u);
            if (su + 14 <= len && r[su + 6] == 'X' && r[su + 7] == 'A') xa = be16(r + su + 4);
            if (r[25] & 2) readDirectory(path + "/", extent, bytes, depth + 1);
            else files_.push_back({path, extent, bytes, xa});
        }
    }
}

const DiscFile* DiscImage::find(std::string_view path) const {
    const std::string want = normalPath(path);
    const auto it = std::find_if(files_.begin(), files_.end(), [&](const DiscFile& f) { return f.path == want; });
    return it == files_.end() ? nullptr : &*it;
}

std::vector<uint8_t> DiscImage::file(std::string_view path) const {
    const DiscFile* f = find(path);
    if (!f) fail("no file " + std::string(path) + " on the disc");
    if (f->streamed()) fail(f->path + " is a stream of form 2 sectors, not a form 1 file");
    auto bytes = dataSectors(f->lba, f->sectors());
    bytes.resize(f->size);
    return bytes;
}

} // namespace od
