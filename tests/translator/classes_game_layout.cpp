// The native records of this area in the exe layout (test-only).
#include "translator/actor_layout.hpp"
#include "game/actor_state.hpp"
#include "game/birdbot.hpp"
#include "game/birdbot_down_state.hpp"
#include "game/birdbot_soldier_state.hpp"
#include "game/bomb_state.hpp"
#include "game/carry.hpp"
#include "game/charge_glow.hpp"
#include "game/charge_glow_state.hpp"
#include "game/clut_animation_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/door_leaves.hpp"
#include "game/dust_puff.hpp"
#include "game/exit_transitions_state.hpp"
#include "game/explosions.hpp"
#include "game/explosions_state.hpp"
#include "game/field_hoverbot_state.hpp"
#include "game/field_reaverbot_state.hpp"
#include "game/floating_device_state.hpp"
#include "game/footstep_dust.hpp"
#include "game/glow_trails.hpp"
#include "game/guardian_reaverbot_state.hpp"
#include "game/gunship_flight.hpp"
#include "game/hit_effects.hpp"
#include "game/lens_flare_state.hpp"
#include "game/lift_box_state.hpp"
#include "game/light_column_state.hpp"
#include "game/line_of_sight_state.hpp"
#include "game/lobbed_shot_state.hpp"
#include "game/message_triggers.hpp"
#include "game/missile_trail.hpp"
#include "game/pickups.hpp"
#include "game/placed_models_state.hpp"
#include "game/player/player_state.hpp"
#include "game/reaverbot_turret_state.hpp"
#include "game/reaverbot_walker_state.hpp"
#include "game/servbot.hpp"
#include "game/shockwave_state.hpp"
#include "game/sign_state.hpp"
#include "game/skid_sparks.hpp"
#include "game/sky_arc_state.hpp"
#include "game/smoke_column.hpp"
#include "game/snow_reaverbot_state.hpp"
#include "game/snow_state.hpp"
#include "game/spark_emitter.hpp"
#include "game/spark_emitter_state.hpp"
#include "game/spark_rings.hpp"
#include "game/status_markers_state.hpp"
#include "game/storm_ribbon.hpp"
#include "game/suction_effect.hpp"
#include "game/tank_shell_state.hpp"
#include "game/throwable_state.hpp"
#include "game/town_npc.hpp"
#include "game/travel_state.hpp"
#include "game/treasure_chest_state.hpp"
#include "game/turret_bullet_state.hpp"
#include "game/walker_shot_state.hpp"
#include "game/wall_crumble.hpp"
#include "game/wall_debris_state.hpp"
#include "game/water_splash_state.hpp"
#include "game/weather_draw.hpp"
#include "content/nino/mission_gunships_state.hpp"
#include "content/nino/st18_gunships_state.hpp"

namespace od::test {
using namespace od::port;

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::Actor& s) {
    using namespace od::port;
    layoutValue(v, a, static_cast<od::port::ActorHeader&>(s));
    layoutValue(v, a + 0x10, s.pos);
    layoutValue(v, a + 0x1c, s.prevPos);
    layoutValue(v, a + 0x28, s.rotation);
    layoutValue(v, a + 0x30, s.prevRotation);
    layoutValue(v, a + 0x38, s.velocity);
    layoutValue(v, a + 0x40, s.acceleration);
    layoutValue(v, a + 0x48, s.spin);
    layoutValue(v, a + 0x50, s.airborne);
    layoutValue(v, a + 0x51, s.wasAirborne);
    layoutValue(v, a + 0x52, s.onPlatform);
    layoutValue(v, a + 0x53, s.wasOnPlatform);
    layoutValue(v, a + 0x54, s.platform);
    layoutValue(v, a + 0x58, s.collisionBox);
    layoutValue(v, a + 0x5c, s.interactionBox);
    layoutValue(v, a + 0x60, s.interaction);
    layoutValue(v, a + 0x64, s.wallHit);
    layoutValue(v, a + 0x68, s.actorHit);
    layoutValue(v, a + 0x6c, s.lastMover);
    layoutValue(v, a + 0x70, s.hitPoints);
    layoutValue(v, a + 0x72, s.maxHitPoints);
    layoutValue(v, a + 0x74, s.rootMatrix);
    for (size_t i = 0; i < s.interactionPad.size(); ++i) layoutValue(v, a + 0x61 + uint32_t(i) * (1), s.interactionPad[i]);
    layoutValue(v, a + 0x86, s.rootPad);
    for (size_t i = 0; i < s.rootTranslation.size(); ++i) layoutValue(v, a + 0x88 + uint32_t(i) * (4), s.rootTranslation[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ActorHeader& s) {
    using namespace od::port;
    layoutValue(v, a + 0x00, s.flags);
    layoutValue(v, a + 0x01, s.runFlags);
    layoutValue(v, a + 0x02, s.slot);
    layoutValue(v, a + 0x03, s.loopFlags);
    layoutValue(v, a + 0x04, s.type);
    layoutValue(v, a + 0x05, s.subtype);
    layoutValue(v, a + 0x06, s.variant);
    layoutValue(v, a + 0x07, s.spawnByte);
    layoutValue(v, a + 0x08, s.state);
    layoutValue(v, a + 0x09, s.subState);
    layoutValue(v, a + 0x0a, s.step);
    layoutValue(v, a + 0x0b, s.stepCounter);
    for (size_t i = 0; i < s.params.size(); ++i) layoutValue(v, a + 0x0c + uint32_t(i) * (1), s.params[i]);
}



void layoutValue(LayoutVisitor& v, uint32_t a, od::port::AnimatedActor& s) {
    using namespace od::port;
    layoutValue(v, a, static_cast<od::port::Actor&>(s));
    layoutValue(v, a + 0x94, s.animScript);
    layoutValue(v, a + 0x98, s.animKeyAt);
    layoutValue(v, a + 0x9c, s.animKey);
    layoutValue(v, a + 0xa0, s.anim);
    layoutValue(v, a + 0xa1, s.prevAnim);
    layoutValue(v, a + 0xa2, s.frameData);
    layoutValue(v, a + 0xa3, s.keyIndex);
    layoutValue(v, a + 0xa4, s.model);
    layoutValue(v, a + 0xa8, s.frameTable);
    layoutValue(v, a + 0xac, s.animTable);
    layoutValue(v, a + 0xb0, s.skeleton);
    layoutValue(v, a + 0xb4, s.extraGeometry);
    layoutValue(v, a + 0xb8, s.collisionMesh);
    for (size_t i = 0; i < s.colourTables.size(); ++i) layoutValue(v, a + 0xbc + uint32_t(i) * (4), s.colourTables[i]);
    layoutValue(v, a + 0xc8, s.boneSlotCount);
    layoutValue(v, a + 0xcb, s.partCount);
    layoutValue(v, a + 0xcc, s.boneBlocks);
    layoutValue(v, a + 0xd0, s.lastBoneBlock);
    layoutValue(v, a + 0xd4, s.cullWidth);
    layoutValue(v, a + 0xd6, s.cullHeight);
    layoutValue(v, a + 0xd8, s.texturePage);
    layoutValue(v, a + 0xdc, s.secondTexturePage);
    layoutValue(v, a + 0xe0, s.scale);
    layoutValue(v, a + 0xe6, s.tint);
    for (size_t i = 0; i < s.rootScaleBytes.size(); ++i) layoutValue(v, a + 0xe8 + uint32_t(i) * (1), s.rootScaleBytes[i]);
    layoutValue(v, a + 0xeb, s.semiTransparency);
    for (size_t i = 0; i < s.parts.size(); ++i) layoutValue(v, a + 0xec + uint32_t(i) * (4), s.parts[i]);
    layoutValue(v, a + 0xc9, s.secondLodParts);
    layoutValue(v, a + 0xca, s.thirdLodParts);
    layoutValue(v, a + 0x158, s.bossFlags);
    layoutValue(v, a + 0x15f, s.guardianBits);
    layoutValue(v, a + 0x160, s.shownParts);
    for (size_t i = 0; i < s.hopperBytes.size(); ++i) layoutValue(v, a + 0x162 + uint32_t(i) * (1), s.hopperBytes[i]);
    layoutValue(v, a + 0x168, s.townBirdHits);
    layoutValue(v, a + 0x169, s.townBirdHitsHigh);
    layoutValue(v, a + 0x16a, s.hopperTimer);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::BirdbotBody& s) {
    using namespace od::port;
    layoutValue(v, a + 0x15e, s.bits);
    layoutValue(v, a + 0x161, s.contact);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::BlizzardLayerFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.otSlot);
    layoutValue(v, a + 0x10, s.scrollX);
    layoutValue(v, a + 0x14, s.scrollY);
    layoutValue(v, a + 0x18, s.speed);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::BurstFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.size);
    layoutValue(v, a + 0xe, s.sizeClass);
    layoutValue(v, a + 0xf, s.ticks);
    layoutValue(v, a + 0x9, s.red);
    layoutValue(v, a + 0xa, s.green);
    layoutValue(v, a + 0xb, s.blue);
    for (size_t i = 0; i < s.spokes.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.spokes[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::CastFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0x168, s.heldKey);
    layoutValue(v, a + 0x14c, s.faceTracks);
    layoutValue(v, a + 0x15c, s.castFlags);
    layoutValue(v, a + 0x15d, s.animKey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ChargeGlowParams& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.held);
    layoutValue(v, a + 0x16, s.damage);
    layoutValue(v, a + 0x18, s.hitBox);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ControllerActor& s) {
    using namespace od::port;
    layoutValue(v, a, static_cast<od::port::ActorHeader&>(s));
    layoutValue(v, a + 0x10, s.recordPoint);
    layoutValue(v, a + 0x18, s.rider);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::DustPuff& s) {
    using namespace od::port;
    layoutValue(v, a + 0, s.first);
    layoutValue(v, a + 6, s.driftX);
    layoutValue(v, a + 8, s.second);
    layoutValue(v, a + 0xe, s.driftZ);
    layoutValue(v, a + 0x10, s.life);
    layoutValue(v, a + 0x12, s.age);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::DustPuffFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.size);
    layoutValue(v, a + 0xe, s.ticks);
    layoutValue(v, a + 0xf, s.airborne);
    layoutValue(v, a + 0x18, s.tick);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::EffectActor& s) {
    using namespace od::port;
    layoutValue(v, a, static_cast<od::port::ActorHeader&>(s));
    layoutValue(v, a + 0x10, s.pos);
    layoutValue(v, a + 0x16, s.posSpare);
    for (size_t i = 0; i < s.trail.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (2), s.trail[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ExplosionFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0x94, s.flashesLeft);
    layoutValue(v, a + 0x96, s.flashesAlive);
    for (size_t i = 0; i < s.lives.size(); ++i) layoutValue(v, a + 0x98 + uint32_t(i) * (1), s.lives[i]);
    for (size_t i = 0; i < s.flashes.size(); ++i) layoutValue(v, a + 0x9c + uint32_t(i) * (8), s.flashes[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ExplosionFlash& s) {
    using namespace od::port;
    layoutValue(v, a + 0, s.point);
    layoutValue(v, a + 6, s.size);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::FootstepDustFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0x74, s.parent);
    for (size_t i = 0; i < s.puffs.size(); ++i) layoutValue(v, a + 0x78 + uint32_t(i) * (0x14), s.puffs[i]);
    layoutValue(v, a + 0xc8, s.made);
    layoutValue(v, a + 0xc9, s.stopped);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::GlowTrailFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xa, s.size);
    layoutValue(v, a + 0xb, s.mode);
    layoutValue(v, a + 0xc, s.parent);
    layoutValue(v, a + 0x16, s.bone);
    layoutValue(v, a + 0x18, s.ringSize);
    layoutValue(v, a + 0x19, s.ringTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::GunshipFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0x2e, s.rollTarget);
    layoutValue(v, a + 0x3e, s.heading);
    layoutValue(v, a + 0x3e, s.pointX);
    layoutValue(v, a + 0x40, s.rollSpeed);
    layoutValue(v, a + 0x46, s.pointZ);
    layoutValue(v, a + 0x4e, s.turnStep);
    layoutValue(v, a + 0x64, s.homeHeight);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc7, s.pitchTarget);
    layoutValue(v, a + 0xc8, s.timer);
    if ((dynamic_cast<mission_gunships_state::MissionGunshipFields*>(&s) || dynamic_cast<st18_gunships_state::FightGunshipFields*>(&s))) layoutValue(v, a + 0xca, s.turnMargin);
    if ((dynamic_cast<mission_gunships_state::MissionGunshipFields*>(&s) || dynamic_cast<st18_gunships_state::FightGunshipFields*>(&s))) layoutValue(v, a + 0xcb, s.attackCooldown);
    layoutValue(v, a + 0xc4, s.flight);
    layoutValue(v, a + 0xc5, s.behaviour);
    layoutValue(v, a + 0xc6, s.contact);
    layoutValue(v, a + 0xc9, s.burst);
    if (!(dynamic_cast<mission_gunships_state::MissionGunshipFields*>(&s) || dynamic_cast<st18_gunships_state::FightGunshipFields*>(&s))) layoutValue(v, a + 0xca, s.cooldown);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::HitSparkFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.size);
    layoutValue(v, a + 0xe, s.frameTicks);
    layoutValue(v, a + 0xf, s.tick);
    layoutValue(v, a + 0x18, s.firstFrame);
    layoutValue(v, a + 0x1c, s.frame);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::HurtSparksFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.pitch);
    layoutValue(v, a + 0xe, s.yaw);
    layoutValue(v, a + 0x9, s.count);
    layoutValue(v, a + 0xa, s.ticks);
    layoutValue(v, a + 0xb, s.startTicks);
    for (size_t i = 0; i < s.angles.size(); ++i) layoutValue(v, a + 0x68 + uint32_t(i) * (1), s.angles[i]);
    for (size_t i = 0; i < s.heights.size(); ++i) layoutValue(v, a + 0x88 + uint32_t(i) * (1), s.heights[i]);
    for (size_t i = 0; i < s.radii.size(); ++i) layoutValue(v, a + 0xa8 + uint32_t(i) * (1), s.radii[i]);
}


void layoutValue(LayoutVisitor& v, uint32_t a, od::port::MeshActor& s) {
    using namespace od::port;
    layoutValue(v, a, static_cast<od::port::Actor&>(s));
    layoutValue(v, a + 0x94, s.model);
    layoutValue(v, a + 0x98, s.extraGeometry);
    layoutValue(v, a + 0x9c, s.collisionMesh);
    for (size_t i = 0; i < s.colourTables.size(); ++i) layoutValue(v, a + 0xa0 + uint32_t(i) * (4), s.colourTables[i]);
    layoutValue(v, a + 0xac, s.cullWidth);
    layoutValue(v, a + 0xae, s.cullHeight);
    layoutValue(v, a + 0xb0, s.scale);
    layoutValue(v, a + 0xb6, s.tint);
    for (size_t i = 0; i < s.scaleBytes.size(); ++i) layoutValue(v, a + 0xb8 + uint32_t(i) * (1), s.scaleBytes[i]);
    layoutValue(v, a + 0xbb, s.semiTransparency);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::MessageTriggerFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.message);
    layoutValue(v, a + 0xe, s.lastStory);
    layoutValue(v, a + 0xf, s.range);
    layoutValue(v, a + 0x16, s.flag);
    for (size_t i = 0; i < s.params.size(); ++i) layoutValue(v, a + 0x10 + uint32_t(i) * (2), s.params[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::MissileTrailFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.parent);
    layoutValue(v, a + 0x28, s.length);
    layoutValue(v, a + 0x2a, s.head);
    for (size_t i = 0; i < s.xs.size(); ++i) layoutValue(v, a + 0x2c + uint32_t(i) * (2), s.xs[i]);
    for (size_t i = 0; i < s.ys.size(); ++i) layoutValue(v, a + 0x60 + uint32_t(i) * (2), s.ys[i]);
    for (size_t i = 0; i < s.zs.size(); ++i) layoutValue(v, a + 0x94 + uint32_t(i) * (2), s.zs[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::PickupFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.kind);
    layoutValue(v, a + 0xd, s.size);
    layoutValue(v, a + 0xc0, s.drawRoll);
    layoutValue(v, a + 0xc7, s.spinSpeed);
    layoutValue(v, a + 0xbc, s.drawPitch);
    layoutValue(v, a + 0xbe, s.drawYaw);
    layoutValue(v, a + 0xc4, s.bob);
    layoutValue(v, a + 0xc6, s.sprite);
    layoutValue(v, a + 0xc8, s.timeout);
    layoutValue(v, a + 0xca, s.bobPhase);
}




void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ProjectileActor& s) {
    using namespace od::port;
    layoutValue(v, a, static_cast<od::port::MeshActor&>(s));
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.debrisFlags);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::RiderFollowerFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.riderSlot);
    layoutValue(v, a + 0xd, s.riderType);
    layoutValue(v, a + 0xe, s.riderLoopFlags);
    layoutValue(v, a + 0x18, s.rider);
    layoutValue(v, a + 0x1c, s.riderStageClass);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ServbotFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0x7, s.colour);
    layoutValue(v, a + 0xc, s.route);
    layoutValue(v, a + 0xd, s.behaviour);
    layoutValue(v, a + 0xe, s.partnerRoute);
    layoutValue(v, a + 0xf, s.message);
    layoutValue(v, a + 0x3e, s.timer);
    layoutValue(v, a + 0x46, s.hitYaw);
    layoutValue(v, a + 0x46, s.fidgetPattern);
    layoutValue(v, a + 0x15f, s.headYawTarget);
    layoutValue(v, a + 0x164, s.bits);
    layoutValue(v, a + 0x165, s.partnerSlot);
    layoutValue(v, a + 0x166, s.lookMode);
    layoutValue(v, a + 0x167, s.waypoint);
    layoutValue(v, a + 0x168, s.previousWaypoint);
    layoutValue(v, a + 0x169, s.headHold);
    layoutValue(v, a + 0x16a, s.collisionBits);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.headPitch);
    layoutValue(v, a + 0x15d, s.headYaw);
    layoutValue(v, a + 0x15e, s.headPitchTarget);
    layoutValue(v, a + 0x162, s.targetZ);
    layoutValue(v, a + 0x16b, s.groundLossReaction);
    layoutValue(v, a + 0x160, s.targetX);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ShotActor& s) {
    using namespace od::port;
    layoutValue(v, a, static_cast<od::port::MeshActor&>(s));
    layoutValue(v, a + 0xdc, s.hitBox);
    layoutValue(v, a + 0xec, s.aimPoint);
    layoutValue(v, a + 0xf4, s.aimOffset);
    layoutValue(v, a + 0xfc, s.target);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::SkidSparkFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.yaw);
    layoutValue(v, a + 0xe, s.spareWord);
    layoutValue(v, a + 0x1c, s.speedX);
    layoutValue(v, a + 0x1e, s.speedY);
    layoutValue(v, a + 0x20, s.speedZ);
    layoutValue(v, a + 0x22, s.gravity);
    layoutValue(v, a + 0x18, s.entry);
    layoutValue(v, a + 0x24, s.life);
    layoutValue(v, a + 0x26, s.tick);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::SlidingLeafFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xbc, s.targetX);
    layoutValue(v, a + 0xbe, s.targetZ);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::SmokeColumnFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xb, s.ended);
    layoutValue(v, a + 0xc, s.bone);
    layoutValue(v, a + 0x28, s.length);
    layoutValue(v, a + 0x2a, s.head);
    layoutValue(v, a + 0x2c, s.rise);
    layoutValue(v, a + 0x2e, s.phase);
    layoutValue(v, a + 0x38, s.segmentWidth);
    layoutValue(v, a + 0x30, s.drawMode);
    layoutValue(v, a + 0x34, s.colourStep);
    for (size_t i = 0; i < s.ringX.size(); ++i) layoutValue(v, a + 0x3c + uint32_t(i) * (2), s.ringX[i]);
    for (size_t i = 0; i < s.ringY.size(); ++i) layoutValue(v, a + 0x5c + uint32_t(i) * (2), s.ringY[i]);
    for (size_t i = 0; i < s.ringZ.size(); ++i) layoutValue(v, a + 0x7c + uint32_t(i) * (2), s.ringZ[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::SparkEmitterParams& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.driftX);
    layoutValue(v, a + 0xd, s.driftZ);
    layoutValue(v, a + 0xe, s.lifetime);
    layoutValue(v, a + 0xbc, s.parent);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::SparkRingParams& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.velocityX);
    layoutValue(v, a + 0xd, s.velocityY);
    layoutValue(v, a + 0xe, s.velocityZ);
    layoutValue(v, a + 0xf, s.height);
    layoutValue(v, a + 0x16, s.size);
    for (size_t i = 0; i < s.angles.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.angles[i]);
    layoutValue(v, a + 0x1b, s.spin);
    layoutValue(v, a + 0x1c, s.life);
    layoutValue(v, a + 0x1d, s.grow);
    layoutValue(v, a + 0x1e, s.targetSize);
    layoutValue(v, a + 0x1f, s.sizeStep);
    for (size_t i = 0; i < s.colour.size(); ++i) layoutValue(v, a + 0x20 + uint32_t(i) * (1), s.colour[i]);
    layoutValue(v, a + 0x23, s.innerRatio);
    layoutValue(v, a + 0x24, s.gradient);
    layoutValue(v, a + 0x25, s.rotationOrder);
    layoutValue(v, a + 0x26, s.peak);
    layoutValue(v, a + 0x27, s.ticksLeft);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::StaticActor& s) {
    using namespace od::port;
    layoutValue(v, a, static_cast<od::port::MeshActor&>(s));
    layoutValue(v, a + 0xbc, s.hitBox);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::StormRibbonFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.steadyBands);
    layoutValue(v, a + 0xd, s.colourCountdown);
    layoutValue(v, a + 0x18, s.wobbleSlow);
    layoutValue(v, a + 0x1a, s.wobbleMid);
    layoutValue(v, a + 0x1c, s.wobbleFast);
    layoutValue(v, a + 0x1e, s.colourPhaseLow);
    layoutValue(v, a + 0x1f, s.colourPhaseMid);
    layoutValue(v, a + 0x20, s.colourPhaseHigh);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::SuctionFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.rangeLevel);
    layoutValue(v, a + 0xe, s.strength);
    layoutValue(v, a + 0xf, s.soundTicks);
    offsetActorRef(v, a + 0x18, s.vacuumShot, 0x74);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::SwingLeafFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0x4e, s.swingAcceleration);
    layoutValue(v, a + 0xbc, s.opening);
    layoutValue(v, a + 0xbe, s.closedYaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::TownNpcFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.route);
    layoutValue(v, a + 0xd, s.behaviour);
    layoutValue(v, a + 0xe, s.partnerRoute);
    layoutValue(v, a + 0xf, s.message);
    layoutValue(v, a + 0x15f, s.waypoint);
    layoutValue(v, a + 0x160, s.previousWaypoint);
    layoutValue(v, a + 0x162, s.collisionBits);
    layoutValue(v, a + 0x14c, s.bumpedBy);
    layoutValue(v, a + 0x150, s.headPitch);
    layoutValue(v, a + 0x151, s.headYaw);
    layoutValue(v, a + 0x152, s.headPitchTarget);
    layoutValue(v, a + 0x153, s.headYawTarget);
    layoutValue(v, a + 0x154, s.timer);
    layoutValue(v, a + 0x156, s.fidgetPattern);
    layoutValue(v, a + 0x15a, s.targetZ);
    layoutValue(v, a + 0x15c, s.bits);
    layoutValue(v, a + 0x15d, s.partnerSlot);
    layoutValue(v, a + 0x15e, s.lookMode);
    layoutValue(v, a + 0x161, s.headHold);
    layoutValue(v, a + 0x164, s.hitBox);
    layoutValue(v, a + 0x158, s.targetX);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::VacuumShotFields& s) {
    using namespace od::port;
    for (size_t i = 0; i < s.angle.size(); ++i) layoutValue(v, a + 0x74 + uint32_t(i) * (1), s.angle[i]);
    for (size_t i = 0; i < s.spare.size(); ++i) layoutValue(v, a + 0x84 + uint32_t(i) * (1), s.spare[i]);
    for (size_t i = 0; i < s.life.size(); ++i) layoutValue(v, a + 0x94 + uint32_t(i) * (1), s.life[i]);
    for (size_t i = 0; i < s.brightness.size(); ++i) layoutValue(v, a + 0xa4 + uint32_t(i) * (1), s.brightness[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::WallCrumbleFields& s) {
    using namespace od::port;
    layoutValue(v, a + 0xc, s.height);
    layoutValue(v, a + 0xe, s.width);
    layoutValue(v, a + 0x10, s.point);
    layoutValue(v, a + 0x18, s.segment);
    layoutValue(v, a + 0x1c, s.poolEntry);
    layoutValue(v, a + 0x18, s.side);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::WaterSplashFields& s) {
    using namespace od::port;
    if ((actorHeaderAt(a).variant >> 6) == 1) layoutValue(v, a + 0x10, s.followed);
    layoutValue(v, a + 0x14, s.offsetY);
    layoutValue(v, a + 0x16, s.offsetX);
    layoutValue(v, a + 0x1a, s.offsetZ);
    layoutValue(v, a + 0x40, s.spawned);
    layoutValue(v, a + 0x41, s.alive);
    layoutValue(v, a + 0x42, s.life);
    if ((actorHeaderAt(a).variant >> 6) == 2) layoutValue(v, a + 0x10, s.point);
    layoutValue(v, a + 0xc, s.sizeMask);
    layoutValue(v, a + 0xd, s.particleLife);
    layoutValue(v, a + 0xe, s.count);
    layoutValue(v, a + 0x3c, s.speed);
    for (size_t i = 0; i < s.particlePoints.size(); ++i) layoutValue(v, a + 0x44 + uint32_t(i) * (8), s.particlePoints[i]);
    for (size_t i = 0; i < s.particleRotations.size(); ++i) layoutValue(v, a + 0x84 + uint32_t(i) * (8), s.particleRotations[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::birdbot_down_state::DownFields& s) {
    using namespace od::port::birdbot_down_state;
    layoutValue(v, a, static_cast<od::port::BirdbotBody&>(s));
    layoutValue(v, a + 0x15f, s.timer);
    layoutValue(v, a + 0x160, s.turnTimer);
    layoutValue(v, a + 0x162, s.offScreen);
    layoutValue(v, a + 0x163, s.animKeyStart);
    layoutValue(v, a + 0x166, s.hitKind);
    layoutValue(v, a + 0x168, s.smoking);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.heading);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::birdbot_soldier_state::SoldierFields& s) {
    using namespace od::port::birdbot_soldier_state;
    layoutValue(v, a, static_cast<od::port::BirdbotBody&>(s));
    layoutValue(v, a + 0xc, s.range);
    layoutValue(v, a + 0xd, s.callBit);
    layoutValue(v, a + 0xe, s.boatSlot);
    layoutValue(v, a + 0xf, s.trackId);
    layoutValue(v, a + 0x2e, s.boatYaw);
    layoutValue(v, a + 0x3e, s.targetX);
    layoutValue(v, a + 0x46, s.targetZ);
    layoutValue(v, a + 0x15f, s.timer);
    layoutValue(v, a + 0x160, s.parts);
    layoutValue(v, a + 0x162, s.group);
    layoutValue(v, a + 0x163, s.animKeyStart);
    layoutValue(v, a + 0x164, s.hitKind);
    layoutValue(v, a + 0x165, s.remainsVariant);
    layoutValue(v, a + 0x168, s.hurt);
    layoutValue(v, a + 0x169, s.chase);
    layoutValue(v, a + 0x16a, s.sideOffset);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.neighbour);
    layoutValue(v, a + 0x166, s.groundHeight);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::bomb_state::BombFields& s) {
    using namespace od::port::bomb_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.collisionBits);
    layoutValue(v, a + 0xc5, s.behaviour);
    layoutValue(v, a + 0xc6, s.fuse);
    layoutValue(v, a + 0xc8, s.fuseLit);
    layoutValue(v, a + 0xc, s.carrierCommand);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::charge_glow_state::ChargeGlowFields& s) {
    using namespace od::port::charge_glow_state;
    layoutValue(v, a, static_cast<od::port::ChargeGlowParams&>(s));
    layoutValue(v, a + 0xf, s.radius);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::clut_animation_state::ClutAnimationFields& s) {
    using namespace od::port::clut_animation_state;
    layoutValue(v, a + 0x18, s.sequence);
    layoutValue(v, a + 0x1c, s.key);
    layoutValue(v, a + 0x20, s.keyIndex);
    layoutValue(v, a + 0x21, s.ticksLeft);
    layoutValue(v, a + 0x22, s.source);
    layoutValue(v, a + 0x23, s.sourceFlags);
    layoutValue(v, a + 0x24, s.destination);
    layoutValue(v, a + 0x25, s.destinationFlags);
    layoutValue(v, a + 0x26, s.live);
    layoutValue(v, a + 0x27, s.size);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::clut_animation_state::RoomFlagClutFields& s) {
    using namespace od::port::clut_animation_state;
    layoutValue(v, a + 0xc, s.firstSequence);
    for (size_t i = 0; i < s.animationSlots.size(); ++i) layoutValue(v, a + 0x10 + uint32_t(i) * (1), s.animationSlots[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::exit_transitions_state::TeleporterSparkFields& s) {
    using namespace od::port::exit_transitions_state;
    for (size_t i = 0; i < s.velocity.size(); ++i) layoutValue(v, a + 0xc + uint32_t(i) * (1), s.velocity[i]);
    layoutValue(v, a + 0xf, s.pad);
    layoutValue(v, a + 0x16, s.spare);
    for (size_t i = 0; i < s.tail.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.tail[i]);
    layoutValue(v, a + 0x1b, s.life);
    for (size_t i = 0; i < s.colour.size(); ++i) layoutValue(v, a + 0x1c + uint32_t(i) * (1), s.colour[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::explosions_state::BlastFields& s) {
    using namespace od::port::explosions_state;
    layoutValue(v, a + 0x74, s.box);
    layoutValue(v, a + 0x94, s.ticks);
    for (size_t i = 0; i < s.flashLives.size(); ++i) layoutValue(v, a + 0x98 + uint32_t(i) * (1), s.flashLives[i]);
    for (size_t i = 0; i < s.flashTilts.size(); ++i) layoutValue(v, a + 0x9c + uint32_t(i) * (1), s.flashTilts[i]);
    for (size_t i = 0; i < s.flashRadii.size(); ++i) layoutValue(v, a + 0xa0 + uint32_t(i) * (1), s.flashRadii[i]);
    for (size_t i = 0; i < s.flashSpreads.size(); ++i) layoutValue(v, a + 0xa4 + uint32_t(i) * (1), s.flashSpreads[i]);
    layoutValue(v, a + 0xa8, s.colours);
    layoutValue(v, a + 0xbc, s.hitBox);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_hoverbot_state::FieldHoverbotFields& s) {
    using namespace od::port::field_hoverbot_state;
    layoutValue(v, a + 0x2e, s.aimYaw);
    layoutValue(v, a + 0x2e, s.sinkSounded);
    layoutValue(v, a + 0x160, s.neighbourLow);
    layoutValue(v, a + 0x162, s.bits);
    layoutValue(v, a + 0x163, s.cooldown);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.contact);
    layoutValue(v, a + 0x160, s.neighbour);
    layoutValue(v, a + 0x164, s.fallTicks);
    layoutValue(v, a + 0x166, s.radius);
    layoutValue(v, a + 0x168, s.pitch);
    layoutValue(v, a + 0x16a, s.yaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::field_reaverbot_state::FieldReaverbotFields& s) {
    using namespace od::port::field_reaverbot_state;
    layoutValue(v, a + 0xd, s.patrolCells);
    layoutValue(v, a + 0x2e, s.homeYaw);
    layoutValue(v, a + 0x4c, s.homeX);
    layoutValue(v, a + 0x4e, s.homeZ);
    layoutValue(v, a + 0x160, s.behaviour);
    layoutValue(v, a + 0x162, s.bits);
    layoutValue(v, a + 0x163, s.hitsLeft);
    layoutValue(v, a + 0x166, s.cooldown);
    layoutValue(v, a + 0x167, s.farTicks);
    layoutValue(v, a + 0x168, s.group);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.heading);
    layoutValue(v, a + 0x161, s.contact);
    layoutValue(v, a + 0x164, s.neighbour);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::floating_device_state::FloatingDeviceFields& s) {
    using namespace od::port::floating_device_state;
    layoutValue(v, a + 0xc, s.leadsOut);
    layoutValue(v, a + 0xd, s.scaleSteps);
    layoutValue(v, a + 0xe, s.spin);
    layoutValue(v, a + 0xf, s.message);
    layoutValue(v, a + 0x14c, s.baseHeight);
    layoutValue(v, a + 0x14e, s.bobPhase);
    layoutValue(v, a + 0x150, s.talking);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::guardian_reaverbot_state::GuardianFields& s) {
    using namespace od::port::guardian_reaverbot_state;
    layoutValue(v, a + 0xc, s.sight);
    layoutValue(v, a + 0x2e, s.invulnerable);
    layoutValue(v, a + 0x15f, s.bits);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.counter);
    layoutValue(v, a + 0x15e, s.animKeyStart);
    layoutValue(v, a + 0x160, s.target);
    layoutValue(v, a + 0x162, s.patience);
    layoutValue(v, a + 0x163, s.headYaw);
    layoutValue(v, a + 0x164, s.headMode);
    layoutValue(v, a + 0x165, s.shakePhase);
    layoutValue(v, a + 0x166, s.cooldown);
    layoutValue(v, a + 0x167, s.wallBits);
    for (size_t i = 0; i < s.glowSlots.size(); ++i) layoutValue(v, a + 0x168 + uint32_t(i) * (1), s.glowSlots[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::lens_flare_state::DroppingRingFields& s) {
    using namespace od::port::lens_flare_state;
    layoutValue(v, a + 0xc, s.tiltX);
    layoutValue(v, a + 0xd, s.tiltY);
    layoutValue(v, a + 0xe, s.halfRadius);
    layoutValue(v, a + 0x16, s.stopAt);
    layoutValue(v, a + 0x18, s.distance);
    layoutValue(v, a + 0x1a, s.ticks);
}

void layoutValue(LayoutVisitor&, uint32_t, od::port::lens_flare_state::PointedFields&) {}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::lens_flare_state::PulsingDiscFields& s) {
    using namespace od::port::lens_flare_state;
    layoutValue(v, a + 0x18, s.ticks);
    layoutValue(v, a + 0x1a, s.radius);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::lens_flare_state::RayFields& s) {
    using namespace od::port::lens_flare_state;
    layoutValue(v, a, static_cast<od::port::lens_flare_state::PointedFields&>(s));
    layoutValue(v, a + 0xa, s.spawnAngle);
    layoutValue(v, a + 0x16, s.spawnWidth);
    layoutValue(v, a + 0x18, s.ticks);
    layoutValue(v, a + 0x1a, s.width);
    layoutValue(v, a + 0x1c, s.startStep);
    layoutValue(v, a + 0x1e, s.turnSpeed);
    layoutValue(v, a + 0x20, s.angle);
    layoutValue(v, a + 0x22, s.unread22);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::lens_flare_state::ShrinkingDiscFields& s) {
    using namespace od::port::lens_flare_state;
    layoutValue(v, a, static_cast<od::port::lens_flare_state::PointedFields&>(s));
    layoutValue(v, a + 0x18, s.ticks);
    layoutValue(v, a + 0x1a, s.width);
    layoutValue(v, a + 0x1c, s.startStep);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::lift_box_state::LiftBoxFields& s) {
    using namespace od::port::lift_box_state;
    layoutValue(v, a + 0xc, s.holder);
    layoutValue(v, a + 0xd, s.carried);
    layoutValue(v, a + 0xe, s.liftLock);
    layoutValue(v, a + 0xf, s.settled);
    layoutValue(v, a + 0x2e, s.travelYaw);
    layoutValue(v, a + 0x3e, s.behaviour);
    layoutValue(v, a + 0x46, s.collisionBits);
    layoutValue(v, a + 0xbc, s.hitBox);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::light_column_state::GlowFields& s) {
    using namespace od::port::light_column_state;
    layoutValue(v, a + 0xc, s.size);
    layoutValue(v, a + 0xe, s.heightSteps);
    layoutValue(v, a + 0xf, s.period);
    layoutValue(v, a + 0x18, s.timer);
    layoutValue(v, a + 0x1a, s.rise);
    layoutValue(v, a + 0x1b, s.brightness);
    layoutValue(v, a + 0x1c, s.column);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::line_of_sight_state::LineOfSightFields& s) {
    using namespace od::port::line_of_sight_state;
    layoutValue(v, a + 0x8, s.result);
    layoutValue(v, a + 0xc, s.boxHalf);
    layoutValue(v, a + 0xe, s.stepsPerTick);
    layoutValue(v, a + 0xf, s.ownerSlot);
    layoutValue(v, a + 0x10, s.point);
    layoutValue(v, a + 0x16, s.collisionMask);
    layoutValue(v, a + 0x18, s.fixedTargetXY);
    layoutValue(v, a + 0x1c, s.fixedTargetZ);
    layoutValue(v, a + 0x1c, s.targetSlot);
    layoutValue(v, a + 0x1d, s.targetBone);
    layoutValue(v, a + 0x1e, s.stepLength);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::lobbed_shot_state::LobbedShotFields& s) {
    using namespace od::port::lobbed_shot_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.spriteU);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::placed_models_state::PlacedModelFields& s) {
    using namespace od::port::placed_models_state;
    layoutValue(v, a + 0xbc, s.targetX);
    layoutValue(v, a + 0xbe, s.targetZ);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::placed_models_state::TurningModelFields& s) {
    using namespace od::port::placed_models_state;
    layoutValue(v, a + 0xbc, s.startCleared);
    layoutValue(v, a + 0xbe, s.startYaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::reaverbot_turret_state::TurretFields& s) {
    using namespace od::port::reaverbot_turret_state;
    layoutValue(v, a + 0xc, s.rangeIndex);
    layoutValue(v, a + 0xf, s.bulletVariant);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.flags);
    layoutValue(v, a + 0x156, s.timer);
    layoutValue(v, a + 0x158, s.pushOut);
    layoutValue(v, a + 0x15c, s.neighbour);
    layoutValue(v, a + 0x15e, s.target);
    layoutValue(v, a + 0x160, s.turnTicks);
    layoutValue(v, a + 0x162, s.yawReference);
    layoutValue(v, a + 0x164, s.chargeSlot);
    layoutValue(v, a + 0x165, s.bulletSlot);
    layoutValue(v, a + 0x166, s.bulletHold);
    layoutValue(v, a + 0x167, s.firing);
    layoutValue(v, a + 0x168, s.travelled);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::reaverbot_walker_state::WalkerFields& s) {
    using namespace od::port::reaverbot_walker_state;
    layoutValue(v, a + 0xd, s.patrolCells);
    layoutValue(v, a + 0xe, s.wakeFlag);
    layoutValue(v, a + 0x2e, s.homeYaw);
    layoutValue(v, a + 0x4e, s.homeZ);
    layoutValue(v, a + 0x160, s.behaviour);
    layoutValue(v, a + 0x162, s.bits);
    layoutValue(v, a + 0x163, s.hitsLeft);
    layoutValue(v, a + 0x166, s.cooldown);
    layoutValue(v, a + 0x167, s.group);
    layoutValue(v, a + 0xe6, s.tintLow_);
    layoutValue(v, a + 0xe7, s.tintHigh_);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.heading);
    layoutValue(v, a + 0x161, s.contact);
    layoutValue(v, a + 0x164, s.neighbour);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::shockwave_state::ShockRingFxFields& s) {
    using namespace od::port::shockwave_state;
    layoutValue(v, a + 0xc, s.done);
    layoutValue(v, a + 0xd, s.downward);
    layoutValue(v, a + 0xe, s.depthExtra);
    layoutValue(v, a + 0x1c, s.radius);
    layoutValue(v, a + 0x1e, s.height);
    layoutValue(v, a + 0x18, s.shock);
    layoutValue(v, a + 0x20, s.colour);
    layoutValue(v, a + 0x24, s.phase);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::shockwave_state::ShockwaveFields& s) {
    using namespace od::port::shockwave_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.ring);
    layoutValue(v, a + 0xc8, s.sequence);
    layoutValue(v, a + 0xcc, s.radius);
    layoutValue(v, a + 0xce, s.height);
    layoutValue(v, a + 0xd0, s.colour);
    layoutValue(v, a + 0xd4, s.delay);
    layoutValue(v, a + 0xd6, s.damage);
    layoutValue(v, a + 0xd8, s.phase);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::sign_state::SignFields& s) {
    using namespace od::port::sign_state;
    layoutValue(v, a + 0xc, s.message);
    layoutValue(v, a + 0xe, s.dropFlag);
    layoutValue(v, a + 0xbc, s.hitBox);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::sky_arc_state::SkyArcFields& s) {
    using namespace od::port::sky_arc_state;
    for (size_t i = 0; i < s.point.size(); ++i) layoutValue(v, a + 0x10 + uint32_t(i) * (4), s.point[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::snow_reaverbot_state::SnowReaverbotFields& s) {
    using namespace od::port::snow_reaverbot_state;
    layoutValue(v, a + 0xc, s.spawnKind);
    layoutValue(v, a + 0xc, s.paceDistance);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x154, s.targetYaw);
    layoutValue(v, a + 0x156, s.hits);
    layoutValue(v, a + 0x157, s.bits);
    layoutValue(v, a + 0x158, s.timer);
    layoutValue(v, a + 0x15a, s.hitEntryMode);
    layoutValue(v, a + 0x15c, s.neighbour);
    layoutValue(v, a + 0x15e, s.home);
    layoutValue(v, a + 0x164, s.homeYaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::snow_state::SnowFields& s) {
    using namespace od::port::snow_state;
    layoutValue(v, a + 0xc, s.windX);
    layoutValue(v, a + 0xd, s.windY);
    layoutValue(v, a + 0xe, s.windZ);
    layoutValue(v, a + 0xf, s.period);
    layoutValue(v, a + 0xac, s.countdown);
    layoutValue(v, a + 0x28, s.live);
    for (size_t i = 0; i < s.particles.size(); ++i) layoutValue(v, a + 0x2c + uint32_t(i) * (4), s.particles[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::spark_emitter_state::SparkEmitterFields& s) {
    using namespace od::port::spark_emitter_state;
    layoutValue(v, a, static_cast<od::port::SparkEmitterParams&>(s));
    for (size_t i = 0; i < s.particles.size(); ++i) layoutValue(v, a + 0x28 + uint32_t(i) * (8), s.particles[i]);
    for (size_t i = 0; i < s.wobble.size(); ++i) layoutValue(v, a + 0x98 + uint32_t(i) * (2), s.wobble[i]);
    layoutValue(v, a + 0xc0, s.ticksLeft);
    layoutValue(v, a + 0xc2, s.emitTimer);
    layoutValue(v, a + 0xc4, s.liveMask);
    layoutValue(v, a + 0xc6, s.rise);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::status_markers_state::DrainBurnMarkerFields& s) {
    using namespace od::port::status_markers_state;
    layoutValue(v, a + 0x64, s.level);
    layoutValue(v, a + 0x66, s.particlesAlive);
    layoutValue(v, a + 0x68, s.soundTicks);
    for (size_t i = 0; i < s.particles.size(); ++i) layoutValue(v, a + 0x6c + uint32_t(i) * (8), s.particles[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::status_markers_state::DrainParticle& s) {
    using namespace od::port::status_markers_state;
    layoutValue(v, a + 0, s.point);
    layoutValue(v, a + 6, s.life);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::status_markers_state::SlowMarkerFields& s) {
    using namespace od::port::status_markers_state;
    layoutValue(v, a + 0x64, s.bolts);
    for (size_t i = 0; i < s.boltEnds.size(); ++i) layoutValue(v, a + 0x68 + uint32_t(i) * (1), s.boltEnds[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::tank_shell_state::TankShellFields& s) {
    using namespace od::port::tank_shell_state;
    layoutValue(v, a + 0xbc, s.hitBox);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::throwable_state::ThrowableFields& s) {
    using namespace od::port::throwable_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xca, s.lifeArmed);
    layoutValue(v, a + 0xcb, s.kind);
    layoutValue(v, a + 0xc4, s.shadow);
    layoutValue(v, a + 0xc5, s.contact);
    layoutValue(v, a + 0xc6, s.travelYaw);
    layoutValue(v, a + 0xc8, s.lifeTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::travel_state::TravelIconFields& s) {
    using namespace od::port::travel_state;
    layoutValue(v, a + 0x0b, s.flightPhase);
    layoutValue(v, a + 0x0e, s.startsFlying);
    layoutValue(v, a + 0x0f, s.kind);
    layoutValue(v, a + 0xbc, s.altitude);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::treasure_chest_state::ChestFields& s) {
    using namespace od::port::treasure_chest_state;
    layoutValue(v, a + 0xc, s.group);
    layoutValue(v, a + 0x14c, s.startKey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::turret_bullet_state::BulletTrailEntry& s) {
    using namespace od::port::turret_bullet_state;
    for (size_t i = 0; i < s.step.size(); ++i) layoutValue(v, a + 0x0 + uint32_t(i) * (1), s.step[i]);
    layoutValue(v, a + 0x3, s.depth);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::turret_bullet_state::TurretBulletFields& s) {
    using namespace od::port::turret_bullet_state;
    layoutValue(v, a + 0xc0, s.timer);
    layoutValue(v, a + 0xc2, s.bits);
    layoutValue(v, a + 0xc3, s.trailLength);
    layoutValue(v, a + 0xc4, s.hitBox);
    for (size_t i = 0; i < s.trail.size(); ++i) layoutValue(v, a + 0xcc + uint32_t(i) * (4), s.trail[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::walker_shot_state::WalkerShotFields& s) {
    using namespace od::port::walker_shot_state;
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.life);
    layoutValue(v, a + 0xc5, s.spriteFrame);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::wall_debris_state::DebrisPiece& s) {
    using namespace od::port::wall_debris_state;
    layoutValue(v, a + 0, s.drop);
    layoutValue(v, a + 2, s.radius);
    layoutValue(v, a + 4, s.dropSpeed);
    layoutValue(v, a + 6, s.radiusSpeed);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::wall_debris_state::WallDebrisFields& s) {
    using namespace od::port::wall_debris_state;
    layoutValue(v, a + 0xc, s.poolEntry);
    layoutValue(v, a + 0xc, s.textureList);
    layoutValue(v, a + 0x1c, s.livePieces);
    layoutValue(v, a + 0x20, s.ticks);
    layoutValue(v, a + 0x22, s.floor);
    layoutValue(v, a + 0x24, s.shapePhase);
    for (size_t i = 0; i < s.pieces.size(); ++i) layoutValue(v, a + 0x28 + uint32_t(i) * (8), s.pieces[i]);
}

bool classes_game(LayoutVisitor& v, uint32_t a, ActorClassState& s) {
    if (typeid(s) == typeid(od::port::charge_glow_state::ChargeGlowFields)) { layoutValue(v, a, static_cast<od::port::charge_glow_state::ChargeGlowFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::SlidingLeafFields)) { layoutValue(v, a, static_cast<od::port::SlidingLeafFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::lobbed_shot_state::LobbedShotFields)) { layoutValue(v, a, static_cast<od::port::lobbed_shot_state::LobbedShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::lens_flare_state::ShrinkingDiscFields)) { layoutValue(v, a, static_cast<od::port::lens_flare_state::ShrinkingDiscFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::birdbot_soldier_state::SoldierFields)) { layoutValue(v, a, static_cast<od::port::birdbot_soldier_state::SoldierFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::turret_bullet_state::TurretBulletFields)) { layoutValue(v, a, static_cast<od::port::turret_bullet_state::TurretBulletFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::spark_emitter_state::SparkEmitterFields)) { layoutValue(v, a, static_cast<od::port::spark_emitter_state::SparkEmitterFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::tank_shell_state::TankShellFields)) { layoutValue(v, a, static_cast<od::port::tank_shell_state::TankShellFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::shockwave_state::ShockwaveFields)) { layoutValue(v, a, static_cast<od::port::shockwave_state::ShockwaveFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::lens_flare_state::RayFields)) { layoutValue(v, a, static_cast<od::port::lens_flare_state::RayFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::birdbot_down_state::DownFields)) { layoutValue(v, a, static_cast<od::port::birdbot_down_state::DownFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::SwingLeafFields)) { layoutValue(v, a, static_cast<od::port::SwingLeafFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::VacuumShotFields)) { layoutValue(v, a, static_cast<od::port::VacuumShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::HitSparkFields)) { layoutValue(v, a, static_cast<od::port::HitSparkFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::SparkEmitterParams)) { layoutValue(v, a, static_cast<od::port::SparkEmitterParams&>(s)); return true; }
    if (typeid(s) == typeid(od::port::GlowTrailFields)) { layoutValue(v, a, static_cast<od::port::GlowTrailFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::CastFields)) { layoutValue(v, a, static_cast<od::port::CastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::reaverbot_turret_state::TurretFields)) { layoutValue(v, a, static_cast<od::port::reaverbot_turret_state::TurretFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::DustPuffFields)) { layoutValue(v, a, static_cast<od::port::DustPuffFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::field_hoverbot_state::FieldHoverbotFields)) { layoutValue(v, a, static_cast<od::port::field_hoverbot_state::FieldHoverbotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::lens_flare_state::PointedFields)) { layoutValue(v, a, static_cast<od::port::lens_flare_state::PointedFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ChargeGlowParams)) { layoutValue(v, a, static_cast<od::port::ChargeGlowParams&>(s)); return true; }
    if (typeid(s) == typeid(od::port::SuctionFields)) { layoutValue(v, a, static_cast<od::port::SuctionFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::BurstFields)) { layoutValue(v, a, static_cast<od::port::BurstFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::snow_state::SnowFields)) { layoutValue(v, a, static_cast<od::port::snow_state::SnowFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::FootstepDustFields)) { layoutValue(v, a, static_cast<od::port::FootstepDustFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::lift_box_state::LiftBoxFields)) { layoutValue(v, a, static_cast<od::port::lift_box_state::LiftBoxFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::MessageTriggerFields)) { layoutValue(v, a, static_cast<od::port::MessageTriggerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::StormRibbonFields)) { layoutValue(v, a, static_cast<od::port::StormRibbonFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::PickupFields)) { layoutValue(v, a, static_cast<od::port::PickupFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::status_markers_state::DrainBurnMarkerFields)) { layoutValue(v, a, static_cast<od::port::status_markers_state::DrainBurnMarkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::explosions_state::BlastFields)) { layoutValue(v, a, static_cast<od::port::explosions_state::BlastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::BlizzardLayerFields)) { layoutValue(v, a, static_cast<od::port::BlizzardLayerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::WallCrumbleFields)) { layoutValue(v, a, static_cast<od::port::WallCrumbleFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::snow_reaverbot_state::SnowReaverbotFields)) { layoutValue(v, a, static_cast<od::port::snow_reaverbot_state::SnowReaverbotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::exit_transitions_state::TeleporterSparkFields)) { layoutValue(v, a, static_cast<od::port::exit_transitions_state::TeleporterSparkFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::BirdbotBody)) { layoutValue(v, a, static_cast<od::port::BirdbotBody&>(s)); return true; }
    if (typeid(s) == typeid(od::port::bomb_state::BombFields)) { layoutValue(v, a, static_cast<od::port::bomb_state::BombFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::placed_models_state::PlacedModelFields)) { layoutValue(v, a, static_cast<od::port::placed_models_state::PlacedModelFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::sky_arc_state::SkyArcFields)) { layoutValue(v, a, static_cast<od::port::sky_arc_state::SkyArcFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::lens_flare_state::DroppingRingFields)) { layoutValue(v, a, static_cast<od::port::lens_flare_state::DroppingRingFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::SparkRingParams)) { layoutValue(v, a, static_cast<od::port::SparkRingParams&>(s)); return true; }
    if (typeid(s) == typeid(od::port::throwable_state::ThrowableFields)) { layoutValue(v, a, static_cast<od::port::throwable_state::ThrowableFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::TownNpcFields)) { layoutValue(v, a, static_cast<od::port::TownNpcFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::placed_models_state::TurningModelFields)) { layoutValue(v, a, static_cast<od::port::placed_models_state::TurningModelFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::shockwave_state::ShockRingFxFields)) { layoutValue(v, a, static_cast<od::port::shockwave_state::ShockRingFxFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::sign_state::SignFields)) { layoutValue(v, a, static_cast<od::port::sign_state::SignFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::walker_shot_state::WalkerShotFields)) { layoutValue(v, a, static_cast<od::port::walker_shot_state::WalkerShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::SmokeColumnFields)) { layoutValue(v, a, static_cast<od::port::SmokeColumnFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::field_reaverbot_state::FieldReaverbotFields)) { layoutValue(v, a, static_cast<od::port::field_reaverbot_state::FieldReaverbotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::floating_device_state::FloatingDeviceFields)) { layoutValue(v, a, static_cast<od::port::floating_device_state::FloatingDeviceFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::light_column_state::GlowFields)) { layoutValue(v, a, static_cast<od::port::light_column_state::GlowFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::WaterSplashFields)) { layoutValue(v, a, static_cast<od::port::WaterSplashFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::HurtSparksFields)) { layoutValue(v, a, static_cast<od::port::HurtSparksFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::GunshipFields)) { layoutValue(v, a, static_cast<od::port::GunshipFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::treasure_chest_state::ChestFields)) { layoutValue(v, a, static_cast<od::port::treasure_chest_state::ChestFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::guardian_reaverbot_state::GuardianFields)) { layoutValue(v, a, static_cast<od::port::guardian_reaverbot_state::GuardianFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::RiderFollowerFields)) { layoutValue(v, a, static_cast<od::port::RiderFollowerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::clut_animation_state::ClutAnimationFields)) { layoutValue(v, a, static_cast<od::port::clut_animation_state::ClutAnimationFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::lens_flare_state::PulsingDiscFields)) { layoutValue(v, a, static_cast<od::port::lens_flare_state::PulsingDiscFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::wall_debris_state::WallDebrisFields)) { layoutValue(v, a, static_cast<od::port::wall_debris_state::WallDebrisFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::reaverbot_walker_state::WalkerFields)) { layoutValue(v, a, static_cast<od::port::reaverbot_walker_state::WalkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::travel_state::TravelIconFields)) { layoutValue(v, a, static_cast<od::port::travel_state::TravelIconFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ExplosionFields)) { layoutValue(v, a, static_cast<od::port::ExplosionFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ServbotFields)) { layoutValue(v, a, static_cast<od::port::ServbotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::clut_animation_state::RoomFlagClutFields)) { layoutValue(v, a, static_cast<od::port::clut_animation_state::RoomFlagClutFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::line_of_sight_state::LineOfSightFields)) { layoutValue(v, a, static_cast<od::port::line_of_sight_state::LineOfSightFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::SkidSparkFields)) { layoutValue(v, a, static_cast<od::port::SkidSparkFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::status_markers_state::SlowMarkerFields)) { layoutValue(v, a, static_cast<od::port::status_markers_state::SlowMarkerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::MissileTrailFields)) { layoutValue(v, a, static_cast<od::port::MissileTrailFields&>(s)); return true; }
    return false;
}
} // namespace od::test
