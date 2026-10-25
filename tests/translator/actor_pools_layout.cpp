// The pools' generic layouts and their held class layouts (including free slots).
#include "translator/actor_layout.hpp"
#include <stdexcept>

namespace od::test {
using namespace od::port;
namespace {
template <class Pool> void poolLayout(LayoutVisitor& v, Pool& pool) {
    for (size_t i = 0; i < pool.size(); ++i) layoutValue(v, pool[i].at, pool[i]);
}
template <class Pool> void poolClasses(LayoutVisitor& v, Pool& pool, const ActorHeader* except) {
    for (size_t i = 0; i < pool.size(); ++i) {
        if (&pool[i] == except || heldStateRetired(pool[i])) continue;
        if (auto* s = pool[i].heldClassState()) classLayout(v, pool[i].at, *s);
    }
}
}
void actorPoolsLayout(LayoutVisitor& v) {
    poolLayout(v, shotPool()); poolLayout(v, animatedPool()); poolLayout(v, projectilePool());
    poolLayout(v, staticPool()); poolLayout(v, effectPool()); poolLayout(v, controllerPool());
    layoutValue(v, kPlayer, player());
}
void actorClassesLayout(LayoutVisitor& v, const ActorHeader* except) {
    poolClasses(v, shotPool(), except); poolClasses(v, animatedPool(), except); poolClasses(v, projectilePool(), except);
    poolClasses(v, staticPool(), except); poolClasses(v, effectPool(), except); poolClasses(v, controllerPool(), except);
}
void slotLayout(LayoutVisitor& v, ActorHeader& slot) {
    if (ShotPool::contains(slot.at)) layoutValue(v, slot.at, static_cast<ShotActor&>(slot));
    else if (AnimatedPool::contains(slot.at)) layoutValue(v, slot.at, static_cast<AnimatedActor&>(slot));
    else if (ProjectilePool::contains(slot.at)) layoutValue(v, slot.at, static_cast<ProjectileActor&>(slot));
    else if (StaticPool::contains(slot.at)) layoutValue(v, slot.at, static_cast<StaticActor&>(slot));
    else if (EffectPool::contains(slot.at)) layoutValue(v, slot.at, static_cast<EffectActor&>(slot));
    else if (ControllerPool::contains(slot.at)) layoutValue(v, slot.at, static_cast<ControllerActor&>(slot));
    else throw std::runtime_error("translator: a slot of no pool");
}
ActorHeader* LayoutVisitor::slotAt(uint32_t a) { return &actorHeaderAt(a); }
void LayoutVisitor::badSlot(uint32_t a) { notAnActorSlot(a, 0); }
void LayoutVisitor::field(uint32_t a, CollisionBoxRef& box) {
    if (!interprets(a, 4)) return;
    uint32_t word = storing() && box.own() ? nativePointerWord(a, box.own(), typeid(BoxExtents)) : box.recordAddress();
    bytes(a, &word, 4);
    if (loading()) {
        void* p = nullptr;
        if (resolveNativePointer(a, word, typeid(BoxExtents), p) && p) box.pointTo(*static_cast<BoxExtents*>(p));
        else box.pointAt(word);
    }
}
} // namespace od::test
