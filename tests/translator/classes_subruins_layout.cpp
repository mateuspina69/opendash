// The native records of this area in the exe layout (test-only).
#include "translator/actor_layout.hpp"
#include "content/subruins/blob_boss_shots_state.hpp"
#include "content/subruins/blob_boss_state.hpp"
#include "content/subruins/leaping_critter_state.hpp"
#include "content/subruins/pickup_eater_state.hpp"
#include "content/subruins/ruin_blocks_state.hpp"
#include "content/subruins/subruin_spawners_state.hpp"

namespace od::test {
using namespace od::port;

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::blob_boss_shots_state::FallingShotFields& s) {
    using namespace od::port::blob_boss_shots_state;
    layoutValue(v, a + 0xbc, s.hit);
    layoutValue(v, a + 0xc4, s.ticks);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::blob_boss_shots_state::LobbedBlobFields& s) {
    using namespace od::port::blob_boss_shots_state;
    layoutValue(v, a + 0xbc, s.hit);
    layoutValue(v, a + 0xc4, s.ticks);
    layoutValue(v, a + 0xc8, s.landed);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::blob_boss_state::BlobFields& s) {
    using namespace od::port::blob_boss_state;
    layoutValue(v, a + 0xc, s.red);
    layoutValue(v, a + 0xd, s.green);
    layoutValue(v, a + 0xe, s.blue);
    layoutValue(v, a + 0xf, s.pulse);
    layoutValue(v, a + 0x158, s.flags);
    layoutValue(v, a + 0x14c, s.prop);
    layoutValue(v, a + 0x150, s.clutEffect);
    layoutValue(v, a + 0x154, s.work);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.counter);
    layoutValue(v, a + 0x160, s.aimWord);
    layoutValue(v, a + 0x164, s.topYaw);
    layoutValue(v, a + 0x166, s.topPitch);
    layoutValue(v, a + 0x168, s.aimDistance);
    layoutValue(v, a + 0x16a, s.reach);
    layoutValue(v, a + 0x160, s.aimYaw);
    layoutValue(v, a + 0x162, s.aimPitch);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::blob_boss_state::BlobFightBlock& s) {
    using namespace od::port::blob_boss_state;
    layoutValue(v, a + 0x00, s.bodyHit);
    layoutValue(v, a + 0x08, s.reachHit);
    layoutValue(v, a + 0x10, s.stats);
    layoutValue(v, a + 0x14, s.bodyYaw);
    layoutValue(v, a + 0x16, s.wanderYaw);
    layoutValue(v, a + 0x18, s.queuedDamage);
    layoutValue(v, a + 0x1a, s.damageTaken);
    layoutValue(v, a + 0x1c, s.height);
    layoutValue(v, a + 0x1e, s.baseHeight);
    layoutValue(v, a + 0x20, s.topHeight);
    layoutValue(v, a + 0x22, s.lastAimYaw);
    layoutValue(v, a + 0x24, s.shots);
    layoutValue(v, a + 0x26, s.ringRounds);
    layoutValue(v, a + 0x28, s.farTicks);
    layoutValue(v, a + 0x2a, s.phaseTicks);
    layoutValue(v, a + 0x2c, s.messageTicks);
    layoutValue(v, a + 0x2e, s.messageStep);
    for (size_t i = 0; i < s.unread.size(); ++i) layoutValue(v, a + 0x2f + uint32_t(i) * (1), s.unread[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::leaping_critter_state::CritterFields& s) {
    using namespace od::port::leaping_critter_state;
    layoutValue(v, a + 0x3e, s.pathLimit);
    layoutValue(v, a + 0x3e, s.turnRate);
    layoutValue(v, a + 0x46, s.waddlePhase);
    layoutValue(v, a + 0x160, s.bits);
    layoutValue(v, a + 0x166, s.cooldown);
    layoutValue(v, a + 0x167, s.hits);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.targetYaw);
    layoutValue(v, a + 0x161, s.contact);
    layoutValue(v, a + 0x162, s.nearPatience);
    layoutValue(v, a + 0x164, s.visiblePatience);
    layoutValue(v, a + 0x168, s.pathCursor);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::pickup_eater_state::EaterFields& s) {
    using namespace od::port::pickup_eater_state;
    layoutValue(v, a + 0x160, s.bits);
    layoutValue(v, a + 0x168, s.target);
    layoutValue(v, a + 0x169, s.loopPhase);
    layoutValue(v, a + 0x14c, s.hitBox);
    layoutValue(v, a + 0x15c, s.timer);
    layoutValue(v, a + 0x15e, s.kept);
    layoutValue(v, a + 0x161, s.contact);
    layoutValue(v, a + 0x162, s.loopAngle);
    layoutValue(v, a + 0x164, s.centreX);
    layoutValue(v, a + 0x166, s.centreZ);
    layoutValue(v, a + 0x16a, s.loopYaw);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::ruin_blocks_state::BlockFields& s) {
    using namespace od::port::ruin_blocks_state;
    layoutValue(v, a + 0x42, s.floorY);
    layoutValue(v, a + 0x46, s.timer);
    layoutValue(v, a + 0xbc, s.hitBox);
    layoutValue(v, a + 0xc4, s.group);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0xc8 + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::subruin_spawners_state::DropSpawnerFields& s) {
    using namespace od::port::subruin_spawners_state;
    layoutValue(v, a + 0x10, s.point);
    layoutValue(v, a + 0x18, s.drop);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::subruin_spawners_state::GroupSpawnerFields& s) {
    using namespace od::port::subruin_spawners_state;
    layoutValue(v, a + 0x18, s.recordActor);
    for (size_t i = 0; i < s.clearedPad0.size(); ++i) layoutValue(v, a + 0x1c + uint32_t(i) * (1), s.clearedPad0[i]);
}

void layoutValue(LayoutVisitor& v, uint32_t a, od::port::subruin_spawners_state::LineTriggerFields& s) {
    using namespace od::port::subruin_spawners_state;
    layoutValue(v, a + 0x10, s.x);
    layoutValue(v, a + 0x14, s.z);
    for (size_t i = 0; i < s.ownBytes.size(); ++i) layoutValue(v, a + 0x18 + uint32_t(i) * (1), s.ownBytes[i]);
}

bool classes_subruins(LayoutVisitor& v, uint32_t a, ActorClassState& s) {
    if (typeid(s) == typeid(od::port::leaping_critter_state::CritterFields)) { layoutValue(v, a, static_cast<od::port::leaping_critter_state::CritterFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::ruin_blocks_state::BlockFields)) { layoutValue(v, a, static_cast<od::port::ruin_blocks_state::BlockFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::subruin_spawners_state::DropSpawnerFields)) { layoutValue(v, a, static_cast<od::port::subruin_spawners_state::DropSpawnerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::subruin_spawners_state::GroupSpawnerFields)) { layoutValue(v, a, static_cast<od::port::subruin_spawners_state::GroupSpawnerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::subruin_spawners_state::LineTriggerFields)) { layoutValue(v, a, static_cast<od::port::subruin_spawners_state::LineTriggerFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::blob_boss_shots_state::LobbedBlobFields)) { layoutValue(v, a, static_cast<od::port::blob_boss_shots_state::LobbedBlobFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::pickup_eater_state::EaterFields)) { layoutValue(v, a, static_cast<od::port::pickup_eater_state::EaterFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::blob_boss_shots_state::FallingShotFields)) { layoutValue(v, a, static_cast<od::port::blob_boss_shots_state::FallingShotFields&>(s)); return true; }
    if (typeid(s) == typeid(od::port::blob_boss_state::BlobFields)) { layoutValue(v, a, static_cast<od::port::blob_boss_state::BlobFields&>(s)); return true; }
    return false;
}
} // namespace od::test
