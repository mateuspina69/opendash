// The native records of this area in the exe layout (test-only).
#include "translator/actor_layout.hpp"
#include "content/saul_kada/bomb_critter_state.hpp"
#include "content/saul_kada/dropped_item_state.hpp"
#include "content/saul_kada/enemy_spawners_state.hpp"
#include "content/saul_kada/fire_ruin_bon_cast_state.hpp"
#include "content/saul_kada/fire_ruin_cutscene_cast_state.hpp"
#include "content/saul_kada/fire_ruin_servbot_cast_state.hpp"
#include "content/saul_kada/flame_tongues_state.hpp"
#include "content/saul_kada/gun_turret_state.hpp"
#include "content/saul_kada/hopping_reaverbot_state.hpp"
#include "content/saul_kada/lava_pit_state.hpp"
#include "content/saul_kada/lava_vents_state.hpp"
#include "content/saul_kada/particle_burst_state.hpp"
#include "content/saul_kada/pirate_servbots_state.hpp"
#include "content/saul_kada/pirate_tank_state.hpp"
#include "content/saul_kada/quicksand_state.hpp"
#include "content/saul_kada/race_bonus_state.hpp"
#include "content/saul_kada/ruin_fading_figure_state.hpp"
#include "content/saul_kada/sand_burrower_state.hpp"
#include "content/saul_kada/sand_trap_state.hpp"
#include "content/saul_kada/smoke_puff_state.hpp"
#include "content/saul_kada/st13_spin_blade_state.hpp"
#include "content/saul_kada/st20_props_state.hpp"
#include "content/saul_kada/st24_script_walker_state.hpp"
#include "content/saul_kada/st26_patrol_guard_state.hpp"
#include "content/saul_kada/st27_lava_valve_state.hpp"
#include "content/saul_kada/st27_mission_state.hpp"
#include "content/saul_kada/st27_partners.hpp"
#include "content/saul_kada/st27_respawner_state.hpp"
#include "content/saul_kada/st27_scroll_glow_state.hpp"
#include "content/saul_kada/st28_bomb_ribbon_state.hpp"
#include "content/saul_kada/st28_cast_props.hpp"
#include "content/saul_kada/st28_cast_props_state.hpp"
#include "content/saul_kada/st28_fire_bomb_state.hpp"
#include "content/saul_kada/st28_flyer_state.hpp"
#include "content/saul_kada/st28_guard_shot_state.hpp"
#include "content/saul_kada/st28_guard_state.hpp"
#include "content/saul_kada/st28_gunner_beam_state.hpp"
#include "content/saul_kada/st28_gunner_bomb_state.hpp"
#include "content/saul_kada/st28_gunner_boss_state.hpp"
#include "content/saul_kada/st28_gunner_helpers_state.hpp"
#include "content/saul_kada/st28_lava_boss_state.hpp"
#include "content/saul_kada/st2a_slide_block_state.hpp"
#include "content/saul_kada/st2b_cannon_boss_state.hpp"
#include "content/saul_kada/st2b_cannon_shell_state.hpp"
#include "content/saul_kada/st2b_props_state.hpp"
#include "content/saul_kada/st33_falling_rock_state.hpp"
#include "content/saul_kada/st33_guardian.hpp"
#include "content/saul_kada/st33_key_script_state.hpp"
#include "content/saul_kada/st34_flyer_shell_state.hpp"
#include "content/saul_kada/st34_ruin_flyer_state.hpp"
#include "content/saul_kada/st34_ruin_sentry_state.hpp"
#include "content/saul_kada/st3b_bomb_turret_state.hpp"
#include "content/saul_kada/st3b_flame_wall_state.hpp"
#include "content/saul_kada/st3b_glow_state.hpp"
#include "content/saul_kada/st3b_script5c_cast.hpp"
#include "content/saul_kada/st3c_boss_shot_state.hpp"
#include "content/saul_kada/st3c_gold_boss_state.hpp"
#include "content/saul_kada/st3c_gold_shockwave_state.hpp"
#include "content/saul_kada/st3c_minion_spawner_state.hpp"
#include "content/saul_kada/st3c_minion_state.hpp"
#include "content/saul_kada/st3c_pirate_cast_state.hpp"
#include "content/saul_kada/st3c_statue_state.hpp"
#include "content/saul_kada/st40_head_npc_state.hpp"
#include "content/saul_kada/st41_carrier.hpp"
#include "content/saul_kada/st41_carrier_pod_state.hpp"
#include "content/saul_kada/st41_enemy_generator_state.hpp"
#include "content/saul_kada/st41_gate_line_state.hpp"
#include "content/saul_kada/st41_hover_gunner_state.hpp"
#include "content/saul_kada/st41_pirate_shot_state.hpp"
#include "content/saul_kada/st41_sliding_door_state.hpp"
#include "content/saul_kada/st48_charger_state.hpp"
#include "content/saul_kada/st48_gunship_state.hpp"
#include "content/saul_kada/st4b_cast_state.hpp"
#include "content/saul_kada/steered_prop.hpp"
#include "content/saul_kada/trapped_miner_state.hpp"
#include "content/saul_kada/two_form_reaverbot_state.hpp"
#include "content/saul_kada/window_giant_state.hpp"

namespace od::test {
using namespace od::port;

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::DroppedPropParams& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc4, s.carrier);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::GateDebrisFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.spinX);
    layoutValue(v, a + 0xe, s.spinY);
    layoutValue(v, a + 0x18, s.angles);
    layoutValue(v, a + 0x20, s.velocity);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::GuardianFields& s) {
    using namespace od::port;
    for (size_t i = 0; i < s.glows.size(); ++i) layoutValue(v, a + 0x14c + uint32_t(i) * (4), s.glows[i]);
    layoutValue(v, a + 0x14c, s.flags);
    layoutValue(v, a + 0x14d, s.timer);
    layoutValue(v, a + 0x14e, s.animKey);
    layoutValue(v, a + 0x154, s.glowerHoldPose);
    layoutValue(v, a + 0x155, s.glowByteB);
    layoutValue(v, a + 0x156, s.glowerAnimKey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::PartnerFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0x160, s.flags);
    layoutValue(v, a + 0x162, s.target);
    layoutValue(v, a + 0x163, s.group);
    layoutValue(v, a + 0xc, s.partner);
    layoutValue(v, a + 0x14c, s.bodyBox);
    layoutValue(v, a + 0x154, s.handBox);
    layoutValue(v, a + 0x15c, s.carryYaw);
    layoutValue(v, a + 0x15e, s.wallYaw);
    layoutValue(v, a + 0x161, s.animKey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::PodCarrierFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0x160, s.pods);
    layoutValue(v, a + 0x162, s.podsBack);
    layoutValue(v, a + 0x150, s.blinkTicks);
    layoutValue(v, a + 0x154, s.hitBox);
    layoutValue(v, a + 0x15c, s.hitOff);
    layoutValue(v, a + 0x15d, s.bobPhase);
    layoutValue(v, a + 0x15e, s.restY);
    layoutValue(v, a + 0x161, s.podsSignalled);
    for (size_t i = 0; i < s.lights.size(); ++i) layoutValue(v, a + 0x164 + uint32_t(i) * (4), s.lights[i]);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0x152 + uint32_t(i) * (1), s.clearedPad0[i]);
    for (size_t i = 0; i < s.clearedPad1.size(); ++i) layoutValue(v, a + 0x163 + uint32_t(i) * (1), s.clearedPad1[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::SteeredPropFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xbc, s.targetYaw);
    layoutValue(v, a + 0xbe, s.targetPitch);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ThrownPropParams& s) {
    using namespace od::port;
    layoutValue(v, a + 0xbc, s.carrier);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::bomb_critter_state::CritterFields& s) {
    using namespace od::port::bomb_critter_state;
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.fuseLit);
    layoutValue(v, a + 0x156, s.nearTicks);
    layoutValue(v, a + 0x158, s.fuseTicks);
    layoutValue(v, a + 0x15a, s.bobPhase);
    layoutValue(v, a + 0x15b, s.smokeTicks);
    layoutValue(v, a + 0x158, s.fuseTicksLow);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::dropped_item_state::ItemFields& s) {
    using namespace od::port::dropped_item_state;
    layoutValue(v, a + 0xbc, s.contacts);
    layoutValue(v, a + 0xbd, s.bobPhase);
    layoutValue(v, a + 0xbe, s.restY);
    layoutValue(v, a + 0xc0, s.settleTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::enemy_spawners_state::SpawnerFields& s) {
    using namespace od::port::enemy_spawners_state;
    layoutValue(v, a + 0x10, s.x);
    layoutValue(v, a + 0x12, s.y);
    layoutValue(v, a + 0x14, s.z);
    layoutValue(v, a + 0x18, s.countdown);
    layoutValue(v, a + 0x18, s.flagSet);
    layoutValue(v, a + 0x18, s.markerShown);
    layoutValue(v, a + 0x19, s.side);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0x1a + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::fire_ruin_bon_cast_state::BonCastFields& s) {
    using namespace od::port::fire_ruin_bon_cast_state;
    layoutValue(v, a + 0x14c, s.ticks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::fire_ruin_cutscene_cast_state::DropperFields& s) {
    using namespace od::port::fire_ruin_cutscene_cast_state;
    layoutValue(v, a + 0x14c, s.groundY);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::fire_ruin_cutscene_cast_state::HolderFields& s) {
    using namespace od::port::fire_ruin_cutscene_cast_state;
    layoutValue(v, a + 0x14c, s.heldProp);
    layoutValue(v, a + 0x150, s.holding);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::fire_ruin_cutscene_cast_state::TickCastFields& s) {
    using namespace od::port::fire_ruin_cutscene_cast_state;
    layoutValue(v, a + 0x14c, s.ticks);
    layoutValue(v, a + 0x150, s.animRuns);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::fire_ruin_servbot_cast_state::ServbotFields& s) {
    using namespace od::port::fire_ruin_servbot_cast_state;
    layoutValue(v, a + 0x14c, s.headTurn);
    layoutValue(v, a + 0x14d, s.posed);
    layoutValue(v, a + 0x14e, s.blinkTick);
    layoutValue(v, a + 0x14f, s.blinkRow);
    layoutValue(v, a + 0x150, s.blinking);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::flame_tongues_state::TongueFields& s) {
    using namespace od::port::flame_tongues_state;
    layoutValue(v, a + 0xc, s.colour);
    layoutValue(v, a + 0x10, s.spawnX);
    layoutValue(v, a + 0x12, s.spawnY);
    layoutValue(v, a + 0x14, s.spawnZ);
    layoutValue(v, a + 0x16, s.spawnYaw);
    layoutValue(v, a + 0x10, s.growth);
    layoutValue(v, a + 0x12, s.height);
    for (size_t i = 0; i < s.velocityX.size(); ++i) layoutValue(v, a + 0x14 + uint32_t(i) * (2), s.velocityX[i]);
    for (size_t i = 0; i < s.x.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (2), s.x[i]);
    for (size_t i = 0; i < s.z.size(); ++i) layoutValue(v, a + 0x1c + uint32_t(i) * (2), s.z[i]);
    for (size_t i = 0; i < s.velocityZ.size(); ++i) layoutValue(v, a + 0x20 + uint32_t(i) * (2), s.velocityZ[i]);
    for (size_t i = 0; i < s.shapes.size(); ++i) layoutValue(v, a + 0x24 + uint32_t(i) * (1), s.shapes[i]);
    layoutValue(v, a + 0x26, s.tiltB);
    layoutValue(v, a + 0x27, s.tiltA);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::gun_turret_state::TurretFields& s) {
    using namespace od::port::gun_turret_state;
    layoutValue(v, a + 0x15f, s.smokeTicks);
    layoutValue(v, a + 0x160, s.farTicks);
    layoutValue(v, a + 0x162, s.aimTimer);
    layoutValue(v, a + 0x163, s.shots);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.gunPitch);
    layoutValue(v, a + 0x156, s.gunYaw);
    layoutValue(v, a + 0x158, s.gunRoll);
    layoutValue(v, a + 0x15c, s.recentDamage);
    layoutValue(v, a + 0x15e, s.recoiled);
    layoutValue(v, a + 0x161, s.sweepTimer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::hopping_reaverbot_state::HopFields& s) {
    using namespace od::port::hopping_reaverbot_state;
    layoutValue(v, a + 0x162, s.bumpTicks);
    layoutValue(v, a + 0x163, s.bumps);
    layoutValue(v, a + 0x164, s.contact);
    layoutValue(v, a + 0x165, s.flags);
    layoutValue(v, a + 0x166, s.animKey);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.target);
    layoutValue(v, a + 0x15c, s.stunDamage);
    layoutValue(v, a + 0x15e, s.neighbour);
    layoutValue(v, a + 0x160, s.spin);
    layoutValue(v, a + 0x158, s.timer);
    layoutValue(v, a + 0x15a, s.heading);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::lava_pit_state::CreatureFields& s) {
    using namespace od::port::lava_pit_state;
    layoutValue(v, a + 0xb, s.hidden);
    layoutValue(v, a + 0x3e, s.velocitySpare);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc8, s.ticks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::lava_pit_state::FlameFields& s) {
    using namespace od::port::lava_pit_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.burnTicks);
    layoutValue(v, a + 0xc6, s.brightness);
    layoutValue(v, a + 0xc8, s.size);
    layoutValue(v, a + 0xca, s.frameU);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::lava_pit_state::PitFields& s) {
    using namespace od::port::lava_pit_state;
    layoutValue(v, a + 0x10, s.creature);
    layoutValue(v, a + 0x16, s.lastSpot);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::lava_vents_state::VentsFields& s) {
    using namespace od::port::lava_vents_state;
    layoutValue(v, a + 0x18, s.plumeTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::particle_burst_state::BurstFields& s) {
    using namespace od::port::particle_burst_state;
    layoutValue(v, a + 0x4e, s.spinSpare);
    layoutValue(v, a + 0x64, s.hitBox);
    for (size_t i = 0; i < s.particles.size(); ++i) layoutValue(v, a + 0x74 + uint32_t(i) * (8), s.particles[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::particle_burst_state::BurstParticle& s) {
    using namespace od::port::particle_burst_state;
    layoutValue(v, a + 0, s.point);
    layoutValue(v, a + 6, s.word);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::pirate_servbots_state::BomberFields& s) {
    using namespace od::port::pirate_servbots_state;
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15d, s.thrownOut);
    layoutValue(v, a + 0x15e, s.wait);
    layoutValue(v, a + 0x160, s.bombsLeft);
    layoutValue(v, a + 0x166, s.homeYaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::pirate_servbots_state::ServbotFields& s) {
    using namespace od::port::pirate_servbots_state;
    layoutValue(v, a + 0x162, s.flags);
    layoutValue(v, a + 0x163, s.wallTimer);
    layoutValue(v, a + 0x164, s.ledge);
    layoutValue(v, a + 0x165, s.reacted);
    layoutValue(v, a + 0x166, s.offScreen);
    layoutValue(v, a + 0x167, s.thrownHit);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.contact);
    layoutValue(v, a + 0x15d, s.side);
    layoutValue(v, a + 0x15e, s.timer);
    layoutValue(v, a + 0x160, s.faceYaw);
    layoutValue(v, a + 0xc, s.goneFlag);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::pirate_tank_state::TankFields& s) {
    using namespace od::port::pirate_tank_state;
    layoutValue(v, a + 0x15f, s.turretAccel);
    layoutValue(v, a + 0x160, s.timer);
    layoutValue(v, a + 0x162, s.turretState);
    layoutValue(v, a + 0x163, s.status);
    layoutValue(v, a + 0x164, s.flags);
    layoutValue(v, a + 0x166, s.count);
    layoutValue(v, a + 0x167, s.turnTimer);
    layoutValue(v, a + 0x16a, s.burst);
    layoutValue(v, a + 0x2e, s.turretYaw);
    layoutValue(v, a + 0x3e, s.turretTarget);
    layoutValue(v, a + 0x4e, s.rollGrowth);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.pitchTarget);
    layoutValue(v, a + 0x156, s.rollTarget);
    layoutValue(v, a + 0x158, s.driveYaw);
    layoutValue(v, a + 0x15a, s.resetMark);
    layoutValue(v, a + 0x15b, s.treads);
    layoutValue(v, a + 0x15c, s.swayDir);
    layoutValue(v, a + 0x15e, s.turretStep);
    layoutValue(v, a + 0x161, s.stepTimer);
    layoutValue(v, a + 0x168, s.bulletYaw);
    layoutValue(v, a + 0x16b, s.shellNear);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::quicksand_state::PitParams& s) {
    using namespace od::port::quicksand_state;
    layoutValue(v, a + 0xc, s.pullStrength);
    layoutValue(v, a + 0xe, s.radius);
    layoutValue(v, a + 0x10, s.point);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::race_bonus_state::BonusFields& s) {
    using namespace od::port::race_bonus_state;
    layoutValue(v, a + 0xbc, s.rise);
    layoutValue(v, a + 0xbe, s.tick);
    layoutValue(v, a + 0xc0, s.restY);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_fading_figure_state::FigureFields& s) {
    using namespace od::port::ruin_fading_figure_state;
    layoutValue(v, a + 0x14c, s.fadeTimer);
    layoutValue(v, a + 0x14e, s.wait);
    layoutValue(v, a + 0x150, s.fading);
    layoutValue(v, a + 0x151, s.red);
    layoutValue(v, a + 0x152, s.green);
    layoutValue(v, a + 0x153, s.blue);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::sand_burrower_state::BurrowerFields& s) {
    using namespace od::port::sand_burrower_state;
    layoutValue(v, a + 0x15f, s.repeats);
    layoutValue(v, a + 0x160, s.group);
    layoutValue(v, a + 0x2e, s.headSway);
    layoutValue(v, a + 0x3e, s.firstTick);
    layoutValue(v, a + 0x14c, s.record);
    layoutValue(v, a + 0x150, s.secondHomeX);
    layoutValue(v, a + 0x152, s.secondHomeZ);
    layoutValue(v, a + 0x154, s.spawnYaw);
    layoutValue(v, a + 0x156, s.damageTaken);
    layoutValue(v, a + 0x158, s.spawnHeight);
    layoutValue(v, a + 0x15a, s.flags);
    layoutValue(v, a + 0x15b, s.timerA);
    layoutValue(v, a + 0x15c, s.timerB);
    layoutValue(v, a + 0x15d, s.attack);
    layoutValue(v, a + 0x15e, s.lastAttack);
    layoutValue(v, a + 0x161, s.groupMarks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::sand_burrower_state::BurrowerRecord& s) {
    using namespace od::port::sand_burrower_state;
    layoutValue(v, a + 0x0, s.body);
    layoutValue(v, a + 0x8, s.limbs);
    layoutValue(v, a + 0x10, s.startPoint);
    layoutValue(v, a + 0x18, s.sinkPoint);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::sand_trap_state::TrapFields& s) {
    using namespace od::port::sand_trap_state;
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.timer);
    layoutValue(v, a + 0x156, s.escapePresses);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::smoke_puff_state::PuffFields& s) {
    using namespace od::port::smoke_puff_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.tick);
    layoutValue(v, a + 0xc4, s.tickLow);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st13_spin_blade_state::BladeFields& s) {
    using namespace od::port::st13_spin_blade_state;
    layoutValue(v, a + 0x4e, s.spinAccel);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x156, s.bladeTurn);
    layoutValue(v, a + 0x158, s.topSpeed);
    layoutValue(v, a + 0x15a, s.soundTicks);
    layoutValue(v, a + 0x15c, s.restartTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st20_props_state::PropFields& s) {
    using namespace od::port::st20_props_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.word);
    layoutValue(v, a + 0xc8, s.tick);
    layoutValue(v, a + 0xca, s.ringCooldown);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st24_script_walker_state::WalkerFields& s) {
    using namespace od::port::st24_script_walker_state;
    layoutValue(v, a + 0x14c, s.walkTicks);
    layoutValue(v, a + 0x14e, s.holdPose);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st26_patrol_guard_state::GuardFields& s) {
    using namespace od::port::st26_patrol_guard_state;
    layoutValue(v, a + 0xc, s.count);
    layoutValue(v, a + 0xc, s.countSigned);
    layoutValue(v, a + 0xe, s.range);
    layoutValue(v, a + 0x168, s.flags);
    layoutValue(v, a + 0x169, s.wallHit);
    layoutValue(v, a + 0x16a, s.dead);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.homeX);
    layoutValue(v, a + 0x15e, s.homeZ);
    layoutValue(v, a + 0x160, s.heading);
    layoutValue(v, a + 0x162, s.wallYaw);
    layoutValue(v, a + 0x164, s.knockYaw);
    layoutValue(v, a + 0x166, s.neighbour);
    layoutValue(v, a + 0x16b, s.leg);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st27_lava_valve_state::ValveFields& s) {
    using namespace od::port::st27_lava_valve_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc8, s.shakeTicks);
    layoutValue(v, a + 0xc4, s.regenTicks);
    layoutValue(v, a + 0xc6, s.homeYaw);
    layoutValue(v, a + 0xc9, s.damageFraction);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st27_mission_state::MissionFields& s) {
    using namespace od::port::st27_mission_state;
    layoutValue(v, a + 0x18, s.valve);
    layoutValue(v, a + 0x10, s.bon);
    layoutValue(v, a + 0x14, s.tron);
    layoutValue(v, a + 0x1c, s.hintTicks);
    layoutValue(v, a + 0x1e, s.hintFlags);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st27_respawner_state::RespawnerFields& s) {
    using namespace od::port::st27_respawner_state;
    for (size_t i = 0; i < s.slots.size(); ++i) layoutValue(v, a + 0xbc + uint32_t(i) * (1), s.slots[i]);
    for (size_t i = 0; i < s.countdowns.size(); ++i) layoutValue(v, a + 0xc4 + uint32_t(i) * (1), s.countdowns[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st27_scroll_glow_state::GlowFields& s) {
    using namespace od::port::st27_scroll_glow_state;
    layoutValue(v, a + 0x18, s.scroll);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st28_bomb_ribbon_state::RibbonFields& s) {
    using namespace od::port::st28_bomb_ribbon_state;
    layoutValue(v, a + 0xc, s.grey);
    layoutValue(v, a + 0xd, s.frame);
    layoutValue(v, a + 0x18, s.firstBomb);
    layoutValue(v, a + 0x1c, s.secondBomb);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st28_cast_props_state::ThrownPropFields& s) {
    using namespace od::port::st28_cast_props_state;
    layoutValue(v, a, static_cast<od::port::ThrownPropParams&>(s));
    layoutValue(v, a + 0xc0, s.ticks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st28_fire_bomb_state::BombFields& s) {
    using namespace od::port::st28_fire_bomb_state;
    layoutValue(v, a + 0xe, s.travelYaw);
    layoutValue(v, a + 0x2e, s.damage);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.boss);
    layoutValue(v, a + 0xc8, s.firstOrbiter);
    layoutValue(v, a + 0xcc, s.secondOrbiter);
    layoutValue(v, a + 0xd0, s.homingTime);
    layoutValue(v, a + 0xd4, s.turnRate);
    layoutValue(v, a + 0xd6, s.status);
    layoutValue(v, a + 0xd7, s.spare);
    layoutValue(v, a + 0xd8, s.fallResult);
    layoutValue(v, a + 0xd9, s.humTimer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st28_flyer_state::FlyerFields& s) {
    using namespace od::port::st28_flyer_state;
    layoutValue(v, a + 0x166, s.flagsLow);
    layoutValue(v, a + 0x168, s.bobCarry);
    layoutValue(v, a + 0x169, s.dropsPickups);
    layoutValue(v, a + 0x16a, s.animEntry);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.stunHits);
    layoutValue(v, a + 0x15e, s.timer);
    layoutValue(v, a + 0x160, s.swayAngle);
    layoutValue(v, a + 0x162, s.bobAngle);
    layoutValue(v, a + 0x164, s.spin);
    layoutValue(v, a + 0x166, s.flags);
    layoutValue(v, a + 0x15e, s.timerLow);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st28_guard_shot_state::ShotFields& s) {
    using namespace od::port::st28_guard_shot_state;
    layoutValue(v, a + 0xc0, s.hitBox);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xbc + uint32_t(i) * (1), s.clearedPad0[i]);
    for (size_t i = 0; i < s.clearedPad1.size(); ++i) layoutValue(v, a + 0xc8 + uint32_t(i) * (1), s.clearedPad1[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st28_guard_state::GuardFields& s) {
    using namespace od::port::st28_guard_state;
    layoutValue(v, a + 0x164, s.shotType);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.timer);
    layoutValue(v, a + 0x156, s.shotAnim);
    layoutValue(v, a + 0x158, s.flags);
    layoutValue(v, a + 0x15a, s.headYaw);
    layoutValue(v, a + 0x15c, s.headTarget);
    layoutValue(v, a + 0x15e, s.homeX);
    layoutValue(v, a + 0x160, s.homeZ);
    layoutValue(v, a + 0x162, s.shotSound);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st28_gunner_beam_state::BeamOwnFields& s) {
    using namespace od::port::st28_gunner_beam_state;
    layoutValue(v, a + 0xc0, s.hitBox);
    layoutValue(v, a + 0xd0, s.effectTicks);
    layoutValue(v, a + 0xd3, s.dotPhase);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st28_gunner_bomb_state::BombFields& s) {
    using namespace od::port::st28_gunner_bomb_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.timer);
    for (size_t i = 0; i < s.seeds.size(); ++i) layoutValue(v, a + 0xc6 + uint32_t(i) * (4), s.seeds[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st28_gunner_bomb_state::FlameSeed& s) {
    using namespace od::port::st28_gunner_bomb_state;
    layoutValue(v, a + 0, s.delay);
    layoutValue(v, a + 2, s.size);
    layoutValue(v, a + 3, s.phase);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st28_gunner_boss_state::BossFields& s) {
    using namespace od::port::st28_gunner_boss_state;
    layoutValue(v, a + 0x166, s.flags);
    layoutValue(v, a + 0x167, s.wallContact);
    layoutValue(v, a + 0x168, s.timer);
    layoutValue(v, a + 0x169, s.count);
    layoutValue(v, a + 0x16a, s.dead);
    layoutValue(v, a + 0x14c, s.bodyBox);
    layoutValue(v, a + 0x154, s.armBox);
    layoutValue(v, a + 0x15c, s.beam);
    layoutValue(v, a + 0x164, s.damageTaken);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st28_gunner_helpers_state::HelperFields& s) {
    using namespace od::port::st28_gunner_helpers_state;
    layoutValue(v, a + 0x168, s.flags);
    layoutValue(v, a + 0x169, s.contact);
    layoutValue(v, a + 0x16a, s.entryKind);
    layoutValue(v, a + 0x14c, s.boss);
    layoutValue(v, a + 0x150, s.hitBox);
    layoutValue(v, a + 0x160, s.hitYaw);
    layoutValue(v, a + 0x162, s.timer);
    layoutValue(v, a + 0x164, s.counter);
    layoutValue(v, a + 0x166, s.neighbour);
    layoutValue(v, a + 0x16b, s.ledgeNext);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st28_lava_boss_state::BossFields& s) {
    using namespace od::port::st28_lava_boss_state;
    layoutValue(v, a + 0xc, s.tintRed);
    layoutValue(v, a + 0xd, s.bombOutAtGetUp);
    layoutValue(v, a + 0xe, s.travelYaw);
    layoutValue(v, a + 0x2e, s.bombOut);
    layoutValue(v, a + 0x3e, s.inBoxWord);
    layoutValue(v, a + 0x48, s.spinSoundTimer);
    layoutValue(v, a + 0x166, s.count);
    layoutValue(v, a + 0x167, s.pose);
    layoutValue(v, a + 0x168, s.inBox);
    layoutValue(v, a + 0x169, s.status);
    layoutValue(v, a + 0x16a, s.fallResult);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.bodyBox);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.walkPhase);
    layoutValue(v, a + 0x160, s.groundNow);
    layoutValue(v, a + 0x162, s.groundBefore);
    layoutValue(v, a + 0x164, s.damageTaken);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st2a_slide_block_state::BlockFields& s) {
    using namespace od::port::st2a_slide_block_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.home);
    layoutValue(v, a + 0xc6, s.rockIndex);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st2b_cannon_boss_state::BossFields& s) {
    using namespace od::port::st2b_cannon_boss_state;
    layoutValue(v, a + 0x160, s.lobFired);
    layoutValue(v, a + 0x162, s.pitchSoundTicks);
    layoutValue(v, a + 0x163, s.invulnerable);
    layoutValue(v, a + 0x164, s.dying);
    layoutValue(v, a + 0x165, s.provoked);
    layoutValue(v, a + 0x4e, s.yawAccel);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.pitch);
    layoutValue(v, a + 0x156, s.barrelYaw);
    layoutValue(v, a + 0x158, s.pitchTarget);
    layoutValue(v, a + 0x15a, s.yawTarget);
    layoutValue(v, a + 0x15c, s.yawMover);
    layoutValue(v, a + 0x15d, s.pitchMoving);
    layoutValue(v, a + 0x15e, s.timer);
    layoutValue(v, a + 0x161, s.turnSoundTicks);
    layoutValue(v, a + 0x15e, s.timerLow);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st2b_cannon_shell_state::ShellFields& s) {
    using namespace od::port::st2b_cannon_shell_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.targetX);
    layoutValue(v, a + 0xc6, s.targetZ);
    layoutValue(v, a + 0xc8, s.blastSize);
    layoutValue(v, a + 0xca, s.bounceTicks);
    layoutValue(v, a + 0xcc, s.warned);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xcb + uint32_t(i) * (1), s.clearedPad0[i]);
    for (size_t i = 0; i < s.clearedPad1.size(); ++i) layoutValue(v, a + 0xcd + uint32_t(i) * (1), s.clearedPad1[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st2b_props_state::PlateFields& s) {
    using namespace od::port::st2b_props_state;
    layoutValue(v, a + 0xbc, s.homeY);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st2b_props_state::PotFields& s) {
    using namespace od::port::st2b_props_state;
    layoutValue(v, a + 0x2e, s.rotationSpare);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.word);
    layoutValue(v, a + 0xc4, s.potFlags);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st33_falling_rock_state::RockFields& s) {
    using namespace od::port::st33_falling_rock_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.startMark);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st33_key_script_state::KeyItemFields& s) {
    using namespace od::port::st33_key_script_state;
    layoutValue(v, a + 0xbc, s.hitBox);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st34_flyer_shell_state::ShellFields& s) {
    using namespace od::port::st34_flyer_shell_state;
    layoutValue(v, a + 0x160, s.flags);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.coreBox);
    layoutValue(v, a + 0x15c, s.dyingTimer);
    layoutValue(v, a + 0x15e, s.decay);
    layoutValue(v, a + 0x161, s.wallHit);
    layoutValue(v, a + 0x15c, s.dyingTimerLow);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st34_ruin_flyer_state::FlyerFields& s) {
    using namespace od::port::st34_ruin_flyer_state;
    layoutValue(v, a + 0xc, s.alerted);
    layoutValue(v, a + 0xf, s.risesFirst);
    layoutValue(v, a + 0x164, s.flags);
    layoutValue(v, a + 0x165, s.contact);
    layoutValue(v, a + 0x166, s.group);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.home);
    layoutValue(v, a + 0x15a, s.homeYaw);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.pitch);
    layoutValue(v, a + 0x160, s.gauge);
    layoutValue(v, a + 0x162, s.reflectedYaw);
    layoutValue(v, a + 0x15c, s.timerLow);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st34_ruin_sentry_state::SentryFields& s) {
    using namespace od::port::st34_ruin_sentry_state;
    layoutValue(v, a + 0x160, s.resumeStep);
    layoutValue(v, a + 0x162, s.flags);
    layoutValue(v, a + 0x163, s.marchTimer);
    layoutValue(v, a + 0x164, s.contact);
    layoutValue(v, a + 0x165, s.staggerCount);
    layoutValue(v, a + 0x166, s.frozen);
    layoutValue(v, a + 0x167, s.group);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.counter);
    layoutValue(v, a + 0x161, s.resume);
    layoutValue(v, a + 0x168, s.dropX);
    layoutValue(v, a + 0x16a, s.dropZ);
    layoutValue(v, a + 0x15c, s.timerLow);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st3b_bomb_turret_state::TurretFields& s) {
    using namespace od::port::st3b_bomb_turret_state;
    layoutValue(v, a + 0x160, s.timer);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.yawOffset);
    layoutValue(v, a + 0x15e, s.bombHeight);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st3b_flame_wall_state::FlameFields& s) {
    using namespace od::port::st3b_flame_wall_state;
    layoutValue(v, a + 0x74, s.phaseA);
    layoutValue(v, a + 0x76, s.phaseB);
    layoutValue(v, a + 0x78, s.ticksLeft);
    layoutValue(v, a + 0x64, s.hitBox);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0x6c + uint32_t(i) * (1), s.clearedPad0[i]);
    for (size_t i = 0; i < s.clearedPad1.size(); ++i) layoutValue(v, a + 0x7a + uint32_t(i) * (1), s.clearedPad1[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st3b_glow_state::GlowFields& s) {
    using namespace od::port::st3b_glow_state;
    layoutValue(v, a + 0xc4, s.brightness);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st3c_boss_shot_state::HomingFields& s) {
    using namespace od::port::st3c_boss_shot_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xf, s.launched);
    layoutValue(v, a + 0xc8, s.timer);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xc4 + uint32_t(i) * (1), s.clearedPad0[i]);
    for (size_t i = 0; i < s.clearedPad1.size(); ++i) layoutValue(v, a + 0xca + uint32_t(i) * (1), s.clearedPad1[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st3c_boss_shot_state::PieceFields& s) {
    using namespace od::port::st3c_boss_shot_state;
    layoutValue(v, a + 0xbc, s.boss);
    layoutValue(v, a + 0x86, s.rootPad);
    for (size_t i = 0; i < s.rootWords.size(); ++i) layoutValue(v, a + 0x74 + uint32_t(i) * (4), s.rootWords[i]);
    layoutValue(v, a + 0xc0, s.hitBox);
    layoutValue(v, a + 0xc8, s.ring);
    layoutValue(v, a + 0xcc, s.ringAngles);
    layoutValue(v, a + 0xd4, s.timer);
    layoutValue(v, a + 0xd6, s.ringSize);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xd8 + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st3c_gold_boss_state::BossBlock& s) {
    using namespace od::port::st3c_gold_boss_state;
    for (size_t i = 0; i < s.boxes.size(); ++i) layoutValue(v, a + 0x00 + uint32_t(i) * (8), s.boxes[i]);
    layoutValue(v, a + 0x20, s.distance);
    layoutValue(v, a + 0x24, s.flags);
    layoutValue(v, a + 0x24, s.flagsLow);
    layoutValue(v, a + 0x28, s.timer);
    layoutValue(v, a + 0x28, s.timerLow);
    layoutValue(v, a + 0x2a, s.phaseTwoTimer);
    for (size_t i = 0; i < s.pieces.size(); ++i) layoutValue(v, a + 0x2c + uint32_t(i) * (4), s.pieces[i]);
    layoutValue(v, a + 0x34, s.playerAngle);
    layoutValue(v, a + 0x36, s.targetAngle);
    layoutValue(v, a + 0x38, s.hitMode);
    layoutValue(v, a + 0x39, s.scriptCursor);
    layoutValue(v, a + 0x3a, s.scriptTick);
    layoutValue(v, a + 0x3b, s.rising);
    layoutValue(v, a + 0x3c, s.heightOffset);
    layoutValue(v, a + 0x3e, s.ring);
    layoutValue(v, a + 0x3f, s.radiusPick);
    layoutValue(v, a + 0x40, s.targetX);
    layoutValue(v, a + 0x42, s.targetZ);
    layoutValue(v, a + 0x46, s.stepSoundTimer);
    layoutValue(v, a + 0x48, s.ringMoves);
    layoutValue(v, a + 0x44, s.unread0);
    for (size_t i = 0; i < s.unread1.size(); ++i) layoutValue(v, a + 0x49 + uint32_t(i) * (1), s.unread1[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st3c_gold_boss_state::BossFields& s) {
    using namespace od::port::st3c_gold_boss_state;
    layoutValue(v, a + 0xd, s.defeatTicks);
    layoutValue(v, a + 0xe, s.status);
    layoutValue(v, a + 0xf, s.statueSignals);
    layoutValue(v, a + 0x3e, s.speedTarget);
    layoutValue(v, a + 0x14c, s.block);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st3c_gold_shockwave_state::WaveFields& s) {
    using namespace od::port::st3c_gold_shockwave_state;
    layoutValue(v, a + 0xc0, s.stepY);
    layoutValue(v, a + 0xbc, s.timer);
    layoutValue(v, a + 0xbe, s.stepXZ);
    layoutValue(v, a + 0xbc, s.timerLow);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st3c_minion_spawner_state::SpawnerFields& s) {
    using namespace od::port::st3c_minion_spawner_state;
    layoutValue(v, a + 0x10, s.timer);
    layoutValue(v, a + 0x12, s.alive);
    for (size_t i = 0; i < s.slots.size(); ++i) layoutValue(v, a + 0x14 + uint32_t(i) * (1), s.slots[i]);
    layoutValue(v, a + 0x17, s.nearTries);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st3c_minion_state::MinionFields& s) {
    using namespace od::port::st3c_minion_state;
    layoutValue(v, a + 0x160, s.servbotGone);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.flags);
    layoutValue(v, a + 0x156, s.timer);
    layoutValue(v, a + 0x158, s.playerYaw);
    layoutValue(v, a + 0x15a, s.playerDistance);
    layoutValue(v, a + 0x15c, s.escapeYaw);
    layoutValue(v, a + 0x154, s.flagsLow);
    layoutValue(v, a + 0xc, s.spawnerSlot);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st3c_pirate_cast_state::PirateFields& s) {
    using namespace od::port::st3c_pirate_cast_state;
    layoutValue(v, a + 0x14c, s.sceneWord);
    layoutValue(v, a + 0x14e, s.sceneFlag);
    layoutValue(v, a + 0x14f, s.sceneByte);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st3c_statue_state::PartFields& s) {
    using namespace od::port::st3c_statue_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.flagsWord);
    layoutValue(v, a + 0xc4, s.flags);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st3c_statue_state::StatueFields& s) {
    using namespace od::port::st3c_statue_state;
    layoutValue(v, a + 0x152, s.holdPose);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st40_head_npc_state::NpcFields& s) {
    using namespace od::port::st40_head_npc_state;
    layoutValue(v, a + 0x14c, s.flags);
    layoutValue(v, a + 0x150, s.angleA);
    layoutValue(v, a + 0x152, s.head);
    layoutValue(v, a + 0x154, s.angleC);
    layoutValue(v, a + 0x156, s.headTargetAngle);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st41_carrier_pod_state::PodFields& s) {
    using namespace od::port::st41_carrier_pod_state;
    layoutValue(v, a + 0x3e, s.anchorX);
    layoutValue(v, a + 0x46, s.anchorY);
    layoutValue(v, a + 0x2e, s.anchorZ);
    layoutValue(v, a + 0x4e, s.phase);
    layoutValue(v, a + 0xbc, s.carrier);
    layoutValue(v, a + 0xc1, s.amplitude);
    layoutValue(v, a + 0xc2, s.glowMode);
    layoutValue(v, a + 0xc3, s.glowLevel);
    layoutValue(v, a + 0xc4, s.hitBox);
    layoutValue(v, a + 0xc0, s.timer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st41_enemy_generator_state::GeneratorFields& s) {
    using namespace od::port::st41_enemy_generator_state;
    layoutValue(v, a + 0x166, s.flagsLow);
    layoutValue(v, a + 0x168, s.bobCarry);
    layoutValue(v, a + 0x16a, s.animEntry);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.flinchHits);
    layoutValue(v, a + 0x15e, s.timer);
    layoutValue(v, a + 0x160, s.swayAngle);
    layoutValue(v, a + 0x162, s.bobAngle);
    layoutValue(v, a + 0x164, s.spin);
    layoutValue(v, a + 0x166, s.flags);
    layoutValue(v, a + 0x15e, s.timerLow);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st41_gate_line_state::LineFields& s) {
    using namespace od::port::st41_gate_line_state;
    layoutValue(v, a + 0x18, s.yaw);
    layoutValue(v, a + 0x1a, s.side);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st41_hover_gunner_state::GunnerFields& s) {
    using namespace od::port::st41_hover_gunner_state;
    layoutValue(v, a + 0x160, s.flags);
    layoutValue(v, a + 0x16a, s.animEntry);
    layoutValue(v, a + 0x150, s.timer);
    layoutValue(v, a + 0x152, s.turnYaw);
    layoutValue(v, a + 0x154, s.hitBox);
    layoutValue(v, a + 0x15c, s.stagger);
    layoutValue(v, a + 0x15e, s.cooldown);
    layoutValue(v, a + 0x161, s.contact);
    layoutValue(v, a + 0x162, s.aimX);
    layoutValue(v, a + 0x164, s.aimZ);
    layoutValue(v, a + 0x166, s.wallSide);
    layoutValue(v, a + 0x168, s.aimSpare);
    layoutValue(v, a + 0x150, s.timerLow);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st41_pirate_shot_state::ShotFields& s) {
    using namespace od::port::st41_pirate_shot_state;
    layoutValue(v, a + 0xc0, s.turnDelay);
    layoutValue(v, a + 0xc2, s.levelY);
    layoutValue(v, a + 0x4e, s.bobPhase);
    layoutValue(v, a + 0xc4, s.hitBox);
    layoutValue(v, a + 0xcc, s.u);
    layoutValue(v, a + 0xce, s.bobAmplitude);
    layoutValue(v, a + 0xd0, s.life);
    layoutValue(v, a + 0xd2, s.clut);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xd4 + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st41_sliding_door_state::DoorFields& s) {
    using namespace od::port::st41_sliding_door_state;
    layoutValue(v, a + 0xbc, s.slideTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st48_charger_state::ChargerFields& s) {
    using namespace od::port::st48_charger_state;
    layoutValue(v, a + 0x2e, s.heading);
    layoutValue(v, a + 0xe, s.noLockOn);
    layoutValue(v, a + 0xf, s.skidSoundTimer);
    layoutValue(v, a + 0x166, s.wallCheck);
    layoutValue(v, a + 0x167, s.knockDown);
    layoutValue(v, a + 0x168, s.wheelHits);
    layoutValue(v, a + 0x169, s.ledgeCheck);
    layoutValue(v, a + 0x16a, s.hitWhileSpinning);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.bodyBox);
    layoutValue(v, a + 0x15c, s.walls);
    layoutValue(v, a + 0x162, s.timer);
    layoutValue(v, a + 0x164, s.target);
    layoutValue(v, a + 0x16b, s.dust);
    for (size_t i = 0; i < s.jets.size(); ++i) layoutValue(v, a + 0x160 + uint32_t(i) * (1), s.jets[i]);
    layoutValue(v, a + 0x160, s.jetsWord);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st48_gunship_state::ShipFields& s) {
    using namespace od::port::st48_gunship_state;
    layoutValue(v, a + 0x2e, s.ground);
    layoutValue(v, a + 0x3e, s.rollTarget);
    layoutValue(v, a + 0x46, s.heading);
    layoutValue(v, a + 0x4e, s.turnStep);
    layoutValue(v, a + 0xc, s.startIndex);
    layoutValue(v, a + 0xd, s.enemyId);
    layoutValue(v, a + 0xe, s.cellX);
    layoutValue(v, a + 0xf, s.cellZ);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc7, s.count);
    layoutValue(v, a + 0xc8, s.flags);
    layoutValue(v, a + 0xca, s.cooldown);
    layoutValue(v, a + 0xc4, s.guns);
    layoutValue(v, a + 0xc5, s.contact);
    layoutValue(v, a + 0xc6, s.pitchTarget);
    layoutValue(v, a + 0xc9, s.offScreen);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4b_cast_state::TalkerFields& s) {
    using namespace od::port::st4b_cast_state;
    layoutValue(v, a + 0x14c, s.word);
    layoutValue(v, a + 0x150, s.stepping);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::trapped_miner_state::MinerFields& s) {
    using namespace od::port::trapped_miner_state;
    layoutValue(v, a + 0x14c, s.pauseTicks);
    layoutValue(v, a + 0x14e, s.cryTicks);
    layoutValue(v, a + 0x150, s.callPhase);
    layoutValue(v, a + 0x152, s.callTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::two_form_reaverbot_state::FormFields& s) {
    using namespace od::port::two_form_reaverbot_state;
    layoutValue(v, a + 0x166, s.flags);
    layoutValue(v, a + 0x167, s.stagger);
    layoutValue(v, a + 0x168, s.capsuleKind);
    layoutValue(v, a + 0x169, s.group);
    layoutValue(v, a + 0x16a, s.groupMark);
    layoutValue(v, a + 0xb, s.animEntry);
    layoutValue(v, a + 0x40, s.spawnX);
    layoutValue(v, a + 0x42, s.spawnY);
    layoutValue(v, a + 0x44, s.spawnZ);
    layoutValue(v, a + 0x46, s.lifeCount);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.wallResult);
    layoutValue(v, a + 0x160, s.neighbour);
    layoutValue(v, a + 0x162, s.targetYaw);
    layoutValue(v, a + 0x164, s.timer);
    layoutValue(v, a + 0x16b, s.soundTimer);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0x154 + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::window_giant_state::GiantFields& s) {
    using namespace od::port::window_giant_state;
    layoutValue(v, a + 0xb, s.side);
    layoutValue(v, a + 0xc, s.reactReturn);
    layoutValue(v, a + 0xd, s.reactAnim);
    layoutValue(v, a + 0xe, s.reactEntry);
    layoutValue(v, a + 0xf, s.reactPush);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.timer);
    layoutValue(v, a + 0x156, s.flags);
    layoutValue(v, a + 0x157, s.animEntry);
    layoutValue(v, a + 0x158, s.seen);
    layoutValue(v, a + 0x159, s.resumeStep);
}

bool classes_saul_kada(LayoutVisitor& v, uint32_t a, ActorClassState& s) {
    if (typeid(s) == typeid(od::port::st27_lava_valve_state::ValveFields)) { layoutValue(v, a, static_cast<od::port::st27_lava_valve_state::ValveFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st28_cast_props_state::ThrownPropFields)) { layoutValue(v, a, static_cast<od::port::st28_cast_props_state::ThrownPropFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::lava_pit_state::PitFields)) { layoutValue(v, a, static_cast<od::port::lava_pit_state::PitFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st28_gunner_bomb_state::BombFields)) { layoutValue(v, a, static_cast<od::port::st28_gunner_bomb_state::BombFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st2b_props_state::PlateFields)) { layoutValue(v, a, static_cast<od::port::st2b_props_state::PlateFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::GuardianFields)) { layoutValue(v, a, static_cast<od::port::GuardianFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::race_bonus_state::BonusFields)) { layoutValue(v, a, static_cast<od::port::race_bonus_state::BonusFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::lava_pit_state::CreatureFields)) { layoutValue(v, a, static_cast<od::port::lava_pit_state::CreatureFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st40_head_npc_state::NpcFields)) { layoutValue(v, a, static_cast<od::port::st40_head_npc_state::NpcFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::fire_ruin_bon_cast_state::BonCastFields)) { layoutValue(v, a, static_cast<od::port::fire_ruin_bon_cast_state::BonCastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st3b_glow_state::GlowFields)) { layoutValue(v, a, static_cast<od::port::st3b_glow_state::GlowFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::sand_burrower_state::BurrowerFields)) { layoutValue(v, a, static_cast<od::port::sand_burrower_state::BurrowerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::lava_pit_state::FlameFields)) { layoutValue(v, a, static_cast<od::port::lava_pit_state::FlameFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st41_gate_line_state::LineFields)) { layoutValue(v, a, static_cast<od::port::st41_gate_line_state::LineFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st3c_statue_state::PartFields)) { layoutValue(v, a, static_cast<od::port::st3c_statue_state::PartFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ThrownPropParams)) { layoutValue(v, a, static_cast<od::port::ThrownPropParams&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st27_scroll_glow_state::GlowFields)) { layoutValue(v, a, static_cast<od::port::st27_scroll_glow_state::GlowFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st27_mission_state::MissionFields)) { layoutValue(v, a, static_cast<od::port::st27_mission_state::MissionFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::DroppedPropParams)) { layoutValue(v, a, static_cast<od::port::DroppedPropParams&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st28_lava_boss_state::BossFields)) { layoutValue(v, a, static_cast<od::port::st28_lava_boss_state::BossFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st28_gunner_helpers_state::HelperFields)) { layoutValue(v, a, static_cast<od::port::st28_gunner_helpers_state::HelperFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st2b_props_state::PotFields)) { layoutValue(v, a, static_cast<od::port::st2b_props_state::PotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st41_enemy_generator_state::GeneratorFields)) { layoutValue(v, a, static_cast<od::port::st41_enemy_generator_state::GeneratorFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::pirate_servbots_state::ServbotFields)) { layoutValue(v, a, static_cast<od::port::pirate_servbots_state::ServbotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::two_form_reaverbot_state::FormFields)) { layoutValue(v, a, static_cast<od::port::two_form_reaverbot_state::FormFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st3c_minion_spawner_state::SpawnerFields)) { layoutValue(v, a, static_cast<od::port::st3c_minion_spawner_state::SpawnerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st28_fire_bomb_state::BombFields)) { layoutValue(v, a, static_cast<od::port::st28_fire_bomb_state::BombFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st3c_boss_shot_state::PieceFields)) { layoutValue(v, a, static_cast<od::port::st3c_boss_shot_state::PieceFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st20_props_state::PropFields)) { layoutValue(v, a, static_cast<od::port::st20_props_state::PropFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::flame_tongues_state::TongueFields)) { layoutValue(v, a, static_cast<od::port::flame_tongues_state::TongueFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::window_giant_state::GiantFields)) { layoutValue(v, a, static_cast<od::port::window_giant_state::GiantFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st28_guard_shot_state::ShotFields)) { layoutValue(v, a, static_cast<od::port::st28_guard_shot_state::ShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st2a_slide_block_state::BlockFields)) { layoutValue(v, a, static_cast<od::port::st2a_slide_block_state::BlockFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st3b_bomb_turret_state::TurretFields)) { layoutValue(v, a, static_cast<od::port::st3b_bomb_turret_state::TurretFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::smoke_puff_state::PuffFields)) { layoutValue(v, a, static_cast<od::port::smoke_puff_state::PuffFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st3c_gold_boss_state::BossFields)) { layoutValue(v, a, static_cast<od::port::st3c_gold_boss_state::BossFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st28_bomb_ribbon_state::RibbonFields)) { layoutValue(v, a, static_cast<od::port::st28_bomb_ribbon_state::RibbonFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st3c_gold_shockwave_state::WaveFields)) { layoutValue(v, a, static_cast<od::port::st3c_gold_shockwave_state::WaveFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::PodCarrierFields)) { layoutValue(v, a, static_cast<od::port::PodCarrierFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st34_flyer_shell_state::ShellFields)) { layoutValue(v, a, static_cast<od::port::st34_flyer_shell_state::ShellFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st27_respawner_state::RespawnerFields)) { layoutValue(v, a, static_cast<od::port::st27_respawner_state::RespawnerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st2b_cannon_shell_state::ShellFields)) { layoutValue(v, a, static_cast<od::port::st2b_cannon_shell_state::ShellFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::GateDebrisFields)) { layoutValue(v, a, static_cast<od::port::GateDebrisFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st28_gunner_boss_state::BossFields)) { layoutValue(v, a, static_cast<od::port::st28_gunner_boss_state::BossFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::hopping_reaverbot_state::HopFields)) { layoutValue(v, a, static_cast<od::port::hopping_reaverbot_state::HopFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st33_falling_rock_state::RockFields)) { layoutValue(v, a, static_cast<od::port::st33_falling_rock_state::RockFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::dropped_item_state::ItemFields)) { layoutValue(v, a, static_cast<od::port::dropped_item_state::ItemFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st48_gunship_state::ShipFields)) { layoutValue(v, a, static_cast<od::port::st48_gunship_state::ShipFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::fire_ruin_cutscene_cast_state::TickCastFields)) { layoutValue(v, a, static_cast<od::port::fire_ruin_cutscene_cast_state::TickCastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st3b_flame_wall_state::FlameFields)) { layoutValue(v, a, static_cast<od::port::st3b_flame_wall_state::FlameFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st24_script_walker_state::WalkerFields)) { layoutValue(v, a, static_cast<od::port::st24_script_walker_state::WalkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st48_charger_state::ChargerFields)) { layoutValue(v, a, static_cast<od::port::st48_charger_state::ChargerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4b_cast_state::TalkerFields)) { layoutValue(v, a, static_cast<od::port::st4b_cast_state::TalkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::SteeredPropFields)) { layoutValue(v, a, static_cast<od::port::SteeredPropFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::sand_trap_state::TrapFields)) { layoutValue(v, a, static_cast<od::port::sand_trap_state::TrapFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st13_spin_blade_state::BladeFields)) { layoutValue(v, a, static_cast<od::port::st13_spin_blade_state::BladeFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::bomb_critter_state::CritterFields)) { layoutValue(v, a, static_cast<od::port::bomb_critter_state::CritterFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st2b_cannon_boss_state::BossFields)) { layoutValue(v, a, static_cast<od::port::st2b_cannon_boss_state::BossFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::pirate_servbots_state::BomberFields)) { layoutValue(v, a, static_cast<od::port::pirate_servbots_state::BomberFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st3c_pirate_cast_state::PirateFields)) { layoutValue(v, a, static_cast<od::port::st3c_pirate_cast_state::PirateFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st3c_boss_shot_state::HomingFields)) { layoutValue(v, a, static_cast<od::port::st3c_boss_shot_state::HomingFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st41_pirate_shot_state::ShotFields)) { layoutValue(v, a, static_cast<od::port::st41_pirate_shot_state::ShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st28_gunner_beam_state::BeamOwnFields)) { layoutValue(v, a, static_cast<od::port::st28_gunner_beam_state::BeamOwnFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::lava_vents_state::VentsFields)) { layoutValue(v, a, static_cast<od::port::lava_vents_state::VentsFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::quicksand_state::PitParams)) { layoutValue(v, a, static_cast<od::port::quicksand_state::PitParams&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st28_guard_state::GuardFields)) { layoutValue(v, a, static_cast<od::port::st28_guard_state::GuardFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st3c_statue_state::StatueFields)) { layoutValue(v, a, static_cast<od::port::st3c_statue_state::StatueFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::fire_ruin_cutscene_cast_state::HolderFields)) { layoutValue(v, a, static_cast<od::port::fire_ruin_cutscene_cast_state::HolderFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st33_key_script_state::KeyItemFields)) { layoutValue(v, a, static_cast<od::port::st33_key_script_state::KeyItemFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::enemy_spawners_state::SpawnerFields)) { layoutValue(v, a, static_cast<od::port::enemy_spawners_state::SpawnerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::particle_burst_state::BurstFields)) { layoutValue(v, a, static_cast<od::port::particle_burst_state::BurstFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st34_ruin_sentry_state::SentryFields)) { layoutValue(v, a, static_cast<od::port::st34_ruin_sentry_state::SentryFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st34_ruin_flyer_state::FlyerFields)) { layoutValue(v, a, static_cast<od::port::st34_ruin_flyer_state::FlyerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::fire_ruin_cutscene_cast_state::DropperFields)) { layoutValue(v, a, static_cast<od::port::fire_ruin_cutscene_cast_state::DropperFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st41_sliding_door_state::DoorFields)) { layoutValue(v, a, static_cast<od::port::st41_sliding_door_state::DoorFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st41_carrier_pod_state::PodFields)) { layoutValue(v, a, static_cast<od::port::st41_carrier_pod_state::PodFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st26_patrol_guard_state::GuardFields)) { layoutValue(v, a, static_cast<od::port::st26_patrol_guard_state::GuardFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ruin_fading_figure_state::FigureFields)) { layoutValue(v, a, static_cast<od::port::ruin_fading_figure_state::FigureFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::trapped_miner_state::MinerFields)) { layoutValue(v, a, static_cast<od::port::trapped_miner_state::MinerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::gun_turret_state::TurretFields)) { layoutValue(v, a, static_cast<od::port::gun_turret_state::TurretFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st28_flyer_state::FlyerFields)) { layoutValue(v, a, static_cast<od::port::st28_flyer_state::FlyerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st41_hover_gunner_state::GunnerFields)) { layoutValue(v, a, static_cast<od::port::st41_hover_gunner_state::GunnerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::PartnerFields)) { layoutValue(v, a, static_cast<od::port::PartnerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::fire_ruin_servbot_cast_state::ServbotFields)) { layoutValue(v, a, static_cast<od::port::fire_ruin_servbot_cast_state::ServbotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st3c_minion_state::MinionFields)) { layoutValue(v, a, static_cast<od::port::st3c_minion_state::MinionFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::pirate_tank_state::TankFields)) { layoutValue(v, a, static_cast<od::port::pirate_tank_state::TankFields&>(s)); return true; }
    return false;
}
} // namespace od::test
