// The native records of this area in the exe layout (test-only).
#include "translator/actor_layout.hpp"
#include "content/intro/attract_effects.hpp"
#include "content/intro/attract_effects_state.hpp"
#include "content/intro/burning_data_state.hpp"
#include "content/intro/crash_landing.hpp"
#include "content/intro/crash_landing_cast_state.hpp"
#include "content/intro/crash_landing_state.hpp"
#include "content/intro/fire_mission.hpp"
#include "content/intro/opening_cast_state.hpp"
#include "content/intro/opening_effects.hpp"
#include "content/intro/st00_figures_state.hpp"
#include "content/intro/st02_attract_cast_state.hpp"
#include "content/intro/st03_attract_cast_state.hpp"
#include "content/intro/st39_script68_state.hpp"
#include "content/intro/st4a_event_triggers_state.hpp"

namespace od::test {
using namespace od::port;

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::BurningFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0x74, s.hitBox);
    layoutValue(v, a + 0x7c, s.strength);
    layoutValue(v, a + 0x80, s.fullStrength);
    layoutValue(v, a + 0x84, s.frame);
    layoutValue(v, a + 0x85, s.frameTicks);
    layoutValue(v, a + 0x86, s.timer);
    layoutValue(v, a + 0x88, s.flicker);
    layoutValue(v, a + 0x8c, s.flame);
    layoutValue(v, a + 0x90, s.flickerTicks);
    layoutValue(v, a + 0x92, s.swing);
    layoutValue(v, a + 0x94, s.emberTimer);
    layoutValue(v, a + 0x96, s.crackleTimer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ChipFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.direction);
    layoutValue(v, a + 0xd, s.reachParam);
    layoutValue(v, a + 0xe, s.liftParam);
    layoutValue(v, a + 0xf, s.lifeParam);
    layoutValue(v, a + 0x16, s.clutParam);
    layoutValue(v, a + 0x18, s.age);
    layoutValue(v, a + 0x1a, s.life);
    layoutValue(v, a + 0x1c, s.yaw);
    layoutValue(v, a + 0x20, s.lift);
    layoutValue(v, a + 0x22, s.reach);
    layoutValue(v, a + 0x1e, s.riseTick);
    layoutValue(v, a + 0x1f, s.riseTicks);
    layoutValue(v, a + 0x24, s.clutWord);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::CloudRingFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.altClut);
    layoutValue(v, a + 0xf, s.textureEntry);
    layoutValue(v, a + 0xc, s.paramsWord_);
    for (size_t i = 0; i < s.angles.size(); ++i) layoutValue(v, a + 0x74 + uint32_t(i) * (2), s.angles[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::CraftFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0x14c, s.castFlags);
    layoutValue(v, a + 0x14d, s.animKey);
    layoutValue(v, a + 0x14e, s.exhaustClock);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::CrashFlashFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.size);
    layoutValue(v, a + 0xb, s.shadeA);
    layoutValue(v, a + 0xe, s.shadeB);
    layoutValue(v, a + 0xf, s.shadeC);
    for (size_t i = 0; i < s.rays.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.rays[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::EngineFlameFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.ownerSlot);
    layoutValue(v, a + 0xd, s.follows);
    for (size_t i = 0; i < s.drawParams.size(); ++i) layoutValue(v, a + 0xe + uint32_t(i) * (1), s.drawParams[i]);
    layoutValue(v, a + 0x16, s.yaw);
    layoutValue(v, a + 0x18, s.owner);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::FallingPieceFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xbc, s.ticks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::FireEffectFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0x10, s.pos);
    layoutValue(v, a + 0x16, s.riseSpeed);
    layoutValue(v, a + 0x18, s.hitBox);
    layoutValue(v, a + 0x20, s.keyTicks);
    layoutValue(v, a + 0x22, s.size);
    layoutValue(v, a + 0x24, s.key);
    layoutValue(v, a + 0x25, s.spin);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::FlameFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.stopped);
    layoutValue(v, a + 0xd, s.emitTimer);
    layoutValue(v, a + 0xe, s.flickerPhase);
    for (size_t i = 0; i < s.tongues.size(); ++i) layoutValue(v, a + 0x28 + uint32_t(i) * (8), s.tongues[i]);
    layoutValue(v, a + 0xb8, s.flickerTimer);
    for (size_t i = 0; i < s.afterFlicker.size(); ++i) layoutValue(v, a + 0xb9 + uint32_t(i) * (1), s.afterFlicker[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::FlameTongue& s) {
    using namespace od::port;
    layoutValue(v, a + 0, s.rise);
    layoutValue(v, a + 2, s.dx);
    layoutValue(v, a + 3, s.dz);
    layoutValue(v, a + 4, s.height);
    layoutValue(v, a + 6, s.frame);
    layoutValue(v, a + 7, s.spin);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::FuseFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.steps);
    layoutValue(v, a + 0xd, s.life);
    layoutValue(v, a + 0xe, s.flashParam);
    layoutValue(v, a + 0x10, s.fromX);
    layoutValue(v, a + 0x12, s.fromZ);
    layoutValue(v, a + 0x14, s.toX);
    layoutValue(v, a + 0x16, s.toZ);
    layoutValue(v, a + 0x20, s.stepX);
    layoutValue(v, a + 0x22, s.stepZ);
    layoutValue(v, a + 0x18, s.x);
    layoutValue(v, a + 0x1c, s.z);
    layoutValue(v, a + 0x24, s.age);
    layoutValue(v, a + 0x26, s.flashWord);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::OwnerPuffFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.ownerSlot);
    layoutValue(v, a + 0xd, s.thrown);
    layoutValue(v, a + 0x18, s.age);
    layoutValue(v, a + 0x1c, s.velocity);
    layoutValue(v, a + 0x22, s.growth);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::RiderSparkFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.ownerSlot);
    layoutValue(v, a + 0xd, s.colour);
    layoutValue(v, a + 0xe, s.ticksParam);
    layoutValue(v, a + 0xf, s.unusedParam);
    layoutValue(v, a + 0x16, s.drawParam);
    layoutValue(v, a + 0x1c, s.age);
    layoutValue(v, a + 0x1e, s.ticks);
    layoutValue(v, a + 0x18, s.owner);
    layoutValue(v, a + 0x20, s.drawWord);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::RingFlashFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.radius);
    layoutValue(v, a + 0x18, s.radiusStep);
    layoutValue(v, a + 0x1a, s.phaseTicks);
    layoutValue(v, a + 0x1c, s.wobbleSeed);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ThrownPuffFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.kind);
    layoutValue(v, a + 0xd, s.lifeParam);
    layoutValue(v, a + 0x16, s.yaw);
    layoutValue(v, a + 0x18, s.age);
    layoutValue(v, a + 0x1a, s.life);
    layoutValue(v, a + 0x22, s.size);
    layoutValue(v, a + 0x1c, s.velocity);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::attract_effects_state::BoltPoint& s) {
    using namespace od::port::attract_effects_state;
    layoutValue(v, a + 0, s.x);
    layoutValue(v, a + 2, s.y);
    layoutValue(v, a + 4, s.z);
    layoutValue(v, a + 6, s.fade);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::attract_effects_state::LightningFields& s) {
    using namespace od::port::attract_effects_state;
    layoutValue(v, a + 0xc, s.firstSequence);
    layoutValue(v, a + 0xd, s.endSequence);
    layoutValue(v, a + 0x28, s.timer);
    for (size_t i = 0; i < s.points.size(); ++i) layoutValue(v, a + 0x2c + uint32_t(i) * (8), s.points[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::burning_data_state::DataFields& s) {
    using namespace od::port::burning_data_state;
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.flame);
    layoutValue(v, a + 0x158, s.flags);
    layoutValue(v, a + 0x15a, s.timer);
    layoutValue(v, a + 0x15c, s.path);
    layoutValue(v, a + 0x15d, s.pathStep);
    layoutValue(v, a + 0x15e, s.cryTimer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::crash_landing_cast_state::TrailingPropFields& s) {
    using namespace od::port::crash_landing_cast_state;
    layoutValue(v, a + 0x14c, s.trailClock);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::crash_landing_state::SkyShipFields& s) {
    using namespace od::port::crash_landing_state;
    layoutValue(v, a + 0xc, s.kind);
    layoutValue(v, a + 0xbc, s.raisedY);
    layoutValue(v, a + 0xc0, s.groundY);
    layoutValue(v, a + 0xc4, s.tick);
    layoutValue(v, a + 0xc6, s.lowerTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::opening_cast_state::CrewFields& s) {
    using namespace od::port::opening_cast_state;
    layoutValue(v, a, static_cast<od::port::CastFields&>(s));
    layoutValue(v, a + 0x15e, s.frameTicks);
    layoutValue(v, a + 0x164, s.shadowWidth);
    layoutValue(v, a + 0x166, s.shadowLength);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::opening_cast_state::OpeningFlutterFields& s) {
    using namespace od::port::opening_cast_state;
    layoutValue(v, a + 0x14c, s.streakClock);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::opening_cast_state::TalkerFields& s) {
    using namespace od::port::opening_cast_state;
    layoutValue(v, a, static_cast<od::port::CastFields&>(s));
    layoutValue(v, a + 0x15e, s.explanation);
    layoutValue(v, a + 0x160, s.ticks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st00_figures_state::TargetFigureFields& s) {
    using namespace od::port::st00_figures_state;
    layoutValue(v, a + 0x14c, s.hitBox);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st00_figures_state::WatchingFigureFields& s) {
    using namespace od::port::st00_figures_state;
    layoutValue(v, a + 0xc, s.blinkClock);
    layoutValue(v, a + 0xd, s.blinkCell);
    layoutValue(v, a + 0x154, s.headYaw);
    layoutValue(v, a + 0x156, s.headPitch);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st02_attract_cast_state::FaceTracksFields& s) {
    using namespace od::port::st02_attract_cast_state;
    layoutValue(v, a + 0x14c, s.tracks); // the two texture tracks at the class block's start, then the hold block
    layoutValue(v, a + 0x15c, s.hold);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st02_attract_cast_state::HoldBlock& s) {
    using namespace od::port::st02_attract_cast_state;
    layoutValue(v, a + 0, s.flags);
    layoutValue(v, a + 1, s.frame);
    layoutValue(v, a + 2, s.startKey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st02_attract_cast_state::HoldOnlyFields& s) {
    using namespace od::port::st02_attract_cast_state;
    layoutValue(v, a + 0x14c, s.hold);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st02_attract_cast_state::PartTrackFields& s) {
    using namespace od::port::st02_attract_cast_state;
    layoutValue(v, a + 0x14c, s.track); // the part track at the class block's start, then the hold block
    layoutValue(v, a + 0x154, s.hold);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st03_attract_cast_state::CarrierFields& s) {
    using namespace od::port::st03_attract_cast_state;
    layoutValue(v, a, static_cast<od::port::CastFields&>(s));
    layoutValue(v, a + 0x163, s.shadowLevel);
    layoutValue(v, a + 0x15e, s.turnClock);
    layoutValue(v, a + 0x164, s.shadowWidth);
    layoutValue(v, a + 0x166, s.shadowLength);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st03_attract_cast_state::CraftFields& s) {
    using namespace od::port::st03_attract_cast_state;
    layoutValue(v, a + 0x158, s.humParam);
    layoutValue(v, a + 0xc, s.bodySlot);
    layoutValue(v, a + 0x14c, s.flags);
    layoutValue(v, a + 0x14d, s.startKey);
    layoutValue(v, a + 0x150, s.heldKey);
    layoutValue(v, a + 0x151, s.rollRamp);
    layoutValue(v, a + 0x152, s.hum);
    layoutValue(v, a + 0x154, s.humHandle);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st03_attract_cast_state::FlutterFields& s) {
    using namespace od::port::st03_attract_cast_state;
    layoutValue(v, a + 0x14c, s.bobClock);
    layoutValue(v, a + 0x14e, s.bobBase);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st03_attract_cast_state::PartBlinkerFields& s) {
    using namespace od::port::st03_attract_cast_state;
    layoutValue(v, a, static_cast<od::port::CastFields&>(s));
    layoutValue(v, a + 0x169, s.applyValue);
    layoutValue(v, a + 0x16a, s.index);
    layoutValue(v, a + 0x16b, s.previousIndex);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4a_event_triggers_state::EventTriggerFields& s) {
    using namespace od::port::st4a_event_triggers_state;
    layoutValue(v, a + 0xc, s.message);
    layoutValue(v, a + 0xe, s.storyLimit);
    layoutValue(v, a + 0xf, s.radius);
    layoutValue(v, a + 0x16, s.flag);
    for (size_t i = 0; i < s.args.size(); ++i) layoutValue(v, a + 0x10 + uint32_t(i) * (2), s.args[i]);
}

bool classes_intro(LayoutVisitor& v, uint32_t a, ActorClassState& s) {
    if (typeid(s) == typeid(od::port::BurningFields)) { layoutValue(v, a, static_cast<od::port::BurningFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::burning_data_state::DataFields)) { layoutValue(v, a, static_cast<od::port::burning_data_state::DataFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::opening_cast_state::TalkerFields)) { layoutValue(v, a, static_cast<od::port::opening_cast_state::TalkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4a_event_triggers_state::EventTriggerFields)) { layoutValue(v, a, static_cast<od::port::st4a_event_triggers_state::EventTriggerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ChipFields)) { layoutValue(v, a, static_cast<od::port::ChipFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st00_figures_state::WatchingFigureFields)) { layoutValue(v, a, static_cast<od::port::st00_figures_state::WatchingFigureFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::CrashFlashFields)) { layoutValue(v, a, static_cast<od::port::CrashFlashFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::FallingPieceFields)) { layoutValue(v, a, static_cast<od::port::FallingPieceFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::CraftFields)) { layoutValue(v, a, static_cast<od::port::CraftFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st02_attract_cast_state::FaceTracksFields)) { layoutValue(v, a, static_cast<od::port::st02_attract_cast_state::FaceTracksFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::FireEffectFields)) { layoutValue(v, a, static_cast<od::port::FireEffectFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st00_figures_state::TargetFigureFields)) { layoutValue(v, a, static_cast<od::port::st00_figures_state::TargetFigureFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ThrownPuffFields)) { layoutValue(v, a, static_cast<od::port::ThrownPuffFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::OwnerPuffFields)) { layoutValue(v, a, static_cast<od::port::OwnerPuffFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::FuseFields)) { layoutValue(v, a, static_cast<od::port::FuseFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st02_attract_cast_state::PartTrackFields)) { layoutValue(v, a, static_cast<od::port::st02_attract_cast_state::PartTrackFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::attract_effects_state::LightningFields)) { layoutValue(v, a, static_cast<od::port::attract_effects_state::LightningFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::FlameFields)) { layoutValue(v, a, static_cast<od::port::FlameFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::RingFlashFields)) { layoutValue(v, a, static_cast<od::port::RingFlashFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::RiderSparkFields)) { layoutValue(v, a, static_cast<od::port::RiderSparkFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st02_attract_cast_state::HoldOnlyFields)) { layoutValue(v, a, static_cast<od::port::st02_attract_cast_state::HoldOnlyFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::crash_landing_cast_state::TrailingPropFields)) { layoutValue(v, a, static_cast<od::port::crash_landing_cast_state::TrailingPropFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::EngineFlameFields)) { layoutValue(v, a, static_cast<od::port::EngineFlameFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::crash_landing_state::SkyShipFields)) { layoutValue(v, a, static_cast<od::port::crash_landing_state::SkyShipFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st03_attract_cast_state::CarrierFields)) { layoutValue(v, a, static_cast<od::port::st03_attract_cast_state::CarrierFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::opening_cast_state::OpeningFlutterFields)) { layoutValue(v, a, static_cast<od::port::opening_cast_state::OpeningFlutterFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st03_attract_cast_state::PartBlinkerFields)) { layoutValue(v, a, static_cast<od::port::st03_attract_cast_state::PartBlinkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::opening_cast_state::CrewFields)) { layoutValue(v, a, static_cast<od::port::opening_cast_state::CrewFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st03_attract_cast_state::CraftFields)) { layoutValue(v, a, static_cast<od::port::st03_attract_cast_state::CraftFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::CloudRingFields)) { layoutValue(v, a, static_cast<od::port::CloudRingFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st03_attract_cast_state::FlutterFields)) { layoutValue(v, a, static_cast<od::port::st03_attract_cast_state::FlutterFields&>(s)); return true; }
    return false;
}
} // namespace od::test
