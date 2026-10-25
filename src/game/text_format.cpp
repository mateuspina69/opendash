// The number and time formatters (see text_format.hpp), transcribed from dash2.exe.
#include "game/text_format.hpp"

#include <array>
#include <span>
#include "game/language_pack.hpp"
#include "game/text_script.hpp"

namespace od::port {

namespace {

// the glyphs of the two fonts (PC: ASCII; PS1, SLUS 0x80048ff4 / 0x8004d5b4: digit d = glyph d)
constexpr uint8_t kPcZero = '0', kPcSpace = ' ', kPcMinus = '-', kPcColon = ':', kPcMinute = '\'', kPcSecond = '"';
constexpr uint8_t kPs1Zero = 0x00, kPs1Space = 0x50, kPs1Minus = 0x4f, kPs1Colon = 0x13, kPs1Minute = 0x0c,
                  kPs1Second = 0x5a;
constexpr int32_t kNumberDigits = 7;
constexpr int32_t kTopDigit = 1000000;
constexpr int32_t kFramesPerSecond = 60;
constexpr int32_t kClockLimit = 21600000; // 100 hours of frames (the clock shows 99:59:59 at most)
constexpr int32_t kLapUnitsShift = 4;     // a lap time's frames are v >> 4, its hundredths' units v & 0xf
constexpr int kTimeLength = 8;

// Both public paths use the same formatter over native bytes. Only the legacy caller's
// final glyphs are written to its text view.
struct NativeText {
    std::span<uint8_t> bytes;
    uint8_t& byte(uint32_t i) { return bytes[i]; }
};

template <class F> std::vector<uint8_t> formattedText(F fmt) {
    std::array<uint8_t, 8> buffer{};
    NativeText out{buffer};
    const int n = fmt(out);
    std::vector<uint8_t> text(buffer.begin(), buffer.begin() + n);
    text.push_back(kTextCodeOp);
    text.push_back(kTextOpReturn);
    return text;
}

} // namespace

template <class Output> int formatNumber(int32_t v, Output& out, int pad) {
    const bool ps1 = ps1Text();
    const uint8_t zero = ps1 ? kPs1Zero : kPcZero, space = ps1 ? kPs1Space : kPcSpace;
    if (v < 0) {
        out.byte(0) = ps1 ? kPs1Minus : kPcMinus;
        return 1;
    }
    int8_t n = 0;
    int32_t div = kTopDigit;
    bool started = false;
    for (int k = kNumberDigits - 1; k >= 0; --k) {
        const int8_t d = int8_t(v / div);
        v %= div;
        div /= 10;
        if (started || d != 0 || k == 0) {
            started = true;
            out.byte(uint32_t(n)) = uint8_t(d + zero);
            ++n;
        } else if (pad == 0) {
            out.byte(uint32_t(n)) = space;
            ++n;
        }
    }
    return n;
}

template <class Output> int formatTime(int32_t v, Output& out, TimeFormat format) {
    if (v < 0) format = TimeFormat::Dashes;
    const bool ps1 = ps1Text();
    auto put = [&](uint32_t i, int c) { out.byte(i) = uint8_t(c); };
    auto dig = [&](int d) { return ps1 ? d : d + kPcZero; };
    const int colon = ps1 ? kPs1Colon : kPcColon;
    constexpr int32_t kHour = 3600 * kFramesPerSecond, kMinute = 60 * kFramesPerSecond;
    if (format == TimeFormat::Clock) {
        if (v >= kClockLimit) v = kClockLimit - 1;
        const int8_t h = int8_t(v / kHour), m = int8_t((v % kHour) / kMinute), sec = int8_t((v % kMinute) / 60);
        put(0, dig(h / 10));
        put(1, dig(h % 10));
        put(2, colon);
        put(3, dig(m / 10));
        put(4, dig(m % 10));
        put(5, colon);
        put(6, dig(sec / 10));
        put(7, dig(sec % 10));
    } else if (format == TimeFormat::Lap) {
        const int32_t x = v >> kLapUnitsShift;
        const int8_t m = int8_t((x % kHour) / kMinute), sec = int8_t((x % kMinute) / 60);
        const int c2 = (x % 60) * 100 / 60;
        const int8_t cs = int8_t(uint8_t((c2 / 10) * 10) + uint8_t(v & 0xf));
        put(0, m / 10 == 0 ? (ps1 ? kPs1Space : kPcSpace) : dig(m / 10));
        put(1, dig(m % 10));
        put(2, ps1 ? kPs1Minute : kPcMinute);
        put(3, dig(sec / 10));
        put(4, dig(sec % 10));
        put(5, ps1 ? kPs1Second : kPcSecond);
        put(6, dig(cs / 10));
        put(7, dig(cs % 10));
    } else {
        for (uint32_t i = 0; i < uint32_t(kTimeLength); ++i) put(i, (i == 2 || i == 5) ? colon : (ps1 ? kPs1Minus : kPcMinus));
    }
    return kTimeLength;
}

int messageNumberWrite(int32_t v, TextBytes& out, int pad) {
    return formatNumber(v, out, pad);
}

int messageTimeWrite(int32_t v, TextBytes& out, TimeFormat format) {
    return formatTime(v, out, format);
}

std::vector<uint8_t> messageNumberText(int32_t v, int pad, bool ps1) {
    const Ps1TextScope scope(ps1);
    return formattedText([&](NativeText& out) { return formatNumber(v, out, pad); });
}

std::vector<uint8_t> messageTimeText(int32_t v, TimeFormat format, bool ps1) {
    const Ps1TextScope scope(ps1);
    return formattedText([&](NativeText& out) { return formatTime(v, out, format); });
}

} // namespace od::port
