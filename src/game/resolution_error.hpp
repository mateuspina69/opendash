// A lookup by an identity that names nothing (an address that is no actor slot, a stat no table holds): a refactor or
// state bug, never a port gap -- it derives from std::runtime_error so the update loops' port-gap handler
// (std::logic_error) does not swallow it.
#pragma once

#include <stdexcept>

namespace od {

class ResolutionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace od
