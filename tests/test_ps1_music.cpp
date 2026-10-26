// Disc-native sequenced music: timing, instrument samples, looping, and malformed input.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "check.hpp"
#include "engine/dat.hpp"
#include "engine/disc_image.hpp"
#include "engine/game_file.hpp"
#include "engine/ps1_music.hpp"

namespace {
void word(std::vector<uint8_t>& b, size_t p, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[p + i] = uint8_t(v >> (i * 8));
}
std::vector<uint8_t> bank() {
    std::vector<uint8_t> b(4096);
    word(b, 0, 0x0e); word(b, 4, 8 + 16 * 20 + 64); word(b, 8, 2);
    word(b, 0x14, 8 + 16 * 20); word(b, 0x18, 64); word(b, 0x1c, 0x407f0001);
    b[0x30] = 1; b[0x31] = 127; b[0x33] = 64;
    const size_t t = 0x38;
    b[t + 4] = 100; b[t + 6] = 127; b[t + 7] = 64; b[t + 8] = 60;
    b[t + 11] = 127; b[t + 14] = 15; b[t + 16] = 0xc0; b[t + 17] = 0x5f;
    for (size_t p = 0x810; p < 0x850; p += 16) {
        b[p] = 3;
        std::fill(b.begin() + p + 2, b.begin() + p + 16, uint8_t(0x11));
    }
    b[0x811] = 4; b[0x841] = 3; // repeat the actual ADPCM sample
    return b;
}
std::vector<uint8_t> sequence(bool loop) {
    std::vector<uint8_t> s(12);
    word(s, 4, 500000); s[8] = 48; s[10] = 4; s[11] = 2;
    const std::vector<uint8_t> events = loop
        ? std::vector<uint8_t>{0xc0, 0, 0, 0xb0, 99, 20, 0, 6, 127, 0, 0x90, 60, 127,
                               48, 60, 0, 0, 0xb0, 99, 30, 0, 0xff, 47}
        : std::vector<uint8_t>{0xc0, 0, 0, 0x90, 60, 127, 48, 60, 0, 0, 0xff, 47};
    s.insert(s.end(), events.begin(), events.end()); word(s, 0, uint32_t(s.size()));
    return s;
}
std::vector<uint8_t> block(const std::vector<uint8_t>& s) {
    std::vector<uint8_t> b(2048);
    word(b, 0, 15); word(b, 4, uint32_t(s.size())); word(b, 8, 1); word(b, 0x18, 1);
    std::copy(s.begin(), s.end(), b.begin() + 48); return b;
}
template<class F> void rejects(F f) {
    bool caught = false;
    try { f(); } catch (const std::runtime_error&) { caught = true; }
    CHECK(caught);
}
void synthetic() {
    auto packets = od::ps1MusicTracks(block(sequence(false)), bank());
    CHECK(packets.size() == 1);
    CHECK(od::isPs1Music(packets[0]));
    od::Ps1MusicPlayer p(packets[0], false);
    CHECK(p.info().endFrame == 22050);
    CHECK(!p.info().hasLoop);
    float l = 0, r = 0, peak = 0;
    uint64_t count = 0;
    while (p.next(l, r) && count < 44100) { peak = std::max(peak, std::abs(l)); ++count; }
    CHECK(peak > 0.01f);
    CHECK(count >= 22050 && count < 23000); // real key-off release, then the stream ends
    p.rewind();
    for (int i = 0; i < 100; ++i) CHECK(p.next(l, r));
    CHECK(std::abs(l - r) < 0.00001f);
    const float first = l;
    p.rewind();
    for (int i = 0; i < 100; ++i) CHECK(p.next(l, r));
    CHECK(first == l);

    packets = od::ps1MusicTracks(block(sequence(true)), bank());
    od::Ps1MusicPlayer looping(packets[0], true);
    CHECK(looping.info().hasLoop && looping.info().loopStartFrame == 0);
    CHECK(looping.info().loopEndFrame == 22050);
    float latePeak = 0;
    for (int i = 0; i < 50000; ++i) {
        CHECK(looping.next(l, r));
        if (i > 45000) latePeak = std::max(latePeak, std::abs(l));
    }
    CHECK(latePeak > 0.01f);
    od::Ps1MusicPlayer once(packets[0], false);
    count = 0; while (once.next(l, r) && count < 50000) ++count;
    CHECK(count >= 22050 && count < 23000);

    auto wetBank = bank(); wetBank[0x38 + 5] = 4; // this tone's authored SPU reverb send
    od::Ps1MusicPlayer wet(od::ps1MusicTracks(block(sequence(false)), wetBank)[0], false);
    float tail = 0;
    count = 0;
    while (wet.next(l, r) && count < 441000) {
        if (count > 26500) tail = std::max({tail, std::abs(l), std::abs(r)});
        ++count;
    }
    CHECK(tail > 0.00001f);
    CHECK(count > 26500 && count < 441000);

    auto finite = sequence(true); finite[20] = 2;
    od::Ps1MusicPlayer repeated(od::ps1MusicTracks(block(finite), bank())[0], true);
    count = 0; while (repeated.next(l, r) && count < 100000) ++count;
    CHECK(count >= 66150 && count < 67000); // two authored repeats after the first pass

    // A tempo meta event changes elapsed time, rather than replaying the header tempo throughout.
    auto tempo = sequence(false);
    tempo.resize(12);
    const std::vector<uint8_t> timed{0xc0, 0, 0, 0x90, 60, 127, 48, 0xff, 0x51, 0x03, 0xd0, 0x90,
                                    48, 0x90, 60, 0, 0, 0xff, 47};
    tempo.insert(tempo.end(), timed.begin(), timed.end()); word(tempo, 0, uint32_t(tempo.size()));
    od::Ps1MusicPlayer changing(od::ps1MusicTracks(block(tempo), bank())[0], false);
    CHECK(changing.info().endFrame == 33075); // 48 ticks at 500000us, 48 at 250000us

    auto broken = packets[0]; broken.resize(20);
    rejects([&] { od::Ps1MusicPlayer bad(broken, false); });
    auto b = bank(); word(b, 0x38, 0xffffffffu);
    rejects([&] { od::Ps1MusicPlayer bad(od::ps1MusicTracks(block(sequence(false)), b)[0], false); });
    auto s = sequence(false); s[12] = 0xf1;
    rejects([&] { od::Ps1MusicPlayer bad(od::ps1MusicTracks(block(s), bank())[0], false); });
}
void disc(const char* cue) {
    od::DiscImage d(cue);
    size_t files = 0, tracks = 0, loops = 0;
    for (const auto& f : d.files()) {
        if (!f.path.ends_with(".BIN") || f.path == "COMMON/STAFF.BIN") continue;
        const auto gf = od::ps1GameFile(f.path, d.file(f.path));
        bool any = false;
        for (const auto& part : gf.parts) {
            if (part.kind != od::FilePart::Kind::Ps1Music) continue;
            any = true; ++tracks;
            od::Ps1MusicPlayer p(part.bytes, part.arg != 0);
            CHECK(p.info().endFrame > 44100);
            CHECK(p.info().events > 0 && p.info().samples > 0);
            if (p.info().hasLoop) {
                ++loops;
                CHECK(p.info().loopEndFrame > p.info().loopStartFrame);
            }
            float l, r, peak = 0;
            // Every disc track must produce actual music in its first five seconds.
            for (int i = 0; i < 44100 * 5 && p.next(l, r); ++i) {
                CHECK(std::isfinite(l) && std::isfinite(r));
                peak = std::max({peak, std::abs(l), std::abs(r)});
            }
            CHECK(peak > 0.0001f);
            if (f.path == "COMMON/TITLE.BIN" || f.path == "DAT/ST08T.BIN" || f.path == "DAT/ST0DT.BIN") {
                p.rewind();
                float afterLoop = 0;
                const uint64_t end = p.info().loopEndFrame;
                for (uint64_t i = 0; i < end + 44100 * 2; ++i) {
                    CHECK(p.next(l, r));
                    if (i >= end) afterLoop = std::max({afterLoop, std::abs(l), std::abs(r)});
                }
                CHECK(afterLoop > 0.001f);
            }
            std::printf("%s track %zu: %llu frames, loop %llu..%llu, peak %.4f\n", f.path.c_str(), tracks,
                        static_cast<unsigned long long>(p.info().endFrame),
                        static_cast<unsigned long long>(p.info().loopStartFrame),
                        static_cast<unsigned long long>(p.info().loopEndFrame), peak);
        }
        files += any;
    }
    CHECK(files == 130 && tracks == 172 && loops == 168);
    std::printf("disc music: %zu files, %zu tracks, %zu loops\n", files, tracks, loops);
}
} // namespace
int main(int argc, char** argv) {
    synthetic();
    if (argc > 1) disc(argv[1]);
    return testResult("ps1_music");
}
