// A story scene's owner's pad as each copy of the scene plays it (the world's, the other players' games): the owner's
// changes go stamped with the tick of the scene they were made on; a copy takes them on the same ticks of its own run of
// the scene, one change a tick (a press and its release never fold into one: a dialogue's page turns on every copy), a
// late one as soon as it comes
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace od::app {

class ScenePads {
public:
    // the scene starts (its first unheld tick is the next one), or this copy joins it this many ticks in
    void start(uint32_t at = 0) {
        changes_.clear();
        level_ = 0;
        tick_ = at;
    }
    uint32_t tick() const { return tick_; }
    void heard(uint32_t at, uint16_t pad) { changes_.emplace_back(at, pad); }
    // this tick's pad
    uint16_t step() {
        if (!changes_.empty() && changes_.front().first <= tick_) {
            level_ = changes_.front().second;
            changes_.pop_front();
        }
        ++tick_;
        return level_;
    }
    uint16_t level() const { return level_; }

    // the owner's side: what it sends (the pad, the scene's tick, the scene's script)
    struct Change {
        uint32_t at = 0;
        uint16_t pad = 0;
        uint8_t script = 0;
    };
    static std::vector<uint8_t> encode(uint16_t pad, uint32_t at, uint8_t script) {
        return {uint8_t(pad), uint8_t(pad >> 8), uint8_t(at), uint8_t(at >> 8), uint8_t(at >> 16), uint8_t(at >> 24), script};
    }
    static std::optional<Change> decode(const std::vector<uint8_t>& b) {
        if (b.size() != 7) return std::nullopt;
        return Change{uint32_t(b[2]) | uint32_t(b[3]) << 8 | uint32_t(b[4]) << 16 | uint32_t(b[5]) << 24, uint16_t(b[0] | b[1] << 8), b[6]};
    }

private:
    std::deque<std::pair<uint32_t, uint16_t>> changes_;
    uint16_t level_ = 0;
    uint32_t tick_ = 0;
};

} // namespace od::app
