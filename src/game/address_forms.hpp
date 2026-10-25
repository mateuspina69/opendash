// What the content transcriptions still keep from the address forms: the player environment their class routines run
// with (E) and the address of an animated slot id by the exe's arithmetic.
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

namespace address_forms {

// the player environment of the class routine that runs (bound by the content class updates that read it)
inline const PlayerEnv* E = nullptr;

// the address of the animated pool slot of a slot id, by the exe's unchecked index arithmetic (a slot 0 gives an
// address below the pool, as in the exe)
inline uint32_t animatedSlotAddress(uint32_t slot) { return 0xa6e3a0 + (slot - 0x20u) * 0x16cu; }

} // namespace address_forms

} // namespace od::port
