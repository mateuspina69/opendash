#include "game/text_memory.hpp"

#include "game/loaded_data.hpp"

namespace od::port {

std::map<uint32_t, uint8_t>& patchedProgramText() {
    static std::map<uint32_t, uint8_t> m;
    return m;
}

uint8_t textByte(uint32_t address) {
    const auto& patched = patchedProgramText();
    if (!patched.empty())
        if (const auto it = patched.find(address); it != patched.end()) return it->second;
    return dataAt(address, 1).u8(0);
}

bool textMapped(uint32_t address, uint32_t n) { return dataMapped(address, n); }

void storeTextByte(uint32_t address, uint8_t v) {
    if (builtDataAt(address, 1) || loadedFileMapped(address, 1)) {
        storeData(address, v);
        return;
    }
    dataAt(address, 1); // an address no data holds: the data source's error
    patchedProgramText()[address] = v;
}

} // namespace od::port
