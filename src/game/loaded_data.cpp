#include "game/loaded_data.hpp"

#include <vector>

namespace od::port {

namespace {
std::vector<BuiltData>& built() {
    static std::vector<BuiltData> b;
    return b;
}
} // namespace

void registerBuiltData(uint32_t base, std::span<uint8_t> bytes) {
    for (const BuiltData& b : built())
        if (b.base == base) return; // registered already (its owner's storage)
    built().push_back({base, bytes});
}

const BuiltData* builtDataAt(uint32_t pointer, uint32_t n) {
    for (const BuiltData& b : built())
        if (b.holds(pointer, n)) return &b;
    return nullptr;
}

} // namespace od::port
