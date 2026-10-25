// The game's two random number sources.
//  - The game RNG: a 32-bit shift-and-xor generator whose state is the word 0x6d0000. The exe inlines its step in
//    every caller (`state = (state * 2 + 1 + (state < 0)) ^ 0x873ca9e5`, then the new state is the random value).
//  - The C runtime's rand() (exe 0x611443): the MSVC linear congruential generator, used by a few classes. Its seed
//    story.CRT_RAND, so the goldens carry it).
#pragma once

#include <cstdint>

namespace od::port {

// The two generators' states
struct RandomState {
    uint32_t rng = 0;     // the game RNG's state word
    uint32_t crtSeed = 0; // the C runtime's rand() seed
};
inline RandomState& randomState() {
    static RandomState s;
    return s;
}

// the game RNG's step from state r (pure)
inline uint32_t rngNext(uint32_t r) { return (uint32_t(1 - (int32_t(r) >> 31)) + r * 2u) ^ 0x873ca9e5u; }
// steps the game RNG and returns the new state (the random value)
uint32_t rngStep();
// the game RNG's current state, the value of its last step (read without stepping)
uint32_t rngLastValue();
// the game RNG's state set (a script reseeds it, e.g. with rngNext of a value it saved)
void rngSetState(uint32_t state);
// the C runtime's rand() (exe 0x611443): steps the seed and returns 15 random bits
uint32_t crtRand();

} // namespace od::port
