// The actor classes of content/subruins in the class registry (game/content_registry), registered by
// content/registry.cpp.
#include "content/registry.hpp"

#include "content/subruins/blob_boss.hpp"
#include "content/subruins/blob_boss_shots.hpp"
#include "content/subruins/leaping_critter.hpp"
#include "content/subruins/pickup_eater.hpp"
#include "content/subruins/ruin_blocks.hpp"
#include "content/subruins/subruin_spawners.hpp"

namespace od::content {

using namespace od::port;

void registerSubruinsClasses(ContentRegistry& r) {
    r.addClasses({
        {RoutineId::RuinBlocksUpdate, &ruinBlocksUpdate},
        {RoutineId::FallingShotUpdate, &fallingShotUpdate},
        {RoutineId::LobbedBlobUpdate, &lobbedBlobUpdate},
        {RoutineId::BlobBossUpdate, &blobBossUpdate},
        {RoutineId::PickupEaterUpdate, &pickupEaterUpdate},
        {RoutineId::LeapingCritterUpdate, &leapingCritterUpdate},
        {RoutineId::St4eGroupSpawnerUpdate, &st4eGroupSpawnerUpdate},
        {RoutineId::St50GroupSpawnerUpdate, &st50GroupSpawnerUpdate},
        {RoutineId::SubruinLineTriggerUpdate, &subruinLineTriggerUpdate},
        {RoutineId::SubruinDropSpawnerUpdate, &subruinDropSpawnerUpdate},
    });
}

} // namespace od::content
