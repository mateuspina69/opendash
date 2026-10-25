// The actor classes of content/late_game in the class registry (game/content_registry), registered by
// content/registry.cpp. Moved from the registry table of game/actors.cpp (same entries, same order).
#include "content/registry.hpp"

#include "content/late_game/st45_spawner.hpp"

#include "content/late_game/st45_boss.hpp"
#include "content/late_game/ch4_story.hpp"
#include "content/late_game/end_cast.hpp"
#include "content/late_game/late_cast.hpp"
#include "content/late_game/st40_fx.hpp"
#include "content/late_game/st43_roller.hpp"
#include "content/late_game/st4b.hpp"
#include "content/late_game/st4b_fx.hpp"
#include "content/late_game/st4b_scripts.hpp"
#include "content/late_game/st4c_boss.hpp"
#include "content/late_game/st4c_story.hpp"
#include "content/late_game/st53_story.hpp"
#include "content/late_game/st54_cast.hpp"
#include "content/late_game/st54_props.hpp"
#include "content/late_game/st54_puffs.hpp"
#include "content/late_game/st54_takeoff.hpp"
#include "content/late_game/st59.hpp"
#include "content/late_game/st5c.hpp"
#include "content/late_game/st5c_fx.hpp"
#include "content/late_game/st5c_fx2.hpp"
#include "content/late_game/st5c_fx3.hpp"
#include "content/yosyonke/st0a_fx.hpp"

namespace od::content {

using namespace od::port;

void registerLateGameClasses(ContentRegistry& r) {
    r.addClasses({
        // the pool-e0 type-0xa enemy spawners of the area's stages
        {RoutineId::SpawnerSt45Update, &spawnerSt45Update},
        {RoutineId::RaceActorUpdate, &raceActorUpdate},
        {RoutineId::EyeTrailUpdate, &eyeTrailUpdate},
        {RoutineId::VolleyShotUpdate, &volleyShotUpdate},
        {RoutineId::ScriptFallingPieceUpdate, &scriptFallingPieceUpdate},
        {RoutineId::Script59FaceRowCastUpdate, &script59FaceRowCastUpdate},
        {RoutineId::Script59WalkInCastUpdate, &script59WalkInCastUpdate},
        {RoutineId::St4bFlyingBossUpdate, &st4bFlyingBossUpdate},
        {RoutineId::St4bCirclingPieceUpdate, &st4bCirclingPieceUpdate},
        {RoutineId::St4bSpinningPieceUpdate, &st4bSpinningPieceUpdate},
        {RoutineId::BossFallingRockUpdate, &bossFallingRockUpdate},
        {RoutineId::BallChargeUpdate, &ballChargeUpdate},
        {RoutineId::BeamChargeUpdate, &beamChargeUpdate},
        {RoutineId::BossBladeUpdate, &bossBladeUpdate},
        {RoutineId::StompShotUpdate, &stompShotUpdate},
        {RoutineId::ChargeBallUpdate, &chargeBallUpdate},
        {RoutineId::St5cBossUpdate, &st5cBossUpdate},
        {RoutineId::BossIntroCastUpdate, &bossIntroCastUpdate},
        {RoutineId::BossAfterImageUpdate, &bossAfterImageUpdate},
        {RoutineId::BossDefeatCastUpdate, &bossDefeatCastUpdate},
        {RoutineId::SparkBurstUpdate, &sparkBurstUpdate},
        {RoutineId::StageDustEffectUpdate, &stageDustEffectUpdate},
        {RoutineId::BeamImpactUpdate, &beamImpactUpdate},
        {RoutineId::ScreenShockRingUpdate, &screenShockRingUpdate},
        {RoutineId::ElectricSparkUpdate, &electricSparkUpdate},
        {RoutineId::BossBeamUpdate, &bossBeamUpdate},
        {RoutineId::BallVisualsUpdate, &ballVisualsUpdate},
        {RoutineId::BallExplosionUpdate, &ballExplosionUpdate},
        {RoutineId::ElysiumBossUpdate, &elysiumBossUpdate},
        {RoutineId::ElysiumOrbitShotUpdate, &elysiumOrbitShotUpdate},
        {RoutineId::ElysiumHomingShotUpdate, &elysiumHomingShotUpdate},
        {RoutineId::ElysiumBoltUpdate, &elysiumBoltUpdate},
        {RoutineId::ElysiumBeamUpdate, &elysiumBeamUpdate},
        {RoutineId::ElysiumSparksUpdate, &elysiumSparksUpdate},
        {RoutineId::ElysiumChargeUpdate, &elysiumChargeUpdate},
        {RoutineId::ElysiumSparklesUpdate, &elysiumSparklesUpdate},
        {RoutineId::ElysiumFlashUpdate, &elysiumFlashUpdate},
        {RoutineId::ElysiumMoteUpdate, &elysiumMoteUpdate},
        {RoutineId::ElysiumFogUpdate, &elysiumFogUpdate},
        {RoutineId::Script60FacedTalkerUpdate, &script60FacedTalkerUpdate},
        {RoutineId::Script61FacedTalkerUpdate, &script61FacedTalkerUpdate},
        {RoutineId::Script60FadingFigureUpdate, &script60FadingFigureUpdate},
        {RoutineId::Script61StandingFigureUpdate, &script61StandingFigureUpdate},
        {RoutineId::Script60WalkerUpdate, &script60WalkerUpdate},
        {RoutineId::Script60MaskedTalkerUpdate, &script60MaskedTalkerUpdate},
        {RoutineId::Script61MaskedTalkerUpdate, &script61MaskedTalkerUpdate},
        {RoutineId::Script60LeadTalkerUpdate, &script60LeadTalkerUpdate},
        {RoutineId::Script61LeadTalkerUpdate, &script61LeadTalkerUpdate},
        {RoutineId::Script60ExtraAUpdate, &script60ExtraAUpdate},
        {RoutineId::Script60ExtraBUpdate, &script60ExtraBUpdate},
        {RoutineId::ElysiumIntroCastUpdate, &elysiumIntroCastUpdate},
        {RoutineId::ElysiumDefeatCastUpdate, &elysiumDefeatCastUpdate},
        {RoutineId::BoneTrailUpdate, &boneTrailUpdate},
        {RoutineId::RayBurstUpdate, &rayBurstUpdate},
        {RoutineId::SwirlingMotesUpdate, &swirlingMotesUpdate},
        {RoutineId::AnimatedSpriteEffectUpdate, &animatedSpriteEffectUpdate},
        {RoutineId::StillSpriteEffectUpdate, &stillSpriteEffectUpdate},
        {RoutineId::EndingTalkerAUpdate, &endingTalkerAUpdate},
        {RoutineId::EndingTalkerBUpdate, &endingTalkerBUpdate},
        {RoutineId::EndingMouthTalkerUpdate, &endingMouthTalkerUpdate},
        {RoutineId::EndingWalkerUpdate, &endingWalkerUpdate},
        {RoutineId::EndingGroupCastUpdate, &endingGroupCastUpdate},
        {RoutineId::EndingGlowCastUpdate, &endingGlowCastUpdate},
        {RoutineId::FinaleCastAUpdate, &finaleCastAUpdate},
        {RoutineId::FinaleCastBUpdate, &finaleCastBUpdate},
        {RoutineId::BobbingPropUpdate, &bobbingPropUpdate},
        {RoutineId::EndingEffectUpdate, &endingEffectUpdate},
        {RoutineId::Script59CuedTalkerAUpdate, &script59CuedTalkerAUpdate},
        {RoutineId::Script59LateStepperUpdate, &script59LateStepperUpdate},
        {RoutineId::Script59SceneTalkerUpdate, &script59SceneTalkerUpdate},
        {RoutineId::Script59FlyerUpdate, &script59FlyerUpdate},
        {RoutineId::Script59IdleOrStepperUpdate, &script59IdleOrStepperUpdate},
        {RoutineId::Script59SceneWalkerUpdate, &script59SceneWalkerUpdate},
        {RoutineId::Script59CuedTalkerBUpdate, &script59CuedTalkerBUpdate},
        {RoutineId::Script59LateTalkerUpdate, &script59LateTalkerUpdate},
        {RoutineId::StageEffectFromVariant2Update, &stageEffectFromVariant2Update},
        {RoutineId::Script6fActionTalkerUpdate, &script6fActionTalkerUpdate},
        {RoutineId::Script6fHoldingTalkerUpdate, &script6fHoldingTalkerUpdate},
        {RoutineId::Script6fPacerUpdate, &script6fPacerUpdate},
        {RoutineId::Script6fRowTalkerUpdate, &script6fRowTalkerUpdate},
        {RoutineId::Script6fMouthTalkerUpdate, &script6fMouthTalkerUpdate},
        {RoutineId::Script6fCirclerUpdate, &script6fCirclerUpdate},
        {RoutineId::Script6fSparkTalkerUpdate, &script6fSparkTalkerUpdate},
        {RoutineId::TeleporterEffectUpdate, &teleporterEffectUpdate},
        {RoutineId::EffectMoverUpdate, &effectMoverUpdate},
        {RoutineId::St45BreathPuffUpdate, &st45BreathPuffUpdate},
        {RoutineId::St45SpitBlobUpdate, &st45SpitBlobUpdate},
        {RoutineId::St45MouthShotUpdate, &st45MouthShotUpdate},
        {RoutineId::St45ShockWaveUpdate, &st45ShockWaveUpdate},
        {RoutineId::St45BossUpdate, &st45BossUpdate},
        {RoutineId::Script62TalkerAUpdate, &script62TalkerAUpdate},
        {RoutineId::Script62TalkerBUpdate, &script62TalkerBUpdate},
        {RoutineId::BrakingTrainUpdate, &brakingTrainUpdate},
        {RoutineId::RisingSmokeColumnUpdate, &risingSmokeColumnUpdate},
        {RoutineId::BossRetreatCastUpdate, &bossRetreatCastUpdate},
        {RoutineId::BossEscapeCastUpdate, &bossEscapeCastUpdate},
        {RoutineId::StillFigureCastUpdate, &stillFigureCastUpdate},
        {RoutineId::SwingingModelUpdate, &swingingModelUpdate},
        {RoutineId::TakeoffArcUpdate, &takeoffArcUpdate},
        {RoutineId::RollerUpdate, &rollerUpdate},
        {RoutineId::PuffEmitterUpdate, &puffEmitterUpdate},
    });
}

} // namespace od::content
