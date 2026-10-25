// The treasure chest class 0x4743c0 (see treasure_chest.hpp).
#include "game/actor_state.hpp"
#include "game/treasure_chest.hpp"
#include "game/treasure_chest_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/event_state.hpp"
#include "game/message.hpp"
#include "game/event.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/game_state.hpp"
#include "game/actor_movement.hpp"
#include "game/player/player_weapons.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace treasure_chest_state;

namespace {

const PlayerEnv* E = nullptr;

constexpr const char* kChestPort = "treasure chest"; // the area of its port-gap errors

// a content record (exe table [0x8906d0 + group * 4] + variant * 8)
struct ChestContent : DataRecord {
    uint16_t openedFlag = 0; // the event flag of an opened chest
    uint16_t message = 0;    // the item-get event's message
    uint32_t content = 0;    // kind << 24 | value (a special weapon id, or an event flag)
    static ChestContent load(const TableReader& r) { return {{}, r.u16(0), r.u16(2), r.u32(4)}; }
};
// the content groups (exe table 0x8906d0: a pointer per group to its records by variant)
struct ChestContentGroups : DataRecord {
    static constexpr size_t kGroups = 0x80, kMaxVariants = 0x100;
    static constexpr uint32_t kBytes = kGroups * 4, kRecordSize = 8;
    std::array<TableReader, kGroups> groups;
    ChestContent content(size_t group, uint32_t variant) const {
        if (group >= kGroups) tableIndexError(group, kGroups);
        return ChestContent::load(groups[group].sub(size_t(variant) * kRecordSize, kRecordSize));
    }
    static ChestContentGroups load(const TableReader& r) {
        ChestContentGroups t;
        for (size_t g = 0; g < kGroups; ++g) t.groups[g] = r.follow(g * 4, kMaxVariants * kRecordSize);
        return t;
    }
};
const DataTable<ChestContentGroups> contentGroups{TableId::TreasureChestContents};

// the states (+8; table 0x8906f4)
enum class ChestState : uint8_t {
    Init = 0,   // exe 0x474480
    Closed = 1, // exe 0x474550
    Opened = 2, // exe 0x52ef00 (ret)
};
// the closed state's sub-states (+9)
enum class ChestSub : uint8_t {
    Wait = 0,       // the player talks to it (from the front)
    StartEvent = 1, // the item-get event, the lid
    LidOpening = 2,
    GiveContent = 3, // exe 0x4746c0 + the opened flag
    EventRuns = 4,
    Closing = 5,    // the lid's last animation, then opened
};

constexpr uint32_t kContentKindMask = 0xff000000u, kContentValueMask = 0xffffff;
constexpr uint32_t kContentWeapon = 0x1000000u;     // a special weapon: its id
constexpr uint32_t kContentFlag = 0x3000000u;       // an event flag
constexpr uint32_t kContentFlagToo = 0x6000000u;    // an event flag (another kind of item)
constexpr uint32_t kItemGetEvent = 0x40023;         // event mode 4 (stand) then 2 (the item pose), no jingle
constexpr uint8_t kAnimClosed = 0, kAnimOpen = 1, kAnimOpened = 2, kNoAnim = 0xff;
constexpr uint8_t kOpenedKey = 0x10; // an opened chest starts its last animation at this key
constexpr uint16_t kSoundLidOpen = 0xf1, kSoundLidClose = 0xf2;
constexpr int kFrontHalfWindow = 0x180; // the player is in front within +-0x180 of the chest's facing

const DataTable<CollisionBoxRecord> chestCollisionBox{TableId::TreasureChestCollisionBox};
const DataTable<InteractionBoxRecord> chestTalkBox{TableId::TreasureChestTalkBox};

// the chest's row of the content table (by its group, then its variant)
ChestContent contentOf(const AnimatedActor& actor, const ChestFields& chest) {
    return contentGroups->content(size_t(int32_t(chest.group)), actor.variant);
}

// exe 0x474480: the model, the boxes, on the ground; already opened (its flag) -> the opened lid at once
void chestInit(AnimatedActor& actor, ChestFields& chest) {
    if (!bindActorModel(actor, 0)) {
        freeActor(actor);
        return;
    }
    actor.clearClassBlock();
    actor.anim = kAnimClosed;
    actor.prevAnim = kNoAnim;
    actor.hitPoints = 1;
    actor.maxHitPoints = 1;
    actor.velocity.clearWords();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(chestCollisionBox.stateAddress());
    actor.interactionBox.pointAt(chestTalkBox.stateAddress());
    actor.interaction = kInteractTalk;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.lastMover = nullptr;
    actor.pos.y.whole = int16_t(actorGround(*E, actor));
    actor.enterState(uint8_t(ChestState::Closed));
    if (storyFlag(contentOf(actor, chest).openedFlag)) {
        actor.anim = kAnimOpened;
        chest.startKey = kOpenedKey;
        actor.enterState(uint8_t(ChestState::Opened));
    }
}
// exe 0x474550: talkable from its front; the talk opens the lid, gives the content, sets the opened flag, closes
void chestClosed(AnimatedActor& actor, const ChestFields& chest) {
    const ChestContent content = contentOf(actor, chest);
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(player().pos, actor.pos)));
    actor.interaction = uint32_t(d + kFrontHalfWindow) < uint32_t(2 * kFrontHalfWindow + 1) ? kInteractTalk : 0;
    switch (ChestSub(actor.subState)) {
    case ChestSub::Wait:
        if (player().talksTo(actor) && eventState().active == 0) actor.subState = uint8_t(ChestSub::StartEvent);
        return;
    case ChestSub::StartEvent: {
        // exe 0x543b40(&{x, y, z} (the 16.16 position's integer halves), 0, 0x40023, 0, message): event mode 4
        // (stand), then mode 2 (the item-get pose)
        Fixed16Vec3& point = eventStartPoint().point;
        point.x = int32_t(uint32_t(int32_t(actor.pos.x.whole)) << 16);
        point.y = int32_t(uint32_t(int32_t(actor.pos.y.whole)) << 16);
        point.z = int32_t(uint32_t(int32_t(actor.pos.z.whole)) << 16);
        eventStart(&point, nullptr, kItemGetEvent, 0, content.message);
        actor.anim = kAnimOpen;
        soundPlayAtPos(kSoundLidOpen, actor.pos);
        actor.subState = uint8_t(ChestSub::LidOpening);
        return;
    }
    case ChestSub::LidOpening:
        if (actor.animEnded()) actor.subState = uint8_t(ChestSub::GiveContent);
        return;
    case ChestSub::GiveContent: { // exe 0x4746c0 + the opened flag
        const uint32_t c = content.content;
        if ((c & kContentKindMask) == kContentWeapon) { // a special weapon: equipped, the buster's stats set
            player().specialWeapon = uint8_t(c);
            setupSpecialGauge(true);
        } else if (((c & kContentKindMask) == kContentFlag || (c & kContentKindMask) == kContentFlagToo) &&
                   (c & kContentValueMask) != 0) {
            setStoryFlag(c & kContentValueMask);
        }
        setStoryFlag(content.openedFlag);
        actor.subState = uint8_t(ChestSub::EventRuns);
        [[fallthrough]];
    }
    case ChestSub::EventRuns:
        if (eventState().active == 0) {
            actor.anim = kAnimOpened;
            actor.subState = uint8_t(ChestSub::Closing);
            soundPlayAtPos(kSoundLidClose, actor.pos);
        }
        return;
    case ChestSub::Closing:
        if (actor.animEnded()) actor.enterState(uint8_t(ChestState::Opened));
        return;
    default: return;
    }
}
// exe 0x4743c0's body: the state, the animation, a stand-on solid
void treasureChest(AnimatedActor& actor) {
    ChestFields& chest = classState<ChestFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (ChestState(actor.state)) {
    case ChestState::Init: chestInit(actor, chest); break;
    case ChestState::Closed: chestClosed(actor, chest); break;
    case ChestState::Opened: break;
    default: notPortedAt(kChestPort, "state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) {
        animStepPose(actor);
    } else {
        animSetPose(actor, actor.anim, chest.startKey);
        chest.startKey = 0;
    }
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0);
}

} // namespace

void treasureChestUpdate(const PlayerEnv& env, uint32_t address) {
    E = &env;
    treasureChest(animatedActorAt(address));
}

} // namespace od::port
