// The blizzard / sky layer (see blizzard_layer.hpp). State 0: its speed record by the variant's bits 4..6 (exe table
// 0x88cae8), kept through warps unless the variant's bit 7; every tick its scroll words move by the record's speeds
// (backwards with the variant's bits 0 / 1, the texture's mirrors).
#include "game/actor_state.hpp"
#include "game/blizzard_layer.hpp"

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/weather_draw.hpp"

namespace od::port {

namespace {

// the speed records (exe data 0x88cae8, 8 bytes each); the layer stores its record's address (the address is the record's identity)
struct BlizzardSpeedRecord : DataRecord {
    uint16_t grey = 0, swing = 0;
    int16_t scrollX = 0, scrollY = 0;
    static BlizzardSpeedRecord load(const TableReader& r) { return {{}, r.u16(0), r.u16(2), r.s16(4), r.s16(6)}; }
};
constexpr uint32_t kSpeedRecordSize = 8;
const DataTable<RecordTable<BlizzardSpeedRecord, 8, kSpeedRecordSize>> blizzardSpeeds{TableId::BlizzardSpeeds};
constexpr uint8_t kBlizzardNotKept = 0x80;
constexpr uint32_t kSpeedShift = 4, kSpeedMask = 7;
// +8: the start (the speed record picked), then the scroll
enum class BlizzardState : uint8_t { Start = 0, Scroll = 1 };

// exe 0x431af0
void blizzardLayer(EffectActor& e) {
    BlizzardLayerFields& f = classState<BlizzardLayerFields>(e);
    if (BlizzardState(e.state) == BlizzardState::Start) {
        f.speed.pointAt(blizzardSpeeds.stateAddress() + ((uint32_t(e.variant) >> kSpeedShift) & kSpeedMask) * kSpeedRecordSize);
        if (!(e.variant & kBlizzardNotKept)) e.runFlags |= kActorWarpKept;
        ++e.state;
    }
    const BlizzardSpeed speed = *f.speed;
    const uint8_t v = e.variant;
    if (v & kBlizzardMirrorU) f.scrollX = int16_t(uint16_t(f.scrollX) - uint16_t(speed.scrollX));
    else f.scrollX = int16_t(uint16_t(f.scrollX) + uint16_t(speed.scrollX));
    if (v & kBlizzardMirrorV) f.scrollY = int16_t(uint16_t(f.scrollY) - uint16_t(speed.scrollY));
    else f.scrollY = int16_t(uint16_t(f.scrollY) + uint16_t(speed.scrollY));
}

} // namespace

// the class's update (blizzard_layer.hpp)
void blizzardLayerUpdate(const PlayerEnv&, uint32_t actor) { blizzardLayer(effectActorAt(actor)); }

} // namespace od::port
