// The item on its pedestal in the ruin under Nino (static type 0x40, exe 0x5b1450), one routine per exe routine; the
// names are hypotheses from the observed behaviour. ST35's room (content/nino/st35_rooms) spawns it from the record
// 0x8cb1b0 with its flag 0x3d1; ST37's key (flag 0x3e9) is spawned by its room (content/nino/st37_rooms) or by the last
// gauge reaverbot down (content/nino/gauge_reaverbots).
#include "content/nino/pedestal_item.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/field_alias.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

enum class ItemState : uint8_t { Init = 0, Wait = 1, Taken = 2 };
// The item's spawn parameters (it turns while its first one is set; a message number its spawners write, which the
// class does not read; its flag) and its hit box
struct ItemFields {
    explicit ItemFields(const StaticActor& actor) : ItemFields(const_cast<StaticActor&>(actor)) {}
    explicit ItemFields(StaticActor& actor) : turning(actor.params[0]), message(actor.params[1]), flag(actor.params, 2), hit(actor.hitBox) {}
    uint8_t& turning;
    uint8_t& message;
    BytesField<uint16_t, 4> flag;
    HitBoxHead& hit;
};
const DataTable<InteractionBoxRecord> itemTalkBox{TableId::PedestalItemTalkBox};
constexpr uint32_t kTakeEventParam = 0x20022;
constexpr uint16_t kTakeWindow = 1, kTakeMessage = 0x13;
constexpr uint32_t kFlagFlooded = 0x90, kFlagDrainedFirst = 0x91, kFlagDrained = 0x92;
constexpr uint8_t kPedestalItemType = 0x40;
enum class ItemVariant : uint8_t { Plain = 0, Talks = 1 };
constexpr uint16_t kFlagSt37Key = 0x3e9;
constexpr uint8_t kSt37KeyMessage = 0x13;
constexpr int32_t kKeyHeight = int32_t(0xfff00000u); // -0x10 (16.16)

// exe 0x5b1450
void pedestalItem(StaticActor& item) {
    ItemFields fields(item);
    switch (ItemState(item.state)) {
    case ItemState::Init: // exe 0x5b1470
        if (!bindActorModel(item, 0)) {
            freeActor(item); // exe 0x5e1540
            return;
        }
        item.state = uint8_t(ItemState::Wait);
        item.flags |= kActorDrawn;
        item.subState = 0;
        fields.hit.result = 0;
        if (ItemVariant(item.variant) == ItemVariant::Plain) {
            item.interaction = 0;
        } else {
            item.interactionBox.pointAt(itemTalkBox.stateAddress());
            item.interaction = kInteractTalk;
        }
        return;
    case ItemState::Wait: { // exe 0x5b14c0
        if (player().talksTo(item) && eventState().active == 0) { // exe 0x573fa0
            eventStartAtActor(item, kTakeEventParam, kTakeWindow, kTakeMessage); // exe 0x543af0
            setStoryFlag(fields.flag);
            item.enterState(uint8_t(ItemState::Taken));
        }
        if (fields.turning != 0) item.rotation.y = int16_t(uint16_t(item.rotation.y) + 0x20u);
        fields.hit.previousX = item.prevPos.x.whole;
        fields.hit.previousZ = item.prevPos.z.whole;
        fields.hit.result = 0;
        const int16_t pos[3] = {item.pos.x.whole, item.pos.y.whole, item.pos.z.whole};
        hitListSubmit(pos, fields.hit, hitOwner(item.slot) | hitGroup(kHitGroupHeavyThrown) | hitRadius(0x64),
                      kHitAttackStrong); // exe 0x5e4120
        return;
    }
    case ItemState::Taken: // exe 0x5b1570
        clearStoryFlag(kFlagFlooded);
        setStoryFlag(kFlagDrainedFirst);
        setStoryFlag(kFlagDrained);
        freeActor(item);
        return;
    default: notPortedAt(kStoryPort, "0x5b1450 state", item.state);
    }
}

} // namespace

bool spawnPedestalKey(int32_t x, int32_t z) {
    const uint32_t e = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!e) return false;
    StaticActor& item = staticActorAt(e);
    ItemFields fields(item);
    item.flags = kActorLive;
    item.loopFlags |= kActorSweepGroup2;
    item.type = kPedestalItemType;
    item.subtype = 0;
    item.variant = uint8_t(ItemVariant::Talks);
    item.spawnByte = 0;
    item.rotation.z = 0;
    item.rotation.x = 0;
    item.rotation.y = 0;
    item.pos.x = x;
    item.pos.y = kKeyHeight;
    item.pos.z = z;
    fields.turning = 1;
    fields.message = kSt37KeyMessage;
    fields.flag = kFlagSt37Key;
    return true;
}

void pedestalItemUpdate(const PlayerEnv&, uint32_t actor) { pedestalItem(staticActorAt(actor)); }

} // namespace od::port
