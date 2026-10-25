#include "game/port_gap.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace od::port {

void notPorted(const char* area, const char* what) {
    throw std::logic_error(std::string(area) + ": not ported: " + what);
}

void notPortedAt(const char* area, const char* what, uint32_t value) {
    char hex[16];
    std::snprintf(hex, sizeof hex, " 0x%x", value);
    notPorted(area, (std::string(what) + hex).c_str());
}

} // namespace od::port
