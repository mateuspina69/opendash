// The native records of this area in the exe layout (test-only).
#include "translator/actor_layout.hpp"
#include "game/game_state.hpp"
#include "content/yosyonke/abandoned_mine_relic_state.hpp"
#include "content/yosyonke/abandoned_mine_roll_state.hpp"
#include "content/yosyonke/flutter_cabin_state.hpp"
#include "content/yosyonke/ice_ruin_blade_barrier_state.hpp"
#include "content/yosyonke/ice_ruin_bubble_trail_state.hpp"
#include "content/yosyonke/ice_ruin_burrowing_shooter_state.hpp"
#include "content/yosyonke/ice_ruin_critters_state.hpp"
#include "content/yosyonke/ice_ruin_herded_creature_state.hpp"
#include "content/yosyonke/ice_ruin_shrink_block_state.hpp"
#include "content/yosyonke/roll_follower_state.hpp"
#include "content/yosyonke/roll_spawners_state.hpp"
#include "content/yosyonke/st09_kick_ball_state.hpp"
#include "content/yosyonke/st09_push_block_state.hpp"
#include "content/yosyonke/st0a_fx_state.hpp"
#include "content/yosyonke/st0d_critter_state.hpp"
#include "content/yosyonke/st0e_boss_vehicle_state.hpp"
#include "content/yosyonke/st0e_carrier_crate_state.hpp"
#include "content/yosyonke/st0e_debug_beam_state.hpp"
#include "content/yosyonke/st0e_defended_vehicle_state.hpp"
#include "content/yosyonke/st0e_servbots_state.hpp"
#include "content/yosyonke/st0e_spawner_state.hpp"
#include "content/yosyonke/st0e_turret_state.hpp"
#include "content/yosyonke/town_props_state.hpp"
#include "content/yosyonke/town_puppet.hpp"
#include "content/yosyonke/town_side_scripts_state.hpp"
#include "content/yosyonke/town_story_cast_state.hpp"

namespace od::test {
using namespace od::port;

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::PuppetFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.variant);
    layoutValue(v, a + 0xd, s.go);
    layoutValue(v, a + 0xf, s.floating);
    layoutValue(v, a + 0x3e, s.headPitch);
    layoutValue(v, a + 0x46, s.headYaw);
    layoutValue(v, a + 0x14c, s.headHeight);
    layoutValue(v, a + 0x14e, s.headTarget);
    layoutValue(v, a + 0x150, s.headRate);
    layoutValue(v, a + 0x152, s.puppetFlags);
    layoutValue(v, a + 0x153, s.animKey);
    layoutValue(v, a + 0x157, s.headOn);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::abandoned_mine_relic_state::RuinRelicFields& s) {
    using namespace od::port::abandoned_mine_relic_state;
    layoutValue(v, a + 0xd, s.cueAnim);
    layoutValue(v, a + 0xf, s.message);
    layoutValue(v, a + 0x14c, s.tracks);
    layoutValue(v, a + 0x15c, s.talkWord_);
    layoutValue(v, a + 0x15c, s.talkBits);
    layoutValue(v, a + 0x15e, s.animKeyStart);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::abandoned_mine_roll_state::RuinRollFields& s) {
    using namespace od::port::abandoned_mine_roll_state;
    layoutValue(v, a + 0xc, s.kind);
    layoutValue(v, a + 0xd, s.animParam);
    layoutValue(v, a + 0xe, s.lookParam);
    layoutValue(v, a + 0xf, s.message);
    layoutValue(v, a + 0x3e, s.headPitch);
    layoutValue(v, a + 0x46, s.headYaw);
    layoutValue(v, a + 0x160, s.pathIndex);
    layoutValue(v, a + 0x14c, s.tracks);
    layoutValue(v, a + 0x15c, s.talkWord_);
    layoutValue(v, a + 0x15c, s.talking);
    layoutValue(v, a + 0x15e, s.animKey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::flutter_cabin_state::CabinNpcFields& s) {
    using namespace od::port::flutter_cabin_state;
    layoutValue(v, a + 0xc, s.variantIndex);
    layoutValue(v, a + 0xf, s.floating);
    layoutValue(v, a + 0x14c, s.lookHeight);
    layoutValue(v, a + 0x14e, s.lookYaw);
    layoutValue(v, a + 0x150, s.turnStep);
    layoutValue(v, a + 0x152, s.npcFlags);
    layoutValue(v, a + 0x153, s.animKey);
    layoutValue(v, a + 0x157, s.looking);
    layoutValue(v, a + 0x3e, s.headPitch);
    layoutValue(v, a + 0x46, s.headYaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::flutter_cabin_state::FlutterRollFields& s) {
    using namespace od::port::flutter_cabin_state;
    layoutValue(v, a + 0xd, s.pose);
    layoutValue(v, a + 0xe, s.shownPart);
    layoutValue(v, a + 0xf, s.message);
    layoutValue(v, a + 0x160, s.mode);
    layoutValue(v, a + 0x162, s.watchTimer);
    layoutValue(v, a + 0x163, s.hitBits);
    layoutValue(v, a + 0x14c, s.box);
    layoutValue(v, a + 0x15e, s.look);
    layoutValue(v, a + 0x161, s.gaze);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ice_ruin_blade_barrier_state::BarrierFields& s) {
    using namespace od::port::ice_ruin_blade_barrier_state;
    layoutValue(v, a + 0x46, s.ticks);
    layoutValue(v, a + 0xbc, s.hub);
    layoutValue(v, a + 0xc0, s.standingSegments);
    layoutValue(v, a + 0xc4, s.hit);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xc1 + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ice_ruin_bubble_trail_state::TrailFields& s) {
    using namespace od::port::ice_ruin_bubble_trail_state;
    layoutValue(v, a + 0x74, s.nextSlot);
    for (size_t i = 0; i < s.bubbleX.size(); ++i) layoutValue(v, a + 0x7c + uint32_t(i) * (2), s.bubbleX[i]);
    for (size_t i = 0; i < s.bubbleY.size(); ++i) layoutValue(v, a + 0x8c + uint32_t(i) * (2), s.bubbleY[i]);
    for (size_t i = 0; i < s.bubbleZ.size(); ++i) layoutValue(v, a + 0x9c + uint32_t(i) * (2), s.bubbleZ[i]);
    for (size_t i = 0; i < s.bubbleFlags.size(); ++i) layoutValue(v, a + 0xb4 + uint32_t(i) * (1), s.bubbleFlags[i]);
    layoutValue(v, a + 0xbc, s.torpedo);
    for (size_t i = 0; i < s.bubbleYaw.size(); ++i) layoutValue(v, a + 0xc0 + uint32_t(i) * (1), s.bubbleYaw[i]);
    for (size_t i = 0; i < s.life.size(); ++i) layoutValue(v, a + 0xac + uint32_t(i) * (1), s.life[i]);
    for (size_t i = 0; i < s.lifeSigned.size(); ++i) layoutValue(v, a + 0xac + uint32_t(i) * (1), s.lifeSigned[i]);
    for (size_t i = 0; i < s.clearedPad1.size(); ++i) layoutValue(v, a + 0x75 + uint32_t(i) * (1), s.clearedPad1[i]);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xc8 + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ice_ruin_burrowing_shooter_state::ShooterFields& s) {
    using namespace od::port::ice_ruin_burrowing_shooter_state;
    layoutValue(v, a + 0x158, s.hitKind);
    layoutValue(v, a + 0x160, s.bits);
    layoutValue(v, a + 0x162, s.ticks);
    layoutValue(v, a + 0x163, s.hopSide);
    layoutValue(v, a + 0x164, s.hopCount);
    layoutValue(v, a + 0x14c, s.hit);
    layoutValue(v, a + 0x154, s.playerDistance);
    layoutValue(v, a + 0x15c, s.turnStep);
    layoutValue(v, a + 0x161, s.contact);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0x15e + uint32_t(i) * (1), s.clearedPad0[i]);
    for (size_t i = 0; i < s.clearedPad1.size(); ++i) layoutValue(v, a + 0x165 + uint32_t(i) * (1), s.clearedPad1[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ice_ruin_burrowing_shooter_state::ShotFields& s) {
    using namespace od::port::ice_ruin_burrowing_shooter_state;
    const auto& slot = actorHeaderAt(a);
    const unsigned phase = slot.state >= 3 ? unsigned(slot.state - 3) : unsigned(slot.subState);
    const bool falling = phase >= 2;
    layoutValue(v, a + 0xc, s.attack);
    layoutValue(v, a + 0xc, s.big);
    if (!falling) layoutValue(v, a + 0xbc, s.shooter);
    if (!falling) layoutValue(v, a + 0xc0, s.hit);
    if (falling) layoutValue(v, a + 0xbc, s.fallHit);
    if (falling) layoutValue(v, a + 0xc4, s.fallTicks);
    layoutValue(v, a + 0xc8, s.ringSize);
    layoutValue(v, a + 0xca, s.timer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ice_ruin_critters_state::CritterFields& s) {
    using namespace od::port::ice_ruin_critters_state;
    layoutValue(v, a + 0x160, s.bits);
    layoutValue(v, a + 0x162, s.animKey);
    layoutValue(v, a + 0x163, s.hits);
    layoutValue(v, a + 0x14c, s.hit);
    layoutValue(v, a + 0x154, s.hitYaw);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.heading);
    layoutValue(v, a + 0x161, s.coin);
    layoutValue(v, a + 0x164, s.hitsInAll);
    layoutValue(v, a + 0x166, s.hurtCooldown);
    layoutValue(v, a + 0x168, s.spawnX);
    layoutValue(v, a + 0x16a, s.spawnZ);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ice_ruin_herded_creature_state::HerdFields& s) {
    using namespace od::port::ice_ruin_herded_creature_state;
    layoutValue(v, a + 0x15f, s.bits);
    layoutValue(v, a + 0x162, s.pensFoundTaken);
    layoutValue(v, a + 0x168, s.watchesPlayer);
    layoutValue(v, a + 0x169, s.frontHitArmed);
    layoutValue(v, a + 0x14c, s.hit);
    layoutValue(v, a + 0x154, s.hitYaw);
    layoutValue(v, a + 0x15c, s.heading);
    layoutValue(v, a + 0x15e, s.cooldown);
    layoutValue(v, a + 0x164, s.neighbour);
    layoutValue(v, a + 0x166, s.lastDamage);
    layoutValue(v, a + 0x160, s.timer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ice_ruin_shrink_block_state::ShrinkFields& s) {
    using namespace od::port::ice_ruin_shrink_block_state;
    layoutValue(v, a + 0x3e, s.size);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.waitTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::roll_follower_state::RollFields& s) {
    using namespace od::port::roll_follower_state;
    layoutValue(v, a + 0xc, s.kind);
    layoutValue(v, a + 0xe, s.faintedMessage);
    layoutValue(v, a + 0xf, s.message);
    layoutValue(v, a + 0x2e, s.searchYaw);
    layoutValue(v, a + 0x3e, s.headPitch);
    layoutValue(v, a + 0x46, s.headYaw);
    layoutValue(v, a + 0x166, s.sight);
    layoutValue(v, a + 0x167, s.hits);
    layoutValue(v, a + 0x168, s.behaviour);
    layoutValue(v, a + 0x169, s.coolDown);
    layoutValue(v, a + 0x16a, s.hitsToKnockBack);
    layoutValue(v, a + 0x14c, s.box);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.seenX);
    layoutValue(v, a + 0x160, s.seenZ);
    layoutValue(v, a + 0x162, s.targetX);
    layoutValue(v, a + 0x164, s.targetZ);
    layoutValue(v, a + 0x16b, s.animKey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::roll_follower_state::ShedRollFields& s) {
    using namespace od::port::roll_follower_state;
    layoutValue(v, a + 0xc, s.kind);
    layoutValue(v, a + 0xf, s.message);
    layoutValue(v, a + 0x3e, s.headPitch);
    layoutValue(v, a + 0x46, s.headYaw);
    layoutValue(v, a + 0x15f, s.behaviour);
    layoutValue(v, a + 0x14c, s.box);
    layoutValue(v, a + 0x15e, s.hits);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::roll_spawners_state::RollSpawnerFields& s) {
    using namespace od::port::roll_spawners_state;
    for (size_t i = 0; i < s.scratch.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.scratch[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st09_kick_ball_state::BallFields& s) {
    using namespace od::port::st09_kick_ball_state;
    layoutValue(v, a + 0xa, s.kicks);
    layoutValue(v, a + 0xbc, s.box);
    layoutValue(v, a + 0xc4, s.flagsWord_);
    layoutValue(v, a + 0xc6, s.timer);
    layoutValue(v, a + 0xc8, s.heading);
    layoutValue(v, a + 0xca, s.playerAngle);
    layoutValue(v, a + 0xc4, s.flags);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st09_push_block_state::BlockFields& s) {
    using namespace od::port::st09_push_block_state;
    layoutValue(v, a + 0xbc, s.box);
    layoutValue(v, a + 0xc8, s.hits);
    layoutValue(v, a + 0xca, s.solid);
    layoutValue(v, a + 0xcb, s.coolDown);
    layoutValue(v, a + 0xc, s.partner);
    layoutValue(v, a + 0xc4, s.pushYaw);
    layoutValue(v, a + 0xc6, s.travelYaw);
    layoutValue(v, a + 0xc9, s.mode);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st0a_fx_state::StageEffectFields& s) {
    using namespace od::port::st0a_fx_state;
    layoutValue(v, a + 0xc, s.firstFrame);
    layoutValue(v, a + 0xc, s.trailRow);
    layoutValue(v, a + 0xc, s.radiusY);
    layoutValue(v, a + 0xd, s.radiusX);
    layoutValue(v, a + 0xd, s.trailBaseQuarter);
    layoutValue(v, a + 0xb, s.released);
    layoutValue(v, a + 0xe, s.size);
    layoutValue(v, a + 0xe, s.vertical);
    layoutValue(v, a + 0xe, s.instant);
    layoutValue(v, a + 0xf, s.trailShrink);
    layoutValue(v, a + 0xf, s.tilePage);
    layoutValue(v, a + 0x16, s.rawSize);
    layoutValue(v, a + 0x16, s.yaw);
    // (its own bytes +0x18..+0x27 are the effect slot's trail lanes: the members alias them)
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st0d_critter_state::CritterFields& s) {
    using namespace od::port::st0d_critter_state;
    layoutValue(v, a + 0x162, s.hits);
    layoutValue(v, a + 0x163, s.behaviour);
    layoutValue(v, a + 0x14c, s.box);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.turn);
    layoutValue(v, a + 0x160, s.heading);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st0e_boss_vehicle_state::FireballFields& s) {
    using namespace od::port::st0e_boss_vehicle_state;
    layoutValue(v, a + 0xa3, s.burstStep);
    layoutValue(v, a + 0xbc, s.box);
    for (size_t i = 0; i < s.sparkSize.size(); ++i) layoutValue(v, a + 0x74 + uint32_t(i) * (2), s.sparkSize[i]);
    for (size_t i = 0; i < s.sparkYaw.size(); ++i) layoutValue(v, a + 0x7e + uint32_t(i) * (2), s.sparkYaw[i]);
    for (size_t i = 0; i < s.sparkPitch.size(); ++i) layoutValue(v, a + 0x88 + uint32_t(i) * (2), s.sparkPitch[i]);
    for (size_t i = 0; i < s.sparkFall.size(); ++i) layoutValue(v, a + 0x92 + uint32_t(i) * (2), s.sparkFall[i]);
    layoutValue(v, a + 0x9c, s.homingTicks);
    for (size_t i = 0; i < s.sparkDelay.size(); ++i) layoutValue(v, a + 0x9e + uint32_t(i) * (1), s.sparkDelay[i]);
    layoutValue(v, a + 0xc4, s.colour);
    layoutValue(v, a + 0xc8, s.size);
    layoutValue(v, a + 0xca, s.burstAngle);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st0e_boss_vehicle_state::VehicleBlock& s) {
    using namespace od::port::st0e_boss_vehicle_state;
    for (size_t i = 0; i < s.boxes.size(); ++i) layoutValue(v, a + 0x00 + uint32_t(i) * (8), s.boxes[i]);
    for (size_t i = 0; i < s.watchers.size(); ++i) layoutValue(v, a + 0x50 + uint32_t(i) * (4), s.watchers[i]);
    layoutValue(v, a + 0x58, s.fireball);
    for (size_t i = 0; i < s.emitters.size(); ++i) layoutValue(v, a + 0x5c + uint32_t(i) * (4), s.emitters[i]);
    layoutValue(v, a + 0x68, s.chargeTicks);
    layoutValue(v, a + 0x6a, s.restTicks);
    layoutValue(v, a + 0x6c, s.burnTicks);
    layoutValue(v, a + 0x6e, s.burnStep);
    layoutValue(v, a + 0x64, s.unread0);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st0e_boss_vehicle_state::VehicleFields& s) {
    using namespace od::port::st0e_boss_vehicle_state;
    layoutValue(v, a + 0xb, s.fireballsSpawned);
    layoutValue(v, a + 0x158, s.turret);
    layoutValue(v, a + 0x15f, s.flagsHigh);
    layoutValue(v, a + 0x16a, s.cratesBeforeTilt);
    layoutValue(v, a + 0x14c, s.headWord_);
    layoutValue(v, a + 0x154, s.block);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.flags);
    layoutValue(v, a + 0x160, s.fireballAim);
    layoutValue(v, a + 0x162, s.chargePointHp);
    layoutValue(v, a + 0x164, s.damageCoolDown);
    layoutValue(v, a + 0x166, s.voiceTimer);
    layoutValue(v, a + 0x168, s.hurtVoiceTimer);
    layoutValue(v, a + 0x16b, s.voiceDelay);
    layoutValue(v, a + 0x15c, s.timerLow);
    layoutValue(v, a + 0x15d, s.timerHigh);
    layoutValue(v, a + 0x15e, s.flagsLow);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st0e_carrier_crate_state::CrateFields& s) {
    using namespace od::port::st0e_carrier_crate_state;
    layoutValue(v, a + 0xc, s.servbot);
    layoutValue(v, a + 0xbc, s.box);
    layoutValue(v, a + 0xca, s.throwTarget);
    layoutValue(v, a + 0xcb, s.ringSlot);
    layoutValue(v, a + 0xc5, s.kind);
    layoutValue(v, a + 0xc6, s.fallTicks);
    layoutValue(v, a + 0xc8, s.life);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st0e_debug_beam_state::DebugBeamFields& s) {
    using namespace od::port::st0e_debug_beam_state;
    layoutValue(v, a + 0xc, s.length);
    layoutValue(v, a + 0xc4, s.bodyLength);
    layoutValue(v, a + 0xbc, s.lastPoint);
    layoutValue(v, a + 0xc8, s.width);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st0e_defended_vehicle_state::DefendedFields& s) {
    using namespace od::port::st0e_defended_vehicle_state;
    layoutValue(v, a + 0xd, s.round);
    layoutValue(v, a + 0xe, s.wantedGap);
    layoutValue(v, a + 0xbc, s.turretGroup);
    layoutValue(v, a + 0xbd, s.timer);
    layoutValue(v, a + 0xbe, s.voicesSaid);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st0e_servbots_state::CrateServbotFields& s) {
    using namespace od::port::st0e_servbots_state;
    layoutValue(v, a + 0x158, s.lieTicks);
    layoutValue(v, a + 0x15a, s.voiceDelay);
    layoutValue(v, a + 0x15c, s.hits);
    layoutValue(v, a + 0x15d, s.voiceSaid);
    layoutValue(v, a + 0x15e, s.face);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st0e_servbots_state::FlungFields& s) {
    using namespace od::port::st0e_servbots_state;
    layoutValue(v, a + 0xc, s.model);
    layoutValue(v, a + 0x15f, s.timer);
    layoutValue(v, a + 0x160, s.turnTimer);
    layoutValue(v, a + 0x162, s.offscreenTicks);
    layoutValue(v, a + 0x163, s.startKey);
    layoutValue(v, a + 0x166, s.hitKind);
    layoutValue(v, a + 0x168, s.smoking);
    layoutValue(v, a + 0x14c, s.box);
    layoutValue(v, a + 0x15c, s.flightYaw);
    layoutValue(v, a + 0x15e, s.behaviour);
    layoutValue(v, a + 0x161, s.contacts);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st0e_servbots_state::RiderFields& s) {
    using namespace od::port::st0e_servbots_state;
    layoutValue(v, a + 0xb, s.side);
    layoutValue(v, a + 0xc, s.bomb);
    layoutValue(v, a + 0x164, s.hits);
    layoutValue(v, a + 0x165, s.flags);
    layoutValue(v, a + 0x14c, s.box);
    layoutValue(v, a + 0x158, s.local);
    layoutValue(v, a + 0x160, s.lieTicks);
    layoutValue(v, a + 0x162, s.walkTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st0e_spawner_state::EncounterSwitchFields& s) {
    using namespace od::port::st0e_spawner_state;
    for (size_t i = 0; i < s.scratch.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.scratch[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st0e_turret_state::BombFields& s) {
    using namespace od::port::st0e_turret_state;
    layoutValue(v, a + 0xe, s.frameU);
    layoutValue(v, a + 0xbc, s.box);
    layoutValue(v, a + 0xc8, s.unusedC8);
    layoutValue(v, a + 0xca, s.blastDelay);
    layoutValue(v, a + 0xd2, s.frameTimer);
    layoutValue(v, a + 0xd4, s.size);
    layoutValue(v, a + 0xd6, s.unusedD6);
    layoutValue(v, a + 0xd7, s.grey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st0e_turret_state::GlowFields& s) {
    using namespace od::port::st0e_turret_state;
    layoutValue(v, a + 0xa, s.released);
    layoutValue(v, a + 0xc, s.targetSize);
    layoutValue(v, a + 0xe, s.growth);
    layoutValue(v, a + 0xf, s.pulse);
    layoutValue(v, a + 0x1c, s.size);
    layoutValue(v, a + 0x18, s.colour);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st0e_turret_state::TurretBlock& s) {
    using namespace od::port::st0e_turret_state;
    for (size_t i = 0; i < s.boxes.size(); ++i) layoutValue(v, a + 0x30 + uint32_t(i) * (8), s.boxes[i]);
    for (size_t i = 0; i < s.partHp.size(); ++i) layoutValue(v, a + 0x78 + uint32_t(i) * (2), s.partHp[i]);
    layoutValue(v, a + 0x88, s.sweep);
    layoutValue(v, a + 0x8a, s.sweepSpeed);
    layoutValue(v, a + 0x8c, s.cannonTimer);
    layoutValue(v, a + 0x8e, s.volleyTimer);
    for (size_t i = 0; i < s.armTimer.size(); ++i) layoutValue(v, a + 0x90 + uint32_t(i) * (2), s.armTimer[i]);
    layoutValue(v, a + 0x94, s.dyingTimer);
    layoutValue(v, a + 0x96, s.endDelay);
    layoutValue(v, a + 0x98, s.burstCount);
    layoutValue(v, a + 0x99, s.bombSlot);
    for (size_t i = 0; i < s.words.size(); ++i) layoutValue(v, a + 0x0 + uint32_t(i) * (4), s.words[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st0e_turret_state::TurretFields& s) {
    using namespace od::port::st0e_turret_state;
    layoutValue(v, a + 0xb, s.beamStep);
    layoutValue(v, a + 0xc, s.volleySlot);
    layoutValue(v, a + 0xd, s.bombsLeft);
    layoutValue(v, a + 0xe, s.rounds);
    layoutValue(v, a + 0xf, s.beamTimer);
    layoutValue(v, a + 0x48, s.hurtVoiceTimer);
    layoutValue(v, a + 0x4a, s.blastMark);
    layoutValue(v, a + 0x4b, s.voicesSaid);
    layoutValue(v, a + 0x168, s.flagsLow);
    layoutValue(v, a + 0x169, s.flagsHigh);
    layoutValue(v, a + 0x16a, s.objectState);
    layoutValue(v, a + 0x14c, s.block);
    layoutValue(v, a + 0x150, s.beam);
    layoutValue(v, a + 0x154, s.glow);
    layoutValue(v, a + 0x158, s.bombAim);
    layoutValue(v, a + 0x15a, s.beamAim);
    layoutValue(v, a + 0x15c, s.beamYaw);
    for (size_t i = 0; i < s.cannonYaw.size(); ++i) layoutValue(v, a + 0x15e + uint32_t(i) * (2), s.cannonYaw[i]);
    for (size_t i = 0; i < s.cannonPitch.size(); ++i) layoutValue(v, a + 0x162 + uint32_t(i) * (2), s.cannonPitch[i]);
    layoutValue(v, a + 0x166, s.coreTimer);
    layoutValue(v, a + 0x168, s.flags);
    layoutValue(v, a + 0x16b, s.damageCoolDown);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::town_props_state::St09PropFields& s) {
    using namespace od::port::town_props_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.flags);
    layoutValue(v, a + 0xc6, s.timer);
    layoutValue(v, a + 0xc8, s.travelYaw);
    layoutValue(v, a + 0xca, s.playerAngle);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::town_side_scripts_state::FlyerFields& s) {
    using namespace od::port::town_side_scripts_state;
    layoutValue(v, a + 0xd, s.go);
    layoutValue(v, a + 0xbc, s.glideTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::town_story_cast_state::RunningRollFields& s) {
    using namespace od::port::town_story_cast_state;
    layoutValue(v, a + 0xc, s.variant);
    layoutValue(v, a + 0xf, s.floating);
    layoutValue(v, a + 0x14c, s.runFlags);
    layoutValue(v, a + 0x14d, s.animKey);
}

bool classes_yosyonke(LayoutVisitor& v, uint32_t a, ActorClassState& s) {
    if (typeid(s) == typeid(od::port::st0e_turret_state::BombFields)) { layoutValue(v, a, static_cast<od::port::st0e_turret_state::BombFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ice_ruin_burrowing_shooter_state::ShotFields)) { layoutValue(v, a, static_cast<od::port::ice_ruin_burrowing_shooter_state::ShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st0e_carrier_crate_state::CrateFields)) { layoutValue(v, a, static_cast<od::port::st0e_carrier_crate_state::CrateFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ice_ruin_critters_state::CritterFields)) { layoutValue(v, a, static_cast<od::port::ice_ruin_critters_state::CritterFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::town_props_state::St09PropFields)) { layoutValue(v, a, static_cast<od::port::town_props_state::St09PropFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::abandoned_mine_roll_state::RuinRollFields)) { layoutValue(v, a, static_cast<od::port::abandoned_mine_roll_state::RuinRollFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st0e_spawner_state::EncounterSwitchFields)) { layoutValue(v, a, static_cast<od::port::st0e_spawner_state::EncounterSwitchFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st0e_debug_beam_state::DebugBeamFields)) { layoutValue(v, a, static_cast<od::port::st0e_debug_beam_state::DebugBeamFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::abandoned_mine_relic_state::RuinRelicFields)) { layoutValue(v, a, static_cast<od::port::abandoned_mine_relic_state::RuinRelicFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st0e_boss_vehicle_state::VehicleFields)) { layoutValue(v, a, static_cast<od::port::st0e_boss_vehicle_state::VehicleFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st09_kick_ball_state::BallFields)) { layoutValue(v, a, static_cast<od::port::st09_kick_ball_state::BallFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st0e_turret_state::GlowFields)) { layoutValue(v, a, static_cast<od::port::st0e_turret_state::GlowFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ice_ruin_bubble_trail_state::TrailFields)) { layoutValue(v, a, static_cast<od::port::ice_ruin_bubble_trail_state::TrailFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ice_ruin_burrowing_shooter_state::ShooterFields)) { layoutValue(v, a, static_cast<od::port::ice_ruin_burrowing_shooter_state::ShooterFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st0e_servbots_state::RiderFields)) { layoutValue(v, a, static_cast<od::port::st0e_servbots_state::RiderFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ice_ruin_shrink_block_state::ShrinkFields)) { layoutValue(v, a, static_cast<od::port::ice_ruin_shrink_block_state::ShrinkFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::roll_follower_state::ShedRollFields)) { layoutValue(v, a, static_cast<od::port::roll_follower_state::ShedRollFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st09_push_block_state::BlockFields)) { layoutValue(v, a, static_cast<od::port::st09_push_block_state::BlockFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st0e_boss_vehicle_state::FireballFields)) { layoutValue(v, a, static_cast<od::port::st0e_boss_vehicle_state::FireballFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st0d_critter_state::CritterFields)) { layoutValue(v, a, static_cast<od::port::st0d_critter_state::CritterFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ice_ruin_herded_creature_state::HerdFields)) { layoutValue(v, a, static_cast<od::port::ice_ruin_herded_creature_state::HerdFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st0e_turret_state::TurretFields)) { layoutValue(v, a, static_cast<od::port::st0e_turret_state::TurretFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st0a_fx_state::StageEffectFields)) { layoutValue(v, a, static_cast<od::port::st0a_fx_state::StageEffectFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st0e_servbots_state::FlungFields)) { layoutValue(v, a, static_cast<od::port::st0e_servbots_state::FlungFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st0e_defended_vehicle_state::DefendedFields)) { layoutValue(v, a, static_cast<od::port::st0e_defended_vehicle_state::DefendedFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::roll_follower_state::RollFields)) { layoutValue(v, a, static_cast<od::port::roll_follower_state::RollFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::town_story_cast_state::RunningRollFields)) { layoutValue(v, a, static_cast<od::port::town_story_cast_state::RunningRollFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::flutter_cabin_state::FlutterRollFields)) { layoutValue(v, a, static_cast<od::port::flutter_cabin_state::FlutterRollFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st0e_servbots_state::CrateServbotFields)) { layoutValue(v, a, static_cast<od::port::st0e_servbots_state::CrateServbotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::town_side_scripts_state::FlyerFields)) { layoutValue(v, a, static_cast<od::port::town_side_scripts_state::FlyerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ice_ruin_blade_barrier_state::BarrierFields)) { layoutValue(v, a, static_cast<od::port::ice_ruin_blade_barrier_state::BarrierFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::flutter_cabin_state::CabinNpcFields)) { layoutValue(v, a, static_cast<od::port::flutter_cabin_state::CabinNpcFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::PuppetFields)) { layoutValue(v, a, static_cast<od::port::PuppetFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::roll_spawners_state::RollSpawnerFields)) { layoutValue(v, a, static_cast<od::port::roll_spawners_state::RollSpawnerFields&>(s)); return true; }
    return false;
}
} // namespace od::test
