// The binding of an actor to its model: see actor_model.hpp.
#include "game/actor_model.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/model_data.hpp"
#include "game/port_gap.hpp"
#include "game/stage_state.hpp"
#include "game/state_array.hpp"

namespace od::port {

namespace {

constexpr uint8_t kModelColourTables = 0x01;
constexpr uint8_t kModelOption = 0x02;

// the colour tables of the model, or kNoColourTable each (and kActorColourTables on the actor when it has them)
void bindColourTables(Actor& actor, StateArray<uint32_t, 3>& tables, const ModelHeaderData& header, uint32_t bank) {
    if (!(header.flags & kModelColourTables)) {
        for (size_t k = 0; k < tables.size(); ++k) tables[k] = kNoColourTable;
    } else {
        actor.flags |= kActorColourTables;
        for (size_t k = 0; k < tables.size(); ++k) tables[k] = header.colourTables[k] + bank;
    }
    if (header.flags & kModelOption) actor.flags |= kActorModelOption;
}

// the mesh pools (0x10 / 0x40 / 0x60)
void bindMeshModel(MeshActor& actor, const ModelHeaderData& header, uint32_t bank) {
    actor.model = header.at;
    actor.extraGeometry = header.extraGeometry + bank;
    actor.collisionMesh.pointAt(header.collisionMesh + bank);
    bindColourTables(actor, actor.colourTables, header, bank);
    actor.cullWidth = int16_t(header.cullWidth >> 2);
    actor.cullHeight = int16_t(header.cullHeight >> 2);
    actor.scale.x = header.scale.x;
    actor.scale.y = header.scale.y;
    actor.scale.z = header.scale.z;
    actor.resetTintAndScale();
}

// the animated pool (0x20): tables, part table, bone blocks (exe 0x5e16c0), scale, texture cells
void bindAnimatedModel(AnimatedActor& actor, const ModelBankEntryData& entry, const ModelHeaderData& header, uint32_t bank) {
    actor.model = header.at;
    actor.frameTable.pointAt(entry.frameTable + bank);
    actor.animTable.pointAt(entry.animTable + bank);
    actor.skeleton.pointAt(header.skeleton + bank);
    actor.setModelCounts(header.counts);
    actor.extraGeometry = header.extraGeometry + bank;
    actor.collisionMesh.pointAt(header.collisionMesh + bank);
    const uint32_t parts = header.parts + bank;
    for (size_t k = 0; k < actor.boneSlotCount; ++k) { // exe 0x40dbe0: boneSlotCount x 4 bytes
        auto&& to = actor.parts[k];
        const ModelPartData from = modelPart(parts, k);
        to.pointSlot = from.pointSlot;
        to.parentSlot = from.parentSlot;
        to.boneSlot = from.boneSlot;
        to.flags = from.flags;
    }
    actor.partCount = uint8_t(modelPart(parts, 0).parentSlot);
    bindColourTables(actor, actor.colourTables, header, bank);
    actor.boneBlocks = 0;
    allocBoneBlocks(actor, uint32_t(((int32_t(actor.partCount) - 1) >> 2) + 1));
    actor.cullWidth = int16_t(header.cullWidth >> 2);
    actor.cullHeight = int16_t(header.cullHeight >> 2);
    actor.scale.x = header.scale.x;
    actor.scale.y = header.scale.y;
    actor.scale.z = header.scale.z;
    actor.resetRootScale();
    actor.tint = kTintNeutral;
    const uint32_t cell = header.textureCell;
    actor.texturePage = ((cell & 0x7f) << 1 | (cell & 0x7f00)) << 15;
    actor.secondTexturePage = ((cell >> 1) & 0x3f800000u) | (cell & 0x7f0000u);
}

} // namespace

bool bindActorModel(Actor& actor, int variant) { // exe 0x5e0be0
    const uint32_t bank = stage().modelBank;
    const FileDataReader records = loadedFile(bank, 4);
    const int32_t count = records.read<int32_t>(0);
    const uint32_t cls = actorSlotClass(actor.slot);
    const uint32_t globalFlag = (~uint32_t(actor.loopFlags) & kActorStageClass) >> 3;
    for (int32_t i = 0; i < count; ++i) {
        const auto entry = FileDataRef<ModelBankEntryData>(records.sub(4 + size_t(i) * 16)).get();
        if (cls != (entry.key & 0xf0u) || globalFlag != (entry.key & 0xfu) || actor.type != entry.type) continue;
        if (variant != kAnyModelVariant && uint8_t(variant) != entry.variant) continue;
        auto header = FileDataRef<ModelHeaderData>(loadedFile(entry.model + bank, 56)).get();
        header.at = entry.model + bank;
        if (cls == kAnimatedClass) header.textureCell = loadedScalar<uint32_t>(header.at + 56);
        switch (cls) {
        case kShotClass:
        case kProjectileClass:
            actor.flags |= kActorMeshModel;
            actor.runFlags |= kActorMeshModel;
            if (cls == kShotClass) bindMeshModel(asShot(actor), header, bank);
            else bindMeshModel(asProjectile(actor), header, bank);
            return true;
        case kStaticClass: bindMeshModel(asStatic(actor), header, bank); return true;
        case kAnimatedClass: bindAnimatedModel(asAnimated(actor), entry, header, bank); return true;
        default: return true;
        }
    }
    if (cls == kAnimatedClass) asAnimated(actor).boneBlocks = 0;
    return false;
}

void resetModelScale(AnimatedActor& actor) {
    actor.scale.x = kModelScaleOne;
    actor.scale.y = kModelScaleOne;
    actor.scale.z = kModelScaleOne;
}

void setTexturePageCell(uint32_t& word, uint32_t n) {
    const uint32_t v = word;
    const uint32_t w = (v >> 16) & 0x7f, h = (v >> 23) & 0x7f;
    if (w == 0 || h == 0)
        notPorted("actor model", "exe 0x5e32c0: an empty texture cell size (the exe divides by zero)");
    const uint32_t perRow = 0x100 / w, perPage = (0x100 / h) * perRow;
    uint32_t page = n / perPage;
    if (page > 3) page = 3;
    const uint32_t rem = n % perPage;
    word = ((((rem / perRow) * h) | (page << 22)) << 8) | ((rem % perRow) * w) | (v & 0x3fff0000u);
}

void setTexturePageTexel(uint32_t& word, uint32_t u, uint32_t v, uint32_t page) {
    word = (((page << 22) | (v & 0xff)) << 8) | (word & 0x3fff0000u) | (u & 0xff);
}

} // namespace od::port
