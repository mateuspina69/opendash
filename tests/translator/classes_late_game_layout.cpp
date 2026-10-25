// The native records of this area in the exe layout (test-only).
#include "translator/actor_layout.hpp"
#include "content/late_game/ch4_story_state.hpp"
#include "content/late_game/end_cast_state.hpp"
#include "content/late_game/late_cast_state.hpp"
#include "content/late_game/st40_fx_state.hpp"
#include "content/late_game/st43_roller_state.hpp"
#include "content/late_game/st45_boss_state.hpp"
#include "content/late_game/st45_spawner_state.hpp"
#include "content/late_game/st4b_fx_state.hpp"
#include "content/late_game/st4b_scripts_state.hpp"
#include "content/late_game/st4b_state.hpp"
#include "content/late_game/st4c_boss_state.hpp"
#include "content/late_game/st4c_story_state.hpp"
#include "content/late_game/st53_story_state.hpp"
#include "content/late_game/st54_cast_state.hpp"
#include "content/late_game/st54_puffs_state.hpp"
#include "content/late_game/st54_takeoff_state.hpp"
#include "content/late_game/st59_state.hpp"
#include "content/late_game/st5c_fx2.hpp"
#include "content/late_game/st5c_fx2_state.hpp"
#include "content/late_game/st5c_fx3.hpp"
#include "content/late_game/st5c_fx3_state.hpp"
#include "content/late_game/st5c_fx_state.hpp"
#include "content/late_game/st5c_state.hpp"

namespace od::test {
using namespace od::port;

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ElectricSparkFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0x18, s.angle);
    layoutValue(v, a + 0x1a, s.length);
    layoutValue(v, a + 0x1c, s.timer);
    layoutValue(v, a + 0x1e, s.depth);
    layoutValue(v, a + 0xc, s.radius);
    layoutValue(v, a + 0x10, s.point);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::WarningRingParams& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.rock);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ch4_story_state::SmokeColumnFields& s) {
    using namespace od::port::ch4_story_state;
    layoutValue(v, a + 0xc, s.parentSlot);
    layoutValue(v, a + 0xd, s.stop);
    layoutValue(v, a + 0xe, s.parentType);
    layoutValue(v, a + 0x3e, s.countdown);
    layoutValue(v, a + 0x40, s.growth);
    layoutValue(v, a + 0x48, s.puffSpin);
    for (size_t i = 0; i < s.puffs.size(); ++i) layoutValue(v, a + 0x64 + uint32_t(i) * (0xc), s.puffs[i]);
    layoutValue(v, a + 0xc4, s.nextPuff);
    layoutValue(v, a + 0xc5, s.tick);
    layoutValue(v, a + 0xc6, s.glowTexel);
    layoutValue(v, a + 0xc7, s.livePuffs);
    layoutValue(v, a + 0xc8, s.size);
    layoutValue(v, a + 0xca, s.glow);
    layoutValue(v, a + 0xcb, s.afterGlow);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ch4_story_state::SmokePuff& s) {
    using namespace od::port::ch4_story_state;
    layoutValue(v, a + 0, s.state);
    layoutValue(v, a + 1, s.rise);
    layoutValue(v, a + 2, s.size);
    layoutValue(v, a + 4, s.point);
    layoutValue(v, a + 0xa, s.spin);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::end_cast_state::BobbingPropFields& s) {
    using namespace od::port::end_cast_state;
    layoutValue(v, a + 0xc, s.kind);
    layoutValue(v, a + 0xd, s.stop);
    layoutValue(v, a + 0x14c, s.ticks);
    layoutValue(v, a + 0x150, s.baseY);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::end_cast_state::EndCastExtra& s) {
    using namespace od::port::end_cast_state;
    layoutValue(v, a, static_cast<od::port::CastFields&>(s));
    layoutValue(v, a + 0x15e, s.fadeTimer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::end_cast_state::GlowDiscFields& s) {
    using namespace od::port::end_cast_state;
    layoutValue(v, a + 0xc, s.slotByte);
    layoutValue(v, a + 0xf, s.followPlayer);
    layoutValue(v, a + 0x18, s.counter);
    layoutValue(v, a + 0x1a, s.radius);
    layoutValue(v, a + 0x1c, s.height);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0x1e + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::end_cast_state::GlowFanFields& s) {
    using namespace od::port::end_cast_state;
    layoutValue(v, a + 0xc, s.followSlot);
    for (size_t i = 0; i < s.spins.size(); ++i) layoutValue(v, a + 0xd + uint32_t(i) * (1), s.spins[i]);
    layoutValue(v, a + 0x16, s.radius);
    layoutValue(v, a + 0x18, s.counter);
    layoutValue(v, a + 0x1a, s.hold);
    layoutValue(v, a + 0x1c, s.anglesA);
    layoutValue(v, a + 0x22, s.anglesB);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::end_cast_state::PuffFields& s) {
    using namespace od::port::end_cast_state;
    for (size_t i = 0; i < s.colour.size(); ++i) layoutValue(v, a + 0xc + uint32_t(i) * (1), s.colour[i]);
    layoutValue(v, a + 0x18, s.counter);
    layoutValue(v, a + 0x1a, s.life);
    layoutValue(v, a + 0x22, s.size);
    layoutValue(v, a + 0x24, s.yaw);
    layoutValue(v, a + 0x26, s.yawStep);
    layoutValue(v, a + 0x1c, s.drift);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::end_cast_state::PulseGlowFields& s) {
    using namespace od::port::end_cast_state;
    layoutValue(v, a + 0x18, s.counter);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::end_cast_state::RingBandFields& s) {
    using namespace od::port::end_cast_state;
    layoutValue(v, a + 0x18, s.counter);
    layoutValue(v, a + 0x1a, s.wobbleX);
    layoutValue(v, a + 0x1c, s.spin);
    layoutValue(v, a + 0x1e, s.wobbleZ);
    layoutValue(v, a + 0x20, s.radius);
    layoutValue(v, a + 0x22, s.lift);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0x24 + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::end_cast_state::TrailFields& s) {
    using namespace od::port::end_cast_state;
    layoutValue(v, a + 0xc, s.counter);
    layoutValue(v, a + 0xc, s.counterLow);
    layoutValue(v, a + 0xf, s.riseCount);
    for (size_t i = 0; i < s.block.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.block[i]);
    for (size_t i = 0; i < s.heights.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (2), s.heights[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::late_cast_state::CirclerFields& s) {
    using namespace od::port::late_cast_state;
    layoutValue(v, a + 0xc, s.angle);
    layoutValue(v, a + 0xc, s.angleLow);
    layoutValue(v, a + 0xd, s.angleHigh);
    layoutValue(v, a + 0xe, s.timer);
    layoutValue(v, a + 0xf, s.row);
    layoutValue(v, a + 0x38, s.countdown);
    layoutValue(v, a + 0x158, s.animEntry);
    layoutValue(v, a + 0x14c, s.centreXWord);
    layoutValue(v, a + 0x150, s.centreZ);
    layoutValue(v, a + 0x154, s.radius);
    layoutValue(v, a + 0x15c, s.tracks);
    layoutValue(v, a + 0x14c, s.centreX);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::late_cast_state::FlyerFields& s) {
    using namespace od::port::late_cast_state;
    layoutValue(v, a + 0xc, s.dimmed);
    layoutValue(v, a + 0x14c, s.counter);
    layoutValue(v, a + 0x150, s.row);
    layoutValue(v, a + 0x154, s.stepping);
    layoutValue(v, a + 0x155, s.heldKey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::late_cast_state::HoldingTalkerFields& s) {
    using namespace od::port::late_cast_state;
    layoutValue(v, a, static_cast<od::port::late_cast_state::St54CastFields&>(s));
    layoutValue(v, a + 0x15e, s.hold);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::late_cast_state::IdleCastFields& s) {
    using namespace od::port::late_cast_state;
    layoutValue(v, a + 0x14c, s.word);
    layoutValue(v, a + 0x150, s.stepping);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::late_cast_state::MouthTalkerFields& s) {
    using namespace od::port::late_cast_state;
    layoutValue(v, a + 0xb, s.mode);
    layoutValue(v, a + 0xf, s.flaps);
    layoutValue(v, a + 0x14c, s.row);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::late_cast_state::PacerFields& s) {
    using namespace od::port::late_cast_state;
    layoutValue(v, a + 0xc, s.firstAction);
    layoutValue(v, a + 0xe, s.turnTimer);
    layoutValue(v, a + 0x14c, s.tracks);
    layoutValue(v, a + 0x15c, s.row);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::late_cast_state::SceneWalkerFields& s) {
    using namespace od::port::late_cast_state;
    layoutValue(v, a + 0x160, s.stepping);
    layoutValue(v, a + 0x14c, s.tracks);
    layoutValue(v, a + 0x15c, s.word);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::late_cast_state::St4bCastFields& s) {
    using namespace od::port::late_cast_state;
    layoutValue(v, a + 0x162, s.stepping);
    layoutValue(v, a + 0x14c, s.tracks);
    layoutValue(v, a + 0x15c, s.word);
    for (size_t i = 0; i < s.rows.size(); ++i) layoutValue(v, a + 0x160 + uint32_t(i) * (1), s.rows[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::late_cast_state::St54CastFields& s) {
    using namespace od::port::late_cast_state;
    layoutValue(v, a + 0x14c, s.tracks);
    for (size_t i = 0; i < s.rows.size(); ++i) layoutValue(v, a + 0x15c + uint32_t(i) * (1), s.rows[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st40_fx_state::RingFields& s) {
    using namespace od::port::st40_fx_state;
    layoutValue(v, a + 0xc, s.release);
    layoutValue(v, a + 0x18, s.ticks);
    layoutValue(v, a + 0x1c, s.radius);
    layoutValue(v, a + 0x20, s.pitch);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st43_roller_state::RollerFields& s) {
    using namespace od::port::st43_roller_state;
    layoutValue(v, a + 0xa, s.damageTaken);
    layoutValue(v, a + 0xc, s.timer);
    layoutValue(v, a + 0xc, s.riseWait);
    layoutValue(v, a + 0xe, s.wobble);
    layoutValue(v, a + 0xf, s.yawDrift);
    layoutValue(v, a + 0x3e, s.shieldCooldown);
    layoutValue(v, a + 0xc0, s.travelYaw);
    layoutValue(v, a + 0xc2, s.pauseOrTarget);
    layoutValue(v, a + 0xbc, s.spawner);
    layoutValue(v, a + 0xc4, s.hitBox);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st45_boss_state::BlobFields& s) {
    using namespace od::port::st45_boss_state;
    layoutValue(v, a + 0xe, s.frame);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.distance);
    layoutValue(v, a + 0xc8, s.life);
    layoutValue(v, a + 0xca, s.burstTicks);
    layoutValue(v, a + 0xcc, s.targetYaw);
    layoutValue(v, a + 0xce, s.targetPitch);
    layoutValue(v, a + 0xd0, s.speed);
    layoutValue(v, a + 0xd2, s.wobblePhase);
    layoutValue(v, a + 0xd4, s.glow);
    layoutValue(v, a + 0xd6, s.burstStep);
    layoutValue(v, a + 0xd7, s.wobbleSpeed);
    layoutValue(v, a + 0xd8, s.alpha);
    layoutValue(v, a + 0xd9, s.blobFlags);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st45_boss_state::BossFields& s) {
    using namespace od::port::st45_boss_state;
    layoutValue(v, a + 0xc, s.nextStep);
    layoutValue(v, a + 0xd, s.stepAnim);
    layoutValue(v, a + 0xe, s.stepKey);
    layoutValue(v, a + 0xf, s.stepDistance);
    layoutValue(v, a + 0x2e, s.headPitch);
    layoutValue(v, a + 0x48, s.soundTimer);
    layoutValue(v, a + 0x165, s.status);
    layoutValue(v, a + 0x166, s.startKey);
    layoutValue(v, a + 0x167, s.anchor);
    layoutValue(v, a + 0x168, s.counter);
    layoutValue(v, a + 0x169, s.mouthTick);
    layoutValue(v, a + 0x16a, s.blobs);
    layoutValue(v, a + 0x14c, s.armBox);
    layoutValue(v, a + 0x154, s.bodyBox);
    layoutValue(v, a + 0x15c, s.armHolder);
    layoutValue(v, a + 0x160, s.timer);
    layoutValue(v, a + 0x162, s.headTurn);
    layoutValue(v, a + 0x16b, s.hurtCooldown);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st45_boss_state::MouthShotFields& s) {
    using namespace od::port::st45_boss_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xcc, s.word);
    layoutValue(v, a + 0xce, s.byte);
    layoutValue(v, a + 0xcf, s.trailU);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st45_boss_state::PuffFields& s) {
    using namespace od::port::st45_boss_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc8, s.life);
    layoutValue(v, a + 0xca, s.alpha);
    layoutValue(v, a + 0xcc, s.size);
    layoutValue(v, a + 0xce, s.frame);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xc4 + uint32_t(i) * (1), s.clearedPad0[i]);
    for (size_t i = 0; i < s.clearedPad1.size(); ++i) layoutValue(v, a + 0xd0 + uint32_t(i) * (1), s.clearedPad1[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st45_boss_state::SplashDrop& s) {
    using namespace od::port::st45_boss_state;
    layoutValue(v, a + 0, s.point);
    layoutValue(v, a + 6, s.yaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st45_boss_state::SplashFields& s) {
    using namespace od::port::st45_boss_state;
    for (size_t i = 0; i < s.drops.size(); ++i) layoutValue(v, a + 0x74 + uint32_t(i) * (8), s.drops[i]);
    for (size_t i = 0; i < s.fall.size(); ++i) layoutValue(v, a + 0xb4 + uint32_t(i) * (2), s.fall[i]);
    for (size_t i = 0; i < s.forward.size(); ++i) layoutValue(v, a + 0xc4 + uint32_t(i) * (2), s.forward[i]);
    layoutValue(v, a + 0xd4, s.sizeFloor);
    layoutValue(v, a + 0xd6, s.size);
    layoutValue(v, a + 0xd7, s.texelU);
    layoutValue(v, a + 0xd8, s.ticks);
    layoutValue(v, a + 0xda, s.life);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st45_boss_state::WaveArm& s) {
    using namespace od::port::st45_boss_state;
    layoutValue(v, a + 0, s.point);
    layoutValue(v, a + 6, s.yaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st45_boss_state::WaveFields& s) {
    using namespace od::port::st45_boss_state;
    layoutValue(v, a + 0xb2, s.rayHeight);
    layoutValue(v, a + 0xc0, s.shrink);
    layoutValue(v, a + 0xc2, s.innerHeight);
    layoutValue(v, a + 0x94, s.hitBox);
    for (size_t i = 0; i < s.arms.size(); ++i) layoutValue(v, a + 0x9c + uint32_t(i) * (8), s.arms[i]);
    layoutValue(v, a + 0xac, s.rayPoint);
    layoutValue(v, a + 0xb4, s.origin);
    layoutValue(v, a + 0xba, s.stopped);
    layoutValue(v, a + 0xbc, s.reach);
    layoutValue(v, a + 0xbe, s.growth);
    layoutValue(v, a + 0xc4, s.outerHeight);
    layoutValue(v, a + 0xc6, s.spread);
    layoutValue(v, a + 0xc8, s.word);
    layoutValue(v, a + 0xca, s.startYaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st45_spawner_state::SpawnerFields& s) {
    using namespace od::port::st45_spawner_state;
    layoutValue(v, a + 0xc, s.countdown);
    for (size_t i = 0; i < s.scratch.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.scratch[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4b_fx_state::EyeTrailFields& s) {
    using namespace od::port::st4b_fx_state;
    layoutValue(v, a + 0x28, s.length);
    layoutValue(v, a + 0x2a, s.head);
    for (size_t i = 0; i < s.xs.size(); ++i) layoutValue(v, a + 0x2c + uint32_t(i) * (2), s.xs[i]);
    for (size_t i = 0; i < s.ys.size(); ++i) layoutValue(v, a + 0x60 + uint32_t(i) * (2), s.ys[i]);
    for (size_t i = 0; i < s.zs.size(); ++i) layoutValue(v, a + 0x94 + uint32_t(i) * (2), s.zs[i]);
    layoutValue(v, a + 0xc, s.eye);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4b_fx_state::PieceFields& s) {
    using namespace od::port::st4b_fx_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.travelYaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4b_fx_state::VolleyFields& s) {
    using namespace od::port::st4b_fx_state;
    layoutValue(v, a + 0xc, s.spawnDamage);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.timer);
    layoutValue(v, a + 0xc8, s.damage);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4b_scripts_state::StillFigureFields& s) {
    using namespace od::port::st4b_scripts_state;
    layoutValue(v, a + 0x14c, s.classWord);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4b_state::BossFields& s) {
    using namespace od::port::st4b_state;
    layoutValue(v, a + 0xc, s.tintR);
    layoutValue(v, a + 0xd, s.tintG);
    layoutValue(v, a + 0xe, s.tintB);
    layoutValue(v, a + 0x14c, s.workBlock);
    layoutValue(v, a + 0x150, s.statsRow);
    layoutValue(v, a + 0x154, s.flags);
    layoutValue(v, a + 0x158, s.timer);
    layoutValue(v, a + 0x15a, s.counter);
    layoutValue(v, a + 0x15c, s.targetYaw);
    layoutValue(v, a + 0x15e, s.targetPitch);
    layoutValue(v, a + 0x160, s.aimPitch);
    layoutValue(v, a + 0x162, s.targetDistance);
    layoutValue(v, a + 0x164, s.pitchGoal);
    layoutValue(v, a + 0x166, s.eyeTimer);
    layoutValue(v, a + 0x168, s.neckBend);
    layoutValue(v, a + 0x16a, s.headYaw);
    layoutValue(v, a + 0x154, s.flagsLow);
    if (s.workBlock) layoutValue(v, s.workBlock, s.workBytes);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4b_state::BossFightBlock& s) {
    using namespace od::port::st4b_state;
    layoutValue(v, a + 0, s.hitBox);
    layoutValue(v, a + 8, s.eyeA);
    layoutValue(v, a + 0x10, s.eyeB);
    for (size_t i = 0; i < s.drops.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (8), s.drops[i]);
    layoutValue(v, a + 0x40, s.pendingDamage);
    layoutValue(v, a + 0x42, s.damageTaken);
    layoutValue(v, a + 0x44, s.timer);
    layoutValue(v, a + 0x46, s.bend);
    layoutValue(v, a + 0x48, s.tauntsUsed);
    layoutValue(v, a + 0x48, s.tauntsUsedLow);
    layoutValue(v, a + 0x4a, s.lastCry);
    for (size_t i = 0; i < s.unread.size(); ++i) layoutValue(v, a + 0x4c + uint32_t(i), s.unread[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4b_state::FaceRowCastFields& s) {
    using namespace od::port::st4b_state;
    layoutValue(v, a + 0x162, s.stepping);
    layoutValue(v, a + 0x14c, s.tracks);
    layoutValue(v, a + 0x15c, s.word);
    for (size_t i = 0; i < s.rows.size(); ++i) layoutValue(v, a + 0x160 + uint32_t(i) * (1), s.rows[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4b_state::PieceFields& s) {
    using namespace od::port::st4b_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc0, s.circleTimer);
    layoutValue(v, a + 0x4c, s.rockAmplitude);
    layoutValue(v, a + 0x40, s.rockSpeed);
    layoutValue(v, a + 0xc4, s.travelYaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4b_state::WalkInCastFields& s) {
    using namespace od::port::st4b_state;
    layoutValue(v, a + 0x160, s.stepping);
    layoutValue(v, a + 0x14c, s.tracks);
    layoutValue(v, a + 0x15c, s.word);
    layoutValue(v, a + 0x15d, s.animKey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_boss_state::BeamShotFields& s) {
    using namespace od::port::st4c_boss_state;
    layoutValue(v, a + 0xc, s.spawnBoss);
    layoutValue(v, a + 0xbc, s.level);
    layoutValue(v, a + 0xbe, s.word);
    layoutValue(v, a + 0xc0, s.hitBox);
    layoutValue(v, a + 0xc8, s.boss);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_boss_state::BoltFields& s) {
    using namespace od::port::st4c_boss_state;
    layoutValue(v, a, static_cast<od::port::st4c_boss_state::ShotCommon&>(s));
    layoutValue(v, a + 0xc0, s.timer);
    layoutValue(v, a + 0xc4, s.rise);
    layoutValue(v, a + 0xc8, s.travel);
    layoutValue(v, a + 0xd0, s.hitBox);
    layoutValue(v, a + 0xd8, s.boss);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_boss_state::BossFields& s) {
    using namespace od::port::st4c_boss_state;
    layoutValue(v, a + 0x15f, s.thresholdsPassed);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.timer);
    layoutValue(v, a + 0x156, s.subTimer);
    layoutValue(v, a + 0x158, s.flagsWord_);
    layoutValue(v, a + 0x15a, s.savedGround);
    layoutValue(v, a + 0x15c, s.verticalSpeed);
    layoutValue(v, a + 0x15e, s.comboLeft);
    layoutValue(v, a + 0x160, s.queuedDamage);
    layoutValue(v, a + 0x158, s.flags);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_boss_state::ChargeFields& s) {
    using namespace od::port::st4c_boss_state;
    layoutValue(v, a + 0xc, s.spawnBoss);
    layoutValue(v, a + 0x28, s.timer);
    layoutValue(v, a + 0x2a, s.word);
    for (size_t i = 0; i < s.angles.size(); ++i) layoutValue(v, a + 0x2c + uint32_t(i) * (2), s.angles[i]);
    for (size_t i = 0; i < s.lengths.size(); ++i) layoutValue(v, a + 0x6c + uint32_t(i) * (1), s.lengths[i]);
    for (size_t i = 0; i < s.spins.size(); ++i) layoutValue(v, a + 0x8c + uint32_t(i) * (1), s.spins[i]);
    layoutValue(v, a + 0xac, s.boss);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_boss_state::FogFields& s) {
    using namespace od::port::st4c_boss_state;
    layoutValue(v, a + 0xc, s.spawnBoss);
    layoutValue(v, a + 0x1c, s.level);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_boss_state::HomingShotFields& s) {
    using namespace od::port::st4c_boss_state;
    layoutValue(v, a, static_cast<od::port::st4c_boss_state::ShotCommon&>(s));
    layoutValue(v, a + 0xc0, s.travel);
    layoutValue(v, a + 0xc8, s.hitBox);
    layoutValue(v, a + 0xd0, s.boss);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_boss_state::OrbitShotFields& s) {
    using namespace od::port::st4c_boss_state;
    layoutValue(v, a, static_cast<od::port::st4c_boss_state::ShotCommon&>(s));
    layoutValue(v, a + 0xc0, s.timer);
    layoutValue(v, a + 0xc4, s.travel);
    layoutValue(v, a + 0xcc, s.hitBox);
    layoutValue(v, a + 0xd4, s.boss);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_boss_state::ShotCommon& s) {
    using namespace od::port::st4c_boss_state;
    layoutValue(v, a + 0xc, s.spawnBoss);
    layoutValue(v, a + 0xbc, s.life);
    layoutValue(v, a + 0xbe, s.attack);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_boss_state::Spark& s) {
    using namespace od::port::st4c_boss_state;
    layoutValue(v, a + 0, s.x);
    layoutValue(v, a + 2, s.y);
    layoutValue(v, a + 4, s.z);
    layoutValue(v, a + 6, s.angle);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_boss_state::Sparkle& s) {
    using namespace od::port::st4c_boss_state;
    layoutValue(v, a + 0, s.phase);
    layoutValue(v, a + 2, s.angle);
    layoutValue(v, a + 4, s.size);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_boss_state::SparklesFields& s) {
    using namespace od::port::st4c_boss_state;
    layoutValue(v, a + 0xc, s.spawnBoss);
    layoutValue(v, a + 0x28, s.level);
    layoutValue(v, a + 0x2a, s.count);
    layoutValue(v, a + 0x2c, s.next);
    for (size_t i = 0; i < s.sparkles.size(); ++i) layoutValue(v, a + 0x30 + uint32_t(i) * (8), s.sparkles[i]);
    layoutValue(v, a + 0x70, s.boss);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_boss_state::SparksFields& s) {
    using namespace od::port::st4c_boss_state;
    layoutValue(v, a + 0x28, s.dead);
    layoutValue(v, a + 0x2a, s.live);
    for (size_t i = 0; i < s.sparks.size(); ++i) layoutValue(v, a + 0x2c + uint32_t(i) * (8), s.sparks[i]);
    for (size_t i = 0; i < s.sizes.size(); ++i) layoutValue(v, a + 0xac + uint32_t(i) * (1), s.sizes[i]);
    for (size_t i = 0; i < s.lives.size(); ++i) layoutValue(v, a + 0xbc + uint32_t(i) * (1), s.lives[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_story_state::BoneTrailFields& s) {
    using namespace od::port::st4c_story_state;
    layoutValue(v, a + 0x28, s.length);
    layoutValue(v, a + 0x2a, s.head);
    layoutValue(v, a + 0x2a, s.headLow);
    layoutValue(v, a + 0x2c, s.rise);
    layoutValue(v, a + 0x2e, s.shapeAngle);
    layoutValue(v, a + 0x38, s.width);
    layoutValue(v, a + 0x30, s.drawMode);
    layoutValue(v, a + 0x34, s.colourStep);
    for (size_t i = 0; i < s.xs.size(); ++i) layoutValue(v, a + 0x3c + uint32_t(i) * (2), s.xs[i]);
    for (size_t i = 0; i < s.ys.size(); ++i) layoutValue(v, a + 0x5c + uint32_t(i) * (2), s.ys[i]);
    for (size_t i = 0; i < s.zs.size(); ++i) layoutValue(v, a + 0x7c + uint32_t(i) * (2), s.zs[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_story_state::DefeatedBossFields& s) {
    using namespace od::port::st4c_story_state;
    layoutValue(v, a + 0x15f, s.animKey);
    layoutValue(v, a + 0x167, s.playing);
    layoutValue(v, a + 0x14c, s.faces);
    layoutValue(v, a + 0x15c, s.fadeCounter);
    layoutValue(v, a + 0x15e, s.castFlags);
    layoutValue(v, a + 0x160, s.pillars);
    layoutValue(v, a + 0x162, s.burstCue);
    layoutValue(v, a + 0x164, s.trailCue);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_story_state::Mote& s) {
    using namespace od::port::st4c_story_state;
    layoutValue(v, a + 0x0, s.x);
    layoutValue(v, a + 0x2, s.y);
    layoutValue(v, a + 0x4, s.z);
    layoutValue(v, a + 0x6, s.speed);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_story_state::MotesFields& s) {
    using namespace od::port::st4c_story_state;
    layoutValue(v, a + 0xb8, s.count);
    for (size_t i = 0; i < s.motes.size(); ++i) layoutValue(v, a + 0x28 + uint32_t(i) * (8), s.motes[i]);
    for (size_t i = 0; i < s.sizes.size(); ++i) layoutValue(v, a + 0xa8 + uint32_t(i) * (1), s.sizes[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_story_state::RayBurstFields& s) {
    using namespace od::port::st4c_story_state;
    for (size_t i = 0; i < s.xAngles.size(); ++i) layoutValue(v, a + 0x68 + uint32_t(i) * (1), s.xAngles[i]);
    for (size_t i = 0; i < s.yAngles.size(); ++i) layoutValue(v, a + 0x88 + uint32_t(i) * (1), s.yAngles[i]);
    for (size_t i = 0; i < s.speeds.size(); ++i) layoutValue(v, a + 0xa8 + uint32_t(i) * (1), s.speeds[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st4c_story_state::SpriteFields& s) {
    using namespace od::port::st4c_story_state;
    layoutValue(v, a + 0x18, s.u);
    layoutValue(v, a + 0x1a, s.rotation);
    layoutValue(v, a + 0x1a, s.stillFrames);
    layoutValue(v, a + 0x1b, s.stillTicks);
    layoutValue(v, a + 0x1c, s.frames);
    layoutValue(v, a + 0x1d, s.frameTicks);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0x1e + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st53_story_state::St53CastFields& s) {
    using namespace od::port::st53_story_state;
    layoutValue(v, a + 0x160, s.faderFlags);
    layoutValue(v, a + 0x14c, s.faces);
    layoutValue(v, a + 0x15c, s.fadeCounter);
    layoutValue(v, a + 0x15e, s.bursts);
    layoutValue(v, a + 0x161, s.faderKey);
    layoutValue(v, a + 0x15c, s.castFlags);
    layoutValue(v, a + 0x15c, s.fadeCounterLow);
    layoutValue(v, a + 0x15d, s.animKey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st54_cast_state::MoverFields& s) {
    using namespace od::port::st54_cast_state;
    layoutValue(v, a + 0xc, s.kind);
    layoutValue(v, a + 0x162, s.ringIndex);
    layoutValue(v, a + 0x163, s.flags);
    layoutValue(v, a + 0x14c, s.emitterA);
    layoutValue(v, a + 0x150, s.emitterB);
    layoutValue(v, a + 0x154, s.frozen);
    layoutValue(v, a + 0x15c, s.spawnTimer);
    layoutValue(v, a + 0x15e, s.ringTimer);
    layoutValue(v, a + 0x160, s.ticks);
    layoutValue(v, a + 0x15c, s.spawnTimerLow);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st54_puffs_state::Puff& s) {
    using namespace od::port::st54_puffs_state;
    layoutValue(v, a + 0x0, s.x);
    layoutValue(v, a + 0x2, s.y);
    layoutValue(v, a + 0x4, s.z);
    layoutValue(v, a + 0x6, s.life);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st54_puffs_state::PuffEmitterFields& s) {
    using namespace od::port::st54_puffs_state;
    layoutValue(v, a + 0xb2, s.spawnWord);
    layoutValue(v, a + 0xb4, s.fadeFlags);
    layoutValue(v, a + 0xb5, s.fadedPuffs);
    layoutValue(v, a + 0xbc, s.owner);
    layoutValue(v, a + 0xc0, s.keptWord);
    layoutValue(v, a + 0xc2, s.spawnDelay);
    for (size_t i = 0; i < s.puffs.size(); ++i) layoutValue(v, a + 0x10 + uint32_t(i) * (8), s.puffs[i]);
    for (size_t i = 0; i < s.brightness.size(); ++i) layoutValue(v, a + 0xa0 + uint32_t(i) * (1), s.brightness[i]);
    layoutValue(v, a + 0xc4, s.liveMask);
    layoutValue(v, a + 0xc6, s.rowByte);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st54_takeoff_state::TakeoffFields& s) {
    using namespace od::port::st54_takeoff_state;
    layoutValue(v, a + 0xc, s.startKey);
    layoutValue(v, a + 0xd, s.fastClimb);
    layoutValue(v, a + 0x14c, s.arcAngle);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st59_state::RaceActorFields& s) {
    using namespace od::port::st59_state;
    layoutValue(v, a + 0xc, s.go);
    layoutValue(v, a + 0x10, s.x);
    layoutValue(v, a + 0x12, s.y);
    layoutValue(v, a + 0x16, s.slide);
    layoutValue(v, a + 0x18, s.timer);
    layoutValue(v, a + 0x1a, s.gaugeValue);
    layoutValue(v, a + 0x1c, s.clut);
    layoutValue(v, a + 0x18, s.timerLow);
    layoutValue(v, a + 0x1a, s.u);
    layoutValue(v, a + 0x1a, s.rank);
    layoutValue(v, a + 0x1b, s.v);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0x1e + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st5c_fx2_state::BallFxFields& s) {
    using namespace od::port::st5c_fx2_state;
    layoutValue(v, a + 0x28, s.timer);
    layoutValue(v, a + 0x2a, s.word);
    for (size_t i = 0; i < s.angles.size(); ++i) layoutValue(v, a + 0x2c + uint32_t(i) * (2), s.angles[i]);
    for (size_t i = 0; i < s.timers.size(); ++i) layoutValue(v, a + 0x5c + uint32_t(i) * (1), s.timers[i]);
    for (size_t i = 0; i < s.lengths.size(); ++i) layoutValue(v, a + 0x74 + uint32_t(i) * (1), s.lengths[i]);
    for (size_t i = 0; i < s.spins.size(); ++i) layoutValue(v, a + 0x8c + uint32_t(i) * (1), s.spins[i]);
    layoutValue(v, a + 0xa4, s.ball);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st5c_fx2_state::BeamFields& s) {
    using namespace od::port::st5c_fx2_state;
    layoutValue(v, a + 0xc, s.spawnBoss);
    layoutValue(v, a + 0xf, s.loopHandle);
    layoutValue(v, a + 0x64, s.timer);
    layoutValue(v, a + 0x68, s.hitBox);
    layoutValue(v, a + 0x70, s.boss);
    for (size_t i = 0; i < s.segments.size(); ++i) layoutValue(v, a + 0x74 + uint32_t(i) * (4), s.segments[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st5c_fx2_state::BlastFields& s) {
    using namespace od::port::st5c_fx2_state;
    layoutValue(v, a + 0xc, s.spawnBoss);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xcc, s.boss);
    layoutValue(v, a + 0xd0, s.shockRings);
    layoutValue(v, a + 0xd4, s.radius);
    layoutValue(v, a + 0xd6, s.timer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st5c_fx2_state::LandingDustFields& s) {
    using namespace od::port::st5c_fx2_state;
    for (size_t i = 0; i < s.randoms.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (4), s.randoms[i]);
    for (size_t i = 0; i < s.randomHalves.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (2), s.randomHalves[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st5c_fx3_state::ImpactFields& s) {
    using namespace od::port::st5c_fx3_state;
    layoutValue(v, a + 0xc, s.paramsWord);
    layoutValue(v, a + 0x28, s.timer);
    layoutValue(v, a + 0x2a, s.unusedWord);
    for (size_t i = 0; i < s.angles.size(); ++i) layoutValue(v, a + 0x2c + uint32_t(i) * (2), s.angles[i]);
    for (size_t i = 0; i < s.lengths.size(); ++i) layoutValue(v, a + 0x6c + uint32_t(i) * (1), s.lengths[i]);
    for (size_t i = 0; i < s.widths.size(); ++i) layoutValue(v, a + 0x8c + uint32_t(i) * (1), s.widths[i]);
    layoutValue(v, a + 0xac, s.paramsCopy);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st5c_fx3_state::ShockRingFields& s) {
    using namespace od::port::st5c_fx3_state;
    layoutValue(v, a + 0xc, s.count);
    layoutValue(v, a + 0xd, s.period);
    layoutValue(v, a + 0xe, s.speed);
    layoutValue(v, a + 0xf, s.width);
    layoutValue(v, a + 0xc, s.paramsWord);
    layoutValue(v, a + 0x18, s.radius);
    layoutValue(v, a + 0x1a, s.toggle);
    layoutValue(v, a + 0x1c, s.spawner);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st5c_fx_state::BallFields& s) {
    using namespace od::port::st5c_fx_state;
    layoutValue(v, a + 0xc, s.spawnBoss);
    layoutValue(v, a + 0xbc, s.timer);
    layoutValue(v, a + 0xbe, s.word);
    layoutValue(v, a + 0xc0, s.hitBox);
    layoutValue(v, a + 0xc8, s.boss);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st5c_fx_state::BladeFields& s) {
    using namespace od::port::st5c_fx_state;
    layoutValue(v, a + 0xc, s.spawnBoss);
    layoutValue(v, a + 0x74, s.timer);
    layoutValue(v, a + 0x76, s.segments);
    layoutValue(v, a + 0x78, s.bone);
    layoutValue(v, a + 0x7a, s.height);
    layoutValue(v, a + 0x7c, s.radius);
    layoutValue(v, a + 0x80, s.hitBox);
    layoutValue(v, a + 0x88, s.boss);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st5c_fx_state::ChargeFields& s) {
    using namespace od::port::st5c_fx_state;
    layoutValue(v, a + 0xc, s.spawnBoss);
    layoutValue(v, a + 0x28, s.timer);
    layoutValue(v, a + 0x2a, s.word);
    for (size_t i = 0; i < s.angles.size(); ++i) layoutValue(v, a + 0x2c + uint32_t(i) * (2), s.angles[i]);
    for (size_t i = 0; i < s.lengths.size(); ++i) layoutValue(v, a + 0x6c + uint32_t(i) * (1), s.lengths[i]);
    for (size_t i = 0; i < s.spins.size(); ++i) layoutValue(v, a + 0x8c + uint32_t(i) * (1), s.spins[i]);
    layoutValue(v, a + 0xac, s.boss);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st5c_fx_state::RockFields& s) {
    using namespace od::port::st5c_fx_state;
    layoutValue(v, a + 0xc, s.spawnBoss);
    layoutValue(v, a + 0x14c, s.tintTicks);
    layoutValue(v, a + 0x14e, s.word);
    layoutValue(v, a + 0x150, s.soundPlayed);
    layoutValue(v, a + 0x154, s.hitBox);
    layoutValue(v, a + 0x15c, s.boss);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st5c_fx_state::StompShotFields& s) {
    using namespace od::port::st5c_fx_state;
    layoutValue(v, a + 0xc, s.spawnBoss);
    layoutValue(v, a + 0xbc, s.timer);
    layoutValue(v, a + 0xbe, s.word);
    layoutValue(v, a + 0xc0, s.travel);
    layoutValue(v, a + 0xc8, s.hitBox);
    layoutValue(v, a + 0xd0, s.boss);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st5c_state::BossFields& s) {
    using namespace od::port::st5c_state;
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.timerA);
    layoutValue(v, a + 0x156, s.timerB);
    layoutValue(v, a + 0x158, s.cooldown);
    layoutValue(v, a + 0x15a, s.word);
    layoutValue(v, a + 0x15c, s.flags);
    layoutValue(v, a + 0x15e, s.queuedDamage);
    for (size_t i = 0; i < s.afterImages.size(); ++i) layoutValue(v, a + 0x160 + uint32_t(i) * (4), s.afterImages[i]);
    layoutValue(v, a + 0x15c, s.flagsLow);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st5c_state::BurstFields& s) {
    using namespace od::port::st5c_state;
    layoutValue(v, a + 0xb8, s.timerLow);
    layoutValue(v, a + 0xb8, s.countB);
    layoutValue(v, a + 0xba, s.count);
    for (size_t i = 0; i < s.sparks.size(); ++i) layoutValue(v, a + 0x28 + uint32_t(i) * (8), s.sparks[i]);
    for (size_t i = 0; i < s.sizes.size(); ++i) layoutValue(v, a + 0xa8 + uint32_t(i) * (1), s.sizes[i]);
    layoutValue(v, a + 0xb8, s.timer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st5c_state::BurstSpark& s) {
    using namespace od::port::st5c_state;
    layoutValue(v, a + 0x0, s.x);
    layoutValue(v, a + 0x2, s.y);
    layoutValue(v, a + 0x4, s.z);
    layoutValue(v, a + 0x6, s.speed);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st5c_state::DefeatFields& s) {
    using namespace od::port::st5c_state;
    layoutValue(v, a, static_cast<od::port::CastFields&>(s));
    layoutValue(v, a + 0x160, s.groundY);
    layoutValue(v, a + 0x164, s.wordA);
    layoutValue(v, a + 0x166, s.wordB);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st5c_state::IntroFields& s) {
    using namespace od::port::st5c_state;
    layoutValue(v, a, static_cast<od::port::CastFields&>(s));
    layoutValue(v, a + 0x15e, s.fadeCounter);
}

bool classes_late_game(LayoutVisitor& v, uint32_t a, ActorClassState& s) {
    if (typeid(s) == typeid(od::port::end_cast_state::PulseGlowFields)) { layoutValue(v, a, static_cast<od::port::end_cast_state::PulseGlowFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4c_story_state::MotesFields)) { layoutValue(v, a, static_cast<od::port::st4c_story_state::MotesFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4c_story_state::RayBurstFields)) { layoutValue(v, a, static_cast<od::port::st4c_story_state::RayBurstFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4c_story_state::SpriteFields)) { layoutValue(v, a, static_cast<od::port::st4c_story_state::SpriteFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st5c_fx2_state::LandingDustFields)) { layoutValue(v, a, static_cast<od::port::st5c_fx2_state::LandingDustFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4c_boss_state::OrbitShotFields)) { layoutValue(v, a, static_cast<od::port::st4c_boss_state::OrbitShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st5c_fx3_state::ShockRingFields)) { layoutValue(v, a, static_cast<od::port::st5c_fx3_state::ShockRingFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::end_cast_state::GlowDiscFields)) { layoutValue(v, a, static_cast<od::port::end_cast_state::GlowDiscFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::late_cast_state::HoldingTalkerFields)) { layoutValue(v, a, static_cast<od::port::late_cast_state::HoldingTalkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::end_cast_state::RingBandFields)) { layoutValue(v, a, static_cast<od::port::end_cast_state::RingBandFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st5c_state::BurstFields)) { layoutValue(v, a, static_cast<od::port::st5c_state::BurstFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st5c_fx2_state::BallFxFields)) { layoutValue(v, a, static_cast<od::port::st5c_fx2_state::BallFxFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4c_boss_state::BoltFields)) { layoutValue(v, a, static_cast<od::port::st4c_boss_state::BoltFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4c_story_state::BoneTrailFields)) { layoutValue(v, a, static_cast<od::port::st4c_story_state::BoneTrailFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4c_boss_state::HomingShotFields)) { layoutValue(v, a, static_cast<od::port::st4c_boss_state::HomingShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st5c_fx_state::BladeFields)) { layoutValue(v, a, static_cast<od::port::st5c_fx_state::BladeFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4b_fx_state::PieceFields)) { layoutValue(v, a, static_cast<od::port::st4b_fx_state::PieceFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4c_boss_state::ChargeFields)) { layoutValue(v, a, static_cast<od::port::st4c_boss_state::ChargeFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st59_state::RaceActorFields)) { layoutValue(v, a, static_cast<od::port::st59_state::RaceActorFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::late_cast_state::St54CastFields)) { layoutValue(v, a, static_cast<od::port::late_cast_state::St54CastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::end_cast_state::BobbingPropFields)) { layoutValue(v, a, static_cast<od::port::end_cast_state::BobbingPropFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4b_state::BossFields)) { layoutValue(v, a, static_cast<od::port::st4b_state::BossFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4b_state::PieceFields)) { layoutValue(v, a, static_cast<od::port::st4b_state::PieceFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st45_boss_state::BlobFields)) { layoutValue(v, a, static_cast<od::port::st45_boss_state::BlobFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::end_cast_state::EndCastExtra)) { layoutValue(v, a, static_cast<od::port::end_cast_state::EndCastExtra&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st45_boss_state::BossFields)) { layoutValue(v, a, static_cast<od::port::st45_boss_state::BossFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4c_boss_state::SparksFields)) { layoutValue(v, a, static_cast<od::port::st4c_boss_state::SparksFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4c_boss_state::SparklesFields)) { layoutValue(v, a, static_cast<od::port::st4c_boss_state::SparklesFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4c_boss_state::ShotCommon)) { layoutValue(v, a, static_cast<od::port::st4c_boss_state::ShotCommon&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4c_boss_state::FogFields)) { layoutValue(v, a, static_cast<od::port::st4c_boss_state::FogFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4c_boss_state::BossFields)) { layoutValue(v, a, static_cast<od::port::st4c_boss_state::BossFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::end_cast_state::GlowFanFields)) { layoutValue(v, a, static_cast<od::port::end_cast_state::GlowFanFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4b_state::FaceRowCastFields)) { layoutValue(v, a, static_cast<od::port::st4b_state::FaceRowCastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st45_spawner_state::SpawnerFields)) { layoutValue(v, a, static_cast<od::port::st45_spawner_state::SpawnerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st40_fx_state::RingFields)) { layoutValue(v, a, static_cast<od::port::st40_fx_state::RingFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4b_fx_state::VolleyFields)) { layoutValue(v, a, static_cast<od::port::st4b_fx_state::VolleyFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4c_boss_state::BeamShotFields)) { layoutValue(v, a, static_cast<od::port::st4c_boss_state::BeamShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st53_story_state::St53CastFields)) { layoutValue(v, a, static_cast<od::port::st53_story_state::St53CastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::late_cast_state::SceneWalkerFields)) { layoutValue(v, a, static_cast<od::port::late_cast_state::SceneWalkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st54_puffs_state::PuffEmitterFields)) { layoutValue(v, a, static_cast<od::port::st54_puffs_state::PuffEmitterFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st5c_state::IntroFields)) { layoutValue(v, a, static_cast<od::port::st5c_state::IntroFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st5c_fx_state::ChargeFields)) { layoutValue(v, a, static_cast<od::port::st5c_fx_state::ChargeFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st5c_state::DefeatFields)) { layoutValue(v, a, static_cast<od::port::st5c_state::DefeatFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st45_boss_state::WaveFields)) { layoutValue(v, a, static_cast<od::port::st45_boss_state::WaveFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st5c_fx_state::BallFields)) { layoutValue(v, a, static_cast<od::port::st5c_fx_state::BallFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4c_story_state::DefeatedBossFields)) { layoutValue(v, a, static_cast<od::port::st4c_story_state::DefeatedBossFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st5c_fx2_state::BeamFields)) { layoutValue(v, a, static_cast<od::port::st5c_fx2_state::BeamFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4b_scripts_state::StillFigureFields)) { layoutValue(v, a, static_cast<od::port::st4b_scripts_state::StillFigureFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st45_boss_state::PuffFields)) { layoutValue(v, a, static_cast<od::port::st45_boss_state::PuffFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ElectricSparkFields)) { layoutValue(v, a, static_cast<od::port::ElectricSparkFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::end_cast_state::TrailFields)) { layoutValue(v, a, static_cast<od::port::end_cast_state::TrailFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st45_boss_state::MouthShotFields)) { layoutValue(v, a, static_cast<od::port::st45_boss_state::MouthShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st5c_fx2_state::BlastFields)) { layoutValue(v, a, static_cast<od::port::st5c_fx2_state::BlastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::late_cast_state::PacerFields)) { layoutValue(v, a, static_cast<od::port::late_cast_state::PacerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st54_cast_state::MoverFields)) { layoutValue(v, a, static_cast<od::port::st54_cast_state::MoverFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st45_boss_state::SplashFields)) { layoutValue(v, a, static_cast<od::port::st45_boss_state::SplashFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4b_state::WalkInCastFields)) { layoutValue(v, a, static_cast<od::port::st4b_state::WalkInCastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st5c_fx3_state::ImpactFields)) { layoutValue(v, a, static_cast<od::port::st5c_fx3_state::ImpactFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::late_cast_state::IdleCastFields)) { layoutValue(v, a, static_cast<od::port::late_cast_state::IdleCastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st43_roller_state::RollerFields)) { layoutValue(v, a, static_cast<od::port::st43_roller_state::RollerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st54_takeoff_state::TakeoffFields)) { layoutValue(v, a, static_cast<od::port::st54_takeoff_state::TakeoffFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st5c_fx_state::StompShotFields)) { layoutValue(v, a, static_cast<od::port::st5c_fx_state::StompShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::late_cast_state::MouthTalkerFields)) { layoutValue(v, a, static_cast<od::port::late_cast_state::MouthTalkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st5c_state::BossFields)) { layoutValue(v, a, static_cast<od::port::st5c_state::BossFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st5c_fx_state::RockFields)) { layoutValue(v, a, static_cast<od::port::st5c_fx_state::RockFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::end_cast_state::PuffFields)) { layoutValue(v, a, static_cast<od::port::end_cast_state::PuffFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::late_cast_state::FlyerFields)) { layoutValue(v, a, static_cast<od::port::late_cast_state::FlyerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st4b_fx_state::EyeTrailFields)) { layoutValue(v, a, static_cast<od::port::st4b_fx_state::EyeTrailFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ch4_story_state::SmokeColumnFields)) { layoutValue(v, a, static_cast<od::port::ch4_story_state::SmokeColumnFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::late_cast_state::CirclerFields)) { layoutValue(v, a, static_cast<od::port::late_cast_state::CirclerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::late_cast_state::St4bCastFields)) { layoutValue(v, a, static_cast<od::port::late_cast_state::St4bCastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::WarningRingParams)) { layoutValue(v, a, static_cast<od::port::WarningRingParams&>(s)); return true; }
    return false;
}
} // namespace od::test
