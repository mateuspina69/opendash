// The native records of this area in the exe layout (test-only).
#include "translator/actor_layout.hpp"
#include "content/forbidden_island/elephant_boss_state.hpp"
#include "content/forbidden_island/island_visit_state.hpp"
#include "content/forbidden_island/st10_cls_state.hpp"
#include "content/forbidden_island/st11_boss_state.hpp"
#include "content/forbidden_island/st11_script2a_state.hpp"
#include "content/forbidden_island/st1c_cast_state.hpp"
#include "content/forbidden_island/st3e_reports_state.hpp"
#include "content/forbidden_island/st49_dropship_state.hpp"

namespace od::test {
using namespace od::port;

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::elephant_boss_state::BossFields& s) {
    using namespace od::port::elephant_boss_state;
    layoutValue(v, a + 0x158, s.flags);
    layoutValue(v, a + 0x168, s.deaths);
    layoutValue(v, a + 0x169, s.groupIndex);
    layoutValue(v, a + 0x16a, s.repeats);
    layoutValue(v, a + 0x14c, s.body);
    layoutValue(v, a + 0x154, s.tuskBlock);
    if (v.loading()) layoutValue(v, a + 0x158, s.flagsLow);
    layoutValue(v, a + 0x15c, s.distance);
    layoutValue(v, a + 0x160, s.counter);
    layoutValue(v, a + 0x164, s.playerAngle);
    layoutValue(v, a + 0x166, s.targetYaw);
    if (s.tuskBlock) layoutValue(v, s.tuskBlock, s.tuskBytes);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::elephant_boss_state::BoulderFields& s) {
    using namespace od::port::elephant_boss_state;
    layoutValue(v, a + 0xbc, s.hit);
    layoutValue(v, a + 0xc4, s.timer);
    layoutValue(v, a + 0xc6, s.ringRadius);
    layoutValue(v, a + 0xc8, s.aimWord);
    layoutValue(v, a + 0xce, s.spareWord);
    layoutValue(v, a + 0xd0, s.gait);
    layoutValue(v, a + 0xd1, s.gaitFlags);
    layoutValue(v, a + 0xc8, s.aimPitch);
    layoutValue(v, a + 0xca, s.aimYaw);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xcc + uint32_t(i) * (1), s.clearedPad0[i]);
    for (size_t i = 0; i < s.clearedPad1.size(); ++i) layoutValue(v, a + 0xd2 + uint32_t(i) * (1), s.clearedPad1[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::elephant_boss_state::CrystalFields& s) {
    using namespace od::port::elephant_boss_state;
    layoutValue(v, a + 0xbc, s.hit);
    layoutValue(v, a + 0xc4, s.homeY);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::elephant_boss_state::DustRingFields& s) {
    using namespace od::port::elephant_boss_state;
    layoutValue(v, a + 0x18, s.radius);
    layoutValue(v, a + 0x1c, s.shrink);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::elephant_boss_state::ExitLightFields& s) {
    using namespace od::port::elephant_boss_state;
    layoutValue(v, a + 0xc0, s.originZ);
    layoutValue(v, a + 0xbc, s.originX);
    layoutValue(v, a + 0xbe, s.originY);
    layoutValue(v, a + 0xc4, s.curtainX);
    layoutValue(v, a + 0xc6, s.columnStep);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::elephant_boss_state::PillarFields& s) {
    using namespace od::port::elephant_boss_state;
    layoutValue(v, a + 0x2e, s.group);
    layoutValue(v, a + 0xbc, s.hit);
    layoutValue(v, a + 0xc4, s.debrisYaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::elephant_boss_state::RockFields& s) {
    using namespace od::port::elephant_boss_state;
    layoutValue(v, a + 0xc, s.mode);
    layoutValue(v, a + 0xd, s.attack);
    layoutValue(v, a + 0x2e, s.lifetime);
    layoutValue(v, a + 0xbc, s.hit);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::elephant_boss_state::TrailFields& s) {
    using namespace od::port::elephant_boss_state;
    layoutValue(v, a + 0x28, s.oldest);
    layoutValue(v, a + 0x2a, s.next);
    layoutValue(v, a + 0xc, s.owner);
    for (size_t i = 0; i < s.xs.size(); ++i) layoutValue(v, a + 0x2c + uint32_t(i) * (2), s.xs[i]);
    for (size_t i = 0; i < s.ys.size(); ++i) layoutValue(v, a + 0x4c + uint32_t(i) * (2), s.ys[i]);
    for (size_t i = 0; i < s.zs.size(); ++i) layoutValue(v, a + 0x6c + uint32_t(i) * (2), s.zs[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::elephant_boss_state::TuskBlock& s) {
    using namespace od::port::elephant_boss_state;
    layoutValue(v, a + 0x0, s.hit);
    layoutValue(v, a + 0x8, s.stagger);
    layoutValue(v, a + 0xa, s.pitchOffset);
    for (size_t i = 0; i < s.unread.size(); ++i) layoutValue(v, a + 0xc + uint32_t(i), s.unread[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::island_visit_state::PackFields& s) {
    using namespace od::port::island_visit_state;
    layoutValue(v, a + 0x18, s.tickWord);
    layoutValue(v, a + 0x1c, s.spareWord);
    layoutValue(v, a + 0x18, s.tick);
    layoutValue(v, a + 0x1a, s.delayIndex);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::island_visit_state::RecordPointFields& s) {
    using namespace od::port::island_visit_state;
    layoutValue(v, a + 0x16, s.yaw);
    layoutValue(v, a + 0x10, s.point);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st10_cls_state::RollerFields& s) {
    using namespace od::port::st10_cls_state;
    layoutValue(v, a + 0xc, s.wobble);
    layoutValue(v, a + 0xd, s.groupBit);
    layoutValue(v, a + 0xe, s.farTicks);
    layoutValue(v, a + 0x46, s.lifetime);
    layoutValue(v, a + 0x166, s.flags);
    layoutValue(v, a + 0x167, s.hitCount);
    layoutValue(v, a + 0x168, s.hitMode);
    layoutValue(v, a + 0x169, s.groupIndex);
    layoutValue(v, a + 0x16a, s.groupBits);
    layoutValue(v, a + 0x14c, s.hit);
    layoutValue(v, a + 0x15c, s.pushResult);
    layoutValue(v, a + 0x160, s.neighbour);
    layoutValue(v, a + 0x162, s.heading);
    layoutValue(v, a + 0x164, s.timer);
    layoutValue(v, a + 0x16b, s.humTicks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st11_boss_state::FighterFields& s) {
    using namespace od::port::st11_boss_state;
    layoutValue(v, a + 0x166, s.flags);
    layoutValue(v, a + 0x167, s.wall);
    layoutValue(v, a + 0x168, s.hopSide);
    layoutValue(v, a + 0x169, s.hopPhase);
    layoutValue(v, a + 0x16a, s.hopApex);
    layoutValue(v, a + 0x2e, s.strafeYaw);
    layoutValue(v, a + 0xe6, s.tintLow);
    layoutValue(v, a + 0xe7, s.tintHigh);
    layoutValue(v, a + 0x14c, s.hit);
    layoutValue(v, a + 0x15c, s.distance);
    layoutValue(v, a + 0x160, s.knockbackAngle);
    layoutValue(v, a + 0x162, s.timer);
    layoutValue(v, a + 0x164, s.timeout);
    layoutValue(v, a + 0x16b, s.dead);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st11_script2a_state::ArrivalCastFields& s) {
    using namespace od::port::st11_script2a_state;
    layoutValue(v, a + 0x154, s.animating);
    layoutValue(v, a + 0x155, s.shadow);
    layoutValue(v, a + 0x156, s.jumpPhase);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st1c_cast_state::CueBlock& s) {
    constexpr uint32_t kClassBlock = od::port::kAnimatedClassBlockOffset; // the class block (+0x14c)
    using namespace od::port::st1c_cast_state;
    layoutValue(v, a + kClassBlock + 0x13, s.holdPose);
    layoutValue(v, a + kClassBlock + 0, s.tracks);
    for (size_t i = 0; i < s.keyCursors.size(); ++i) layoutValue(v, a + kClassBlock + 0x10 + uint32_t(i) * (1), s.keyCursors[i]);
    layoutValue(v, a + kClassBlock + 0x12, s.startKey);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st1c_cast_state::GlowBlock& s) {
    constexpr uint32_t kClassBlock = od::port::kAnimatedClassBlockOffset; // the class block (+0x14c)
    using namespace od::port::st1c_cast_state;
    layoutValue(v, a + kClassBlock + 0x16, s.startKey);
    layoutValue(v, a + kClassBlock + 0, s.tracks);
    layoutValue(v, a + kClassBlock + 0x10, s.glow);
    for (size_t i = 0; i < s.keyCursors.size(); ++i) layoutValue(v, a + kClassBlock + 0x14 + uint32_t(i) * (1), s.keyCursors[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st1c_cast_state::RisingPropBlock& s) {
    constexpr uint32_t kClassBlock = od::port::kAnimatedClassBlockOffset; // the class block (+0x14c)
    using namespace od::port::st1c_cast_state;
    layoutValue(v, a + kClassBlock + 0, s.bobPhase);
    layoutValue(v, a + kClassBlock + 2, s.baseY);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st1c_cast_state::ShipFields& s) {
    constexpr uint32_t kClassBlock = od::port::kStaticClassBlockOffset; // the class block (+0xbc)
    using namespace od::port::st1c_cast_state;
    layoutValue(v, a + kClassBlock + 4, s.baseY);
    layoutValue(v, a + kClassBlock + 0, s.flare);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st3e_reports_state::SpeakerFields& s) {
    using namespace od::port::st3e_reports_state;
    layoutValue(v, a + 0xc, s.animRequest);
    layoutValue(v, a + 0xd, s.yawOffset);
    layoutValue(v, a + 0xe, s.turnRequest);
    layoutValue(v, a + 0xf, s.faceRequest);
    layoutValue(v, a + 0x14c, s.holdPose);
    layoutValue(v, a + 0x150, s.targetYaw);
    layoutValue(v, a + 0x152, s.turnStep);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st49_dropship_state::CableFields& s) {
    using namespace od::port::st49_dropship_state;
    layoutValue(v, a + 0x18, s.dropship);
    layoutValue(v, a + 0x1c, s.cargo);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st49_dropship_state::CargoFields& s) {
    using namespace od::port::st49_dropship_state;
    layoutValue(v, a + 0x14c, s.dropship);
    layoutValue(v, a + 0x158, s.hangDepth);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::st49_dropship_state::DropshipFields& s) {
    using namespace od::port::st49_dropship_state;
    layoutValue(v, a + 0x14c, s.counter);
}

bool classes_forbidden_island(LayoutVisitor& v, uint32_t a, ActorClassState& s) {
    if (typeid(s) == typeid(od::port::elephant_boss_state::PillarFields)) { layoutValue(v, a, static_cast<od::port::elephant_boss_state::PillarFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st1c_cast_state::GlowBlock)) { layoutValue(v, a, static_cast<od::port::st1c_cast_state::GlowBlock&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st1c_cast_state::CueBlock)) { layoutValue(v, a, static_cast<od::port::st1c_cast_state::CueBlock&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st1c_cast_state::RisingPropBlock)) { layoutValue(v, a, static_cast<od::port::st1c_cast_state::RisingPropBlock&>(s)); return true; }
    if (typeid(s) == typeid(od::port::elephant_boss_state::CrystalFields)) { layoutValue(v, a, static_cast<od::port::elephant_boss_state::CrystalFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::elephant_boss_state::RockFields)) { layoutValue(v, a, static_cast<od::port::elephant_boss_state::RockFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st1c_cast_state::ShipFields)) { layoutValue(v, a, static_cast<od::port::st1c_cast_state::ShipFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st3e_reports_state::SpeakerFields)) { layoutValue(v, a, static_cast<od::port::st3e_reports_state::SpeakerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::elephant_boss_state::BoulderFields)) { layoutValue(v, a, static_cast<od::port::elephant_boss_state::BoulderFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::island_visit_state::PackFields)) { layoutValue(v, a, static_cast<od::port::island_visit_state::PackFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st11_script2a_state::ArrivalCastFields)) { layoutValue(v, a, static_cast<od::port::st11_script2a_state::ArrivalCastFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::island_visit_state::RecordPointFields)) { layoutValue(v, a, static_cast<od::port::island_visit_state::RecordPointFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st11_boss_state::FighterFields)) { layoutValue(v, a, static_cast<od::port::st11_boss_state::FighterFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::elephant_boss_state::ExitLightFields)) { layoutValue(v, a, static_cast<od::port::elephant_boss_state::ExitLightFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::elephant_boss_state::TrailFields)) { layoutValue(v, a, static_cast<od::port::elephant_boss_state::TrailFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::elephant_boss_state::DustRingFields)) { layoutValue(v, a, static_cast<od::port::elephant_boss_state::DustRingFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st10_cls_state::RollerFields)) { layoutValue(v, a, static_cast<od::port::st10_cls_state::RollerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st49_dropship_state::CargoFields)) { layoutValue(v, a, static_cast<od::port::st49_dropship_state::CargoFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::elephant_boss_state::BossFields)) { layoutValue(v, a, static_cast<od::port::elephant_boss_state::BossFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st49_dropship_state::CableFields)) { layoutValue(v, a, static_cast<od::port::st49_dropship_state::CableFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::st49_dropship_state::DropshipFields)) { layoutValue(v, a, static_cast<od::port::st49_dropship_state::DropshipFields&>(s)); return true; }
    return false;
}
} // namespace od::test
