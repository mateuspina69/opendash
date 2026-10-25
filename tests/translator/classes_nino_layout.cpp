// The native records of this area in the exe layout (test-only).
#include "translator/actor_layout.hpp"
#include "content/nino/bars_and_gate_state.hpp"
#include "content/nino/blade_trap_state.hpp"
#include "content/nino/carrier_boat_state.hpp"
#include "content/nino/ch2_field_cast_state.hpp"
#include "content/nino/ch2_script_cast_state.hpp"
#include "content/nino/field_enemies.hpp"
#include "content/nino/field_enemies_state.hpp"
#include "content/nino/field_spawners.hpp"
#include "content/nino/field_spawners_state.hpp"
#include "content/nino/fleeing_reaverbot_state.hpp"
#include "content/nino/floor_switch_state.hpp"
#include "content/nino/fly_point.hpp"
#include "content/nino/gauge_reaverbots_state.hpp"
#include "content/nino/glyde_attack_cast_state.hpp"
#include "content/nino/glyde_attack_talkers_state.hpp"
#include "content/nino/glyde_box_walkers_state.hpp"
#include "content/nino/glyde_missiles_state.hpp"
#include "content/nino/guardian_cast_state.hpp"
#include "content/nino/homing_shot_state.hpp"
#include "content/nino/mission_gunships_state.hpp"
#include "content/nino/mission_tank_state.hpp"
#include "content/nino/mission_targets_state.hpp"
#include "content/nino/nino_ruin_spawners_state.hpp"
#include "content/nino/platform_boss_state.hpp"
#include "content/nino/repair_machine_state.hpp"
#include "content/nino/ruin_effects.hpp"
#include "content/nino/ruin_effects_state.hpp"
#include "content/nino/ruin_guardian_state.hpp"
#include "content/nino/ruin_swimmers_state.hpp"
#include "content/nino/screen_ripple.hpp"
#include "content/nino/screen_ripple_state.hpp"
#include "content/nino/square_gunship_state.hpp"
#include "content/nino/st15_cls_state.hpp"
#include "content/nino/st15_props_state.hpp"
#include "content/nino/st15_spray_state.hpp"
#include "content/nino/st16_crane_state.hpp"
#include "content/nino/st18_bomber_birdbots_state.hpp"
#include "content/nino/st18_gunships_state.hpp"
#include "content/nino/st18_props_state.hpp"
#include "content/nino/st19_state.hpp"

namespace od::test {
using namespace od::port;

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::BoxCarrierGrip& s) {
    using namespace od::port;
    layoutValue(v, a + 0x160, s.carry);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::BoxSpawnerFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xb, s.boxesAlive);
    layoutValue(v, a + 0xc, s.lever);
    layoutValue(v, a + 0xd, s.crane);
    layoutValue(v, a + 0xe, s.callCrane);
    layoutValue(v, a + 0xf, s.boxesMissing);
    for (size_t i = 0; i < s.boxes.size(); ++i) layoutValue(v, a + 0x10 + uint32_t(i) * (2), s.boxes[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::FlyPointFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.ticks);
    layoutValue(v, a + 0xe, s.flag);
    layoutValue(v, a + 0x10, s.from);
    layoutValue(v, a + 0x18, s.to);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::GrowingRippleFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.startRadius);
    layoutValue(v, a + 0xd, s.growth);
    layoutValue(v, a + 0xe, s.shift);
    layoutValue(v, a + 0x16, s.maxRadius);
    layoutValue(v, a + 0x18, s.radius);
    layoutValue(v, a + 0x1a, s.phase);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::VariantEffectFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.param);
    for (size_t i = 0; i < s.tint.size(); ++i) layoutValue(v, a + 0xc + uint32_t(i) * (1), s.tint[i]);
    layoutValue(v, a + 0xf, s.waveStrength);
    layoutValue(v, a + 0x1c, s.size);
    layoutValue(v, a + 0x1e, s.side);
    layoutValue(v, a + 0x18, s.tickA);
    layoutValue(v, a + 0x1a, s.tickB);
    layoutValue(v, a + 0x20, s.kind);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::bars_and_gate_state::BarFields& s) {
    using namespace od::port::bars_and_gate_state;
    layoutValue(v, a + 0xc8, s.light);
    layoutValue(v, a + 0xc6, s.hangY);
    layoutValue(v, a + 0xc9, s.triggerSpawned);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xbc + uint32_t(i) * (1), s.clearedPad0[i]);
    for (size_t i = 0; i < s.clearedPad1.size(); ++i) layoutValue(v, a + 0xca + uint32_t(i) * (1), s.clearedPad1[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::bars_and_gate_state::GateFields& s) {
    using namespace od::port::bars_and_gate_state;
    layoutValue(v, a + 0xc7, s.ticks);
    layoutValue(v, a + 0xc8, s.waveLeft);
    layoutValue(v, a + 0xc4, s.targetY);
    layoutValue(v, a + 0xc6, s.open);
    layoutValue(v, a + 0xc9, s.moved);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xbc + uint32_t(i) * (1), s.clearedPad0[i]);
    for (size_t i = 0; i < s.clearedPad1.size(); ++i) layoutValue(v, a + 0xca + uint32_t(i) * (1), s.clearedPad1[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::blade_trap_state::TrapFields& s) {
    using namespace od::port::blade_trap_state;
    layoutValue(v, a + 0x2e, s.travelYaw);
    layoutValue(v, a + 0x38, s.spinSpeed);
    layoutValue(v, a + 0x3a, s.bob);
    layoutValue(v, a + 0x3c, s.speed);
    layoutValue(v, a + 0x3e, s.stepSize);
    layoutValue(v, a + 0x40, s.timer);
    layoutValue(v, a + 0x42, s.partsLeft);
    layoutValue(v, a + 0x44, s.bladeRadius);
    layoutValue(v, a + 0x46, s.scaleOffset);
    layoutValue(v, a + 0x48, s.legLeft);
    layoutValue(v, a + 0x4a, s.contact);
    layoutValue(v, a + 0x4b, s.mode);
    layoutValue(v, a + 0xbc, s.hit);
    layoutValue(v, a + 0xc4, s.armedHit);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0x4c + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::carrier_boat_state::BoatFields& s) {
    using namespace od::port::carrier_boat_state;
    layoutValue(v, a + 0xc, s.busy);
    layoutValue(v, a + 0x3e, s.engineTicks);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc7, s.hurtTicks);
    layoutValue(v, a + 0xc8, s.damage);
    layoutValue(v, a + 0xc4, s.gunCycle);
    layoutValue(v, a + 0xc5, s.ram);
    layoutValue(v, a + 0xc6, s.trip);
    layoutValue(v, a + 0xc9, s.timer);
    for (size_t i = 0; i < s.fires.size(); ++i) layoutValue(v, a + 0xca + uint32_t(i) * (1), s.fires[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ch2_field_cast_state::CrashCarrierFields& s) {
    using namespace od::port::ch2_field_cast_state;
    layoutValue(v, a + 0x160, s.carry);
    layoutValue(v, a + 0x162, s.contact);
    layoutValue(v, a + 0x163, s.timer);
    layoutValue(v, a + 0x169, s.group);
    layoutValue(v, a + 0x15c, s.fallYaw);
    layoutValue(v, a + 0x164, s.homeX);
    layoutValue(v, a + 0x166, s.homeZ);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ch2_field_cast_state::HoverFields& s) {
    using namespace od::port::ch2_field_cast_state;
    layoutValue(v, a + 0x15f, s.alarmSent);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.smokeStage);
    layoutValue(v, a + 0x15d, s.hurtTicks);
    layoutValue(v, a + 0x15e, s.smokeChanged);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ch2_field_cast_state::WalkerFields& s) {
    using namespace od::port::ch2_field_cast_state;
    layoutValue(v, a + 0x151, s.timer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ch2_script_cast_state::FlashFields& s) {
    using namespace od::port::ch2_script_cast_state;
    layoutValue(v, a + 0xc, s.size);
    layoutValue(v, a + 0xf, s.ticks);
    for (size_t i = 0; i < s.spikes.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.spikes[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ch2_script_cast_state::TakeOffCastFields& s) {
    using namespace od::port::ch2_script_cast_state;
    layoutValue(v, a + 0x14c, s.child);
    layoutValue(v, a + 0x150, s.tracks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ch2_script_cast_state::TalkingCastFields& s) {
    using namespace od::port::ch2_script_cast_state;
    layoutValue(v, a + 0x163, s.animKeyStart);
    layoutValue(v, a + 0x164, s.smokeStepByte);
    layoutValue(v, a + 0x167, s.ticks);
    layoutValue(v, a + 0x168, s.animated);
    layoutValue(v, a + 0x169, s.mouthOpen);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_enemies_state::CarrierFields& s) {
    using namespace od::port::field_enemies_state;
    layoutValue(v, a, static_cast<od::port::BoxCarrierGrip&>(s));
    layoutValue(v, a + 0x162, s.contact);
    layoutValue(v, a + 0x163, s.timer);
    layoutValue(v, a + 0x168, s.damage);
    layoutValue(v, a + 0x169, s.group);
    layoutValue(v, a + 0x16a, s.footstepTicks);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x161, s.boxIndex);
    layoutValue(v, a + 0x164, s.homeX);
    layoutValue(v, a + 0x166, s.homeZ);
    layoutValue(v, a + 0x15c, s.brakeDistance);
    for (size_t i = 0; i < s.riders.size(); ++i) layoutValue(v, a + 0x15e + uint32_t(i) * (1), s.riders[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_enemies_state::CritterFields& s) {
    using namespace od::port::field_enemies_state;
    layoutValue(v, a + 0xf, s.knockedMark);
    layoutValue(v, a + 0x2e, s.tiltY);
    layoutValue(v, a + 0x3e, s.flightYaw);
    layoutValue(v, a + 0x46, s.tiltX);
    layoutValue(v, a + 0x162, s.behaviour);
    layoutValue(v, a + 0x164, s.wanderTicks);
    layoutValue(v, a + 0x165, s.contact);
    layoutValue(v, a + 0x166, s.animKeyStart);
    layoutValue(v, a + 0x167, s.dropTicks);
    layoutValue(v, a + 0x16a, s.offScreenTicks);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x16b, s.stagger);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_spawners_state::St15ClockFields& s) {
    using namespace od::port::field_spawners_state;
    layoutValue(v, a + 0x18, s.timer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_spawners_state::St15GroupFields& s) {
    using namespace od::port::field_spawners_state;
    for (size_t i = 0; i < s.members.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.members[i]);
    layoutValue(v, a + 0x1b, s.holder);
    layoutValue(v, a + 0x1c, s.group);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_spawners_state::St15WaveFields& s) {
    using namespace od::port::field_spawners_state;
    for (size_t i = 0; i < 3; ++i) layoutValue(v, a + 0x10 + uint32_t(i), s.shipSlots[i]);
    layoutValue(v, a + 0x18, s.waveTimer);
    layoutValue(v, a + 0x19, s.targetRow);
    layoutValue(v, a + 0x1a, s.wave);
    layoutValue(v, a + 0x1b, s.follower);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_spawners_state::St16CarrierFields& s) {
    using namespace od::port::field_spawners_state;
    layoutValue(v, a + 0x18, s.pointIndex);
    layoutValue(v, a + 0x19, s.targetIndex);
    for (size_t i = 0; i < s.carriers.size(); ++i) layoutValue(v, a + 0x1a + uint32_t(i) * (1), s.carriers[i]);
    layoutValue(v, a + 0x1e, s.lastCarrier);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_spawners_state::St16ClockFields& s) {
    using namespace od::port::field_spawners_state;
    layoutValue(v, a + 0x18, s.ticks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_spawners_state::St16GroupFields& s) {
    using namespace od::port::field_spawners_state;
    for (size_t i = 0; i < s.members.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.members[i]);
    layoutValue(v, a + 0x1a, s.holder);
    layoutValue(v, a + 0x1b, s.group);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_spawners_state::St17ClockFields& s) {
    using namespace od::port::field_spawners_state;
    layoutValue(v, a + 0x18, s.timer);
    layoutValue(v, a + 0x1c, s.countdown);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_spawners_state::St17WalkerFields& s) {
    using namespace od::port::field_spawners_state;
    for (size_t i = 0; i < s.entries.size(); ++i) layoutValue(v, a + 0x10 + uint32_t(i) * (4), s.entries[i]);
    layoutValue(v, a + 0x1c, s.routesSwitched);
    layoutValue(v, a + 0x1d, s.retreatSent);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_spawners_state::St18FightFields& s) {
    using namespace od::port::field_spawners_state;
    for (size_t i = 0; i < s.gunships.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.gunships[i]);
    layoutValue(v, a + 0x1c, s.birdbots);
    layoutValue(v, a + 0x1d, s.callsStopped);
    layoutValue(v, a + 0x1e, s.secondWave);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_spawners_state::St18RepairFields& s) {
    using namespace od::port::field_spawners_state;
    layoutValue(v, a + 0x18, s.machine);
    layoutValue(v, a + 0x1c, s.gauge);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_spawners_state::St18ReturnFields& s) {
    using namespace od::port::field_spawners_state;
    layoutValue(v, a + 0x18, s.shortTimer);
    layoutValue(v, a + 0x1a, s.pointIndex);
    layoutValue(v, a + 0x1c, s.gunship);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_spawners_state::St18TimerFields& s) {
    using namespace od::port::field_spawners_state;
    layoutValue(v, a + 0x18, s.timer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_spawners_state::WalkerEntry& s) {
    using namespace od::port::field_spawners_state;
    layoutValue(v, a + 0x0, s.state);
    layoutValue(v, a + 0x1, s.route);
    layoutValue(v, a + 0x2, s.step);
    layoutValue(v, a + 0x3, s.slot);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::fleeing_reaverbot_state::ReaverbotFields& s) {
    using namespace od::port::fleeing_reaverbot_state;
    layoutValue(v, a + 0x14c, s.hit);
    layoutValue(v, a + 0x154, s.ticks);
    layoutValue(v, a + 0x156, s.leave);
    layoutValue(v, a + 0x158, s.homeY);
    layoutValue(v, a + 0x15a, s.neighbour);
    layoutValue(v, a + 0x15c, s.contact);
    layoutValue(v, a + 0x15d, s.mode);
    layoutValue(v, a + 0x15e, s.startKey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::floor_switch_state::SwitchFields& s) {
    using namespace od::port::floor_switch_state;
    layoutValue(v, a + 0xbc, s.homeY);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::gauge_reaverbots_state::CastFields& s) {
    using namespace od::port::gauge_reaverbots_state;
    layoutValue(v, a + 0x3e, s.velocitySpare);
    layoutValue(v, a + 0x46, s.accelerationSpare);
    layoutValue(v, a + 0x152, s.animFlags);
    layoutValue(v, a + 0x153, s.startKey);
    layoutValue(v, a + 0x158, s.glow);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::gauge_reaverbots_state::GaugeFields& s) {
    using namespace od::port::gauge_reaverbots_state;
    layoutValue(v, a + 0x2e, s.homeX);
    layoutValue(v, a + 0x3e, s.homeZ);
    layoutValue(v, a + 0x164, s.contact);
    layoutValue(v, a + 0x165, s.mode);
    layoutValue(v, a + 0x166, s.startKey);
    layoutValue(v, a + 0x167, s.glow);
    layoutValue(v, a + 0x168, s.glowStep);
    layoutValue(v, a + 0x40, s.circlePoint);
    layoutValue(v, a + 0x14c, s.hit);
    layoutValue(v, a + 0x15c, s.ticks);
    layoutValue(v, a + 0x15e, s.counter);
    layoutValue(v, a + 0x160, s.startY);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::gauge_reaverbots_state::MineFields& s) {
    using namespace od::port::gauge_reaverbots_state;
    layoutValue(v, a + 0x14c, s.hit);
    layoutValue(v, a + 0x154, s.ticks);
    layoutValue(v, a + 0x158, s.contact);
    layoutValue(v, a + 0x159, s.mode);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_attack_cast_state::BlinkerFields& s) {
    using namespace od::port::glyde_attack_cast_state;
    layoutValue(v, a + 0x150, s.ticks);
    layoutValue(v, a + 0x152, s.flipped);
    layoutValue(v, a + 0x153, s.frozen);
    layoutValue(v, a + 0x154, s.blinking);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_attack_cast_state::DescenderFields& s) {
    using namespace od::port::glyde_attack_cast_state;
    layoutValue(v, a + 0x14c, s.tracks);
    layoutValue(v, a + 0x15c, s.ticks);
    layoutValue(v, a + 0x15e, s.frozen);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_attack_cast_state::RiserFields& s) {
    using namespace od::port::glyde_attack_cast_state;
    layoutValue(v, a + 0x2e, s.riseTicks);
    layoutValue(v, a + 0x14c, s.tracks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_attack_cast_state::Script39BirdbotFields& s) {
    using namespace od::port::glyde_attack_cast_state;
    layoutValue(v, a + 0x163, s.startKey);
    layoutValue(v, a + 0x168, s.stepping);
    layoutValue(v, a + 0x169, s.pairFlipped);
    layoutValue(v, a + 0x164, s.slotPhase);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_attack_cast_state::Script3bBirdbotFields& s) {
    using namespace od::port::glyde_attack_cast_state;
    layoutValue(v, a + 0x14c, s.nextYawStep);
    layoutValue(v, a + 0x150, s.firstYawStep);
    layoutValue(v, a + 0x154, s.startKey);
    layoutValue(v, a + 0x155, s.ticks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_attack_talkers_state::TalkerFields& s) {
    using namespace od::port::glyde_attack_talkers_state;
    layoutValue(v, a + 0x14c, s.tracks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_box_walkers_state::BombFields& s) {
    using namespace od::port::glyde_box_walkers_state;
    layoutValue(v, a + 0xbc, s.thrower);
    layoutValue(v, a + 0xc0, s.hit);
    layoutValue(v, a + 0xc8, s.rollSoundTicks);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xca + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_box_walkers_state::WalkerFields& s) {
    using namespace od::port::glyde_box_walkers_state;
    layoutValue(v, a + 0x3e, s.plannedCell);
    layoutValue(v, a + 0x40, s.spreadLeft);
    layoutValue(v, a + 0x44, s.spreadRight);
    layoutValue(v, a + 0x46, s.gunPitch);
    layoutValue(v, a + 0x48, s.footstepTicks);
    layoutValue(v, a + 0x4a, s.hurtSoundTicks);
    layoutValue(v, a + 0x160, s.action);
    layoutValue(v, a + 0x166, s.cellX);
    layoutValue(v, a + 0x167, s.cellZ);
    layoutValue(v, a + 0x16a, s.routeStep);
    layoutValue(v, a + 0x14c, s.target);
    layoutValue(v, a + 0x150, s.ticks);
    layoutValue(v, a + 0x152, s.lastCell);
    layoutValue(v, a + 0x154, s.hit);
    layoutValue(v, a + 0x15c, s.flags);
    layoutValue(v, a + 0x15e, s.heading);
    layoutValue(v, a + 0x161, s.actionStep);
    layoutValue(v, a + 0x162, s.actionTicks);
    layoutValue(v, a + 0x164, s.headYaw);
    layoutValue(v, a + 0x16b, s.shotCount);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_missiles_state::GlowFields& s) {
    using namespace od::port::glyde_missiles_state;
    layoutValue(v, a + 0xc, s.slot);
    layoutValue(v, a + 0xd, s.bone);
    layoutValue(v, a + 0xe, s.size);
    layoutValue(v, a + 0x18, s.offset);
    layoutValue(v, a + 0x20, s.followed);
    layoutValue(v, a + 0x24, s.angle);
    layoutValue(v, a + 0x26, s.mode);
    layoutValue(v, a + 0x27, s.spare);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_missiles_state::MissileFields& s) {
    using namespace od::port::glyde_missiles_state;
    layoutValue(v, a + 0xbc, s.hit);
    layoutValue(v, a + 0xca, s.target);
    layoutValue(v, a + 0xcb, s.trail);
    layoutValue(v, a + 0xc5, s.hitKind);
    layoutValue(v, a + 0xc6, s.homing);
    layoutValue(v, a + 0xc8, s.life);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_missiles_state::MissileShipFields& s) {
    using namespace od::port::glyde_missiles_state;
    layoutValue(v, a + 0xbc, s.launch);
    layoutValue(v, a + 0xc0, s.countdown);
    layoutValue(v, a + 0xc2, s.missilesLeft);
    layoutValue(v, a + 0xc3, s.nextVariant);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::guardian_cast_state::CastFields& s) {
    using namespace od::port::guardian_cast_state;
    layoutValue(v, a + 0x14c, s.ticks);
    layoutValue(v, a + 0x14e, s.sparkles);
    layoutValue(v, a + 0x150, s.sparkDrift);
    layoutValue(v, a + 0x151, s.sparkCount);
    layoutValue(v, a + 0x152, s.sparkBone);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::homing_shot_state::ShotFields& s) {
    using namespace od::port::homing_shot_state;
    layoutValue(v, a + 0xbc, s.hit);
    layoutValue(v, a + 0xd0, s.spin);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::mission_gunships_state::MissionGunshipFields& s) {
    using namespace od::port::mission_gunships_state;
    layoutValue(v, a, static_cast<od::port::GunshipFields&>(s));
    layoutValue(v, a + 0xc, s.mode);
    layoutValue(v, a + 0xd, s.count);
    layoutValue(v, a + 0xe, s.reinforcementRow);
    layoutValue(v, a + 0xf, s.leaveHeading);
    layoutValue(v, a + 0x50, s.entrySlot);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::mission_tank_state::TankFields& s) {
    using namespace od::port::mission_tank_state;
    layoutValue(v, a + 0xc, s.targetClass);
    layoutValue(v, a + 0xd, s.targetType);
    layoutValue(v, a + 0xe, s.targetIndex);
    layoutValue(v, a + 0xf, s.damageLevel);
    layoutValue(v, a + 0x2e, s.barrelPitch);
    layoutValue(v, a + 0x46, s.wheelsYaw);
    layoutValue(v, a + 0x15f, s.leadShift);
    layoutValue(v, a + 0x160, s.lastLevel);
    layoutValue(v, a + 0x162, s.timer);
    layoutValue(v, a + 0x163, s.hurtCooldown);
    layoutValue(v, a + 0x164, s.group);
    layoutValue(v, a + 0x165, s.targetHold);
    layoutValue(v, a + 0x168, s.repair);
    layoutValue(v, a + 0x169, s.engineTimer);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.target);
    layoutValue(v, a + 0x15e, s.bits);
    layoutValue(v, a + 0x161, s.fireTimer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::mission_targets_state::SmokeColumnsFields& s) {
    using namespace od::port::mission_targets_state;
    layoutValue(v, a + 0xc8, s.unusedC8);
    for (size_t i = 0; i < s.puffY.size(); ++i) layoutValue(v, a + 0x74 + uint32_t(i) * (2), s.puffY[i]);
    for (size_t i = 0; i < s.puffTick.size(); ++i) layoutValue(v, a + 0x8c + uint32_t(i) * (2), s.puffTick[i]);
    for (size_t i = 0; i < s.puffLife.size(); ++i) layoutValue(v, a + 0xa4 + uint32_t(i) * (1), s.puffLife[i]);
    for (size_t i = 0; i < s.puffRise.size(); ++i) layoutValue(v, a + 0xb0 + uint32_t(i) * (1), s.puffRise[i]);
    for (size_t i = 0; i < s.puffGrey.size(); ++i) layoutValue(v, a + 0xbc + uint32_t(i) * (1), s.puffGrey[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::nino_ruin_spawners_state::RuinSpawnerFields& s) {
    using namespace od::port::nino_ruin_spawners_state;
    layoutValue(v, a + 0xc, s.pullStrength);
    layoutValue(v, a + 0xe, s.pullRange);
    layoutValue(v, a + 0xf, s.scriptAlive);
    layoutValue(v, a + 0x10, s.trapPoint);
    layoutValue(v, a + 0x18, s.countdown);
    layoutValue(v, a + 0x1a, s.limit);
    layoutValue(v, a + 0x1c, s.entry);
    layoutValue(v, a + 0x1e, s.soundTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::platform_boss_state::BoltFields& s) {
    using namespace od::port::platform_boss_state;
    layoutValue(v, a + 0xc, s.length);
    layoutValue(v, a + 0xe, s.widthParam);
    layoutValue(v, a + 0xf, s.amplitudeParam);
    layoutValue(v, a + 0x16, s.parent);
    layoutValue(v, a + 0x10, s.pos);
    layoutValue(v, a + 0x18, s.halfWidth);
    layoutValue(v, a + 0x1a, s.amplitude);
    layoutValue(v, a + 0x1c, s.fade);
    layoutValue(v, a + 0x1e, s.startAngle);
    layoutValue(v, a + 0x1f, s.angleStep);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::platform_boss_state::BossFields& s) {
    using namespace od::port::platform_boss_state;
    layoutValue(v, a + 0x2e, s.diveZ);
    layoutValue(v, a + 0x3e, s.diveX);
    layoutValue(v, a + 0x40, s.tailPoint);
    layoutValue(v, a + 0x48, s.headPoint);
    layoutValue(v, a + 0x14c, s.hit);
    layoutValue(v, a + 0x154, s.ticks);
    layoutValue(v, a + 0x158, s.contact);
    layoutValue(v, a + 0x159, s.mode);
    layoutValue(v, a + 0x15a, s.waypoint);
    layoutValue(v, a + 0x15b, s.fromWaypoint);
    layoutValue(v, a + 0x15c, s.startKey);
    layoutValue(v, a + 0x15d, s.sparkKind);
    layoutValue(v, a + 0x15e, s.sparkCooldown);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::platform_boss_state::ShotFields& s) {
    using namespace od::port::platform_boss_state;
    layoutValue(v, a + 0xc, s.fade);
    layoutValue(v, a + 0xbc, s.hit);
    layoutValue(v, a + 0xcc, s.orbTicks);
    layoutValue(v, a + 0xd0, s.spin);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::platform_boss_state::SparkFields& s) {
    using namespace od::port::platform_boss_state;
    for (size_t i = 0; i < s.particles.size(); ++i) layoutValue(v, a + 0x40 + uint32_t(i) * (8), s.particles[i]);
    layoutValue(v, a + 0xc0, s.shotsLeft);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::platform_boss_state::SparkParticle& s) {
    using namespace od::port::platform_boss_state;
    layoutValue(v, a + 0x0, s.point);
    layoutValue(v, a + 0x6, s.life);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::repair_machine_state::MachineFields& s) {
    using namespace od::port::repair_machine_state;
    layoutValue(v, a + 0x15f, s.hitCooldown);
    layoutValue(v, a + 0x164, s.endTicks);
    layoutValue(v, a + 0x14c, s.hit);
    layoutValue(v, a + 0x15c, s.smokeTicks);
    layoutValue(v, a + 0x15e, s.damageStage);
    for (size_t i = 0; i < s.smokeSlots.size(); ++i) layoutValue(v, a + 0x160 + uint32_t(i) * (1), s.smokeSlots[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::repair_machine_state::PartFields& s) {
    using namespace od::port::repair_machine_state;
    layoutValue(v, a + 0x164, s.shakeTicks);
    layoutValue(v, a + 0x15c, s.rider);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::repair_machine_state::PropFields& s) {
    using namespace od::port::repair_machine_state;
    layoutValue(v, a + 0x160, s.detached);
    layoutValue(v, a + 0x163, s.frozen);
    layoutValue(v, a + 0x14c, s.tracks);
    layoutValue(v, a + 0x15c, s.part);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::repair_machine_state::RollFields& s) {
    using namespace od::port::repair_machine_state;
    layoutValue(v, a + 0x14c, s.machine);
    layoutValue(v, a + 0x150, s.flags);
    layoutValue(v, a + 0x151, s.workLoops);
    layoutValue(v, a + 0x152, s.toolShown);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_effects_state::BlobFields& s) {
    using namespace od::port::ruin_effects_state;
    layoutValue(v, a + 0xc, s.attack);
    layoutValue(v, a + 0xe, s.hits);
    layoutValue(v, a + 0xf, s.life);
    layoutValue(v, a + 0x2e, s.startSize);
    layoutValue(v, a + 0x3e, s.growth);
    layoutValue(v, a + 0xbc, s.hit);
    layoutValue(v, a + 0xc8, s.ticksLeft);
    layoutValue(v, a + 0xca, s.brightness);
    layoutValue(v, a + 0xcc, s.size);
    layoutValue(v, a + 0xce, s.burstFrame);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xc4 + uint32_t(i) * (1), s.clearedPad0[i]);
    for (size_t i = 0; i < s.clearedPad1.size(); ++i) layoutValue(v, a + 0xd0 + uint32_t(i) * (1), s.clearedPad1[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_effects_state::FlashFields& s) {
    using namespace od::port::ruin_effects_state;
    layoutValue(v, a + 0x18, s.ticks);
    layoutValue(v, a + 0x1a, s.depthOffset);
    layoutValue(v, a + 0x1c, s.red);
    layoutValue(v, a + 0x1e, s.green);
    layoutValue(v, a + 0x20, s.blue);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_guardian_state::BeamFields& s) {
    using namespace od::port::ruin_guardian_state;
    layoutValue(v, a + 0xc, s.length);
    layoutValue(v, a + 0x48, s.swayPhase);
    layoutValue(v, a + 0x4a, s.swaySpeed);
    layoutValue(v, a + 0x4c, s.phaseStep);
    layoutValue(v, a + 0x4e, s.swayStep);
    layoutValue(v, a + 0x88, s.loopSound);
    layoutValue(v, a + 0x8c, s.life);
    layoutValue(v, a + 0x8e, s.width);
    layoutValue(v, a + 0x74, s.hit);
    layoutValue(v, a + 0x7c, s.lastPoint);
    layoutValue(v, a + 0x84, s.backLength);
    layoutValue(v, a + 0xbc, s.longest);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_guardian_state::GuardianFields& s) {
    using namespace od::port::ruin_guardian_state;
    layoutValue(v, a + 0x2e, s.sweepYaw);
    layoutValue(v, a + 0x164, s.mode);
    layoutValue(v, a + 0x165, s.mineGroup);
    layoutValue(v, a + 0x166, s.mineMarks);
    layoutValue(v, a + 0x167, s.shotsLeft);
    layoutValue(v, a + 0x168, s.damageStage);
    layoutValue(v, a + 0x14c, s.hit);
    layoutValue(v, a + 0x154, s.ticks);
    layoutValue(v, a + 0x158, s.headYaw);
    layoutValue(v, a + 0x15a, s.headPitch);
    layoutValue(v, a + 0x15c, s.aimOffset);
    layoutValue(v, a + 0x15e, s.hurtVoiceTicks);
    for (size_t i = 0; i < s.beams.size(); ++i) layoutValue(v, a + 0x160 + uint32_t(i) * (1), s.beams[i]);
    layoutValue(v, a + 0x16b, s.minesLive);
    layoutValue(v, a + 0x156, s.volleys);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_guardian_state::OrbFields& s) {
    using namespace od::port::ruin_guardian_state;
    layoutValue(v, a + 0x74, s.phase);
    layoutValue(v, a + 0xbc, s.guardian);
    layoutValue(v, a + 0xc0, s.ticks);
    layoutValue(v, a + 0xc2, s.turnLevel);
    layoutValue(v, a + 0xc4, s.hit);
    layoutValue(v, a + 0xcc, s.bobY);
    layoutValue(v, a + 0xce, s.bobPhase);
    layoutValue(v, a + 0xcf, s.ringRadius);
    layoutValue(v, a + 0xd0, s.driftX);
    layoutValue(v, a + 0xd2, s.driftZ);
    layoutValue(v, a + 0xd4, s.ownGroup);
    layoutValue(v, a + 0xd5, s.guardianGroup);
    for (size_t i = 0; i < s.pointRow.size(); ++i) layoutValue(v, a + 0x7c + uint32_t(i) * (6), s.pointRow[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_swimmers_state::BigSwimmerFields& s) {
    using namespace od::port::ruin_swimmers_state;
    layoutValue(v, a + 0x3e, s.lungeYaw);
    layoutValue(v, a + 0x164, s.contact);
    layoutValue(v, a + 0x165, s.mode);
    layoutValue(v, a + 0x166, s.startKey);
    layoutValue(v, a + 0x14c, s.hit);
    layoutValue(v, a + 0x154, s.tailHit);
    layoutValue(v, a + 0x15c, s.ticks);
    layoutValue(v, a + 0x15e, s.counter);
    layoutValue(v, a + 0x160, s.groundY);
    layoutValue(v, a + 0x162, s.neighbour);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_swimmers_state::SmallSwimmerFields& s) {
    using namespace od::port::ruin_swimmers_state;
    layoutValue(v, a + 0x3e, s.lungeYaw);
    layoutValue(v, a + 0x14c, s.hit);
    layoutValue(v, a + 0x154, s.ticks);
    layoutValue(v, a + 0x156, s.heading);
    layoutValue(v, a + 0x158, s.groundY);
    layoutValue(v, a + 0x15a, s.neighbour);
    layoutValue(v, a + 0x15c, s.contact);
    layoutValue(v, a + 0x15d, s.mode);
    layoutValue(v, a + 0x15e, s.startKey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_swimmers_state::TorpedoCompanionFields& s) {
    using namespace od::port::ruin_swimmers_state;
    layoutValue(v, a + 0xbc, s.torpedo);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_swimmers_state::TorpedoFields& s) {
    using namespace od::port::ruin_swimmers_state;
    layoutValue(v, a + 0xbc, s.shooter);
    layoutValue(v, a + 0xc0, s.hit);
    layoutValue(v, a + 0xca, s.homingTicks);
    layoutValue(v, a + 0xcd, s.exhaustTick);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xc8 + uint32_t(i) * (1), s.clearedPad0[i]);
    for (size_t i = 0; i < s.clearedPad1.size(); ++i) layoutValue(v, a + 0xcc + uint32_t(i) * (1), s.clearedPad1[i]);
    for (size_t i = 0; i < s.clearedPad2.size(); ++i) layoutValue(v, a + 0xce + uint32_t(i) * (1), s.clearedPad2[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::screen_ripple_state::RayStarFields& s) {
    using namespace od::port::screen_ripple_state;
    layoutValue(v, a + 0xf, s.ticks);
    for (size_t i = 0; i < s.rayAngles.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.rayAngles[i]);
    for (size_t i = 0; i < s.rayLengths.size(); ++i) layoutValue(v, a + 0x20 + uint32_t(i) * (1), s.rayLengths[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::square_gunship_state::SquareGunshipFields& s) {
    using namespace od::port::square_gunship_state;
    layoutValue(v, a, static_cast<od::port::GunshipFields&>(s));
    layoutValue(v, a + 0xc, s.settleFlag);
    layoutValue(v, a + 0xf, s.rockTicks);
    layoutValue(v, a + 0x3e, s.divePitch);
    layoutValue(v, a + 0x46, s.diveHeight);
    layoutValue(v, a + 0x2e, s.bombCooldown);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st15_cls_state::MemberFields& s) {
    using namespace od::port::st15_cls_state;
    layoutValue(v, a + 0x2e, s.tiltY);
    layoutValue(v, a + 0x3e, s.flightYaw);
    layoutValue(v, a + 0x3e, s.boxYaw);
    layoutValue(v, a + 0x46, s.running);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.targetX);
    layoutValue(v, a + 0x15e, s.targetZ);
    layoutValue(v, a + 0x160, s.node);
    layoutValue(v, a + 0x161, s.prevNode);
    layoutValue(v, a + 0x162, s.behaviour);
    layoutValue(v, a + 0x163, s.afterWalk);
    layoutValue(v, a + 0x164, s.point);
    layoutValue(v, a + 0x165, s.contact);
    layoutValue(v, a + 0x166, s.animKeyStart);
    layoutValue(v, a + 0x167, s.timer);
    layoutValue(v, a + 0x168, s.spraySlot);
    layoutValue(v, a + 0x169, s.downKnocked);
    layoutValue(v, a + 0x16a, s.offScreenTicks);
    layoutValue(v, a + 0x16b, s.stagger);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st15_props_state::HoverMachineFields& s) {
    using namespace od::port::st15_props_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc7, s.dead);
    layoutValue(v, a + 0xc4, s.timer);
    layoutValue(v, a + 0xc5, s.hurtTicks);
    layoutValue(v, a + 0xc6, s.fireStage);
    for (size_t i = 0; i < s.fires.size(); ++i) layoutValue(v, a + 0xc8 + uint32_t(i) * (1), s.fires[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st15_props_state::St19TargetFields& s) {
    using namespace od::port::st15_props_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc8, s.joltTicks);
    layoutValue(v, a + 0xca, s.jolt);
    layoutValue(v, a + 0xc4, s.regenTicks);
    layoutValue(v, a + 0xc6, s.homeYaw);
    layoutValue(v, a + 0xc9, s.damageMeter);
    layoutValue(v, a + 0xc4, s.regenTicksLow);
    layoutValue(v, a + 0xc5, s.regenTicksHigh);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st15_spray_state::SprayFields& s) {
    using namespace od::port::st15_spray_state;
    for (size_t i = 0; i < s.puffs.size(); ++i) layoutValue(v, a + 0x74 + uint32_t(i) * (0x18), s.puffs[i]);
    layoutValue(v, a + 0xbc, s.puffCount);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xbd + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st15_spray_state::SprayPuff& s) {
    using namespace od::port::st15_spray_state;
    layoutValue(v, a + 0x0, s.state);
    layoutValue(v, a + 0x1, s.frame);
    layoutValue(v, a + 0x2, s.age);
    layoutValue(v, a + 0x3, s.brightness);
    layoutValue(v, a + 0x4, s.hitBox);
    layoutValue(v, a + 0xc, s.pos);
    layoutValue(v, a + 0x12, s.yaw);
    layoutValue(v, a + 0x14, s.riseSpeed);
    layoutValue(v, a + 0x16, s.forwardSpeed);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st16_crane_state::CraneFields& s) {
    using namespace od::port::st16_crane_state;
    layoutValue(v, a + 0x2e, s.homeYaw);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15d, s.box);
    layoutValue(v, a + 0x15e, s.turnLeft);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st18_bomber_birdbots_state::BomberFields& s) {
    using namespace od::port::st18_bomber_birdbots_state;
    layoutValue(v, a, static_cast<od::port::BirdbotBody&>(s));
    layoutValue(v, a + 0xf, s.trackId);
    layoutValue(v, a + 0x15f, s.timer);
    layoutValue(v, a + 0x160, s.unused160);
    layoutValue(v, a + 0x163, s.animKeyStart);
    layoutValue(v, a + 0x164, s.hitKind);
    layoutValue(v, a + 0x165, s.oddTicks);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x166, s.groundHeight);
    layoutValue(v, a + 0x168, s.bomb);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st18_gunships_state::FightGunshipFields& s) {
    using namespace od::port::st18_gunships_state;
    layoutValue(v, a, static_cast<od::port::GunshipFields&>(s));
    layoutValue(v, a + 0xc, s.orders);
    layoutValue(v, a + 0xd, s.shielded);
    layoutValue(v, a + 0x46, s.orbit);
    layoutValue(v, a + 0x4e, s.orbitStep);
    layoutValue(v, a + 0x3e, s.orbitRadius);
    layoutValue(v, a + 0x3e, s.lastBearing);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st18_props_state::SunFields& s) {
    using namespace od::port::st18_props_state;
    layoutValue(v, a + 0x10, s.point);
    layoutValue(v, a + 0x18, s.flareChain);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st19_state::MachineFields& s) {
    using namespace od::port::st19_state;
    layoutValue(v, a + 0x14c, s.timer);
    layoutValue(v, a + 0x14e, s.partTilt);
    layoutValue(v, a + 0x150, s.companion);
}

bool classes_nino(LayoutVisitor& v, uint32_t a, ActorClassState& s) {
    if (typeid(s) == typeid(od::port::ruin_effects_state::FlashFields)) { layoutValue(v, a, static_cast<od::port::ruin_effects_state::FlashFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::field_spawners_state::St17WalkerFields)) { layoutValue(v, a, static_cast<od::port::field_spawners_state::St17WalkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st16_crane_state::CraneFields)) { layoutValue(v, a, static_cast<od::port::st16_crane_state::CraneFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::BoxCarrierGrip)) { layoutValue(v, a, static_cast<od::port::BoxCarrierGrip&>(s)); return true; }
    if (typeid(s) == typeid(od::port::square_gunship_state::SquareGunshipFields)) { layoutValue(v, a, static_cast<od::port::square_gunship_state::SquareGunshipFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::field_spawners_state::St16CarrierFields)) { layoutValue(v, a, static_cast<od::port::field_spawners_state::St16CarrierFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::field_enemies_state::CarrierFields)) { layoutValue(v, a, static_cast<od::port::field_enemies_state::CarrierFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st15_cls_state::MemberFields)) { layoutValue(v, a, static_cast<od::port::st15_cls_state::MemberFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st18_gunships_state::FightGunshipFields)) { layoutValue(v, a, static_cast<od::port::st18_gunships_state::FightGunshipFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st18_bomber_birdbots_state::BomberFields)) { layoutValue(v, a, static_cast<od::port::st18_bomber_birdbots_state::BomberFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::mission_gunships_state::MissionGunshipFields)) { layoutValue(v, a, static_cast<od::port::mission_gunships_state::MissionGunshipFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ch2_field_cast_state::WalkerFields)) { layoutValue(v, a, static_cast<od::port::ch2_field_cast_state::WalkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::field_spawners_state::St15WaveFields)) { layoutValue(v, a, static_cast<od::port::field_spawners_state::St15WaveFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::repair_machine_state::PropFields)) { layoutValue(v, a, static_cast<od::port::repair_machine_state::PropFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::carrier_boat_state::BoatFields)) { layoutValue(v, a, static_cast<od::port::carrier_boat_state::BoatFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::FlyPointFields)) { layoutValue(v, a, static_cast<od::port::FlyPointFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::glyde_missiles_state::MissileShipFields)) { layoutValue(v, a, static_cast<od::port::glyde_missiles_state::MissileShipFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::field_spawners_state::St16GroupFields)) { layoutValue(v, a, static_cast<od::port::field_spawners_state::St16GroupFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st15_props_state::HoverMachineFields)) { layoutValue(v, a, static_cast<od::port::st15_props_state::HoverMachineFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::field_spawners_state::St15ClockFields)) { layoutValue(v, a, static_cast<od::port::field_spawners_state::St15ClockFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::platform_boss_state::BossFields)) { layoutValue(v, a, static_cast<od::port::platform_boss_state::BossFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ruin_swimmers_state::BigSwimmerFields)) { layoutValue(v, a, static_cast<od::port::ruin_swimmers_state::BigSwimmerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::gauge_reaverbots_state::CastFields)) { layoutValue(v, a, static_cast<od::port::gauge_reaverbots_state::CastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::GrowingRippleFields)) { layoutValue(v, a, static_cast<od::port::GrowingRippleFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st15_props_state::St19TargetFields)) { layoutValue(v, a, static_cast<od::port::st15_props_state::St19TargetFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::blade_trap_state::TrapFields)) { layoutValue(v, a, static_cast<od::port::blade_trap_state::TrapFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::glyde_attack_cast_state::RiserFields)) { layoutValue(v, a, static_cast<od::port::glyde_attack_cast_state::RiserFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::glyde_attack_cast_state::DescenderFields)) { layoutValue(v, a, static_cast<od::port::glyde_attack_cast_state::DescenderFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::platform_boss_state::ShotFields)) { layoutValue(v, a, static_cast<od::port::platform_boss_state::ShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::platform_boss_state::SparkFields)) { layoutValue(v, a, static_cast<od::port::platform_boss_state::SparkFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::glyde_box_walkers_state::BombFields)) { layoutValue(v, a, static_cast<od::port::glyde_box_walkers_state::BombFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st15_spray_state::SprayFields)) { layoutValue(v, a, static_cast<od::port::st15_spray_state::SprayFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::glyde_attack_talkers_state::TalkerFields)) { layoutValue(v, a, static_cast<od::port::glyde_attack_talkers_state::TalkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::nino_ruin_spawners_state::RuinSpawnerFields)) { layoutValue(v, a, static_cast<od::port::nino_ruin_spawners_state::RuinSpawnerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ch2_script_cast_state::TalkingCastFields)) { layoutValue(v, a, static_cast<od::port::ch2_script_cast_state::TalkingCastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ch2_script_cast_state::TakeOffCastFields)) { layoutValue(v, a, static_cast<od::port::ch2_script_cast_state::TakeOffCastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ruin_swimmers_state::TorpedoFields)) { layoutValue(v, a, static_cast<od::port::ruin_swimmers_state::TorpedoFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::screen_ripple_state::RayStarFields)) { layoutValue(v, a, static_cast<od::port::screen_ripple_state::RayStarFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::glyde_box_walkers_state::WalkerFields)) { layoutValue(v, a, static_cast<od::port::glyde_box_walkers_state::WalkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::glyde_attack_cast_state::BlinkerFields)) { layoutValue(v, a, static_cast<od::port::glyde_attack_cast_state::BlinkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::gauge_reaverbots_state::MineFields)) { layoutValue(v, a, static_cast<od::port::gauge_reaverbots_state::MineFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::field_spawners_state::St18ReturnFields)) { layoutValue(v, a, static_cast<od::port::field_spawners_state::St18ReturnFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::repair_machine_state::PartFields)) { layoutValue(v, a, static_cast<od::port::repair_machine_state::PartFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::glyde_attack_cast_state::Script3bBirdbotFields)) { layoutValue(v, a, static_cast<od::port::glyde_attack_cast_state::Script3bBirdbotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::homing_shot_state::ShotFields)) { layoutValue(v, a, static_cast<od::port::homing_shot_state::ShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st19_state::MachineFields)) { layoutValue(v, a, static_cast<od::port::st19_state::MachineFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ruin_effects_state::BlobFields)) { layoutValue(v, a, static_cast<od::port::ruin_effects_state::BlobFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::gauge_reaverbots_state::GaugeFields)) { layoutValue(v, a, static_cast<od::port::gauge_reaverbots_state::GaugeFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ruin_swimmers_state::TorpedoCompanionFields)) { layoutValue(v, a, static_cast<od::port::ruin_swimmers_state::TorpedoCompanionFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::mission_targets_state::SmokeColumnsFields)) { layoutValue(v, a, static_cast<od::port::mission_targets_state::SmokeColumnsFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::field_spawners_state::St18FightFields)) { layoutValue(v, a, static_cast<od::port::field_spawners_state::St18FightFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::mission_tank_state::TankFields)) { layoutValue(v, a, static_cast<od::port::mission_tank_state::TankFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::bars_and_gate_state::GateFields)) { layoutValue(v, a, static_cast<od::port::bars_and_gate_state::GateFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::field_spawners_state::St18RepairFields)) { layoutValue(v, a, static_cast<od::port::field_spawners_state::St18RepairFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ruin_guardian_state::BeamFields)) { layoutValue(v, a, static_cast<od::port::ruin_guardian_state::BeamFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::bars_and_gate_state::BarFields)) { layoutValue(v, a, static_cast<od::port::bars_and_gate_state::BarFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::glyde_missiles_state::GlowFields)) { layoutValue(v, a, static_cast<od::port::glyde_missiles_state::GlowFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::guardian_cast_state::CastFields)) { layoutValue(v, a, static_cast<od::port::guardian_cast_state::CastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::fleeing_reaverbot_state::ReaverbotFields)) { layoutValue(v, a, static_cast<od::port::fleeing_reaverbot_state::ReaverbotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::field_spawners_state::St16ClockFields)) { layoutValue(v, a, static_cast<od::port::field_spawners_state::St16ClockFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::glyde_attack_cast_state::Script39BirdbotFields)) { layoutValue(v, a, static_cast<od::port::glyde_attack_cast_state::Script39BirdbotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::field_spawners_state::St18TimerFields)) { layoutValue(v, a, static_cast<od::port::field_spawners_state::St18TimerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ruin_swimmers_state::SmallSwimmerFields)) { layoutValue(v, a, static_cast<od::port::ruin_swimmers_state::SmallSwimmerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::repair_machine_state::MachineFields)) { layoutValue(v, a, static_cast<od::port::repair_machine_state::MachineFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::field_spawners_state::St17ClockFields)) { layoutValue(v, a, static_cast<od::port::field_spawners_state::St17ClockFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ruin_guardian_state::OrbFields)) { layoutValue(v, a, static_cast<od::port::ruin_guardian_state::OrbFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::field_enemies_state::CritterFields)) { layoutValue(v, a, static_cast<od::port::field_enemies_state::CritterFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ch2_script_cast_state::FlashFields)) { layoutValue(v, a, static_cast<od::port::ch2_script_cast_state::FlashFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::glyde_missiles_state::MissileFields)) { layoutValue(v, a, static_cast<od::port::glyde_missiles_state::MissileFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::field_spawners_state::St15GroupFields)) { layoutValue(v, a, static_cast<od::port::field_spawners_state::St15GroupFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ch2_field_cast_state::CrashCarrierFields)) { layoutValue(v, a, static_cast<od::port::ch2_field_cast_state::CrashCarrierFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::VariantEffectFields)) { layoutValue(v, a, static_cast<od::port::VariantEffectFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ch2_field_cast_state::HoverFields)) { layoutValue(v, a, static_cast<od::port::ch2_field_cast_state::HoverFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st18_props_state::SunFields)) { layoutValue(v, a, static_cast<od::port::st18_props_state::SunFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::platform_boss_state::BoltFields)) { layoutValue(v, a, static_cast<od::port::platform_boss_state::BoltFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ruin_guardian_state::GuardianFields)) { layoutValue(v, a, static_cast<od::port::ruin_guardian_state::GuardianFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::BoxSpawnerFields)) { layoutValue(v, a, static_cast<od::port::BoxSpawnerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::floor_switch_state::SwitchFields)) { layoutValue(v, a, static_cast<od::port::floor_switch_state::SwitchFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::repair_machine_state::RollFields)) { layoutValue(v, a, static_cast<od::port::repair_machine_state::RollFields&>(s)); return true; }
    return false;
}
} // namespace od::test
