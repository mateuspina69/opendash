// The player and its animation/pose records in the exe layout (test-only).
#include "translator/actor_layout.hpp"
#include "game/player/player_state.hpp"

namespace od::test {
using namespace od::port;

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::AnimBlend& s) {
    using namespace od::port;
    layoutValue(v, a + 0, s.targetFrame);
    layoutValue(v, a + 1, s.steps);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::AnimChannel& s) {
    using namespace od::port;
    layoutValue(v, a + 0x0, s.script);
    layoutValue(v, a + 0x4, s.keyAt);
    layoutValue(v, a + 0x8, s.key);
    layoutValue(v, a + 0xc, s.anim);
    layoutValue(v, a + 0xd, s.prevAnim);
    layoutValue(v, a + 0xe, s.frameData);
    layoutValue(v, a + 0xf, s.keyIndex);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::IdleTrack& s) {
    using namespace od::port;
    layoutValue(v, a + 0x0, s.start);
    layoutValue(v, a + 0x4, s.key);
    layoutValue(v, a + 0x8, s.table);
    layoutValue(v, a + 0x9, s.ticks);
    layoutValue(v, a + 0xa, s.count);
    layoutValue(v, a + 0xb, s.pad);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::Player& s) {
    using namespace od::port;
    layoutValue(v, a, static_cast<od::port::Actor&>(s));
    layoutValue(v, a + 0x4e, s.lastTurnStep);
    layoutValue(v, a + 0x94, s.body);
    layoutValue(v, a + 0xa4, s.upper);
    layoutValue(v, a + 0xb4, s.scriptAnim);
    layoutValue(v, a + 0xb5, s.scriptAnimStep);
    layoutValue(v, a + 0xb6, s.invulnerable);
    layoutValue(v, a + 0xb8, s.status);
    layoutValue(v, a + 0xba, s.pulled);
    layoutValue(v, a + 0xbb, s.wadeDepth);
    layoutValue(v, a + 0xbc, s.drag);
    layoutValue(v, a + 0xbd, s.slippery);
    layoutValue(v, a + 0xbe, s.inWater);
    layoutValue(v, a + 0xbf, s.burden);
    layoutValue(v, a + 0xc0, s.channelMix);
    layoutValue(v, a + 0xc1, s.leanLocked);
    layoutValue(v, a + 0xc2, s.inputMode);
    layoutValue(v, a + 0xc4, s.recentreAim);
    layoutValue(v, a + 0xc5, s.lockSuspended);
    layoutValue(v, a + 0xc6, s.jumpBuffered);
    layoutValue(v, a + 0xc7, s.liftMode);
    layoutValue(v, a + 0xc8, s.specialFiring);
    layoutValue(v, a + 0xc9, s.charge);
    layoutValue(v, a + 0xca, s.suction);
    layoutValue(v, a + 0xcc, s.lockTarget);
    layoutValue(v, a + 0xcd, s.softTarget);
    layoutValue(v, a + 0xce, s.lockRemembered);
    layoutValue(v, a + 0xcf, s.softRemembered);
    layoutValue(v, a + 0xd0, s.lockSoundTicks);
    layoutValue(v, a + 0xd1, s.lockReleaseTicks);
    layoutValue(v, a + 0xd2, s.lockTicks);
    layoutValue(v, a + 0xd3, s.lockFlags);
    layoutValue(v, a + 0xd4, s.latchedDirections);
    layoutValue(v, a + 0xd6, s.quickTurnSpeed);
    layoutValue(v, a + 0xd8, s.pushVelocity);
    layoutValue(v, a + 0xe0, s.pullX);
    layoutValue(v, a + 0xe2, s.pullY);
    layoutValue(v, a + 0xe4, s.pullZ);
    layoutValue(v, a + 0xe8, s.pullStrength);
    layoutValue(v, a + 0xea, s.pullRadius);
    layoutValue(v, a + 0xec, s.immunity);
    layoutValue(v, a + 0xed, s.immunityTicks);
    layoutValue(v, a + 0xf0, s.directionOffset);
    layoutValue(v, a + 0xf8, s.ledgePoint);
    layoutValue(v, a + 0x104, s.lean);
    layoutValue(v, a + 0x106, s.leanTarget);
    layoutValue(v, a + 0x108, s.headTurn);
    layoutValue(v, a + 0x10a, s.headTurnTarget);
    layoutValue(v, a + 0x10c, s.padType);
    layoutValue(v, a + 0x10e, s.held);
    layoutValue(v, a + 0x110, s.pressed);
    layoutValue(v, a + 0x112, s.released);
    layoutValue(v, a + 0x114, s.doubleTapped);
    layoutValue(v, a + 0x116, s.repeated);
    layoutValue(v, a + 0x118, s.turnAxis);
    layoutValue(v, a + 0x119, s.lookAxis);
    layoutValue(v, a + 0x11a, s.moveX);
    layoutValue(v, a + 0x11b, s.moveY);
    for (size_t i = 0; i < s.buttonMap.size(); ++i) layoutValue(v, a + 0x11c + uint32_t(i) * (2), s.buttonMap[i]);
    layoutValue(v, a + 0x13c, s.armFlags);
    layoutValue(v, a + 0x13e, s.armStep);
    layoutValue(v, a + 0x140, s.carryFlags);
    layoutValue(v, a + 0x142, s.carryStep);
    layoutValue(v, a + 0x144, s.lockRequest);
    layoutValue(v, a + 0x146, s.lockRequestTicks);
    layoutValue(v, a + 0x148, s.skating);
    layoutValue(v, a + 0x149, s.skateHoldTicks);
    layoutValue(v, a + 0x14a, s.skateSpray);
    layoutValue(v, a + 0x14b, s.skateEngine);
    for (size_t i = 0; i < s.idleTracks.size(); ++i) layoutValue(v, a + 0x14c + uint32_t(i) * (0xc), s.idleTracks[i]);
    layoutValue(v, a + 0x164, s.lockAimPoint);
    layoutValue(v, a + 0x16c, s.screenAimPoint);
    layoutValue(v, a + 0x174, s.lockedActor);
    layoutValue(v, a + 0x178, s.homingTarget);
    layoutValue(v, a + 0x17c, s.softTargetActor);
    for (size_t i = 0; i < s.statusTicks.size(); ++i) layoutValue(v, a + 0x180 + uint32_t(i) * (2), s.statusTicks[i]);
    layoutValue(v, a + 0x188, s.hurtSum);
    layoutValue(v, a + 0x18a, s.hurtCount);
    layoutValue(v, a + 0x18c, s.activeSpecial);
    layoutValue(v, a + 0x18d, s.activeBuster);
    layoutValue(v, a + 0x18e, s.specialWeapon);
    layoutValue(v, a + 0x18f, s.busterWeapon);
    layoutValue(v, a + 0x190, s.energyReserve);
    layoutValue(v, a + 0x192, s.energyPool);
    layoutValue(v, a + 0x194, s.gaugeMax);
    layoutValue(v, a + 0x196, s.gaugeRate);
    layoutValue(v, a + 0x198, s.shotCost);
    layoutValue(v, a + 0x19a, s.gauge);
    layoutValue(v, a + 0x19c, s.busterShotsInFlight);
    layoutValue(v, a + 0x19e, s.weaponMode);
    layoutValue(v, a + 0x19f, s.specialAllowed);
    for (size_t i = 0; i < s.idleFaces.size(); ++i) layoutValue(v, a + 0x1a0 + uint32_t(i) * (1), s.idleFaces[i]);
    layoutValue(v, a + 0x1a2, s.helmetPart);
    layoutValue(v, a + 0x1a3, s.legPart);
    layoutValue(v, a + 0x1a4, s.armourPart);
    for (size_t i = 0; i < s.handShapes.size(); ++i) layoutValue(v, a + 0x1a5 + uint32_t(i) * (1), s.handShapes[i]);
    for (size_t i = 0; i < s.busterParts.size(); ++i) layoutValue(v, a + 0x1a8 + uint32_t(i) * (1), s.busterParts[i]);
    layoutValue(v, a + 0x1ab, s.hitHeightLevel);
    for (size_t i = 0; i < s.ownedParts.size(); ++i) layoutValue(v, a + 0x1ac + uint32_t(i) * (1), s.ownedParts[i]);
    for (size_t i = 0; i < s.unreadPartBytes.size(); ++i) layoutValue(v, a + 0x1cc + uint32_t(i) * (1), s.unreadPartBytes[i]);
    layoutValue(v, a + 0x1d0, s.interactionTarget);
    layoutValue(v, a + 0x1d4, s.solidHit);
    layoutValue(v, a + 0x1e4, s.hitBox);
    for (size_t i = 0; i < s.weaponLevels.size(); ++i) layoutValue(v, a + 0x1f4 + uint32_t(i) * (1), s.weaponLevels[i]);
    layoutValue(v, a + 0x294, s.hiddenBones);
    layoutValue(v, a + 0x296, s.animPaused);
    layoutValue(v, a + 0x298, s.stageAnims);
    layoutValue(v, a + 0x29c, s.stageAnimsExtra);
    layoutValue(v, a + 0x2a4, s.bodyBlend);
    layoutValue(v, a + 0x2a6, s.upperBlend);
    layoutValue(v, a + 0x2a8, s.ambientColour);
    layoutValue(v, a + 0x2ac, s.stageAnimMarker);
    for (size_t i = 0; i < s.bones.size(); ++i) layoutValue(v, a + 0x2b0 + uint32_t(i) * (0x20), s.bones[i]);
    for (size_t i = 0; i < s.bonePoints.size(); ++i) layoutValue(v, a + 0x490 + uint32_t(i) * (8), s.bonePoints[i]);
    layoutValue(v, a + 0xb7, s.padB7);
    layoutValue(v, a + 0xc3, s.padC3);
    layoutValue(v, a + 0xcb, s.padCB);
    layoutValue(v, a + 0xe6, s.pullPad);
    layoutValue(v, a + 0xf6, s.directionPad);
    layoutValue(v, a + 0x13f, s.armStepPad);
    layoutValue(v, a + 0x143, s.carryStepPad);
    layoutValue(v, a + 0x1a7, s.handShapesPad);
    layoutValue(v, a + 0x297, s.animPausedPad);
    for (size_t i = 0; i < s.beforeBlends.size(); ++i) layoutValue(v, a + 0x2a0 + uint32_t(i) * (1), s.beforeBlends[i]);
    int8_t move = int8_t(s.moveDir()), previous = int8_t(s.prevMoveDir());
    v.field(a + 0xee, move); v.field(a + 0xef, previous);
    if (v.loading()) { s.setMoveDir(MoveDir(move)); s.setPrevMoveDir(MoveDir(previous)); }
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::PlayerBonePoint& s) {
    using namespace od::port;
    v.bind(a, static_cast<ShortVec3&>(s));
    v.field(a, static_cast<ShortVec3&>(s));
    layoutValue(v, a + 0x6, s.colour);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::PoseMatrix& s) {
    using namespace od::port;
    for (size_t i = 0; i < s.rotation.size(); ++i) layoutValue(v, a + 0x0 + uint32_t(i) * (2), s.rotation[i]);
    for (size_t i = 0; i < s.translation.size(); ++i) layoutValue(v, a + 0x14 + uint32_t(i) * (4), s.translation[i]);
}

} // namespace od::test
