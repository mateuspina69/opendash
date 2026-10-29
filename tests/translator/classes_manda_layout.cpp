// The native records of this area in the exe layout (test-only).
#include "translator/actor_layout.hpp"
#include "content/manda/exam_enemies_a_state.hpp"
#include "content/manda/exam_enemies_b_state.hpp"
#include "content/manda/exam_spawner_state.hpp"
#include "content/manda/hopping_drone_state.hpp"
#include "content/manda/manda_boss_state.hpp"
#include "content/manda/manda_guardian_state.hpp"
#include "content/manda/manda_ruin_burrower_state.hpp"
#include "content/manda/manda_ruin_props_state.hpp"
#include "content/manda/manda_ruin_scripts_state.hpp"
#include "content/manda/ruin_spawners_state.hpp"
#include "content/manda/st12_14_state.hpp"
#include "content/manda/st14_enemy_state.hpp"
#include "content/manda/st24_ruin_gate_state.hpp"
#include "content/manda/st25_tron_scenes_state.hpp"

namespace od::test {
using namespace od::port;

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::exam_enemies_a_state::gunner::Fields& s) {
    using namespace od::port::exam_enemies_a_state::gunner;
    layoutValue(v, a + 0xf, s.startKey);
    layoutValue(v, a + 0x2e, s.shotTimer);
    layoutValue(v, a + 0x2e, s.flightYaw);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.weakBox);
    layoutValue(v, a + 0x164, s.sight);
    layoutValue(v, a + 0x165, s.mode);
    layoutValue(v, a + 0x166, s.mayFall);
    layoutValue(v, a + 0x168, s.timer);
    layoutValue(v, a + 0x16a, s.counter);
    layoutValue(v, a + 0x16b, s.contact);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::exam_enemies_a_state::gunnerShot::Fields& s) {
    using namespace od::port::exam_enemies_a_state::gunnerShot;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc8, s.ticks);
    layoutValue(v, a + 0xca, s.brightness);
    layoutValue(v, a + 0xcc, s.radius);
    layoutValue(v, a + 0xce, s.texelU);
    layoutValue(v, a + 0xce, s.texelUByte);
    layoutValue(v, a + 0xd0, s.phase);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::exam_enemies_a_state::hopper::Fields& s) {
    using namespace od::port::exam_enemies_a_state::hopper;
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.turn);
    layoutValue(v, a + 0x161, s.contact);
    layoutValue(v, a + 0x162, s.mode);
    layoutValue(v, a + 0x163, s.hitsLeft);
    layoutValue(v, a + 0x164, s.side);
    layoutValue(v, a + 0x168, s.thinkTimer);
    layoutValue(v, a + 0x16a, s.throwYaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::exam_enemies_a_state::shield::Fields& s) {
    using namespace od::port::exam_enemies_a_state::shield;
    layoutValue(v, a + 0xbc, s.flightYaw);
    layoutValue(v, a + 0xbe, s.bounced);
    layoutValue(v, a + 0xbf, s.fade);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::exam_enemies_a_state::tumbler::Fields& s) {
    using namespace od::port::exam_enemies_a_state::tumbler;
    layoutValue(v, a + 0xc, s.bouncesLeft);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.targetYaw);
    layoutValue(v, a + 0x160, s.contactWord);
    layoutValue(v, a + 0x160, s.contact);
    layoutValue(v, a + 0x162, s.shell);
    layoutValue(v, a + 0x163, s.alert);
    layoutValue(v, a + 0x164, s.mode);
    layoutValue(v, a + 0x165, s.stun);
    layoutValue(v, a + 0x168, s.ground);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::exam_enemies_b_state::AmbushFields& s) {
    using namespace od::port::exam_enemies_b_state;
    layoutValue(v, a + 0xc, s.records);
    layoutValue(v, a + 0xd, s.holdNear);
    for (size_t i = 0; i < s.spawned.size(); ++i) layoutValue(v, a + 0x10 + uint32_t(i) * (4), s.spawned[i]);
    layoutValue(v, a + 0x1c, s.group);
    layoutValue(v, a + 0x1d, s.woken);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::exam_enemies_b_state::DiggerFields& s) {
    using namespace od::port::exam_enemies_b_state;
    layoutValue(v, a + 0xc, s.nextSub);
    layoutValue(v, a + 0xd, s.cooldown);
    layoutValue(v, a + 0xe, s.offset);
    layoutValue(v, a + 0x14c, s.hits);
    layoutValue(v, a + 0x150, s.timer);
    layoutValue(v, a + 0x152, s.knockThreshold);
    layoutValue(v, a + 0x154, s.unused154);
    layoutValue(v, a + 0x156, s.fadeTicks);
    layoutValue(v, a + 0x158, s.offscreen);
    layoutValue(v, a + 0x15a, s.flags);
    layoutValue(v, a + 0x15b, s.red);
    layoutValue(v, a + 0x15c, s.green);
    layoutValue(v, a + 0x15d, s.blue);
    layoutValue(v, a + 0x15e, s.contact);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::exam_enemies_b_state::DiggerHitBlock& s) {
    using namespace od::port::exam_enemies_b_state;
    layoutValue(v, a + 0x0, s.body);
    layoutValue(v, a + 0x10, s.claws);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::exam_enemies_b_state::DroneFields& s) {
    using namespace od::port::exam_enemies_b_state;
    layoutValue(v, a + 0x46, s.timer);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.retarget);
    layoutValue(v, a + 0xc6, s.heightOffset);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::exam_enemies_b_state::FlyerFields& s) {
    using namespace od::port::exam_enemies_b_state;
    layoutValue(v, a + 0xb, s.keepZ);
    layoutValue(v, a + 0xc, s.wake);
    layoutValue(v, a + 0x164, s.flags);
    layoutValue(v, a + 0x165, s.contact);
    layoutValue(v, a + 0x166, s.group);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.perch);
    layoutValue(v, a + 0x15a, s.perchYaw);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.pitch);
    layoutValue(v, a + 0x160, s.gauge);
    layoutValue(v, a + 0x162, s.reflectedYaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::exam_enemies_b_state::HatchFields& s) {
    using namespace od::port::exam_enemies_b_state;
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x150, s.scriptMark);
    layoutValue(v, a + 0x15c, s.blinkTicks);
    layoutValue(v, a + 0x160, s.releaseTimer);
    layoutValue(v, a + 0x163, s.mode);
    layoutValue(v, a + 0xf, s.cellZ1);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::exam_enemies_b_state::MineFields& s) {
    using namespace od::port::exam_enemies_b_state;
    layoutValue(v, a + 0xb, s.skipNeighbour);
    layoutValue(v, a + 0x3e, s.returnHeight);
    layoutValue(v, a + 0x4e, s.swayAngle);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xca, s.flags);
    layoutValue(v, a + 0xcb, s.phase);
    layoutValue(v, a + 0xc4, s.baseHeight);
    layoutValue(v, a + 0xc6, s.timer);
    for (size_t i = 0; i < s.glows.size(); ++i) layoutValue(v, a + 0xc8 + uint32_t(i) * (1), s.glows[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::exam_spawner_state::ExamSpawnerFields& s) {
    using namespace od::port::exam_spawner_state;
    for (size_t i = 0; i < s.unread.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.unread[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::hopping_drone_state::DroneFields& s) {
    using namespace od::port::hopping_drone_state;
    layoutValue(v, a + 0x168, s.side);
    layoutValue(v, a + 0x169, s.beats);
    layoutValue(v, a + 0x16a, s.flameSlot);
    layoutValue(v, a + 0xc, s.paramsWord_);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.timer);
    layoutValue(v, a + 0x156, s.flightYaw);
    layoutValue(v, a + 0x158, s.contact);
    layoutValue(v, a + 0x159, s.behaviour);
    layoutValue(v, a + 0x15a, s.hopStepX);
    layoutValue(v, a + 0x15c, s.hopStepZ);
    layoutValue(v, a + 0x15e, s.hopDistance);
    layoutValue(v, a + 0x162, s.homeYaw);
    layoutValue(v, a + 0x164, s.drift);
    layoutValue(v, a + 0x166, s.driftLimit);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_boss_state::BeamHeadFields& s) {
    using namespace od::port::manda_boss_state;
    layoutValue(v, a + 0xc, s.pattern);
    layoutValue(v, a + 0xd, s.tick);
    layoutValue(v, a + 0xe, s.done);
    layoutValue(v, a + 0xf, s.segments);
    layoutValue(v, a + 0x4c, s.aimPitch);
    layoutValue(v, a + 0x4e, s.aimYaw);
    layoutValue(v, a + 0x70, s.pitchOffset);
    layoutValue(v, a + 0x74, s.target);
    layoutValue(v, a + 0x78, s.headFlags);
    layoutValue(v, a + 0x78, s.headFlagsByte);
    layoutValue(v, a + 0x7a, s.holdTicks);
    for (size_t i = 0; i < s.segmentSlots.size(); ++i) layoutValue(v, a + 0x8c + uint32_t(i) * (4), s.segmentSlots[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_boss_state::BeamSegmentFields& s) {
    using namespace od::port::manda_boss_state;
    layoutValue(v, a + 0xf, s.segments);
    layoutValue(v, a + 0x86, s.matrixPad);
    layoutValue(v, a + 0x94, s.segmentFlags);
    layoutValue(v, a + 0x94, s.segmentFlagsByte);
    layoutValue(v, a + 0x98, s.previous);
    layoutValue(v, a + 0x9c, s.hitBox);
    offsetActorRef(v, a + 0xb4, s.cornersLink, 0xa4);
    layoutValue(v, a + 0xb8, s.width);
    layoutValue(v, a + 0xba, s.length);
    layoutValue(v, a + 0xbc, s.floater);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_boss_state::DebrisFields& s) {
    using namespace od::port::manda_boss_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.flags);
    layoutValue(v, a + 0xc8, s.timer);
    layoutValue(v, a + 0xca, s.burstTicks);
    layoutValue(v, a + 0xcc, s.playerDistance);
    layoutValue(v, a + 0xd0, s.playerYaw);
    layoutValue(v, a + 0xd2, s.playerPitch);
    layoutValue(v, a + 0xd4, s.launchSpeed);
    layoutValue(v, a + 0xd6, s.column);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_boss_state::FightFields& s) {
    using namespace od::port::manda_boss_state;
    layoutValue(v, a + 0xc, s.bits);
    layoutValue(v, a + 0xd, s.floaterTimer);
    layoutValue(v, a + 0xe, s.rollerTimer);
    layoutValue(v, a + 0x18, s.guardian);
    layoutValue(v, a + 0x1c, s.floater);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_boss_state::FloaterFields& s) {
    using namespace od::port::manda_boss_state;
    layoutValue(v, a + 0xd, s.caught);
    layoutValue(v, a + 0xe, s.released);
    layoutValue(v, a + 0xf, s.startKey);
    layoutValue(v, a + 0xa, s.turnMode);
    layoutValue(v, a + 0xb, s.speedMode);
    layoutValue(v, a + 0x158, s.playerDistance);
    layoutValue(v, a + 0x4e, s.turnAccel);
    layoutValue(v, a + 0x14c, s.controller);
    layoutValue(v, a + 0x150, s.flags);
    layoutValue(v, a + 0x154, s.timer);
    layoutValue(v, a + 0x156, s.bob);
    layoutValue(v, a + 0x15c, s.centreDistance);
    layoutValue(v, a + 0x15e, s.centreAngle);
    layoutValue(v, a + 0x160, s.hitBox);
    layoutValue(v, a + 0x168, s.statsRow);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_boss_state::GuardianFields& s) {
    using namespace od::port::manda_boss_state;
    layoutValue(v, a + 0xc, s.red);
    layoutValue(v, a + 0xf, s.startKey);
    layoutValue(v, a + 0x14c, s.controller);
    layoutValue(v, a + 0x150, s.target);
    layoutValue(v, a + 0x154, s.beam);
    layoutValue(v, a + 0x158, s.work);
    layoutValue(v, a + 0x15c, s.hits);
    layoutValue(v, a + 0x160, s.flags);
    layoutValue(v, a + 0x164, s.timer);
    layoutValue(v, a + 0x166, s.counter);
    layoutValue(v, a + 0x168, s.playerYaw);
    layoutValue(v, a + 0x16a, s.playerPitch);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_boss_state::GuardianHitBoxes& s) {
    using namespace od::port::manda_boss_state;
    layoutValue(v, a + 0x0, s.main);
    layoutValue(v, a + 0x8, s.body);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_boss_state::GuardianFightBlock& s) {
    using namespace od::port::manda_boss_state;
    layoutValue(v, a + 0x0, s.statsRow);
    layoutValue(v, a + 0x4, s.perch);
    layoutValue(v, a + 0x5, s.nextPerch);
    layoutValue(v, a + 0x6, s.playerQuadrant);
    layoutValue(v, a + 0x7, s.beamQuadrant);
    layoutValue(v, a + 0x8, s.targetYaw);
    layoutValue(v, a + 0xa, s.turnYaw);
    for (size_t i = 0; i < s.unread.size(); ++i) layoutValue(v, a + 0xc + uint32_t(i) * (1), s.unread[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_boss_state::PuppetFields& s) {
    using namespace od::port::manda_boss_state;
    layoutValue(v, a + 0xf, s.startKey);
    layoutValue(v, a + 0x14e, s.timer);
    layoutValue(v, a + 0x150, s.ground);
    layoutValue(v, a + 0x150, s.fadeCounter);
    layoutValue(v, a + 0x151, s.dustPhase);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_guardian_state::BladeFields& s) {
    using namespace od::port::manda_guardian_state;
    layoutValue(v, a + 0xc, s.corner);
    layoutValue(v, a + 0x3e, s.travelYaw);
    layoutValue(v, a + 0x4e, s.timer);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc5, s.group);
    layoutValue(v, a + 0xc6, s.travel);
    layoutValue(v, a + 0xc8, s.targetX);
    layoutValue(v, a + 0xca, s.targetZ);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_guardian_state::ChestFields& s) {
    using namespace od::port::manda_guardian_state;
    layoutValue(v, a + 0x14c, s.startKey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_guardian_state::CloudFields& s) {
    using namespace od::port::manda_guardian_state;
    layoutValue(v, a + 0x9, s.ringIndex);
    layoutValue(v, a + 0xa, s.size);
    layoutValue(v, a + 0xb, s.shrinkTicks);
    layoutValue(v, a + 0xc, s.spin);
    layoutValue(v, a + 0xd, s.shade);
    layoutValue(v, a + 0xe, s.phase);
    layoutValue(v, a + 0xf, s.phaseStep);
    layoutValue(v, a + 0x16, s.holdTicks);
    for (size_t i = 0; i < s.ring.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.ring[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_guardian_state::FallingBladeFields& s) {
    using namespace od::port::manda_guardian_state;
    layoutValue(v, a + 0xc4, s.hits);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_guardian_state::GuardianFields& s) {
    using namespace od::port::manda_guardian_state;
    layoutValue(v, a + 0x2e, s.leanTarget);
    layoutValue(v, a + 0x3e, s.headTurn);
    layoutValue(v, a + 0x15f, s.volleys);
    layoutValue(v, a + 0x160, s.hits);
    layoutValue(v, a + 0x162, s.fadeCounter);
    layoutValue(v, a + 0x163, s.soundTimer);
    layoutValue(v, a + 0x166, s.bobs);
    layoutValue(v, a + 0x167, s.group);
    layoutValue(v, a + 0x168, s.invulnerable);
    layoutValue(v, a + 0x169, s.count);
    layoutValue(v, a + 0x14c, s.bodyBox);
    layoutValue(v, a + 0x154, s.landingBox);
    layoutValue(v, a + 0x15c, s.rollSpeed);
    layoutValue(v, a + 0x15e, s.flags);
    layoutValue(v, a + 0x161, s.retreats);
    layoutValue(v, a + 0x164, s.damageTaken);
    layoutValue(v, a + 0x16a, s.timer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_guardian_state::HopperFields& s) {
    using namespace od::port::manda_guardian_state;
    layoutValue(v, a + 0xe, s.turnRate);
    layoutValue(v, a + 0x160, s.hits);
    layoutValue(v, a + 0x164, s.regenTicks);
    layoutValue(v, a + 0x165, s.group);
    layoutValue(v, a + 0x166, s.marks);
    layoutValue(v, a + 0x167, s.mode);
    layoutValue(v, a + 0x168, s.knockedOut);
    layoutValue(v, a + 0x169, s.hopsLeft);
    layoutValue(v, a + 0x16a, s.glow);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.heading);
    layoutValue(v, a + 0x161, s.flags);
    layoutValue(v, a + 0x162, s.regen);
    layoutValue(v, a + 0x16b, s.rollSide);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_guardian_state::ShotFields& s) {
    using namespace od::port::manda_guardian_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.heading);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xcc + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_guardian_state::ShotTrailFields& s) {
    using namespace od::port::manda_guardian_state;
    layoutValue(v, a + 0xc, s.roll);
    layoutValue(v, a + 0xd, s.yaw);
    layoutValue(v, a + 0xe, s.prevRoll);
    layoutValue(v, a + 0xf, s.prevYaw);
    layoutValue(v, a + 0x10, s.head);
    layoutValue(v, a + 0x18, s.tail);
    layoutValue(v, a + 0x1e, s.ticks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_ruin_burrower_state::BurrowerFields& s) {
    using namespace od::port::manda_ruin_burrower_state;
    layoutValue(v, a + 0x2e, s.glowSlots);
    layoutValue(v, a + 0x2e, s.secondGlow);
    layoutValue(v, a + 0x2f, s.firstGlow);
    layoutValue(v, a + 0x166, s.swayStep);
    layoutValue(v, a + 0x167, s.contact);
    layoutValue(v, a + 0x168, s.mode);
    layoutValue(v, a + 0x169, s.bouncesLeft);
    layoutValue(v, a + 0x16a, s.startKey);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.savedYaw);
    layoutValue(v, a + 0x160, s.turnLeft);
    layoutValue(v, a + 0x162, s.headYaw);
    layoutValue(v, a + 0x164, s.sway);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_ruin_props_state::PlatformFields& s) {
    using namespace od::port::manda_ruin_props_state;
    layoutValue(v, a + 0xc, s.runCells);
    layoutValue(v, a + 0xc, s.runLength);
    layoutValue(v, a + 0xc, s.driven);
    layoutValue(v, a + 0xe, s.riderId);
    layoutValue(v, a + 0xe, s.sparkStep);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc7, s.landed);
    layoutValue(v, a + 0xc8, s.hitFlags);
    layoutValue(v, a + 0xca, s.rider);
    layoutValue(v, a + 0xc4, s.distanceLeft);
    layoutValue(v, a + 0xc6, s.companion);
    layoutValue(v, a + 0xc9, s.speed);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_ruin_scripts_state::BurningWreckFields& s) {
    using namespace od::port::manda_ruin_scripts_state;
    layoutValue(v, a + 0x14e, s.groundY);
    layoutValue(v, a + 0x150, s.life);
    layoutValue(v, a + 0x152, s.blastTimer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::manda_ruin_scripts_state::GroundedFigureFields& s) {
    using namespace od::port::manda_ruin_scripts_state;
    layoutValue(v, a + 0x14d, s.startKey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_spawners_state::DroneSetFields& s) {
    using namespace od::port::ruin_spawners_state;
    layoutValue(v, a, static_cast<od::port::ruin_spawners_state::SpawnerFields&>(s));
    for (size_t i = 0; i < s.drones.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.drones[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_spawners_state::DropFields& s) {
    using namespace od::port::ruin_spawners_state;
    layoutValue(v, a, static_cast<od::port::ruin_spawners_state::SpawnerFields&>(s));
    layoutValue(v, a + 0x10, s.anchorX);
    layoutValue(v, a + 0x14, s.anchorZ);
    layoutValue(v, a + 0x1c, s.count);
    layoutValue(v, a + 0x1d, s.delay);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_spawners_state::EmitterFields& s) {
    using namespace od::port::ruin_spawners_state;
    layoutValue(v, a, static_cast<od::port::ruin_spawners_state::SpawnerFields&>(s));
    layoutValue(v, a + 0x19, s.side);
    for (size_t i = 0; i < s.timers.size(); ++i) layoutValue(v, a + 0x1a + uint32_t(i) * (1), s.timers[i]);
    for (size_t i = 0; i < s.halves.size(); ++i) layoutValue(v, a + 0x1d + uint32_t(i) * (1), s.halves[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_spawners_state::HopperSetFields& s) {
    using namespace od::port::ruin_spawners_state;
    layoutValue(v, a, static_cast<od::port::ruin_spawners_state::SpawnerFields&>(s));
    layoutValue(v, a + 0xc, s.guardian);
    layoutValue(v, a + 0xd, s.delay);
    for (size_t i = 0; i < s.slots.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.slots[i]);
    layoutValue(v, a + 0x1c, s.pointSet);
    layoutValue(v, a + 0x1d, s.aliveMask);
    layoutValue(v, a + 0x1e, s.aliveCount);
    layoutValue(v, a + 0x1f, s.group);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_spawners_state::RespawnFields& s) {
    using namespace od::port::ruin_spawners_state;
    layoutValue(v, a, static_cast<od::port::ruin_spawners_state::SpawnerFields&>(s));
    layoutValue(v, a + 0x1a, s.respawnsLeft);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_spawners_state::SpawnerFields& s) {
    (void)v, (void)a, (void)s; // the common base keeps no bytes of its own
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_spawners_state::St14RespawnFields& s) {
    using namespace od::port::ruin_spawners_state;
    layoutValue(v, a, static_cast<od::port::ruin_spawners_state::SpawnerFields&>(s));
    layoutValue(v, a + 0x18, s.respawnsLeft);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st12_14_state::BoulderFields& s) {
    using namespace od::port::st12_14_state;
    layoutValue(v, a + 0xb, s.companionSlot);
    layoutValue(v, a + 0xc, s.endCellX);
    layoutValue(v, a + 0xd, s.endCellZ);
    layoutValue(v, a + 0xf, s.modelVariant);
    layoutValue(v, a + 0x2e, s.pathYaw);
    layoutValue(v, a + 0x3e, s.returning);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.startX);
    layoutValue(v, a + 0xc6, s.startZ);
    layoutValue(v, a + 0xc8, s.endX);
    layoutValue(v, a + 0xca, s.endZ);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st12_14_state::TumbleFields& s) {
    using namespace od::port::st12_14_state;
    layoutValue(v, a + 0xc, s.facingBits);
    layoutValue(v, a + 0x2e, s.rockAmplitude);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc8, s.active);
    layoutValue(v, a + 0xc4, s.facing);
    layoutValue(v, a + 0xc6, s.slideLeft);
    layoutValue(v, a + 0xc9, s.timer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st12_14_state::TurretFields& s) {
    using namespace od::port::st12_14_state;
    layoutValue(v, a + 0xd, s.unused);
    layoutValue(v, a + 0xe, s.bursts);
    layoutValue(v, a + 0xf, s.startKey);
    layoutValue(v, a + 0x14c, s.flags);
    layoutValue(v, a + 0x14e, s.timer);
    layoutValue(v, a + 0x150, s.playerDistance);
    layoutValue(v, a + 0x154, s.playerYaw);
    layoutValue(v, a + 0x156, s.playerPitch);
    layoutValue(v, a + 0x158, s.headYaw);
    layoutValue(v, a + 0x15a, s.headPitch);
    layoutValue(v, a + 0x15c, s.bodyBox);
    layoutValue(v, a + 0x164, s.barrelBox);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st12_14_state::WalkerFields& s) {
    using namespace od::port::st12_14_state;
    layoutValue(v, a + 0x163, s.contact);
    layoutValue(v, a + 0x164, s.mode);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.blinkTicks);
    layoutValue(v, a + 0x15e, s.knockYaw);
    layoutValue(v, a + 0x160, s.vanishTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st14_enemy_state::HoverFields& s) {
    using namespace od::port::st14_enemy_state;
    layoutValue(v, a + 0x3e, s.cruiseSpeed);
    layoutValue(v, a + 0x4e, s.spinAccel);
    layoutValue(v, a + 0x168, s.noticeCooldown);
    layoutValue(v, a + 0x169, s.hitResult);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.mode);
    layoutValue(v, a + 0x155, s.contact);
    layoutValue(v, a + 0x156, s.home);
    layoutValue(v, a + 0x15c, s.target);
    layoutValue(v, a + 0x162, s.movePitch);
    layoutValue(v, a + 0x164, s.moveYaw);
    layoutValue(v, a + 0x166, s.timer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st24_ruin_gate_state::GateGuardianFields& s) {
    using namespace od::port::st24_ruin_gate_state;
    layoutValue(v, a + 0x2e, s.leanTarget);
    layoutValue(v, a + 0x3e, s.headTurn);
    layoutValue(v, a + 0x160, s.hits);
    layoutValue(v, a + 0x162, s.fadeCounter);
    layoutValue(v, a + 0x166, s.bobs);
    layoutValue(v, a + 0x169, s.dustPhase);
    layoutValue(v, a + 0x15c, s.rollSpeed);
    layoutValue(v, a + 0x15e, s.flags);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st25_tron_scenes_state::FigureFields& s) {
    using namespace od::port::st25_tron_scenes_state;
    constexpr uint32_t kClassBlock = od::port::kAnimatedClassBlockOffset; // the class block (+0x14c)
    layoutValue(v, a + kClassBlock + 0, s.tracks);
    layoutValue(v, a + kClassBlock + 0x10, s.flags);
    layoutValue(v, a + kClassBlock + 0x11, s.startKey);
    layoutValue(v, a + kClassBlock + 0x12, s.hover);
    layoutValue(v, a + kClassBlock + 0x14, s.shadowSize);
    layoutValue(v, a + kClassBlock + 0x16, s.sparkCounter);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st25_tron_scenes_state::TankLandingFields& s) {
    using namespace od::port::st25_tron_scenes_state;
    constexpr uint32_t kClassBlock = od::port::kAnimatedClassBlockOffset; // the class block (+0x14c)
    layoutValue(v, a + kClassBlock + 0, s.startKey);
    layoutValue(v, a + kClassBlock + 1, s.flags);
    layoutValue(v, a + kClassBlock + 4, s.dustTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st25_tron_scenes_state::WreckFields& s) {
    using namespace od::port::st25_tron_scenes_state;
    constexpr uint32_t kClassBlock = od::port::kAnimatedClassBlockOffset; // the class block (+0x14c)
    layoutValue(v, a + kClassBlock + 0, s.flags);
    layoutValue(v, a + kClassBlock + 1, s.startKey);
    layoutValue(v, a + kClassBlock + 2, s.baseY);
    for (size_t i = 0; i < s.partMarks.size(); ++i) layoutValue(v, a + kClassBlock + 4 + uint32_t(i) * (1), s.partMarks[i]);
}

bool classes_manda(LayoutVisitor& v, uint32_t a, ActorClassState& s) {
    if (typeid(s) == typeid(od::port::ruin_spawners_state::EmitterFields)) { layoutValue(v, a, static_cast<od::port::ruin_spawners_state::EmitterFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::hopping_drone_state::DroneFields)) { layoutValue(v, a, static_cast<od::port::hopping_drone_state::DroneFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::exam_enemies_a_state::shield::Fields)) { layoutValue(v, a, static_cast<od::port::exam_enemies_a_state::shield::Fields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ruin_spawners_state::St14RespawnFields)) { layoutValue(v, a, static_cast<od::port::ruin_spawners_state::St14RespawnFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_ruin_burrower_state::BurrowerFields)) { layoutValue(v, a, static_cast<od::port::manda_ruin_burrower_state::BurrowerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ruin_spawners_state::RespawnFields)) { layoutValue(v, a, static_cast<od::port::ruin_spawners_state::RespawnFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_guardian_state::HopperFields)) { layoutValue(v, a, static_cast<od::port::manda_guardian_state::HopperFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::exam_enemies_a_state::gunnerShot::Fields)) { layoutValue(v, a, static_cast<od::port::exam_enemies_a_state::gunnerShot::Fields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ruin_spawners_state::DropFields)) { layoutValue(v, a, static_cast<od::port::ruin_spawners_state::DropFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ruin_spawners_state::HopperSetFields)) { layoutValue(v, a, static_cast<od::port::ruin_spawners_state::HopperSetFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ruin_spawners_state::DroneSetFields)) { layoutValue(v, a, static_cast<od::port::ruin_spawners_state::DroneSetFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_ruin_scripts_state::GroundedFigureFields)) { layoutValue(v, a, static_cast<od::port::manda_ruin_scripts_state::GroundedFigureFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::exam_enemies_a_state::gunner::Fields)) { layoutValue(v, a, static_cast<od::port::exam_enemies_a_state::gunner::Fields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_guardian_state::FallingBladeFields)) { layoutValue(v, a, static_cast<od::port::manda_guardian_state::FallingBladeFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st25_tron_scenes_state::FigureFields)) { layoutValue(v, a, static_cast<od::port::st25_tron_scenes_state::FigureFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::exam_enemies_b_state::HatchFields)) { layoutValue(v, a, static_cast<od::port::exam_enemies_b_state::HatchFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_guardian_state::ShotTrailFields)) { layoutValue(v, a, static_cast<od::port::manda_guardian_state::ShotTrailFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_guardian_state::ShotFields)) { layoutValue(v, a, static_cast<od::port::manda_guardian_state::ShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::exam_enemies_b_state::DiggerFields)) { layoutValue(v, a, static_cast<od::port::exam_enemies_b_state::DiggerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st12_14_state::TumbleFields)) { layoutValue(v, a, static_cast<od::port::st12_14_state::TumbleFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::exam_enemies_b_state::DroneFields)) { layoutValue(v, a, static_cast<od::port::exam_enemies_b_state::DroneFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_boss_state::FloaterFields)) { layoutValue(v, a, static_cast<od::port::manda_boss_state::FloaterFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_ruin_scripts_state::BurningWreckFields)) { layoutValue(v, a, static_cast<od::port::manda_ruin_scripts_state::BurningWreckFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::exam_enemies_b_state::MineFields)) { layoutValue(v, a, static_cast<od::port::exam_enemies_b_state::MineFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st25_tron_scenes_state::WreckFields)) { layoutValue(v, a, static_cast<od::port::st25_tron_scenes_state::WreckFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::exam_enemies_a_state::hopper::Fields)) { layoutValue(v, a, static_cast<od::port::exam_enemies_a_state::hopper::Fields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::exam_enemies_a_state::tumbler::Fields)) { layoutValue(v, a, static_cast<od::port::exam_enemies_a_state::tumbler::Fields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st25_tron_scenes_state::TankLandingFields)) { layoutValue(v, a, static_cast<od::port::st25_tron_scenes_state::TankLandingFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_guardian_state::CloudFields)) { layoutValue(v, a, static_cast<od::port::manda_guardian_state::CloudFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::exam_enemies_b_state::AmbushFields)) { layoutValue(v, a, static_cast<od::port::exam_enemies_b_state::AmbushFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st12_14_state::TurretFields)) { layoutValue(v, a, static_cast<od::port::st12_14_state::TurretFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::exam_spawner_state::ExamSpawnerFields)) { layoutValue(v, a, static_cast<od::port::exam_spawner_state::ExamSpawnerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st12_14_state::BoulderFields)) { layoutValue(v, a, static_cast<od::port::st12_14_state::BoulderFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_boss_state::FightFields)) { layoutValue(v, a, static_cast<od::port::manda_boss_state::FightFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st24_ruin_gate_state::GateGuardianFields)) { layoutValue(v, a, static_cast<od::port::st24_ruin_gate_state::GateGuardianFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_boss_state::GuardianFields)) { layoutValue(v, a, static_cast<od::port::manda_boss_state::GuardianFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_boss_state::BeamSegmentFields)) { layoutValue(v, a, static_cast<od::port::manda_boss_state::BeamSegmentFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::exam_enemies_b_state::FlyerFields)) { layoutValue(v, a, static_cast<od::port::exam_enemies_b_state::FlyerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st14_enemy_state::HoverFields)) { layoutValue(v, a, static_cast<od::port::st14_enemy_state::HoverFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_guardian_state::GuardianFields)) { layoutValue(v, a, static_cast<od::port::manda_guardian_state::GuardianFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_boss_state::BeamHeadFields)) { layoutValue(v, a, static_cast<od::port::manda_boss_state::BeamHeadFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_guardian_state::BladeFields)) { layoutValue(v, a, static_cast<od::port::manda_guardian_state::BladeFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_boss_state::PuppetFields)) { layoutValue(v, a, static_cast<od::port::manda_boss_state::PuppetFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ruin_spawners_state::SpawnerFields)) { layoutValue(v, a, static_cast<od::port::ruin_spawners_state::SpawnerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_ruin_props_state::PlatformFields)) { layoutValue(v, a, static_cast<od::port::manda_ruin_props_state::PlatformFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_boss_state::DebrisFields)) { layoutValue(v, a, static_cast<od::port::manda_boss_state::DebrisFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st12_14_state::WalkerFields)) { layoutValue(v, a, static_cast<od::port::st12_14_state::WalkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::manda_guardian_state::ChestFields)) { layoutValue(v, a, static_cast<od::port::manda_guardian_state::ChestFields&>(s)); return true; }
    return false;
}
} // namespace od::test
