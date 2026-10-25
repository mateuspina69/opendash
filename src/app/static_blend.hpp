#pragma once
// The semi-transparency of a static-pool bank-model actor (pool 0x60), as Maybe_DrawBankModelActor 0x5f45a0 sets it:
// draw word 0x6d00e0 |= +0xbb << 20 (0x5f4906) -- +0xbb bit 0x20 = GP0 semi bit 25, bits 1..2 = tpage ABR (bits 21..22).
// FUN_005e1a70(a, on, mode) writes +0xbb = (on << 4 | mode) << 1 (e.g. the breakable ice of class 0x5a41a0).
#include <cstdint>


namespace od::app {

// the DynamicObject blend of the actor: 0 opaque, 1 + ABR (see od::DynamicObject::blend)
uint8_t staticModelBlend(uint32_t actor);

} // namespace od::app
