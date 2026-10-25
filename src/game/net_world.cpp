// The game's side of netplay's shared world (see net_world.hpp).
#include "game/net_world.hpp"

#include <set>

#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"

namespace od::port {

namespace {

// the cutscene cast (the classes named for their script, cast, scene or talker role): every machine runs its own
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
    RoutineId::EntranceCastUpdate,
    RoutineId::EscapeTimerUpdate,
    RoutineId::FadingCastUpdate,
    RoutineId::FinaleCastAUpdate,
    RoutineId::FinaleCastBUpdate,
    RoutineId::GaugeCastUpdate,
    RoutineId::GlydeBaseSceneBirdbotUpdate,
    RoutineId::GuardianBeatenUpdate,
    RoutineId::GuardianDissolvesUpdate,
    RoutineId::GuardianWakesUpdate,
    RoutineId::JoesLabCastUpdate,
    RoutineId::JunkShopCastUpdate,
    RoutineId::KeyScriptFigureUpdate,
    RoutineId::MandaCast2GuardianUpdate,
    RoutineId::MandaCastGuardianUpdate,
    RoutineId::MandaOutroGuardianUpdate,
    RoutineId::OpeningTalkerUpdate,
    RoutineId::PirateBaseCastUpdate,
    RoutineId::PirateCastUpdate,
    RoutineId::PlayerScriptedUpdate,
    RoutineId::QuizCastUpdate,
    RoutineId::ResultSceneDataUpdate,
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
    RoutineId::St02SceneryUpdate,
    RoutineId::St02TalkerUpdate,
    RoutineId::St03SceneryUpdate,
    RoutineId::St03TalkerUpdate,
    RoutineId::St14DroppingPuppetUpdate,
    RoutineId::St14RisingPuppetUpdate,
    RoutineId::St17TalkerUpdate,
    RoutineId::St18TalkerUpdate,
    RoutineId::St19CutsceneDroneUpdate,
    RoutineId::St25Script1cFigureUpdate,
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

NetWorldHooks& netWorldHooks() {
    static NetWorldHooks h;
    return h;
}

bool playerDown() { return player().state == uint8_t(PlayerState::Dead); }

} // namespace od::port
