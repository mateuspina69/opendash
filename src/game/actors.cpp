// The actor system's update loops, class dispatch and registry (see actors.hpp).
#include "game/actor_state.hpp"
#include "game/actors.hpp"
#include "game/port_gap.hpp"

#include <cstdio>
#include <map>
#include <stdexcept>

#include "game/actor_pools.hpp"
#include "game/blizzard_layer.hpp"
#include "game/bomb.hpp"
#include "game/carry.hpp"
#include "game/charge_glow.hpp"
#include "game/clut_animation.hpp"
#include "game/content_registry.hpp"
#include "game/door_actor.hpp"
#include "game/dust_puff.hpp"
#include "game/enemy_shot.hpp"
#include "game/explosions.hpp"
#include "game/floating_device.hpp"
#include "game/footstep_dust.hpp"
#include "game/field_hoverbot.hpp"
#include "game/field_reaverbot.hpp"
#include "game/birdbot_down.hpp"
#include "game/birdbot_soldier.hpp"
#include "game/glow_trails.hpp"
#include "game/guardian_reaverbot.hpp"
#include "game/game_state.hpp"
#include "game/hit_effects.hpp"
#include "game/lens_flare.hpp"
#include "game/lift_box.hpp"
#include "game/lobbed_shot.hpp"
#include "game/light_column.hpp"
#include "game/line_of_sight.hpp"
#include "game/missile_trail.hpp"
#include "game/pickups.hpp"
#include "game/placed_models.hpp"
#include "game/reaverbot_turret.hpp"
#include "game/reaverbot_walker.hpp"
#include "game/room_uv_animation.hpp"
#include "game/servbot.hpp"
#include "game/shots/player_shots.hpp"
#include "game/shockwave.hpp"
#include "game/sign.hpp"
#include "game/sky_arc.hpp"
#include "game/snow.hpp"
#include "game/snow_reaverbot.hpp"
#include "game/spark_emitter.hpp"
#include "game/spark_rings.hpp"
#include "game/status_markers.hpp"
#include "game/tank_shell.hpp"
#include "game/throwable.hpp"
#include "game/skid_sparks.hpp"
#include "game/smoke_column.hpp"
#include "game/message_triggers.hpp"
#include "game/suction_effect.hpp"
#include "game/title_model.hpp"
#include "game/town_npc.hpp"
#include "game/actor_movement.hpp"
#include "game/door_leaves.hpp"
#include "game/storm_ribbon.hpp"
#include "game/travel.hpp"
#include "game/turret_bullet.hpp"
#include "game/treasure_chest.hpp"
#include "game/vram_moves.hpp"
#include "game/walker_shot.hpp"
#include "game/wall_crumble.hpp"
#include "game/wall_debris.hpp"

namespace od::port {

namespace {

// ---- class tables ----------------------------------------------------------------------------------------------------
// (the exe's global class tables: actors.hpp)
constexpr uint8_t kStatusMenuSwapFirst
 = 2, kStatusMenuSwapLast = 3; // the status menu's steps with its own table

// the handler of (type, sub-type) in a two-level class table set (0: none)
uint32_t handlerOf(const DataPtr<>& set, uint8_t type, uint8_t subtype) {
    return classTableEntry(classTableEntry(set.value(), type), subtype);
}
// the routine a class table entry names (the state's table pointer: the source translates it)
RoutineRef classRoutine(uint32_t pointer) { return stateRoutine(pointer); }

// ---- registry -------------------------------------------------------------------------------------------------------
// the ported classes by update routine: the game's own, then the content's (built once)
const std::map<RoutineId, ActorUpdateFn>& registry() {
    static const std::map<RoutineId, ActorUpdateFn> r = [] {
    // the game's own classes, then the content's (content/*/register.cpp, game/content_registry); a routine
    // registered twice throws
    ContentRegistry t;
    t.addClasses({
        {RoutineId::TownNpcUpdate, &townNpcUpdate},
        {RoutineId::DoorActorUpdate, &doorActorUpdate},
        {RoutineId::DoorLeafHingedUpdate, &doorLeafUpdate},
        {RoutineId::DoorLeafDoubleUpdate, &doorLeafUpdate},
        {RoutineId::DoorLeafSlidingUpdate, &doorLeafUpdate},
        {RoutineId::SignUpdate, &signUpdate},
        {RoutineId::SnowUpdate, &snowUpdate},
        {RoutineId::WallCrumbleUpdate, &wallCrumbleUpdate},
        {RoutineId::BombUpdate, &bombUpdate},
        {RoutineId::LiftBoxUpdate, &liftBoxUpdate},
        {RoutineId::RiderFollowUpdate, &riderFollowUpdate},
        {RoutineId::ServbotUpdate, &servbotUpdate},
        {RoutineId::TownKickUpdate, &townKickUpdate},
        {RoutineId::BusterShotUpdate, &busterShotUpdate},
        {RoutineId::HitSparkUpdate, &hitSparkUpdate},
        {RoutineId::BurstUpdate, &burstUpdate},
        {RoutineId::PickupUpdate, &pickupUpdate},
        {RoutineId::ExplosionUpdate, &explosionUpdate},
        {RoutineId::HurtSparksUpdate, &hurtSparksUpdate},
        {RoutineId::DrainBurnMarkerUpdate, &drainBurnMarkerUpdate},
        {RoutineId::SlowMarkerUpdate, &slowMarkerUpdate},
        {RoutineId::PlacedModelUpdate, &placedModelUpdate},
        {RoutineId::DarkTrailUpdate, &darkTrailUpdate},
        {RoutineId::ThrowableUpdate, &throwableUpdate},
        {RoutineId::TurningModelUpdate, &turningModelUpdate},
        {RoutineId::FlashBurstUpdate, &flashBurstUpdate},
        {RoutineId::WallDebrisUpdate, &wallDebrisUpdate},
        {RoutineId::ChargeMarkerUpdate, &chargeMarkerUpdate},
        {RoutineId::SparkSprayUpdate, &sparkSprayUpdate},
        {RoutineId::EnergyOrbUpdate, &energyOrbUpdate},
        {RoutineId::ShellCasingUpdate, &shellCasingUpdate},
        {RoutineId::SparkleUpdate, &sparkleUpdate},
        {RoutineId::ShockWaveUpdate, &shockWaveUpdate},
        {RoutineId::CrystalShotUpdate, &crystalShotUpdate},
        {RoutineId::DrillConeUpdate, &drillConeUpdate},
        {RoutineId::ExtinguisherShotUpdate, &extinguisherShotUpdate},
        {RoutineId::BladeArmShotUpdate, &bladeArmShotUpdate},
        {RoutineId::ExtinguisherMistUpdate, &extinguisherMistUpdate},
        {RoutineId::DustPuffUpdate, &dustPuffUpdate},
        {RoutineId::StormRibbonUpdate, &stormRibbonUpdate},
        {RoutineId::ClutAnimationUpdate, &clutAnimationUpdate},
        {RoutineId::LineOfSightUpdate, &lineOfSightUpdate},
        {RoutineId::RoomUvAnimationUpdate, &roomUvAnimationUpdate},
        {RoutineId::FootstepDustUpdate, &footstepDustUpdate},
        {RoutineId::SkidSparksUpdate, &skidSparksUpdate},
        {RoutineId::BladeTrailUpdate, &bladeTrailUpdate},
        {RoutineId::DrillArmShotUpdate, &drillArmShotUpdate},
        {RoutineId::ShiningLaserShotUpdate, &shiningLaserShotUpdate},
        {RoutineId::ShiningLaserBeamUpdate, &shiningLaserBeamUpdate},
        {RoutineId::LaserWallSparkUpdate, &laserWallSparkUpdate},
        {RoutineId::EnemyShotUpdate, &enemyShotUpdate},
        {RoutineId::MissileTrailUpdate, &missileTrailUpdate},
        {RoutineId::BlastUpdate, &blastUpdate},
        {RoutineId::ScriptedMessageUpdate, &scriptedMessageUpdate},
        {RoutineId::SmokeColumnUpdate, &smokeColumnUpdate},
        {RoutineId::TreasureChestUpdate, &treasureChestUpdate},
        {RoutineId::WaveTileUpdate, &waveTileUpdate},
        {RoutineId::FlipBookTilesUpdate, &flipBookTilesUpdate},
        {RoutineId::StripScrollUpdate, &stripScrollUpdate},
        {RoutineId::LavaFlowUpdate, &lavaFlowUpdate},
        {RoutineId::CrusherShotUpdate, &crusherShotUpdate},
        {RoutineId::BusterCannonShotUpdate, &busterCannonShotUpdate},
        {RoutineId::HyperShellShotUpdate, &hyperShellShotUpdate},
        {RoutineId::HomingMissileShotUpdate, &homingMissileShotUpdate},
        {RoutineId::GroundCrawlerShotUpdate, &groundCrawlerShotUpdate},
        {RoutineId::VacuumArmShotUpdate, &vacuumArmShotUpdate},
        {RoutineId::ReflectorShotUpdate, &reflectorShotUpdate},
        {RoutineId::MachineGunShotUpdate, &machineGunShotUpdate},
        {RoutineId::SpreadBusterShotUpdate, &spreadBusterShotUpdate},
        {RoutineId::HunterSeekerShotUpdate, &hunterSeekerShotUpdate},
        {RoutineId::SuctionUpdate, &suctionUpdate},
        {RoutineId::MachineGunMuzzleUpdate, &machineGunMuzzleUpdate},
        {RoutineId::MachineGunImpactUpdate, &machineGunImpactUpdate},
        {RoutineId::TitleModelUpdate, &titleModelUpdate},
        {RoutineId::LensFlareUpdate, &lensFlareUpdate},
        {RoutineId::SandFlowUpdate, &sandFlowUpdate},
        {RoutineId::TravelIconUpdate, &travelIconUpdate},
        {RoutineId::MessageTriggerUpdate, &messageTriggerUpdate},
        {RoutineId::BlizzardLayerUpdate, &blizzardLayerUpdate},
        {RoutineId::SkyArcUpdate, &skyArcUpdate},
        {RoutineId::FloatingDeviceUpdate, &floatingDeviceUpdate},
        {RoutineId::LightGlowUpdate, &lightGlowUpdate},
        {RoutineId::LightColumnUpdate, &lightColumnUpdate},
        {RoutineId::RoomFlagClutAnimationsUpdate, &roomFlagClutAnimationsUpdate},
        {RoutineId::GuardianReaverbotUpdate, &guardianReaverbotUpdate},
        {RoutineId::GlowTrailUpdate, &glowTrailUpdate},
        {RoutineId::ShockwaveUpdate, &shockwaveUpdate},
        {RoutineId::ShockRingUpdate, &shockRingUpdate},
        {RoutineId::SparkEmitterUpdate, &sparkEmitterUpdate},
        {RoutineId::ReaverbotWalkerUpdate, &reaverbotWalkerUpdate},
        {RoutineId::FieldReaverbotUpdate, &fieldReaverbotUpdate},
        {RoutineId::FieldHoverbotUpdate, &fieldHoverbotUpdate},
        {RoutineId::ReaverbotTurretUpdate, &reaverbotTurretUpdate},
        {RoutineId::TurretBulletUpdate, &turretBulletUpdate},
        {RoutineId::LobbedShotUpdate, &lobbedShotUpdate},
        {RoutineId::ChargeGlowUpdate, &chargeGlowUpdate},
        {RoutineId::SparkRingsUpdate, &sparkRingsUpdate},
        {RoutineId::SnowReaverbotUpdate, &snowReaverbotUpdate},
        {RoutineId::BirdbotSoldierUpdate, &birdbotSoldierUpdate},
        {RoutineId::BirdbotDownUpdate, &birdbotDownUpdate},
        {RoutineId::WalkerShotUpdate, &walkerShotUpdate},
        {RoutineId::TankShellUpdate, &tankShellUpdate},
    });
    for (const auto& [cls, fn] : content().classes) t.addClass(cls, fn);
    return std::move(t.classes);
    }();
    return r;
}

// ---- the pool loops -------------------------------------------------------------------------------------------------
// One loop's pool: its slots (the field's part of the animated pool), the slot views and the loop's cursor
struct PoolLoop {
    uint32_t base;
    size_t count;
    ActorHeader& (*slot)(size_t i);
    uint32_t& cursor;
};
// the slots, views and cursor of one pool loop
PoolLoop poolLoop(ActorPool p) {
    ActorSystemState& s = actorSystem();
    switch (p) {
    case ActorPool::Controllers:
    case ActorPool::ControllersLate:
        return {kControllerPool, ControllerPool::size(), [](size_t i) -> ActorHeader& { return controllerPool()[i]; },
                s.controllerCursor};
    case ActorPool::Animated:
        return {kAnimatedPool, kAnimatedFieldSlots, [](size_t i) -> ActorHeader& { return animatedPool()[i]; },
                s.animatedCursor};
    case ActorPool::Projectiles:
        return {kProjectilePool, ProjectilePool::size(), [](size_t i) -> ActorHeader& { return projectilePool()[i]; },
                s.projectileCursor};
    case ActorPool::Effects:
    case ActorPool::EffectsLate:
        return {kEffectPool, EffectPool::size(), [](size_t i) -> ActorHeader& { return effectPool()[i]; }, s.effectCursor};
    case ActorPool::Statics:
    case ActorPool::StaticsLate:
        return {kStaticPool, StaticPool::size(), [](size_t i) -> ActorHeader& { return staticPool()[i]; }, s.staticCursor};
    case ActorPool::Shots:
        return {kShotPool, ShotPool::size(), [](size_t i) -> ActorHeader& { return shotPool()[i]; }, s.shotCursor};
    }
    throw std::logic_error("actors: unknown pool loop");
}

// The late controller loop's type 0: a controller that waits on a byte of the game state (its parameters hold the
// byte's address; the exe frees it when the byte is 0, else keeps it live)
constexpr uint8_t kByteWatchType = 0;

} // namespace

RoutineRef fieldActorHandler(const ActorHeader& actor, ActorPool pool, bool updating) {
    const GameState& game = gameState();
    const bool swapped = game.gameMode() == GameMode::StatusMenu && game.modeStep >= kStatusMenuSwapFirst &&
                         game.modeStep <= kStatusMenuSwapLast;
    if (pool != ActorPool::Animated || !swapped) return actorHandler(actor, pool, updating);
    if (!(actor.flags & (updating ? actor.runFlags : uint8_t(0xff)) & kActorLive)) return {};
    const uint32_t table = classTableEntry(stageClassTables().fieldAnimated.value(), actor.type);
    return table ? classRoutine(classTableEntry(table, actor.subtype)) : RoutineRef();
}

std::vector<ControllerActor*> controllersRunning(RoutineId cls) {
    std::vector<ControllerActor*> out;
    for (size_t i = 0; i < controllerPool().size(); ++i) {
        ControllerActor& c = controllerPool()[i];
        RoutineRef h = actorHandler(c, ActorPool::Controllers);
        if (!h) h = actorHandler(c, ActorPool::ControllersLate);
        if (h.mapped() && h.is(cls)) out.push_back(&c);
    }
    return out;
}

RoutineRef actorHandler(const ActorHeader& actor, ActorPool pool, bool updating) {
    const uint8_t b0 = actor.flags, b1 = actor.runFlags, b3 = actor.loopFlags, type = actor.type, sub = actor.subtype;
    if (!(b0 & (updating ? b1 : 0xff) & kActorLive)) return {};
    const bool late = (b3 & kActorLateLoop) != 0;
    const bool stageClass = (b3 & kActorStageClass) != 0;
    const StageClassTables& stage = stageClassTables();
    switch (pool) {
    case ActorPool::Controllers:
        if (late || type == kByteWatchType) return {};
        return classRoutine(classTableEntry(stage.controllers.value(), type));
    case ActorPool::ControllersLate:
        if (!late || type == kByteWatchType) return {}; // type 0: freed / kept by the loop itself
        return classRoutine(classTableEntry(stage.controllers.value(), type));
    case ActorPool::Animated: return classRoutine(handlerOf(stage.animated, type, sub));
    case ActorPool::Shots: return globalShotClasses[type];
    case ActorPool::Projectiles:
        return stageClass ? classRoutine(classTableEntry(stage.projectiles.value(), type)) : globalShotClasses[type];
    case ActorPool::Effects:
    case ActorPool::EffectsLate:
        if (late != (pool == ActorPool::EffectsLate)) return {};
        return stageClass ? classRoutine(classTableEntry(stage.effects.value(), type)) : globalEffectClasses[type];
    case ActorPool::Statics:
    case ActorPool::StaticsLate:
        if (late != (pool == ActorPool::StaticsLate)) return {};
        return stageClass ? classRoutine(handlerOf(stage.statics, type, sub)) : globalStaticClasses[type];
    }
    return {};
}

ActorUpdateFn portedActorClass(RoutineId cls) {
    if (cls == RoutineId::None) return nullptr;
    const auto& r = registry();
    const auto it = r.find(cls);
    return it == r.end() ? nullptr : it->second;
}

int updatePool(const PlayerEnv& env, ActorContext& ctx, ActorPool pool) {
    PoolLoop loop = poolLoop(pool);
    ctx.skipped.clear();
    int n = 0;
    for (size_t i = 0; i < loop.count; ++i) {
        ActorHeader& actor = loop.slot(i);
        loop.cursor = actor.at;
        if (pool == ActorPool::ControllersLate && actor.live() && (actor.loopFlags & kActorLateLoop) &&
            actor.type == kByteWatchType) {
            // the byte is game state the exe names by its address; no ported class spawns such a watch, so the
            // byte it names has no native member to read (the port gap names the address)
            notPortedAt("actor pools", "a late controller of type 0 (a byte watch) on the byte at", actor.paramsWord());
        }
        const RoutineRef h = actorHandler(actor, pool);
        if (!h) continue;
        const ActorUpdateFn fn = ctx.disabled.count(h.id()) ? nullptr : portedActorClass(h.id());
        if (!fn) {
            ctx.skipped.push_back(actor.at);
            if (ctx.reported.insert(h.pointerValue()).second && ctx.log) {
                char b[128];
                std::snprintf(b, sizeof b, "PORT GAP: actor class %06x (pool %06x type %u) not ported -- frozen",
                              h.pointerValue(), loop.base, unsigned(actor.type));
                ctx.log(b);
            }
            continue;
        }
        try {
            fn(env, actor.at);
            ++n;
        } catch (const std::logic_error& e) {
            ctx.disabled.insert(h.id());
            if (ctx.log) ctx.log(std::string("PORT GAP: actor class disabled: ") + e.what());
        }
    }
    return n;
}

} // namespace od::port
