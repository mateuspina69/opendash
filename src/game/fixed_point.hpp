// The game's fixed-point helpers shared by the systems: the 16-bit wrap of the exe's short stores, the integer part of a
// 16.16 value and the exe's wrapping 32-bit arithmetic (the exe's 4.12 / 16.16 conventions: game/angles, game/matrix).
#pragma once

#include <cstdint>

namespace od::port {

// v truncated to 16 bits, read back signed (the exe's short store of an int)
inline int16_t wrap16(int v) { return int16_t(uint16_t(uint32_t(v))); }
// the integer part (high 16 bits, signed) of a 16.16 value
inline int16_t wholePart(int32_t v) { return int16_t(uint32_t(v) >> 16); }
// the exe's 32-bit imul / add / sub (they wrap)
inline int32_t mul32(int32_t a, int32_t b) { return int32_t(uint32_t(a) * uint32_t(b)); }
inline int32_t add32(int32_t a, int32_t b) { return int32_t(uint32_t(a) + uint32_t(b)); }
inline int32_t sub32(int32_t a, int32_t b) { return int32_t(uint32_t(a) - uint32_t(b)); }
// the 16.16 value of an integer (its low 16 bits the integer part, a zero fraction)
inline int32_t wholeToFixed16(int whole) { return int32_t(uint32_t(whole) << 16); }

} // namespace od::port
