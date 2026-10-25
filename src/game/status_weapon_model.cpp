// The equipment page's turning special weapon (see status_weapon_model.hpp).
#include "game/status_weapon_model.hpp"

#include <vector>

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actors.hpp"
#include "game/display.hpp"
#include "game/menu_draw.hpp"
#include "game/model_preview.hpp"
#include "game/status_screen.hpp"

namespace od::port {

using namespace menu_draw;

namespace {

// the model actor: its flags (live, drawn, kModelPlain: no depth cue), the sub-screen table's type, its turn
constexpr uint8_t kModelPlain = 0x20;
constexpr uint8_t kWeaponModelFlags = kActorLiveDrawn | kModelPlain;
constexpr uint8_t kWeaponModelType = 0x65;
constexpr int16_t kWeaponModelPitch = 0x200, kWeaponModelYaw = -0x400;
// the weapons' model places (exe table 0x91b578: s16 x / y / z by weapon, 8 bytes each)
struct WeaponModelPlace : DataRecord {
    int16_t x = 0, y = 0, z = 0;
    static WeaponModelPlace load(const TableReader& r) {
        WeaponModelPlace rec;
        rec.x = r.s16(0);
        rec.y = r.s16(2);
        rec.z = r.s16(4);
        return rec;
    }
};
const DataTable<RecordTable<WeaponModelPlace, 0x14, 8>> weaponModelPlaces{TableId::StatusWeaponModelPlaces};
// the boxes the model is drawn in: their screen-centre offsets and their drawing areas (exe tables, by box)
struct BoxOffset : DataRecord {
    int16_t dx = 0, dy = 0;
    static BoxOffset load(const TableReader& r) {
        BoxOffset rec;
        rec.dx = r.s16(0);
        rec.dy = r.s16(2);
        return rec;
    }
};
const DataTable<RecordTable<BoxOffset, 4, 4>> modelBoxOffsets{TableId::StatusWeaponModelBoxOffsets};
const DataTable<RecordTable<DrawArea, 4, 8>> modelBoxAreas{TableId::StatusWeaponModelBoxAreas};

// exe 0x5f40b0(dx, dy) into the chain c: nothing while the frame draws nothing; the screen centre = the display mode's
// area / 2 + (dx, dy), the sub-screen's slots 31 .. 24 drawn (live and shown, with a model)
void subScreenActorsDraw(size_t c, int dx, int dy) {
    if (display().drawingOff != 0) return;
    // the drawing areas of the display modes (game/display: by the draw buffer + the mode * 2; the screen centre is the
    // mode's first area / 2)
    const DrawArea& area = displayDrawAreas[size_t(display().mode) * 2];
    std::vector<const AnimatedActor*> actors;
    for (size_t i = AnimatedPool::size(); i-- > kAnimatedFieldSlots;) {
        const AnimatedActor& a = animatedPool()[i];
        if ((a.runFlags & a.flags & kActorDrawn) && a.model != 0) actors.push_back(&a);
    }
    modelPreviewDraw(c, actors, int(area.w) / 2 + dx, int(area.h) / 2 + dy, kPreviewEquipment);
}

} // namespace

void weaponModelSet(StatusScreen& s, uint8_t weapon) {
    if (weapon == s.modelWeapon) return;
    AnimatedActor* old = s.modelActor.get();
    if (old != nullptr && old->flags != 0) freeActor(*old);
    s.modelActor = nullptr;
    if (weapon != 0) {
        const uint32_t address = allocSubScreenActor(); // exe 0x5e11c0
        if (address != 0) {
            AnimatedActor& a = animatedActorAt(address);
            s.modelActor = &a;
            const WeaponModelPlace place = weaponModelPlaces[weapon];
            a.flags = kWeaponModelFlags;
            a.type = kWeaponModelType;
            a.variant = uint8_t(weapon - 1u);
            a.pos.x = int32_t(uint32_t(int32_t(place.x)) << 16);
            a.pos.y = int32_t(uint32_t(int32_t(place.y)) << 16);
            a.pos.z = int32_t(uint32_t(int32_t(place.z)) << 16);
            a.rotation.x = kWeaponModelPitch;
            a.rotation.y = kWeaponModelYaw;
            a.rotation.z = 0;
        }
    }
    s.modelWeapon = weapon;
}

void subScreenActorsUpdate() {
    static const PlayerEnv kNoRoom{}; // the classes run here read no collision
    ActorSystemState& system = actorSystem();
    for (size_t i = kAnimatedFieldSlots; i < AnimatedPool::size(); ++i) {
        AnimatedActor& a = animatedPool()[i];
        system.animatedCursor = a.at;
        if ((a.runFlags & a.flags & kActorLive) == 0) continue;
        const RoutineRef h = stateRoutine(classTableEntry(classTableEntry(stageClassTables().animated.value(), a.type), a.subtype));
        if (const ActorUpdateFn fn = portedActorClass(h.id())) fn(kNoRoom, a.at);
    }
    system.animatedCursor = AnimatedPool::base() + uint32_t(AnimatedPool::size()) * AnimatedPool::stride();
}

void weaponModelDraw(int k, int ot) {
    if (!drawing()) return;
    const uint32_t slot = kOtModel + uint32_t(ot) * 4;
    const DrawArea area = displayDrawArea();
    otClipDrawArea(otOpen(slot));
    const BoxOffset offset = modelBoxOffsets[size_t(k)];
    subScreenActorsDraw(otOpen(slot), offset.dx, offset.dy);
    const DrawArea& box = modelBoxAreas[size_t(k)];
    otClip(otOpen(slot), box.x, box.y + area.y, box.w, box.h);
}

} // namespace od::port
