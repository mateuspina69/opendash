// The port gap: the error a ported routine raises where it reaches a branch of the exe that has no C++ routine yet
// (a state / variant / dispatch value without a case) or a state the exe itself could not run (a table index past
// its end, a null pointer the original would dereference). The message reads "<area>: not ported: <what>"; the
// area names the ported system or class family ("story port", "combat port", ...), so test outputs and the
// playthrough logs, which look for "not ported", point at the routine.
#pragma once

#include <cstdint>

namespace od::port {

// throws std::logic_error("<area>: not ported: <what>")
[[noreturn]] void notPorted(const char* area, const char* what);
// throws std::logic_error("<area>: not ported: <what> 0x<value in lowercase hex>")
[[noreturn]] void notPortedAt(const char* area, const char* what, uint32_t value);

} // namespace od::port
