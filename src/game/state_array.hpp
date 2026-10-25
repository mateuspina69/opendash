// The native state's fixed arrays:
// a std::array whose index is checked. The exe indexes its arrays unchecked and an index past the end reads whatever
// follows; the native state has no "what follows", so such an index is an explicit StateIndexError instead of a
// silent read of another member.
#pragma once

#include <array>
#include <cstddef>
#include <cstdio>
#include <stdexcept>

namespace od::port {

// an index past a native state array (a refactor bug or an exe overrun the port must model by name, never a port
// gap: it derives from std::runtime_error so the update loops' port-gap handler, std::logic_error, does not swallow it)
class StateIndexError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
[[noreturn]] inline void stateIndexError(size_t index, size_t count) {
    char b[80];
    std::snprintf(b, sizeof b, "native state: index %zu past an array of %zu", index, count);
    throw StateIndexError(b);
}

template <class T, size_t N> struct StateArray : std::array<T, N> {
    T& operator[](size_t i) {
        if (i >= N) stateIndexError(i, N);
        return std::array<T, N>::operator[](i);
    }
    const T& operator[](size_t i) const {
        if (i >= N) stateIndexError(i, N);
        return std::array<T, N>::operator[](i);
    }
};

} // namespace od::port
