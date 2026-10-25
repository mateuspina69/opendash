// The native records of this area in the exe layout (test-only).
#include "translator/actor_layout.hpp"
#include "game/shots/blade_arm.hpp"
#include "game/shots/buster.hpp"
#include "game/shots/buster_cannon_state.hpp"
#include "game/shots/charge_state.hpp"
#include "game/shots/crusher_state.hpp"
#include "game/shots/drill_arm_state.hpp"
#include "game/shots/extinguisher.hpp"
#include "game/shots/extinguisher_state.hpp"
#include "game/shots/ground_crawler_state.hpp"
#include "game/shots/homing_missile_state.hpp"
#include "game/shots/hunter_seeker_state.hpp"
#include "game/shots/hyper_shell_state.hpp"
#include "game/shots/machine_gun_state.hpp"
#include "game/shots/reflector_state.hpp"
#include "game/shots/shining_laser.hpp"
#include "game/shots/shining_laser_state.hpp"
#include "game/shots/shot_common.hpp"
#include "game/shots/spread_buster_state.hpp"

namespace od::test {
using namespace od::port;

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::BladeFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0x74, s.stats);
    layoutValue(v, a + 0x7a, s.ringIndex);
    for (size_t i = 0; i < s.ringPoints.size(); ++i) layoutValue(v, a + 0x7c + uint32_t(i) * (8), s.ringPoints[i]);
    for (size_t i = 0; i < s.ringPitches.size(); ++i) layoutValue(v, a + 0xbc + uint32_t(i) * (2), s.ringPitches[i]);
    for (size_t i = 0; i < s.ringYaws.size(); ++i) layoutValue(v, a + 0xcc + uint32_t(i) * (2), s.ringYaws[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::BladeTrailFields& s) {
    using namespace od::port;
    offsetActorRef(v, a + 0x18, s.swingShot, 0x74);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::BusterShotFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xbc, s.stats);
    layoutValue(v, a + 0xc2, s.deflectTicks);
    layoutValue(v, a + 0xc4, s.lockSlot);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ExtinguisherFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.isPartner);
    layoutValue(v, a + 0x64, s.stats);
    layoutValue(v, a + 0x6a, s.statsPad);
    layoutValue(v, a + 0x6c, s.pointCount);
    layoutValue(v, a + 0x6e, s.tail);
    layoutValue(v, a + 0x70, s.soundTicks);
    layoutValue(v, a + 0x74, s.partner);
    for (size_t i = 0; i < s.points.size(); ++i) layoutValue(v, a + 0x78 + uint32_t(i) * (8), s.points[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::LaserBeamFields& s) {
    using namespace od::port;
    offsetActorRef(v, a + 0x18, s.laserShot, 0x74);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::LaserBlock& s) {
    using namespace od::port;
    layoutValue(v, a, s.stats);
    layoutValue(v, a + 6, s.drawnSteps);
    layoutValue(v, a + 8, s.counter);
    layoutValue(v, a + 0xa, s.humTicks);
    layoutValue(v, a + 0xc, s.beamOut);
    layoutValue(v, a + 0x10, s.muzzle);
    layoutValue(v, a + 0x18, s.aim);
    for (size_t i = 0; i < s.sparkLengths.size(); ++i) layoutValue(v, a + 0x20 + uint32_t(i) * (1), s.sparkLengths[i]);
    for (size_t i = 0; i < s.sparkAngles.size(); ++i) layoutValue(v, a + 0x40 + uint32_t(i) * (1), s.sparkAngles[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ShotStats& s) {
    using namespace od::port;
    layoutValue(v, a + 0x0, s.range);
    layoutValue(v, a + 0x2, s.attack);
    layoutValue(v, a + 0x4, s.level);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::StreamPartnerFields& s) {
    using namespace od::port;
    for (size_t i = 0; i < s.nozzle.size(); ++i) layoutValue(v, a + 0x28 + uint32_t(i) * (2), s.nozzle[i]);
    for (size_t i = 0; i < s.pitches.size(); ++i) layoutValue(v, a + 0x30 + uint32_t(i) * (2), s.pitches[i]);
    for (size_t i = 0; i < s.yaws.size(); ++i) layoutValue(v, a + 0x50 + uint32_t(i) * (2), s.yaws[i]);
    for (size_t i = 0; i < s.hitBoxes.size(); ++i) layoutValue(v, a + 0x70 + uint32_t(i) * (8), s.hitBoxes[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::buster_cannon_state::CannonFields& s) {
    using namespace od::port::buster_cannon_state;
    layoutValue(v, a + 0xbc, s.stats);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::buster_cannon_state::FlashBurstFields& s) {
    using namespace od::port::buster_cannon_state;
    layoutValue(v, a + 0xc, s.size);
    layoutValue(v, a + 0xe, s.ticks);
    for (size_t i = 0; i < s.spikeAngles.size(); ++i) layoutValue(v, a + 0x68 + uint32_t(i) * (1), s.spikeAngles[i]);
    for (size_t i = 0; i < s.spikeLengths.size(); ++i) layoutValue(v, a + 0x78 + uint32_t(i) * (1), s.spikeLengths[i]);
    for (size_t i = 0; i < s.sparkAngles.size(); ++i) layoutValue(v, a + 0x88 + uint32_t(i) * (1), s.sparkAngles[i]);
    for (size_t i = 0; i < s.sparkSpeeds.size(); ++i) layoutValue(v, a + 0xa8 + uint32_t(i) * (1), s.sparkSpeeds[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::charge_state::ChargeMarkerFields& s) {
    using namespace od::port::charge_state;
    layoutValue(v, a + 0x2e, s.phase);
    layoutValue(v, a + 0x30, s.shrink);
    layoutValue(v, a + 0x32, s.soundTicks);
    for (size_t i = 0; i < s.particles.size(); ++i) layoutValue(v, a + 0x34 + uint32_t(i) * (2), s.particles[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::charge_state::ShockWaveFields& s) {
    using namespace od::port::charge_state;
    layoutValue(v, a + 0xc, s.level);
    layoutValue(v, a + 0x84, s.attack);
    layoutValue(v, a + 0x88, s.ticks);
    layoutValue(v, a + 0x8a, s.life);
    layoutValue(v, a + 0x8c, s.fullSize);
    layoutValue(v, a + 0x8e, s.size);
    layoutValue(v, a + 0x74, s.hitBox);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::crusher_state::CrusherFields& s) {
    using namespace od::port::crusher_state;
    layoutValue(v, a + 0xbc, s.holdTicks);
    layoutValue(v, a + 0xc0, s.throwYaw);
    layoutValue(v, a + 0xc4, s.orb);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::crusher_state::EnergyOrbFields& s) {
    using namespace od::port::crusher_state;
    layoutValue(v, a + 0xc, s.burst);
    layoutValue(v, a + 0x84, s.attack);
    layoutValue(v, a + 0x86, s.level);
    layoutValue(v, a + 0x88, s.ticks);
    layoutValue(v, a + 0x8a, s.life);
    layoutValue(v, a + 0x8c, s.soundTicks);
    layoutValue(v, a + 0x90, s.fullSize);
    layoutValue(v, a + 0x92, s.size);
    layoutValue(v, a + 0xc, s.paramsWord_);
    layoutValue(v, a + 0x74, s.hitBox);
    for (size_t i = 0; i < s.rings.size(); ++i) layoutValue(v, a + 0x98 + uint32_t(i) * (8), s.rings[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::drill_arm_state::DrillConeFields& s) {
    using namespace od::port::drill_arm_state;
    layoutValue(v, a + 0xc, s.shot);
    layoutValue(v, a + 0x28, s.strength);
    layoutValue(v, a + 0x2c, s.parent);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::drill_arm_state::DrillFields& s) {
    using namespace od::port::drill_arm_state;
    layoutValue(v, a + 0x7a, s.sparkTicks);
    layoutValue(v, a + 0x7c, s.soundTicks);
    layoutValue(v, a + 0x7e, s.soundPad);
    layoutValue(v, a + 0x74, s.stats);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::extinguisher_state::MistFields& s) {
    using namespace od::port::extinguisher_state;
    layoutValue(v, a + 0x28, s.count);
    layoutValue(v, a + 0x2a, s.alive);
    for (size_t i = 0; i < s.particles.size(); ++i) layoutValue(v, a + 0x2c + uint32_t(i) * (8), s.particles[i]);
    for (size_t i = 0; i < s.motions.size(); ++i) layoutValue(v, a + 0x7c + uint32_t(i) * (8), s.motions[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::extinguisher_state::MistMotion& s) {
    using namespace od::port::extinguisher_state;
    layoutValue(v, a + 6, s.ticks);
    layoutValue(v, a, s.vx);
    layoutValue(v, a + 2, s.vy);
    layoutValue(v, a + 4, s.vz);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::extinguisher_state::MistParticle& s) {
    using namespace od::port::extinguisher_state;
    layoutValue(v, a, s.point);
    layoutValue(v, a + 6, s.yaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ground_crawler_state::CrawlerFields& s) {
    using namespace od::port::ground_crawler_state;
    layoutValue(v, a + 0xbc, s.stats);
    layoutValue(v, a + 0xc2, s.turnRate);
    layoutValue(v, a + 0xc4, s.turnStep);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::homing_missile_state::MissileFields& s) {
    using namespace od::port::homing_missile_state;
    layoutValue(v, a + 0xbc, s.stats);
    layoutValue(v, a + 0xc2, s.wobbleDelay);
    layoutValue(v, a + 0xc4, s.turnRate);
    layoutValue(v, a + 0xc6, s.turnStep);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::hunter_seeker_state::CrystalFields& s) {
    using namespace od::port::hunter_seeker_state;
    layoutValue(v, a + 0x74, s.stats);
    layoutValue(v, a + 0x7c, s.hitBox);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::hunter_seeker_state::HunterFields& s) {
    using namespace od::port::hunter_seeker_state;
    layoutValue(v, a + 0xbc, s.stats);
    layoutValue(v, a + 0xc2, s.crystalTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::hyper_shell_state::DarkTrailFields& s) {
    using namespace od::port::hyper_shell_state;
    layoutValue(v, a + 0xc, s.parent);
    layoutValue(v, a + 0x64, s.length);
    layoutValue(v, a + 0x66, s.head);
    for (size_t i = 0; i < s.points.size(); ++i) layoutValue(v, a + 0x68 + uint32_t(i) * (8), s.points[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::hyper_shell_state::ShellFields& s) {
    using namespace od::port::hyper_shell_state;
    layoutValue(v, a + 0xbc, s.stats);
    layoutValue(v, a + 0xc2, s.homingDelay);
    layoutValue(v, a + 0xc4, s.locked);
    layoutValue(v, a + 0xc6, s.blastTicks);
    layoutValue(v, a + 0xc8, s.blastHalf);
    layoutValue(v, a + 0xcc, s.closest);
    layoutValue(v, a + 0xd0, s.trail);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::hyper_shell_state::TrailPoint& s) {
    using namespace od::port::hyper_shell_state;
    layoutValue(v, a + 0, s.point);
    layoutValue(v, a + 6, s.age);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::machine_gun_state::CasingFields& s) {
    using namespace od::port::machine_gun_state;
    layoutValue(v, a + 0xc0, s.soundTicks);
    layoutValue(v, a + 0xbc, s.life);
    layoutValue(v, a + 0xbe, s.spin);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::machine_gun_state::MachineGunFields& s) {
    using namespace od::port::machine_gun_state;
    layoutValue(v, a + 0xbc, s.stats);
    layoutValue(v, a + 0xc4, s.muzzle);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::machine_gun_state::MuzzleFields& s) {
    using namespace od::port::machine_gun_state;
    layoutValue(v, a + 0x18, s.shot);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::reflector_state::ReflectorFields& s) {
    using namespace od::port::reflector_state;
    layoutValue(v, a + 0xbc, s.stats);
    layoutValue(v, a + 0xc2, s.deflectTicks);
    layoutValue(v, a + 0xc4, s.skidTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::reflector_state::SparkSprayFields& s) {
    using namespace od::port::reflector_state;
    layoutValue(v, a + 0xc, s.pitch);
    layoutValue(v, a + 0xe, s.yaw);
    layoutValue(v, a + 0xb, s.ticks);
    for (size_t i = 0; i < s.sparks.size(); ++i) layoutValue(v, a + 0x68 + uint32_t(i) * (8), s.sparks[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::reflector_state::SpraySpark& s) {
    using namespace od::port::reflector_state;
    layoutValue(v, a + 0, s.pitch);
    layoutValue(v, a + 2, s.yaw);
    layoutValue(v, a + 4, s.roll);
    layoutValue(v, a + 6, s.speed);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::shining_laser_state::LaserWallSparkFields& s) {
    using namespace od::port::shining_laser_state;
    layoutValue(v, a + 0xc, s.size);
    layoutValue(v, a + 0x18, s.discSize);
    layoutValue(v, a + 0x1a, s.shrink);
    layoutValue(v, a + 0x1c, s.fadeTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::spread_buster_state::SparkleFields& s) {
    using namespace od::port::spread_buster_state;
    layoutValue(v, a + 0x64, s.ticks);
    for (size_t i = 0; i < s.rayAngles.size(); ++i) layoutValue(v, a + 0x68 + uint32_t(i) * (1), s.rayAngles[i]);
    for (size_t i = 0; i < s.rayLengths.size(); ++i) layoutValue(v, a + 0x70 + uint32_t(i) * (1), s.rayLengths[i]);
    for (size_t i = 0; i < s.sparkAngles.size(); ++i) layoutValue(v, a + 0x78 + uint32_t(i) * (1), s.sparkAngles[i]);
    for (size_t i = 0; i < s.sparkSpeeds.size(); ++i) layoutValue(v, a + 0x7c + uint32_t(i) * (1), s.sparkSpeeds[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::spread_buster_state::SpreadFields& s) {
    using namespace od::port::spread_buster_state;
    layoutValue(v, a + 0xc, s.copy);
    layoutValue(v, a + 0xc, s.copyWord_);
    layoutValue(v, a + 0xbc, s.stats);
}

bool classes_game_shots(LayoutVisitor& v, uint32_t a, ActorClassState& s) {
    if (typeid(s) == typeid(od::port::homing_missile_state::MissileFields)) { layoutValue(v, a, static_cast<od::port::homing_missile_state::MissileFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::machine_gun_state::MachineGunFields)) { layoutValue(v, a, static_cast<od::port::machine_gun_state::MachineGunFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::hunter_seeker_state::HunterFields)) { layoutValue(v, a, static_cast<od::port::hunter_seeker_state::HunterFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::spread_buster_state::SpreadFields)) { layoutValue(v, a, static_cast<od::port::spread_buster_state::SpreadFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::shining_laser_state::LaserWallSparkFields)) { layoutValue(v, a, static_cast<od::port::shining_laser_state::LaserWallSparkFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::charge_state::ShockWaveFields)) { layoutValue(v, a, static_cast<od::port::charge_state::ShockWaveFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::buster_cannon_state::CannonFields)) { layoutValue(v, a, static_cast<od::port::buster_cannon_state::CannonFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::drill_arm_state::DrillConeFields)) { layoutValue(v, a, static_cast<od::port::drill_arm_state::DrillConeFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::crusher_state::EnergyOrbFields)) { layoutValue(v, a, static_cast<od::port::crusher_state::EnergyOrbFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::BladeTrailFields)) { layoutValue(v, a, static_cast<od::port::BladeTrailFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::machine_gun_state::CasingFields)) { layoutValue(v, a, static_cast<od::port::machine_gun_state::CasingFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::extinguisher_state::MistFields)) { layoutValue(v, a, static_cast<od::port::extinguisher_state::MistFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::LaserBlock)) { layoutValue(v, a, static_cast<od::port::LaserBlock&>(s)); return true; }
    if (typeid(s) == typeid(od::port::drill_arm_state::DrillFields)) { layoutValue(v, a, static_cast<od::port::drill_arm_state::DrillFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::reflector_state::SparkSprayFields)) { layoutValue(v, a, static_cast<od::port::reflector_state::SparkSprayFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::reflector_state::ReflectorFields)) { layoutValue(v, a, static_cast<od::port::reflector_state::ReflectorFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::spread_buster_state::SparkleFields)) { layoutValue(v, a, static_cast<od::port::spread_buster_state::SparkleFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::charge_state::ChargeMarkerFields)) { layoutValue(v, a, static_cast<od::port::charge_state::ChargeMarkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::crusher_state::CrusherFields)) { layoutValue(v, a, static_cast<od::port::crusher_state::CrusherFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::buster_cannon_state::FlashBurstFields)) { layoutValue(v, a, static_cast<od::port::buster_cannon_state::FlashBurstFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::BladeFields)) { layoutValue(v, a, static_cast<od::port::BladeFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::StreamPartnerFields)) { layoutValue(v, a, static_cast<od::port::StreamPartnerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::LaserBeamFields)) { layoutValue(v, a, static_cast<od::port::LaserBeamFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ExtinguisherFields)) { layoutValue(v, a, static_cast<od::port::ExtinguisherFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::BusterShotFields)) { layoutValue(v, a, static_cast<od::port::BusterShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::hyper_shell_state::DarkTrailFields)) { layoutValue(v, a, static_cast<od::port::hyper_shell_state::DarkTrailFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::hyper_shell_state::ShellFields)) { layoutValue(v, a, static_cast<od::port::hyper_shell_state::ShellFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::hunter_seeker_state::CrystalFields)) { layoutValue(v, a, static_cast<od::port::hunter_seeker_state::CrystalFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ground_crawler_state::CrawlerFields)) { layoutValue(v, a, static_cast<od::port::ground_crawler_state::CrawlerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::machine_gun_state::MuzzleFields)) { layoutValue(v, a, static_cast<od::port::machine_gun_state::MuzzleFields&>(s)); return true; }
    return false;
}
} // namespace od::test
