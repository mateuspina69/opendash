#include "game/random.hpp"

namespace od::port {

uint32_t rngStep() {
    const uint32_t n = rngNext(randomState().rng);
    randomState().rng = n;
    return n;
}

uint32_t rngLastValue() { return randomState().rng; }

void rngSetState(uint32_t state) { randomState().rng = state; }

uint32_t crtRand() {
    RandomState& crt = randomState();
    const uint32_t s = crt.crtSeed * 0x343fdu + 0x269ec3u;
    crt.crtSeed = s;
    return (s >> 16) & 0x7fff;
}

} // namespace od::port
