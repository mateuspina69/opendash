// OpenDash: the mouse on the message windows' choices (see choice_pointer.hpp).
#include "game/choice_pointer.hpp"

#include <algorithm>

#include "game/language_pack.hpp"
#include "game/text_script.hpp"

namespace od::port {

namespace {
// the choice code's bytes (game/message_choice): fb nn n {column, line} x n
constexpr uint32_t kChoiceCount = 2, kChoiceTable = 3;
constexpr uint8_t kChoiceCountMask = 0x7f;
constexpr int kPcColumn = 12, kPs1Column = 2;
constexpr int kSmallHandX = 8;     // the small layout's hand: x * 12 - 8
constexpr float kHandMargin = 2.0f; // an option starts this far left of the hand's place
constexpr float kOptionHeight = 16.0f;
} // namespace

ChoicePointer& choicePointer() {
    static ChoicePointer p;
    return p;
}

const MessageWindow* choiceWindow() {
    for (size_t i = 0; i < kWindowCount; ++i) {
        const MessageWindow& w = messageWindow(i);
        const uint32_t f = w.flags;
        if ((f & kWindowFrameShown) && (f & kWindowChoice) && !(f & kWindowMoreArrow)) return &w;
    }
    return nullptr;
}

int choiceOptionAt(const MessageWindow& w, float px, float py) {
    const TextBytes op = w.readText();
    if (!op.mapped(kChoiceTable)) return -1;
    const int n = op.byte(kChoiceCount) & kChoiceCountMask;
    if (n == 0 || !TextBytes(op.at + kChoiceTable).mapped(uint32_t(n) * 2)) return -1;
    const bool small = (w.flags & kWindowSmallLayout) != 0; // the PC's own layout (x * 12 - 8, the line + 7)
    const int unit = ps1Window(w) ? kPs1Column : kPcColumn;
    const float x0 = w.x, y0 = w.y;
    const float right = float(w.frameTarget.x + w.frameTarget.w);
    auto kx = [&](int k) {
        const int column = op.byte(kChoiceTable + uint32_t(k) * 2);
        return float(small ? column * kPcColumn - kSmallHandX : column * unit);
    };
    auto lineOf = [&](int k) { return int(op.byte(kChoiceTable + 1 + uint32_t(k) * 2)); };
    for (int k = 0; k < n; ++k) {
        const int line = lineOf(k);
        const float top = y0 + float(messageLineY(w, line)), left = x0 + kx(k) - kHandMargin;
        float end = right;
        for (int j = 0; j < n; ++j)
            if (j != k && lineOf(j) == line && kx(j) > kx(k)) end = std::min(end, x0 + kx(j) - kHandMargin);
        if (px >= left && px < end && py >= top && py < top + kOptionHeight) return k;
    }
    return -1;
}

} // namespace od::port
