// The native records of this area in the exe layout (test-only).
#include "translator/actor_layout.hpp"
#include "content/calbania/appo_dah_a_state.hpp"
#include "content/calbania/appo_dah_b_state.hpp"
#include "content/calbania/appo_dah_face.hpp"
#include "content/calbania/appo_dah_fight.hpp"
#include "content/calbania/base_alarm_state.hpp"
#include "content/calbania/calbania_cast_state.hpp"
#include "content/calbania/calbania_spawners_state.hpp"
#include "content/calbania/carried_npc_state.hpp"
#include "content/calbania/ch3_script_cast_state.hpp"
#include "content/calbania/escape_timer_state.hpp"
#include "content/calbania/glyde_base_bombers_state.hpp"
#include "content/calbania/glyde_base_gun_state.hpp"
#include "content/calbania/glyde_base_key_state.hpp"
#include "content/calbania/glyde_base_scene_birdbots_state.hpp"
#include "content/calbania/glyde_base_tank_state.hpp"
#include "content/calbania/glyde_base_turret_state.hpp"
#include "content/calbania/nino_return_scripts_state.hpp"
#include "content/calbania/st1f_birdbot_tank_state.hpp"
#include "content/calbania/st21_props_state.hpp"
#include "content/calbania/st22_burrower_state.hpp"
#include "content/calbania/st22_fire_shots_state.hpp"
#include "content/calbania/st32_quiz_state.hpp"
#include "content/calbania/talking_npc_state.hpp"
#include "content/calbania/tron_boss_state.hpp"

namespace od::test {
using namespace od::port;

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::AppoDahFightFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.ridden);
    layoutValue(v, a + 0x2e, s.carried);
    layoutValue(v, a + 0x160, s.collision);
    layoutValue(v, a + 0x162, s.waypoint);
    layoutValue(v, a + 0x163, s.nextWaypoint);
    layoutValue(v, a + 0x164, s.prevWaypoint);
    layoutValue(v, a + 0x166, s.blinkTicks);
    layoutValue(v, a + 0x167, s.grey);
    layoutValue(v, a + 0x168, s.emote);
    layoutValue(v, a + 0x169, s.animKey);
    layoutValue(v, a + 0x16a, s.standOn);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.knockYaw);
    layoutValue(v, a + 0x161, s.flags);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::AppoDahMouth& s) {
    using namespace od::port;
    layoutValue(v, a, s.step);
    layoutValue(v, a + 1, s.ticks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::appo_dah_a_state::GatePuppetFields& s) {
    using namespace od::port::appo_dah_a_state;
    layoutValue(v, a + 0xc, s.routine);
    layoutValue(v, a + 0xf, s.noGround);
    layoutValue(v, a + 0x14c, s.castFlags);
    layoutValue(v, a + 0x14d, s.animKey);
    layoutValue(v, a + 0x14f, s.mouth);
    layoutValue(v, a + 0x151, s.lastTick);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::appo_dah_a_state::GuideFields& s) {
    using namespace od::port::appo_dah_a_state;
    layoutValue(v, a + 0xc, s.path);
    layoutValue(v, a + 0x164, s.lastNode);
    layoutValue(v, a + 0x165, s.collision);
    layoutValue(v, a + 0x166, s.flags);
    layoutValue(v, a + 0x167, s.reachedNode);
    layoutValue(v, a + 0x168, s.targetNode);
    layoutValue(v, a + 0x169, s.blinkTicks);
    layoutValue(v, a + 0x16a, s.animKey);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.shoutTicks);
    layoutValue(v, a + 0x15e, s.knockYaw);
    layoutValue(v, a + 0x160, s.nodes);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::appo_dah_b_state::AppoDahBombFields& s) {
    using namespace od::port::appo_dah_b_state;
    layoutValue(v, a + 0xc, s.ticks);
    layoutValue(v, a + 0xe, s.attack);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xd0, s.spin);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::appo_dah_b_state::ScriptPuppetFields& s) {
    using namespace od::port::appo_dah_b_state;
    layoutValue(v, a + 0xc, s.routine);
    layoutValue(v, a + 0xd, s.speaker);
    layoutValue(v, a + 0xf, s.noGround);
    layoutValue(v, a + 0x14c, s.castFlags);
    layoutValue(v, a + 0x14d, s.animKey);
    layoutValue(v, a + 0x14f, s.mouth);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::base_alarm_state::AlarmFields& s) {
    using namespace od::port::base_alarm_state;
    layoutValue(v, a + 0x18, s.phase);
    layoutValue(v, a + 0x1a, s.sirenTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::calbania_cast_state::SittingPuppetFields& s) {
    using namespace od::port::calbania_cast_state;
    layoutValue(v, a + 0xf, s.noGround);
    layoutValue(v, a + 0x3e, s.headPitch);
    layoutValue(v, a + 0x46, s.headYaw);
    layoutValue(v, a + 0x14c, s.lookHeight);
    layoutValue(v, a + 0x14e, s.lookYaw);
    layoutValue(v, a + 0x150, s.headRate);
    layoutValue(v, a + 0x152, s.castFlags);
    layoutValue(v, a + 0x153, s.animKey);
    layoutValue(v, a + 0x155, s.mouthStep);
    layoutValue(v, a + 0x156, s.mouthTicks);
    layoutValue(v, a + 0x157, s.looking);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::calbania_cast_state::StandingCastFields& s) {
    using namespace od::port::calbania_cast_state;
    layoutValue(v, a + 0xc, s.recordKey);
    layoutValue(v, a + 0x14c, s.animKey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::calbania_cast_state::TownBirdFields& s) {
    using namespace od::port::calbania_cast_state;
    layoutValue(v, a + 0xc, s.followSlot);
    layoutValue(v, a + 0x160, s.bits);
    layoutValue(v, a + 0x162, s.animKey);
    layoutValue(v, a + 0x163, s.hitsTaken);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.heading);
    layoutValue(v, a + 0x161, s.side);
    layoutValue(v, a + 0x164, s.homeX);
    layoutValue(v, a + 0x166, s.homeZ);
    layoutValue(v, a + 0x168, s.hits);
    layoutValue(v, a + 0x16a, s.hurtTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::calbania_cast_state::WalkingPuppetFields& s) {
    using namespace od::port::calbania_cast_state;
    layoutValue(v, a + 0xf, s.noGround);
    layoutValue(v, a + 0x14c, s.castFlags);
    layoutValue(v, a + 0x14d, s.animKey);
    layoutValue(v, a + 0x14f, s.mouth);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::calbania_spawners_state::St1fSpawnerFields& s) {
    using namespace od::port::calbania_spawners_state;
    for (size_t i = 0; i < s.pairSlots.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.pairSlots[i]);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0x1a + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::carried_npc_state::CarriedNpcFields& s) {
    using namespace od::port::carried_npc_state;
    layoutValue(v, a + 0xc, s.cue);
    layoutValue(v, a + 0xd, s.lastCue);
    layoutValue(v, a + 0xf, s.scriptSignal);
    layoutValue(v, a + 0x160, s.bits);
    layoutValue(v, a + 0x168, s.wanderPoint);
    layoutValue(v, a + 0x169, s.animHold);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.hitYaw);
    layoutValue(v, a + 0x161, s.collisionBits);
    layoutValue(v, a + 0x162, s.targetX);
    layoutValue(v, a + 0x164, s.targetZ);
    layoutValue(v, a + 0x166, s.damageTaken);
    layoutValue(v, a + 0x16a, s.soundTimer);
    if ((actorHeaderAt(a).variant & 0x80) == 0) layoutValue(v, a + 0x14c, s.faceTracks);
    if ((actorHeaderAt(a).variant & 0x80) != 0) layoutValue(v, a + 0x14c, s.hitBox);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ch3_script_cast_state::Script20AppoDahFields& s) {
    using namespace od::port::ch3_script_cast_state;
    layoutValue(v, a + 0xc, s.routine);
    layoutValue(v, a + 0xd, s.seated);
    layoutValue(v, a + 0xf, s.noGround);
    layoutValue(v, a + 0x14c, s.castFlags);
    layoutValue(v, a + 0x14d, s.animKey);
    layoutValue(v, a + 0x14f, s.mouth);
    layoutValue(v, a + 0x151, s.lastTick);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ch3_script_cast_state::Script3cCastFields& s) {
    using namespace od::port::ch3_script_cast_state;
    layoutValue(v, a + 0x153, s.unread153);
    layoutValue(v, a + 0x154, s.holdPose);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::escape_timer_state::EscapeTimerFields& s) {
    using namespace od::port::escape_timer_state;
    layoutValue(v, a + 0xc, s.timer);
    layoutValue(v, a + 0x18, s.frameCountdown);
    layoutValue(v, a + 0x19, s.stopwatchFrame);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_base_bombers_state::GlydeBomberFields& s) {
    using namespace od::port::glyde_base_bombers_state;
    layoutValue(v, a + 0x160, s.bits);
    layoutValue(v, a + 0x166, s.hitsLeft);
    layoutValue(v, a + 0x167, s.runsLeft);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.bomb);
    layoutValue(v, a + 0x161, s.timer);
    layoutValue(v, a + 0x164, s.spawnYaw);
    layoutValue(v, a + 0x162, s.aimYaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_base_gun_state::GunFields& s) {
    using namespace od::port::glyde_base_gun_state;
    layoutValue(v, a + 0xc, s.arc);
    layoutValue(v, a + 0xe, s.appoSlot);
    layoutValue(v, a + 0xf, s.dahSlot);
    layoutValue(v, a + 0x2e, s.barrelPitch);
    layoutValue(v, a + 0x15f, s.ignored);
    layoutValue(v, a + 0x160, s.unused160);
    layoutValue(v, a + 0x16a, s.fireMode);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.timer);
    layoutValue(v, a + 0x158, s.arcMin);
    layoutValue(v, a + 0x15a, s.arcMax);
    layoutValue(v, a + 0x15c, s.downCountdown);
    layoutValue(v, a + 0x15d, s.targetKind);
    layoutValue(v, a + 0x15e, s.prevTargetKind);
    layoutValue(v, a + 0x161, s.bits);
    layoutValue(v, a + 0x164, s.target);
    layoutValue(v, a + 0x168, s.shotPitch);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_base_key_state::GlydeKeyFields& s) {
    using namespace od::port::glyde_base_key_state;
    layoutValue(v, a + 0xc, s.flagIndex);
    layoutValue(v, a + 0xc6, s.landedMark);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_base_scene_birdbots_state::SceneBirdbotFields& s) {
    using namespace od::port::glyde_base_scene_birdbots_state;
    layoutValue(v, a + 0xf, s.steady);
    layoutValue(v, a + 0x14c, s.baseY);
    layoutValue(v, a + 0x14e, s.smokeTicks);
    layoutValue(v, a + 0x150, s.shakePhase);
    layoutValue(v, a + 0x152, s.animKey);
    layoutValue(v, a + 0x154, s.blink);
    layoutValue(v, a + 0x155, s.blinkStarted);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_base_tank_state::GlydeTankFields& s) {
    using namespace od::port::glyde_base_tank_state;
    layoutValue(v, a + 0xc, s.circleBack);
    layoutValue(v, a + 0x2e, s.turretYaw);
    layoutValue(v, a + 0x48, s.pitchStep);
    layoutValue(v, a + 0x4a, s.rollStep);
    layoutValue(v, a + 0x4c, s.pitchGrowth);
    layoutValue(v, a + 0x4e, s.rollGrowth);
    layoutValue(v, a + 0x160, s.fireTicks);
    layoutValue(v, a + 0x162, s.contact);
    layoutValue(v, a + 0x163, s.mode);
    layoutValue(v, a + 0x164, s.fireRequest);
    layoutValue(v, a + 0x165, s.aimTicks);
    layoutValue(v, a + 0x166, s.homeCellX);
    layoutValue(v, a + 0x167, s.homeCellZ);
    layoutValue(v, a + 0x168, s.fireCycle);
    layoutValue(v, a + 0x169, s.stepTicks);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.slopePitch);
    layoutValue(v, a + 0x156, s.slopeRoll);
    layoutValue(v, a + 0x158, s.steerYaw);
    layoutValue(v, a + 0x15a, s.gait);
    layoutValue(v, a + 0x15b, s.gaitBits);
    layoutValue(v, a + 0x15c, s.neighbour);
    layoutValue(v, a + 0x161, s.turnRate);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::glyde_base_turret_state::TurretFields& s) {
    using namespace od::port::glyde_base_turret_state;
    layoutValue(v, a + 0x160, s.bits);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.lastHitContact);
    layoutValue(v, a + 0x162, s.yawOffset);
    layoutValue(v, a + 0x164, s.pitchOffset);
    layoutValue(v, a + 0x166, s.yawTarget);
    layoutValue(v, a + 0x168, s.pitchTarget);
    layoutValue(v, a + 0x16a, s.shotTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st1f_birdbot_tank_state::BirdbotTankFields& s) {
    using namespace od::port::st1f_birdbot_tank_state;
    layoutValue(v, a + 0xc, s.circleBack);
    layoutValue(v, a + 0xe, s.appoSlot);
    layoutValue(v, a + 0xf, s.dahSlot);
    layoutValue(v, a + 0x2e, s.headYaw);
    layoutValue(v, a + 0x48, s.pitchStep);
    layoutValue(v, a + 0x4a, s.rollStep);
    layoutValue(v, a + 0x4c, s.pitchGrowth);
    layoutValue(v, a + 0x4e, s.rollGrowth);
    layoutValue(v, a + 0x160, s.fireTicks);
    layoutValue(v, a + 0x162, s.contact);
    layoutValue(v, a + 0x163, s.mode);
    layoutValue(v, a + 0x164, s.fireRequest);
    layoutValue(v, a + 0x165, s.aimTicks);
    layoutValue(v, a + 0x166, s.homeCellX);
    layoutValue(v, a + 0x167, s.homeCellZ);
    layoutValue(v, a + 0x168, s.fireCycle);
    layoutValue(v, a + 0x169, s.stepTicks);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.slopePitch);
    layoutValue(v, a + 0x156, s.slopeRoll);
    layoutValue(v, a + 0x158, s.steerYaw);
    layoutValue(v, a + 0x15a, s.gait);
    layoutValue(v, a + 0x15b, s.gaitBits);
    layoutValue(v, a + 0x15c, s.neighbour);
    layoutValue(v, a + 0x161, s.turnRate);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st21_props_state::LurePieceFields& s) {
    using namespace od::port::st21_props_state;
    layoutValue(v, a + 0xc, s.owner);
    layoutValue(v, a + 0xd, s.liftBlock);
    layoutValue(v, a + 0xe, s.life);
    for (size_t i = 0; i < s.lifeBytes.size(); ++i) layoutValue(v, a + 0xe + uint32_t(i) * (1), s.lifeBytes[i]);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.pieceFlags);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st21_props_state::RisingModelFields& s) {
    using namespace od::port::st21_props_state;
    layoutValue(v, a + 0xc, s.stop);
    layoutValue(v, a + 0xf, s.riseBias);
    layoutValue(v, a + 0xc0, s.bobPhase);
    layoutValue(v, a + 0xc2, s.dustTicks);
    layoutValue(v, a + 0xbc, s.restY);
    layoutValue(v, a + 0xbe, s.startY);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st22_burrower_state::BurrowerFields& s) {
    using namespace od::port::st22_burrower_state;
    layoutValue(v, a + 0xc, s.throws);
    layoutValue(v, a + 0xf, s.rockVariant);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.bits);
    layoutValue(v, a + 0x156, s.timer);
    layoutValue(v, a + 0x158, s.pushResult);
    layoutValue(v, a + 0x15c, s.neighbour);
    layoutValue(v, a + 0x15e, s.groundY);
    layoutValue(v, a + 0x160, s.lungeSlot);
    layoutValue(v, a + 0x161, s.rockSlot);
    layoutValue(v, a + 0x162, s.rockHold);
    layoutValue(v, a + 0x163, s.throwsLeft);
    layoutValue(v, a + 0x164, s.heading);
    layoutValue(v, a + 0x166, s.wallTurn);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st22_fire_shots_state::FireShotFields& s) {
    using namespace od::port::st22_fire_shots_state;
    layoutValue(v, a + 0xc, s.phase);
    layoutValue(v, a + 0xd, s.unreadD);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xd0, s.frame);
    layoutValue(v, a + 0xd1, s.loopSound);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st32_quiz_state::QuizCastFields& s) {
    using namespace od::port::st32_quiz_state;
    layoutValue(v, a + 0xc, s.cue);
    layoutValue(v, a + 0xe, s.holdPose);
    layoutValue(v, a + 0xf, s.message);
    layoutValue(v, a + 0x15f, s.headYawTarget);
    layoutValue(v, a + 0x168, s.castFlags);
    layoutValue(v, a + 0x169, s.lookMode);
    layoutValue(v, a + 0x16a, s.headHold);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.animTaken);
    layoutValue(v, a + 0x15c, s.headPitch);
    layoutValue(v, a + 0x15d, s.headYaw);
    layoutValue(v, a + 0x15e, s.headPitchTarget);
    layoutValue(v, a + 0x160, s.cueStep);
    layoutValue(v, a + 0x164, s.lookX);
    layoutValue(v, a + 0x166, s.lookZ);
    layoutValue(v, a + 0x16b, s.contact);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::talking_npc_state::TalkingNpcFields& s) {
    using namespace od::port::talking_npc_state;
    layoutValue(v, a + 0xc, s.anim);
    layoutValue(v, a + 0xd, s.behaviour);
    layoutValue(v, a + 0xe, s.hintsMessage);
    layoutValue(v, a + 0xf, s.message);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.talkFlags);
    layoutValue(v, a + 0x156, s.startKey);
    layoutValue(v, a + 0x157, s.frameCopy);
    layoutValue(v, a + 0x158, s.saveMenu);
    layoutValue(v, a + 0x155, s.talkPad);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::tron_boss_state::TronFields& s) {
    using namespace od::port::tron_boss_state;
    layoutValue(v, a + 0xc, s.red);
    layoutValue(v, a + 0xd, s.green);
    layoutValue(v, a + 0xe, s.blue);
    layoutValue(v, a + 0xf, s.animKey);
    layoutValue(v, a + 0x2e, s.missileSide);
    layoutValue(v, a + 0x158, s.flags);
    layoutValue(v, a + 0x150, s.stats);
    layoutValue(v, a + 0x154, s.work);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x160, s.playerDistance);
    layoutValue(v, a + 0x162, s.playerAngle);
    layoutValue(v, a + 0x164, s.playerPitch);
    layoutValue(v, a + 0x166, s.jumpSpeed);
    if (s.work) layoutValue(v, s.work, s.workBlock);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0x15e + uint32_t(i) * (1), s.clearedPad0[i]);
    for (size_t i = 0; i < s.clearedPad1.size(); ++i) layoutValue(v, a + 0x168 + uint32_t(i) * (1), s.clearedPad1[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::tron_boss_state::TronFightBlock& s) {
    using namespace od::port::tron_boss_state;
    layoutValue(v, a + 0x00, s.body);
    layoutValue(v, a + 0x10, s.leftArm);
    layoutValue(v, a + 0x20, s.rightArm);
    layoutValue(v, a + 0x30, s.box);
    layoutValue(v, a + 0x3c, s.moveYaw);
    layoutValue(v, a + 0x3e, s.prevMoveYaw);
    layoutValue(v, a + 0x40, s.leftArmHitPoints);
    layoutValue(v, a + 0x42, s.rightArmHitPoints);
    layoutValue(v, a + 0x44, s.queuedDamage);
    layoutValue(v, a + 0x46, s.heavyHits);
    layoutValue(v, a + 0x4a, s.dustCount);
    layoutValue(v, a + 0x4c, s.lastAttack);
    layoutValue(v, a + 0x4d, s.bouncesLeft);
    layoutValue(v, a + 0x4e, s.tauntsSaid);
    layoutValue(v, a + 0x50, s.tauntTicks);
    layoutValue(v, a + 0x52, s.bigJumpTicks);
    for (size_t i = 0; i < s.unread0.size(); ++i) layoutValue(v, a + 0x8 + uint32_t(i), s.unread0[i]);
    for (size_t i = 0; i < s.unread1.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i), s.unread1[i]);
    for (size_t i = 0; i < s.unread2.size(); ++i) layoutValue(v, a + 0x28 + uint32_t(i), s.unread2[i]);
    layoutValue(v, a + 0x48, s.unread3);
    for (size_t i = 0; i < s.unread4.size(); ++i) layoutValue(v, a + 0x54 + uint32_t(i), s.unread4[i]);
}

bool classes_calbania(LayoutVisitor& v, uint32_t a, ActorClassState& s) {
    if (typeid(s) == typeid(od::port::glyde_base_scene_birdbots_state::SceneBirdbotFields)) { layoutValue(v, a, static_cast<od::port::glyde_base_scene_birdbots_state::SceneBirdbotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::base_alarm_state::AlarmFields)) { layoutValue(v, a, static_cast<od::port::base_alarm_state::AlarmFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::appo_dah_a_state::GatePuppetFields)) { layoutValue(v, a, static_cast<od::port::appo_dah_a_state::GatePuppetFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::glyde_base_tank_state::GlydeTankFields)) { layoutValue(v, a, static_cast<od::port::glyde_base_tank_state::GlydeTankFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::appo_dah_a_state::GuideFields)) { layoutValue(v, a, static_cast<od::port::appo_dah_a_state::GuideFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::glyde_base_bombers_state::GlydeBomberFields)) { layoutValue(v, a, static_cast<od::port::glyde_base_bombers_state::GlydeBomberFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::carried_npc_state::CarriedNpcFields)) { layoutValue(v, a, static_cast<od::port::carried_npc_state::CarriedNpcFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::appo_dah_b_state::AppoDahBombFields)) { layoutValue(v, a, static_cast<od::port::appo_dah_b_state::AppoDahBombFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st1f_birdbot_tank_state::BirdbotTankFields)) { layoutValue(v, a, static_cast<od::port::st1f_birdbot_tank_state::BirdbotTankFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ch3_script_cast_state::Script3cCastFields)) { layoutValue(v, a, static_cast<od::port::ch3_script_cast_state::Script3cCastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st22_burrower_state::BurrowerFields)) { layoutValue(v, a, static_cast<od::port::st22_burrower_state::BurrowerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st32_quiz_state::QuizCastFields)) { layoutValue(v, a, static_cast<od::port::st32_quiz_state::QuizCastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::calbania_cast_state::StandingCastFields)) { layoutValue(v, a, static_cast<od::port::calbania_cast_state::StandingCastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st22_fire_shots_state::FireShotFields)) { layoutValue(v, a, static_cast<od::port::st22_fire_shots_state::FireShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::calbania_cast_state::TownBirdFields)) { layoutValue(v, a, static_cast<od::port::calbania_cast_state::TownBirdFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::glyde_base_key_state::GlydeKeyFields)) { layoutValue(v, a, static_cast<od::port::glyde_base_key_state::GlydeKeyFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::calbania_spawners_state::St1fSpawnerFields)) { layoutValue(v, a, static_cast<od::port::calbania_spawners_state::St1fSpawnerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::calbania_cast_state::SittingPuppetFields)) { layoutValue(v, a, static_cast<od::port::calbania_cast_state::SittingPuppetFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ch3_script_cast_state::Script20AppoDahFields)) { layoutValue(v, a, static_cast<od::port::ch3_script_cast_state::Script20AppoDahFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::AppoDahFightFields)) { layoutValue(v, a, static_cast<od::port::AppoDahFightFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::tron_boss_state::TronFields)) { layoutValue(v, a, static_cast<od::port::tron_boss_state::TronFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::appo_dah_b_state::ScriptPuppetFields)) { layoutValue(v, a, static_cast<od::port::appo_dah_b_state::ScriptPuppetFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::escape_timer_state::EscapeTimerFields)) { layoutValue(v, a, static_cast<od::port::escape_timer_state::EscapeTimerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::glyde_base_gun_state::GunFields)) { layoutValue(v, a, static_cast<od::port::glyde_base_gun_state::GunFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::glyde_base_turret_state::TurretFields)) { layoutValue(v, a, static_cast<od::port::glyde_base_turret_state::TurretFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st21_props_state::LurePieceFields)) { layoutValue(v, a, static_cast<od::port::st21_props_state::LurePieceFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::talking_npc_state::TalkingNpcFields)) { layoutValue(v, a, static_cast<od::port::talking_npc_state::TalkingNpcFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::calbania_cast_state::WalkingPuppetFields)) { layoutValue(v, a, static_cast<od::port::calbania_cast_state::WalkingPuppetFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st21_props_state::RisingModelFields)) { layoutValue(v, a, static_cast<od::port::st21_props_state::RisingModelFields&>(s)); return true; }
    return false;
}
} // namespace od::test
