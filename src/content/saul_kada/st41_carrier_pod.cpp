// The ST41 / ST42 carrier's pods (static type 0x4f, exe 0x5b6be0; hypothesis names), dropped by the carrier 0x506d10
// (content/saul_kada/st41_carrier: its pod bits, a bit per pod by its variant). States [0x8d0a84]: 0 bind 0x5b6c50, 1 run
// 0x5b6df0 (its sub-state [0x8d0a94], then its hits), 2 dying 0x5b75f0, 3 freed 0x5afdd0. Sub-states: 0 fall 0x5b6e40,
// 1 sway 0x5b6eb0, 2 lunge 0x5b7040, 3 back to the carrier 0x5b73b0. Every tick its glow (0x5b7790) and its hit entry
// (0x5b7550).
#include "content/saul_kada/st41_carrier_pod.hpp"
#include "content/saul_kada/st41_carrier_pod_state.hpp"

#include "content/saul_kada/st41_carrier.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/explosions.hpp"
#include "game/vectors.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st41_carrier_pod_state;

namespace {

enum class PodState : uint8_t { Bind = 0, Run = 1, Dying = 2, Free = 3 };
enum class PodSub : uint8_t { Fall = 0, Sway = 1, Lunge = 2, Return = 3 };
enum class GlowMode : uint8_t { Off = 0, Start = 1, Rise = 2, Hold = 3, Fade = 4 };
constexpr uint16_t kLungeSound = 0x2df;
constexpr uint16_t kTintBlink = 0x4214;

AnimatedActor& carrierOf(const PodFields& pod) {
    if (!pod.carrier) notPorted(kStoryPort, "0x5b6be0: a pod without its carrier (the exe reads [0])");
    return *pod.carrier;
}
uint8_t podBit(const StaticActor& actor) { return uint8_t(1u << (actor.variant & 0x1f)); } // the exe's SHL DL, CL
// exe 0x5b76e0: its velocity less its steps, its position (16.16) plus its velocity << 12
void podMove(StaticActor& actor) {
    actor.velocity.x = int16_t(uint16_t(actor.velocity.x) - uint16_t(actor.acceleration.x));
    actor.velocity.y = int16_t(uint16_t(actor.velocity.y) - uint16_t(actor.acceleration.y));
    actor.velocity.z = int16_t(uint16_t(actor.velocity.z) - uint16_t(actor.acceleration.z));
    actor.pos.x = int32_t(uint32_t(actor.pos.x) + (uint32_t(int32_t(actor.velocity.x)) << 12));
    actor.pos.y = int32_t(uint32_t(actor.pos.y) + (uint32_t(int32_t(actor.velocity.y)) << 12));
    actor.pos.z = int32_t(uint32_t(actor.pos.z) + (uint32_t(int32_t(actor.velocity.z)) << 12));
}
// exe 0x5b7740: still, its steps (the offset << 5) / t / t
void podLaunch(StaticActor& actor, int dx, int dy, int dz, int t) {
    actor.velocity.z = 0;
    actor.velocity.y = 0;
    actor.velocity.x = 0;
    actor.acceleration.x = int16_t((int32_t(uint32_t(dx) << 5) / t) / t);
    actor.acceleration.y = int16_t((int32_t(uint32_t(dy) << 5) / t) / t);
    actor.acceleration.z = int16_t((int32_t(uint32_t(dz) << 5) / t) / t);
}
// 8 ticks of braking: its steps its velocity / 8
void brakeIn8(StaticActor& actor, PodFields& pod) {
    pod.timer = 8;
    actor.acceleration.x = int16_t(actor.velocity.x >> 3);
    actor.acceleration.y = int16_t(actor.velocity.y >> 3);
    actor.acceleration.z = int16_t(actor.velocity.z >> 3);
}
bool moveTick(StaticActor& actor, PodFields& pod) { // exe 0x5b76e0, then its timer out?
    podMove(actor);
    pod.timer -= 1u;
    return pod.timer == 0;
}
// exe 0x5b6e40: falling until its z speed changes sign; then anchored here, the sway
void fall(StaticActor& actor, PodFields& pod) {
    int dy = actor.acceleration.y + actor.velocity.y;
    int dz = actor.acceleration.z + actor.velocity.z;
    if ((uint32_t(dz) ^ uint32_t(int32_t(actor.velocity.z))) & 0x80000000u) {
        dy = 0;
        pod.anchorX = actor.pos.x.whole;
        pod.anchorY = actor.pos.y.whole;
        dz = 0;
        actor.subState = uint8_t(PodSub::Sway);
        actor.restartStep();
        pod.anchorZ = actor.pos.z.whole;
    }
    actor.velocity.y = int16_t(dy);
    actor.velocity.z = int16_t(dz);
    integrateVelocity(actor, 0, dy, dz); // exe 0x5e3690
}
// exe 0x5b6eb0: a sine bob around its anchor; at a cycle's end maybe the lunge (by the live pods' chances)
void sway(StaticActor& actor, PodFields& pod) {
    AnimatedActor& carrier = carrierOf(pod);
    PodCarrierFields& bits = classState<PodCarrierFields>(carrier);
    const uint8_t step = actor.step;
    if (step == 0) {
        actor.step = 1;
        pod.amplitude = uint8_t((crtRand() & 0xf) + 0x10u);
        const uint32_t r = crtRand();
        pod.phase = 0;
        actor.pos.x.whole = pod.anchorX;
        actor.pos.z.whole = pod.anchorZ;
        actor.spin.z = int16_t(0x1000 / int32_t((r & 0x3f) + 0x2f));
        actor.pos.y.whole = pod.anchorY;
        actor.spin.y = int16_t((int32_t(carrier.rotation.y) - int32_t(actor.rotation.y)) >> 3);
        actor.spin.x = int16_t((-int32_t(actor.rotation.x)) >> 3);
    }
    if (step == 0 || step == 1) {
        pod.phase = int16_t(uint16_t(uint16_t(pod.phase) + uint16_t(actor.spin.z)));
        if (bits.podsSignalled != 0) {
            actor.subState = uint8_t(PodSub::Return);
            actor.restartStep();
            return;
        }
        if (pod.phase > 0xfc0) {
            const uint32_t row = dataScalar<uint8_t>(stateAddress(TableId::St41CarrierPodLungeClasses) + bits.pods);
            if (dataScalar<uint8_t>(stateAddress(TableId::St41CarrierPodLungeChances) + row + (crtRand() & 7)) == 0) {
                pod.phase = 0;
                actor.step = 0;
            } else {
                actor.restartStep();
                ++actor.subState;
            }
        }
    }
    if (uint16_t(carrier.rotation.y) != uint16_t(actor.rotation.y))
        actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + uint16_t(actor.spin.y)));
    if (actor.rotation.x != 0) actor.rotation.x = int16_t(uint16_t(uint16_t(actor.rotation.x) + uint16_t(actor.spin.x)));
    const int k = (int32_t(fixedSin(uint16_t(pod.phase) & 0xfff)) * int32_t(pod.amplitude)) >> 12;
    int ey = k, ex = k;
    switch (actor.variant) {
    case 0: ey = -k; ex = -k; break;
    case 1: ex = -k; break;
    case 2: ey = -k; break;
    default: break;
    }
    actor.pos.y.whole = int16_t(uint16_t(pod.anchorY) + uint16_t(ey));
    if (uint16_t(carrier.rotation.y) & 0x400) actor.pos.z.whole = int16_t(uint16_t(pod.anchorZ) - uint16_t(ex));
    else actor.pos.x.whole = int16_t(uint16_t(pod.anchorX) - uint16_t(ex));
}
int clamp60(int v) { return v < -0x60 ? -0x60 : (v > 0x60 ? 0x60 : v); }
// exe 0x5b7040 (its jump table 0x5b7384): the lunge at the player (aimed near him along the carrier's facing; too far
// along it: given up, signalled), the glow, braking, back to its anchor
void lunge(StaticActor& actor, PodFields& pod) {
    AnimatedActor& carrier = carrierOf(pod);
    PodCarrierFields& bits = classState<PodCarrierFields>(carrier);
    switch (actor.step) {
    case 0:
        ++actor.step;
        [[fallthrough]];
    case 1:
        ++actor.step;
        [[fallthrough]];
    case 2: {
        ++actor.step;
        int d = planarDistance(actor.pos, player().pos);
        if (d >= 0x400) d = 0x3ff;
        const uint32_t t = dataScalar<uint16_t>(stateAddress(TableId::St41CarrierPodLungeTicks) + uint32_t(d >> 6) * 2);
        pod.timer = uint8_t(t);
        pod.amplitude = uint8_t(t);
        const int y = actor.pos.y.whole + 0x6e;
        const Player& p = player();
        int dx = actor.pos.x.whole - p.pos.x.whole;
        int dz = actor.pos.z.whole - p.pos.z.whole;
        bool off = false; // the player too far along the carrier's facing
        const uint16_t yaw = uint16_t(carrier.rotation.y);
        if (!(yaw & 0x400)) {
            dx = clamp60(dx + int(crtRand() & 0x7f) - 0x40);
            if (!(uint16_t(carrier.rotation.y) & 0x800)) {
                if (dz <= -0x40) dz = -0x40;
                else if (dz > 0xa00) off = true;
            } else {
                if (dz >= 0x40) dz = 0x40;
                else if (dz < -0xa00) off = true;
            }
        } else {
            dz = clamp60(int(crtRand() & 0x7f) + dz - 0x40);
            if (!(uint16_t(carrier.rotation.y) & 0x800)) {
                if (dx <= -0x40) dx = -0x40;
                else if (dx > 0xa00) off = true;
            } else {
                if (dx >= 0x40) dx = 0x40;
                else if (dx < -0xa00) off = true;
            }
        }
        if (off) {
            ++actor.subState;
            actor.restartStep();
            bits.podsSignalled |= podBit(actor);
            return;
        }
        podLaunch(actor, dx, y, dz, int(t)); // exe 0x5b7740
        actor.spin.z = actor.acceleration.z;
        actor.spin.x = actor.acceleration.x;
        actor.spin.y = actor.acceleration.y;
        pod.glowMode = uint8_t(GlowMode::Start);
        soundPlayAtPos(kLungeSound, actor.pos);
        [[fallthrough]];
    }
    case 3:
        if (!moveTick(actor, pod)) return;
        ++actor.step;
        brakeIn8(actor, pod);
        return;
    case 4:
        if (!moveTick(actor, pod)) return;
        ++actor.step;
        return;
    case 5:
        ++actor.step;
        pod.timer = pod.amplitude;
        actor.velocity.z = 0;
        actor.velocity.y = 0;
        actor.velocity.x = 0;
        actor.acceleration.x = int16_t(-int32_t(actor.spin.x));
        actor.acceleration.y = int16_t(-int32_t(actor.spin.y));
        actor.acceleration.z = int16_t(-int32_t(actor.spin.z));
        [[fallthrough]];
    case 6:
        if (!moveTick(actor, pod)) return;
        ++actor.step;
        brakeIn8(actor, pod);
        pod.glowMode = uint8_t(GlowMode::Fade);
        return;
    case 7:
        if (!moveTick(actor, pod)) return;
        --actor.subState;
        actor.restartStep();
        actor.pos.x.whole = pod.anchorX;
        actor.pos.y.whole = pod.anchorY;
        actor.pos.z.whole = pod.anchorZ;
        return;
    default: return;
    }
}
// exe 0x5b73b0: tilting (by its variant), signalling the carrier, then (all signalled) homing in on its carrier bone
void returnHome(StaticActor& actor, PodFields& pod) {
    AnimatedActor& carrier = carrierOf(pod);
    PodCarrierFields& bits = classState<PodCarrierFields>(carrier);
    switch (actor.step) {
    case 0:
        switch (actor.variant) { // its jump table 0x5b7538
        case 0:
            actor.spin.x = int16_t(0xffc0);
            actor.spin.y = 0x80;
            actor.acceleration.y = 0xa;
            actor.acceleration.z = 0xa;
            break;
        case 1:
            actor.spin.x = 0x40;
            actor.spin.y = 0x80;
            actor.acceleration.y = int16_t(0xfff6);
            actor.acceleration.z = 0xa;
            break;
        case 2:
            actor.spin.x = int16_t(0xffc0);
            actor.spin.y = int16_t(0xff80);
            actor.acceleration.y = 0xa;
            actor.acceleration.z = 0xa;
            break;
        case 3:
            actor.spin.x = 0x40;
            actor.spin.y = int16_t(0xff80);
            actor.acceleration.y = int16_t(0xfff6);
            actor.acceleration.z = 0xa;
            break;
        default: break;
        }
        actor.step = 1;
        pod.timer = 8;
        [[fallthrough]];
    case 1:
        actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + uint16_t(actor.spin.y)));
        actor.rotation.x = int16_t(uint16_t(uint16_t(actor.rotation.x) + uint16_t(actor.spin.x)));
        pod.timer -= 1u;
        if (pod.timer != 0) return;
        ++actor.step;
        bits.podsSignalled |= podBit(actor);
        pod.timer = 0xa;
        return;
    case 2: {
        if (bits.pods != bits.podsSignalled) return;
        const ShortVec4& dock = bonePoint(carrier, uint32_t(actor.variant) + 1, "0x5b73b0: the carrier's dock bone"); // 0x5e1810
        const int c = pod.timerSigned;
        int16_t* lanes[3] = {&actor.pos.x.whole, &actor.pos.y.whole, &actor.pos.z.whole};
        const int16_t target[3] = {dock.x, dock.y, dock.z};
        for (uint32_t k = 0; k < 3; ++k)
            *lanes[k] = int16_t(uint16_t(uint32_t((int32_t(target[k]) - int32_t(*lanes[k])) / c) + uint16_t(*lanes[k])));
        pod.timer -= 1u;
        if (pod.timer != 0) return;
        bits.podsBack |= podBit(actor);
        actor.enterState(uint8_t(PodState::Free));
        return;
    }
    default: return;
    }
}
// exe 0x5b75f0: off the carrier's live pods, an explosion, 0x14 ticks blinking, freed
void dying(StaticActor& actor, PodFields& pod) {
    actor.tint = kTintFlash;
    switch (actor.subState) {
    case 0:
        actor.subState = 1;
        pod.glowMode = uint8_t(GlowMode::Off);
        [[fallthrough]];
    case 1: {
        PodCarrierFields& bits = classState<PodCarrierFields>(carrierOf(pod));
        bits.pods &= uint8_t(~podBit(actor));
        actor.velocity.x = 0;
        ++actor.subState;
        actor.velocity.y = 0;
        actor.velocity.z = 0;
        pod.timer = 0x14;
        spawnDeathExplosionAt(actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole, 0x50,
                              uint16_t(int32_t(pod.timerSigned))); // exe 0x5e1300
        [[fallthrough]];
    }
    case 2:
        if (pod.timer & 1) actor.tint = kTintBlink;
        pod.timer -= 1u;
        if (pod.timer == 0) freeActor(actor); // exe 0x5e1540
        return;
    default: return;
    }
}
// exe 0x5b7790(actor, its block): its glow's level by its mode, a ring 0x8c0 behind it
void glow(const StaticActor& actor, PodFields& pod) {
    switch (GlowMode(pod.glowMode)) { // its jump table 0x5b787c
    case GlowMode::Off: return;
    case GlowMode::Start:
        pod.glowMode += 1u;
        pod.glowLevel = 0;
        break;
    case GlowMode::Rise:
        pod.glowLevel += 1u;
        if (pod.glowLevel == 7) pod.glowMode += 1u;
        break;
    case GlowMode::Fade:
        pod.glowLevel -= 1u;
        if (pod.glowLevel == 0) {
            pod.glowMode = 0;
            pod.glowLevel = 0;
        }
        break;
    default: break;
    }
    // its position 0x8c0 behind along its yaw (exe 0x5e36f0 on the exe's stack copy)
    int32_t d[3];
    rotateByYaw(actor.rotation.y, 0, 0, -0x8c0, d); // exe 0x5e38d0
    const int32_t moved[3] = {int32_t(uint32_t(actor.pos.x) + uint32_t(d[0])), int32_t(uint32_t(actor.pos.y) + uint32_t(d[1])),
                              int32_t(uint32_t(actor.pos.z) + uint32_t(d[2]))};
    dc::Ring r;
    r.arc = dc::kRingFullArc;
    r.inner = dc::kRingOpcode;
    r.xy = dc::packXY(int16_t(moved[0] >> 16), int16_t(moved[1] >> 16));
    r.outer = ((uint32_t(int32_t(pod.glowLevel)) * 0x1010fu) | 0x11000000u) << 1;
    r.zStep = dc::zWord(int16_t(moved[2] >> 16)) | dc::ringStep(1);
    r.radii = dc::ringRadii(0x80, 0);
    dc::emit(r, actor.at);
}
// exe 0x5b7550: its hit entry 0x640 ahead
void submitHit(const StaticActor& actor, PodFields& pod) {
    pod.hitBox.result = 0;
    pod.hitBox.previousX = actor.prevPos.x.whole;
    pod.hitBox.previousZ = actor.prevPos.z.whole;
    int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    moveAlongYaw16(p, actor.rotation.y, 0, 0, -0x640); // exe 0x5e3750
    hitListSubmit(p, pod.hitBox,
                  hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | hitHeightLevel(7) | hitRadius(0x6c),
                  uint32_t(staticStats(actor.type, 0).damage) | kHitAttackSolid); // exe 0x5e4120
}
void runSub(StaticActor& actor, PodFields& pod) { // [0x8d0a94]
    switch (PodSub(actor.subState)) {
    case PodSub::Fall: fall(actor, pod); return;
    case PodSub::Sway: sway(actor, pod); return;
    case PodSub::Lunge: lunge(actor, pod); return;
    case PodSub::Return: returnHome(actor, pod); return;
    default: notPortedAt(kStoryPort, "0x5b6df0: sub-state past [0x8d0a94]", actor.subState);
    }
}
// exe 0x5b6df0: its sub-state, then its hits
void run(StaticActor& actor, PodFields& pod) {
    runSub(actor, pod);
    const uint32_t damage = pod.hitBox.result & kHitDamageMask;
    if (damage == 0) return;
    actor.tint = kTintFlash;
    if (hitPointsAdd(actor, -int(damage))) actor.enterState(uint8_t(PodState::Dying)); // exe 0x5e52f0
}
// exe 0x5b6c50: at its carrier's dock bone, tilted and launched by its variant
void bind(StaticActor& actor, PodFields& pod) {
    AnimatedActor& carrier = carrierOf(pod);
    if (!bindActorModel(actor, kAnyModelVariant)) { // exe 0x5e0bc0(actor, 0)
        freeActor(actor);                           // exe 0x5e1540
        return;
    }
    actor.flags |= kActorDrawn;
    actor.enterState(uint8_t(PodState::Run));
    actor.collisionBox.pointAt(TableId::St41CarrierPodBox);
    const uint16_t hp =
        hasStaticStats(actor.type) ? uint16_t(uint32_t(staticStats(actor.type, 0).hitPoints) << 2) : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    pod.clearBytes(); // exe 0x40dc40
    pod.hitBox.previousX = actor.pos.x.whole;
    pod.hitBox.previousZ = actor.pos.z.whole;
    pod.hitBox.result = 0;
    const ShortVec4& dock = bonePoint(carrier, uint32_t(actor.variant) + 1, "0x5b6c50: the carrier's dock bone"); // 0x5e1810
    actor.pos.x.whole = dock.x;
    actor.pos.y.whole = dock.y;
    actor.pos.z.whole = dock.z;
    const uint8_t v = actor.variant;
    if (v <= 3) { // its jump table 0x5b6dd8
        actor.rotation.x = int16_t((v & 1) ? 0x200 : 0xfe00);
        actor.rotation.y = int16_t(uint16_t(uint16_t(carrier.rotation.y) + (v < 2 ? 0x400u : 0xfc00u)));
        actor.rotation.z = 0x55;
        actor.velocity.x = 0;
        actor.velocity.z = int16_t(0xff60);
        actor.acceleration.z = 0xa;
        if (v & 1) {
            actor.velocity.y = 0xa0;
            actor.acceleration.y = int16_t(0xfff6);
        } else {
            actor.velocity.y = int16_t(0xff60);
            actor.acceleration.y = 0xa;
        }
    }
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    run(actor, pod);
}
void carrierPod(StaticActor& actor) { // exe 0x5b6be0
    PodFields& pod = classState<PodFields>(actor);
    actor.tint = kTintNeutral;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (PodState(actor.state)) { // [0x8d0a84]
    case PodState::Bind: bind(actor, pod); break;
    case PodState::Run: run(actor, pod); break;
    case PodState::Dying: dying(actor, pod); break;
    case PodState::Free: freeActor(actor); break; // exe 0x5e1540
    default: notPortedAt(kStoryPort, "0x5b6be0: state past [0x8d0a84]", actor.state);
    }
    glow(actor, pod);      // exe 0x5b7790
    submitHit(actor, pod); // exe 0x5b7550
}

} // namespace

void carrierPodUpdate(const PlayerEnv&, uint32_t actor) { carrierPod(staticActorAt(actor)); }

} // namespace od::port
