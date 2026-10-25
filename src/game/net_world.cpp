// The game's side of netplay's shared world (see net_world.hpp).
#include "game/net_world.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"

namespace od::port {

namespace {

// the cutscene cast (the classes named for their script, cast, scene or talker role, and those routines.csv gives as a
// script's / the attract's actors): every machine runs its own
const std::set<RoutineId> kCutsceneCast = {
    RoutineId::AppoDahGatePuppetUpdate,
    RoutineId::AppoDahPuppetUpdate,
    RoutineId::BonRetreatCastUpdate,
    RoutineId::BonScriptCastUpdate,
    RoutineId::BossDefeatCastUpdate,
    RoutineId::BossEscapeCastUpdate,
    RoutineId::BossIntroCastUpdate,
    RoutineId::BossRetreatCastUpdate,
    RoutineId::CellSceneBirdbotUpdate,
    RoutineId::CrashPropUpdate,
    RoutineId::CrashTalkerUpdate,
    RoutineId::CutsceneGunshipUpdate,
    RoutineId::DepthsCastUpdate,
    RoutineId::ElephantCutsceneUpdate,
    RoutineId::ElysiumDefeatCastUpdate,
    RoutineId::ElysiumIntroCastUpdate,
    RoutineId::EndingGlowCastUpdate,
    RoutineId::EndingGroupCastUpdate,
    RoutineId::EndingMouthTalkerUpdate,
    RoutineId::EndingTalkerAUpdate,
    RoutineId::EndingTalkerBUpdate,
    RoutineId::EndingWalkerUpdate,
    RoutineId::EntranceCastUpdate,
    RoutineId::EscapeTimerUpdate,
    RoutineId::FadingCastUpdate,
    RoutineId::FadingFigureUpdate,
    RoutineId::FallingPieceUpdate,
    RoutineId::FinaleCastAUpdate,
    RoutineId::FinaleCastBUpdate,
    RoutineId::GateFigureUpdate,
    RoutineId::GaugeCastUpdate,
    RoutineId::GlydeBaseSceneBirdbotUpdate,
    RoutineId::GlydeFlyInShipUpdate,
    RoutineId::GlydeHoverShipUpdate,
    RoutineId::GuardianBeatenUpdate,
    RoutineId::GuardianDissolvesUpdate,
    RoutineId::GuardianDropsInUpdate,
    RoutineId::GuardianWakesUpdate,
    RoutineId::JoesLabCastUpdate,
    RoutineId::JunkShopCastUpdate,
    RoutineId::KeyScriptFigureUpdate,
    RoutineId::MachinePartPropUpdate,
    RoutineId::MandaCast2GuardianUpdate,
    RoutineId::MandaCastGuardianUpdate,
    RoutineId::MandaOpenedChestUpdate,
    RoutineId::MandaOutroGuardianUpdate,
    RoutineId::MandaRuinBurningWreckUpdate,
    RoutineId::MandaRuinGroundedFigureUpdate,
    RoutineId::OpeningCrewUpdate,
    RoutineId::OpeningFlutterUpdate,
    RoutineId::OpeningTalkerUpdate,
    RoutineId::PirateBaseCastUpdate,
    RoutineId::PirateCastUpdate,
    RoutineId::PlayerScriptedUpdate,
    RoutineId::PropDropperUpdate,
    RoutineId::PropHolderUpdate,
    RoutineId::QuizCastUpdate,
    RoutineId::ResultSceneDataUpdate,
    RoutineId::RollRunsOffUpdate,
    RoutineId::SceneCritterUpdate,
    RoutineId::Script1dAppoDahUpdate,
    RoutineId::Script1fAppoDahUpdate,
    RoutineId::Script20AppoDahUpdate,
    RoutineId::Script2aArrivalCastUpdate,
    RoutineId::Script32CastUpdate,
    RoutineId::Script37CastUpdate,
    RoutineId::Script37FlashUpdate,
    RoutineId::Script39BirdbotUpdate,
    RoutineId::Script39BlinkerUpdate,
    RoutineId::Script39DescenderUpdate,
    RoutineId::Script39RiserUpdate,
    RoutineId::Script3bBirdbotUpdate,
    RoutineId::Script3cCastUpdate,
    RoutineId::Script59CuedTalkerAUpdate,
    RoutineId::Script59CuedTalkerBUpdate,
    RoutineId::Script59FaceRowCastUpdate,
    RoutineId::Script59FlyerUpdate,
    RoutineId::Script59IdleOrStepperUpdate,
    RoutineId::Script59LateStepperUpdate,
    RoutineId::Script59LateTalkerUpdate,
    RoutineId::Script59SceneTalkerUpdate,
    RoutineId::Script59SceneWalkerUpdate,
    RoutineId::Script59WalkInCastUpdate,
    RoutineId::Script60ExtraAUpdate,
    RoutineId::Script60ExtraBUpdate,
    RoutineId::Script60FacedTalkerUpdate,
    RoutineId::Script60FadingFigureUpdate,
    RoutineId::Script60LeadTalkerUpdate,
    RoutineId::Script60MaskedTalkerUpdate,
    RoutineId::Script60WalkerUpdate,
    RoutineId::Script61FacedTalkerUpdate,
    RoutineId::Script61LeadTalkerUpdate,
    RoutineId::Script61MaskedTalkerUpdate,
    RoutineId::Script61StandingFigureUpdate,
    RoutineId::Script62TalkerAUpdate,
    RoutineId::Script62TalkerBUpdate,
    RoutineId::Script68CastUpdate,
    RoutineId::Script69CastUpdate,
    RoutineId::Script6fActionTalkerUpdate,
    RoutineId::Script6fCirclerUpdate,
    RoutineId::Script6fHoldingTalkerUpdate,
    RoutineId::Script6fMouthTalkerUpdate,
    RoutineId::Script6fPacerUpdate,
    RoutineId::Script6fRowTalkerUpdate,
    RoutineId::Script6fSparkTalkerUpdate,
    RoutineId::ScriptAnimatedUpdate,
    RoutineId::ScriptFallingPieceUpdate,
    RoutineId::ScriptStatueUpdate,
    RoutineId::ScriptWalkerUpdate,
    RoutineId::ScriptedMessageUpdate,
    RoutineId::ScriptedWalkerUpdate,
    RoutineId::ServbotScriptCastUpdate,
    RoutineId::SittingPuppetUpdate,
    RoutineId::St02BarrelUpdate,
    RoutineId::St02BlinkerUpdate,
    RoutineId::St02FaceUpdate,
    RoutineId::St02PropUpdate,
    RoutineId::St02RollUpdate,
    RoutineId::St02SceneryUpdate,
    RoutineId::St02ScreenUpdate,
    RoutineId::St02SpeakerUpdate,
    RoutineId::St02StanderUpdate,
    RoutineId::St02TalkerUpdate,
    RoutineId::St02WatcherUpdate,
    RoutineId::St03BarrelUpdate,
    RoutineId::St03BlinkerUpdate,
    RoutineId::St03CraftBodyUpdate,
    RoutineId::St03CraftUpdate,
    RoutineId::St03FaceUpdate,
    RoutineId::St03FlutterUpdate,
    RoutineId::St03LightsUpdate,
    RoutineId::St03PartBlinkerUpdate,
    RoutineId::St03PieceCarrierUpdate,
    RoutineId::St03RollUpdate,
    RoutineId::St03SceneryUpdate,
    RoutineId::St03ScreenUpdate,
    RoutineId::St03SpeakerUpdate,
    RoutineId::St03StanderUpdate,
    RoutineId::St03TalkerUpdate,
    RoutineId::St03WalkerUpdate,
    RoutineId::St03WatcherUpdate,
    RoutineId::St09BystanderUpdate,
    RoutineId::St14DroppingPuppetUpdate,
    RoutineId::St14RisingPuppetUpdate,
    RoutineId::St17TalkerUpdate,
    RoutineId::St18TalkerUpdate,
    RoutineId::St19CutsceneDroneUpdate,
    RoutineId::St19MachineUpdate,
    RoutineId::St1cCuedFigureUpdate,
    RoutineId::St1cFaceFigureUpdate,
    RoutineId::St1cFallingFigureUpdate,
    RoutineId::St1cFidgetingFigureUpdate,
    RoutineId::St1cFlyerUpdate,
    RoutineId::St1cGlowingFigureUpdate,
    RoutineId::St1cGroundedHopperUpdate,
    RoutineId::St1cHopperUpdate,
    RoutineId::St1cHoppingFlyerUpdate,
    RoutineId::St1cIdleFigureUpdate,
    RoutineId::St1cMeshFlipFigureUpdate,
    RoutineId::St1cPosingFigureUpdate,
    RoutineId::St1cRisingPropUpdate,
    RoutineId::St1cVanishingWalkerUpdate,
    RoutineId::St1cWalkingFigureUpdate,
    RoutineId::St24GateGuardianUpdate,
    RoutineId::St25Script1cFigureUpdate,
    RoutineId::St25TankWreckUpdate,
    RoutineId::St47PersonUpdate,
    RoutineId::St47VisitorUpdate,
    RoutineId::St49DropshipUpdate,
    RoutineId::StandingCastUpdate,
    RoutineId::StillFigureCastUpdate,
    RoutineId::TakeOffCastUpdate,
    RoutineId::TalkerUpdate,
    RoutineId::TimedCastUpdate,
    RoutineId::TownStandingCastUpdate,
};

} // namespace

bool netLocalOnly(RoutineId cls) { return kCutsceneCast.contains(cls); }

bool netSharedEntry(uint32_t ownerShape, uint32_t attack) {
    const uint32_t group = (ownerShape >> kHitGroupShift) & kHitGroupMask;
    if (group == kHitGroupHeavyThrown) return true; // the enemies' shots (game/enemy_shot's group)
    return group == kHitGroupBodies && (attack & kHitDamageMask) != 0;
}

namespace {
using Place = std::pair<int16_t, int16_t>;
std::map<Place, int32_t> gFireSteps; // client: the host's strengths not taken yet
std::map<Place, int> gFireHits;      // host: the clients' damage not applied yet
} // namespace

void fireApplyStep(const FireStep& s) { gFireSteps[{s.x, s.z}] = s.strength; }

std::optional<int32_t> takeFireStep(int16_t x, int16_t z) {
    const auto it = gFireSteps.find({x, z});
    if (it == gFireSteps.end()) return std::nullopt;
    const int32_t s = it->second;
    gFireSteps.erase(it);
    return s;
}

void fireQueueHit(int16_t x, int16_t z, int damage) { gFireHits[{x, z}] += damage; }

int takeFireHits(int16_t x, int16_t z) {
    const auto it = gFireHits.find({x, z});
    if (it == gFireHits.end()) return 0;
    const int d = it->second;
    gFireHits.erase(it);
    return d;
}

void firesReset() {
    gFireSteps.clear();
    gFireHits.clear();
}

NetWorldHooks& netWorldHooks() {
    static NetWorldHooks h;
    return h;
}

bool playerDown() { return player().state == uint8_t(PlayerState::Dead); }

uint32_t netPlayerHitAttack(uint32_t attack, int scalePercent) {
    const uint32_t damage = attack & kHitDamageMask;
    const uint32_t scaled = damage == 0 ? 0 : std::max<uint32_t>(1, damage * uint32_t(scalePercent) / 100);
    return (attack & ~kHitDamageMask) | scaled | kHitAttackSolid;
}

NetStory liveStory() {
    NetStory s;
    s.story = gameState().story;
    s.nextStory = gameState().nextStory;
    s.flags = storyFlags();
    return s;
}

void showStory(const NetStory& s) {
    gameState().story = s.story;
    gameState().nextStory = s.nextStory;
    const auto local = [](uint32_t flag) { return storyFlag(flag); };
    const bool menuOpen = local(kFlagStoryMenuOpen), rescue = local(kFlagGameOverRescue);
    const uint8_t transition = storyFlagByte(kFlagTransitionArmed); // 0x700..0x707: the transition's, the door's
    storyFlags() = s.flags;
    storyFlagByte(kFlagTransitionArmed) = transition;
    menuOpen ? setStoryFlag(kFlagStoryMenuOpen) : clearStoryFlag(kFlagStoryMenuOpen);
    rescue ? setStoryFlag(kFlagGameOverRescue) : clearStoryFlag(kFlagGameOverRescue);
}

std::optional<NetStory>& netHostStory() {
    static std::optional<NetStory> s;
    return s;
}
std::optional<NetStory>& netOwnStory() {
    static std::optional<NetStory> s;
    return s;
}

void netStoryLoaded() {
    if (!netHostStory()) return;
    netOwnStory() = liveStory();
    showStory(*netHostStory());
}

} // namespace od::port
