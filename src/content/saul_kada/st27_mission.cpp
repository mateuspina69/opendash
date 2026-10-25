// ST27 sub 8 (the Saul Kada fire ruin's entrance, story 13): the lava-valve mission's controller (pool 0xe0 type 0x17,
// ST27 table 0x8ecd40, exe 0x425560; hypothesis names).
//
// Script 0x2e (0x526fa0) spawns it at its end with flag 0x681 (the player agreed to help Tron: the records 0x8a5980 x 4
// + 0x8a59d0, this controller); without the flag the end spawns the cast / valve records 0x8a59e8 instead. The room
// handler 0x582bf0 then runs the mission's opening (0x582f50, message 0x2b), which freezes every class while the window
// is up.
// States [0x88be5c]:
//  - 0 start 0x425580: the hint timer 0x3c; the lava floor (e0 type 0x16 variant 2, list 1); the valve (static ST27
//    type 0x3b at (-0x400, -0x100, -0x600) yaw 0x800) with a boss gauge (exe 0x415d30, style 1, shift 1, colour
//    0x2222aa); Tron (pool 0x20 type 0x48 sub 1) at (-0x200, -0x200, 0); Bon (type 0x51 sub 1) at (-0x400, -0x200,
//    0x280); the two linked as partners; Reaverbots (type 0x64 variant 2) up to 6 live pool-0x20 actors, at one of the
//    4 corners of the table 0x88be68 picked by the RNG, y -0x300
//  - 1 run 0x4257a0: the valve's hit points out -> (once no transition is armed) flag 0x4c, done. Else the Reaverbots
//    topped up again (table 0x88be78); both partners alive and one under half its hit points -> flag 0x685 (window 2
//    idle); one down -> Tron down: sound 0x230, Bon down: voice 0x803a (voice idle), flag 0x684; with 0x684 both throw
//    at the player (window 2 idle). The hint timer: the player throwing or raising an arm resets it (and the partners
//    throw into the lava again); past 0x708 ticks the hint voice 0x8037 (first) / 0x8038, then the partners throw at
//    the player and the timer restarts
//  - 2 done 0x427cf0: freed (exe 0x5e15c0)
#include "content/saul_kada/st27_mission.hpp"
#include "content/saul_kada/st27_mission_state.hpp"

#include "content/saul_kada/hopping_reaverbot.hpp"
#include "content/saul_kada/lava_vents.hpp"
#include "content/saul_kada/st27_partners.hpp"

#include "game/loaded_data.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/hud.hpp"
#include "game/message.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace st27_mission_state;

namespace {

// [0x88be5c]
enum class MissionState : uint8_t { Start = 0, Run = 1, Done = 2 };
// the hint's sub-state
enum class HintState : uint8_t { Waiting = 0, Said = 1 };
constexpr uint8_t kFirstHintSaid = 0x01;

// A corner the Reaverbots come in at (exe data: s16 x / z)
struct ReaverbotCorner : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    int16_t x = 0; // +0x0
    int16_t z = 0; // +0x2
    template <class R> static ReaverbotCorner load(const R& r) {
        ReaverbotCorner v;
        v.x = r.template read<int16_t>(0x0);
        v.z = r.template read<int16_t>(0x2);
        return v;
    }
};
constexpr uint8_t kReaverbotVariant = 2;
constexpr int8_t kMaxLiveAnimated = 6;
constexpr uint8_t kValveType = 0x3b;
constexpr uint8_t kPartnerSubtype = 1;
constexpr uint8_t kMissionVentsVariant = 2;
constexpr int8_t kMissionVentList = 1;
constexpr uint32_t kValveGaugeColour = 0x2222aa;
constexpr int16_t kFacingBack = 0x800;
constexpr uint32_t kFlagValveDown = 0x4c, kFlagPartnerDown = 0x684, kFlagPartnerHurt = 0x685;
constexpr uint8_t kPartnerWindow = 2;
constexpr uint16_t kTronDownSound = 0x230;
constexpr uint32_t kBonDownVoice = 0x803a, kFirstHintVoice = 0x8037, kAgainHintVoice = 0x8038;
constexpr int16_t kHintTicks = 0x709;
constexpr int16_t kOpeningHintTicks = 0x3c;

// the live animated slots (the pool's live mask's set bits, a signed char count as the exe)
int8_t liveAnimated() {
    int8_t n = 0;
    for (uint32_t i = 0; i < 0x20; ++i)
        if (actorSystem().animatedLive & (1u << i)) ++n;
    return n;
}
// the Reaverbots up to 6 live pool-0x20 actors, each at a random corner of the table
void topUpReaverbots(uint32_t corners) {
    const int8_t live = liveAnimated();
    if (live > kMaxLiveAnimated - 1) return;
    for (int8_t i = 0; i < kMaxLiveAnimated - live; ++i) {
        const uint8_t r = uint8_t(rngStep());
        const uint32_t slot = allocActor20(); // exe 0x5e1160
        if (!slot) continue;
        AnimatedActor& reaverbot = animatedActorAt(slot);
        reaverbot.flags = kActorLive;
        reaverbot.type = kHoppingReaverbotType;
        reaverbot.loopFlags |= kActorSweepGroup2;
        reaverbot.subtype = 0;
        const ReaverbotCorner corner = dataRecord<ReaverbotCorner>(corners + uint32_t(r & 3u) * 4, ReaverbotCorner::kBytes);
        reaverbot.variant = kReaverbotVariant;
        reaverbot.spawnByte = 0;
        reaverbot.rotation.z = 0;
        reaverbot.rotation.x = 0;
        reaverbot.rotation.y = kFacingBack;
        reaverbot.pos.x = int32_t(uint32_t(int32_t(corner.x)) << 16);
        reaverbot.pos.y = int32_t(0xfd000000u);
        reaverbot.pos.z = int32_t(uint32_t(int32_t(corner.z)) << 16);
        reaverbot.setParamsWord(0);
    }
}
// a partner (pool 0x20, the type's sub-type 1) at the 16.16 position, facing back; 0 when the pool is full
uint32_t spawnPartner(uint8_t type, int32_t x, int32_t y, int32_t z) {
    const uint32_t slot = allocActor20();
    if (!slot) return 0;
    AnimatedActor& partner = animatedActorAt(slot);
    partner.flags = kActorLive;
    partner.type = type;
    partner.loopFlags |= kActorSweepGroup2;
    partner.subtype = kPartnerSubtype;
    partner.variant = 0;
    partner.spawnByte = 0;
    partner.rotation.z = 0;
    partner.rotation.x = 0;
    partner.rotation.y = kFacingBack;
    partner.pos.x = x;
    partner.pos.y = y;
    partner.pos.z = z;
    return slot;
}

// exe 0x425580
void start(ControllerActor& actor, MissionFields& mission) {
    mission.hintTicks = kOpeningHintTicks;
    if (const uint32_t slot = allocActorE0()) { // exe 0x5e1440: the lava floor
        ControllerActor& vents = controllerActorAt(slot);
        vents.flags = kActorLive;
        vents.type = kLavaVentsType;
        vents.subtype = 0;
        vents.variant = kMissionVentsVariant;
        vents.spawnByte = 0;
        LavaVentsParams(controllerActorAt(slot)).ventList = kMissionVentList;
    }
    const uint32_t valveSlot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (valveSlot) {
        StaticActor& valve = staticActorAt(valveSlot);
        valve.flags = kActorLive;
        valve.loopFlags |= kActorSweepGroup2;
        valve.type = kValveType;
        valve.subtype = 0;
        valve.variant = 0;
        valve.spawnByte = 0;
        valve.rotation.z = 0;
        valve.rotation.x = 0;
        valve.rotation.y = kFacingBack;
        valve.pos.x = kLavaX;
        valve.pos.y = kLavaY;
        valve.pos.z = kLavaZ;
        mission.valve = &valve;
    }
    hudAddBossGauge(actorAt(valveSlot), 1, 1, kValveGaugeColour); // exe 0x415d30 (called with no valve too)
    if (const uint32_t tron = spawnPartner(kPartnerTronType, int32_t(0xfe000000u), int32_t(0xfe000000u), 0))
        pointAtWord(mission.tron, tron);
    if (const uint32_t bon = spawnPartner(kPartnerBonType, int32_t(0xfc000000u), int32_t(0xfe000000u), 0x2800000))
        pointAtWord(mission.bon, bon);
    topUpReaverbots(stateAddress(TableId::St27MissionOpeningCorners));
    PartnerFields& tronFields = classState<PartnerFields>(*mission.tron);
    tronFields.partner = mission.bon;
    PartnerFields& bonFields = classState<PartnerFields>(*mission.bon);
    bonFields.partner = mission.tron;
    actor.state = uint8_t(MissionState::Run);
    actor.subState = uint8_t(HintState::Waiting);
}

// exe 0x4257a0
void run(ControllerActor& actor, MissionFields& mission) {
    const StaticActor& valve = staticPool().of(*mission.valve);
    if (valve.hitPoints < 1) {
        if (storyFlag(kFlagTransitionArmed)) return;
        setStoryFlag(kFlagValveDown);
        actor.state = uint8_t(MissionState::Done);
        return;
    }
    topUpReaverbots(stateAddress(TableId::St27MissionRunCorners));
    AnimatedActor& tron = *mission.tron;
    AnimatedActor& bon = *mission.bon;
    PartnerFields& tronFields = classState<PartnerFields>(tron);
    PartnerFields& bonFields = classState<PartnerFields>(bon);
    const int16_t tronHp = tron.hitPoints;
    const int16_t bonHp = tronHp >= 0 ? int16_t(bon.hitPoints) : int16_t(-1);
    if (tronHp >= 0 && bonHp >= 0) {
        if ((int32_t(tronHp) < int32_t(tron.maxHitPoints) / 2 || int32_t(bonHp) < int32_t(bon.maxHitPoints) / 2) &&
            !storyFlag(kFlagPartnerHurt) && windowStatus(kPartnerWindow) == 0) // exe 0x557810
            setStoryFlag(kFlagPartnerHurt);
    } else if (!storyFlag(kFlagPartnerDown)) {
        if (bon.hitPoints < 0) {
            if (voiceClipIdle()) { // exe 0x601260
                voiceClipPlay(kBonDownVoice);
                setStoryFlag(kFlagPartnerDown);
            }
        } else {
            soundPlayPanned(kTronDownSound, 0, 0); // exe 0x5d5340
            setStoryFlag(kFlagPartnerDown);
        }
    } else if (windowStatus(kPartnerWindow) == 0) {
        tronFields.flags |= kPartnerThrowAtPlayer;
        bonFields.flags |= kPartnerThrowAtPlayer;
    }
    // 0x425983: the hint timer
    const Player& p = player();
    if (((uint8_t(p.carryFlags & kCarryThrowing) | uint8_t(p.armFlags & kArmUp)) & 0xc0) != 0) {
        mission.hintTicks = 0;
        mission.hintFlags &= uint8_t(~kFirstHintSaid);
        if (tron.hitPoints >= 0) tronFields.flags &= uint8_t(~kPartnerThrowAtPlayer);
        if (bon.hitPoints >= 0) bonFields.flags &= uint8_t(~kPartnerThrowAtPlayer);
        return;
    }
    if (mission.hintTicks < kHintTicks) {
        mission.hintTicks = int16_t(mission.hintTicks + 1);
        return;
    }
    if (HintState(actor.subState) == HintState::Waiting) {
        if (windowStatus(kPartnerWindow) == 0 && voiceClipIdle()) {
            voiceClipPlay((mission.hintFlags & kFirstHintSaid) ? kAgainHintVoice : kFirstHintVoice);
            mission.hintFlags |= kFirstHintSaid;
            ++actor.subState;
        }
    } else if (windowStatus(kPartnerWindow) == 0) {
        tronFields.flags |= kPartnerThrowAtPlayer;
        bonFields.flags |= kPartnerThrowAtPlayer;
        mission.hintTicks = 0;
        actor.subState = uint8_t(HintState::Waiting);
    }
}

// exe 0x425560 [0x88be5c]
void lavaValveMission(ControllerActor& actor) {
    MissionFields& mission = classState<MissionFields>(actor);
    switch (MissionState(actor.state)) {
    case MissionState::Start: start(actor, mission); break;
    case MissionState::Run: run(actor, mission); break;
    case MissionState::Done: freeActor(actor); break; // 0x427cf0: exe 0x5e15c0
    default: notPortedAt(kStoryPort, "0x425560 state", actor.state);
    }
}

} // namespace

void lavaValveMissionUpdate(const PlayerEnv&, uint32_t actor) { lavaValveMission(controllerActorAt(actor)); }

} // namespace od::port
