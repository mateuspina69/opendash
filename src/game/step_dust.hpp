// The dust the enemies raise: a step's dust by the ground kind under it (exe 0x5e6300: the stage's dust effect on one
// kind, the hit spark's ground-dust variant on two others, nothing on the rest) and a landing's dust spark (exe
// 0x5e62c0: the hit spark's dust variant), and a vehicle's tread dust (exe 0x46a130: a larger spark, then a step's
// dust by the ground kind, the stage's effect in its second variant).
#pragma once

#include <cstdint>

namespace od::port {

// exe 0x5e62c0(x, y, z): the global hit spark, its dust variant, at the s16 point
void spawnDustSpark(int16_t x, int16_t y, int16_t z);
// exe 0x5e6300(x, y, z, kind): the dust of a step at the s16 point on the ground kind (game/room_grid groundKindAt)
void spawnStepDust(int16_t x, int16_t y, int16_t z, int32_t groundKind);
// exe 0x46a130(x, y, z): the tread dust of a tank at the s16 point -- the hit spark's tread variant, then by the ground
// kind under it (none: the first spark-dust kind) the stage's dust effect or the hit spark's ground-dust variant
void spawnTreadDust(int16_t x, int16_t y, int16_t z);

} // namespace od::port
