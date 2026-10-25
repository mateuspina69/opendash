// The actors' state: the native slots of the pools, the
// classes' own state they keep, the references between actors, and the records the actor routines read through them
// (collision boxes, animation keys, bone blocks, hit boxes).
//
//   ActorHeader          the 16 bytes every slot starts with (all six pools) and the class state
//   Actor                + the body of the model-bearing pools (0x10 shots, 0x20 animated, 0x40 projectiles,
//                          0x60 statics) and of the player: position, rotation, velocities, collision, root matrix
//   AnimatedActor        pool 0x20: + animation, model tables, skeleton, scale, part table
//   MeshActor            the model fields of the mesh pools (0x10 shots, 0x40 projectiles, 0x60 statics):
//                          model, collision mesh, colour tables, scale, tint
//   StaticActor          pool 0x60, ShotActor pool 0x10, ProjectileActor pool 0x40: MeshActor + the fields their
//                          classes share
//   EffectActor          pool 0xa0: ActorHeader + an s16 position
//   ControllerActor      pool 0xe0: ActorHeader + a spawn record's point
//
// The slots are objects that live from the boot to the end (game/actor_pools); an allocation resets the fields the
// exe resets and keeps the others, as the exe does. A class's own fields are its class state (ActorClassState, in the
// file of that class), held by the slot. A reference to another actor is an ActorRef.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <typeinfo>

#include "engine/data_source.hpp"
#include "game/field_alias.hpp"
#include "game/loaded_data.hpp"
#include "game/matrix.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port {

struct ActorHeader;
struct Actor;
struct AnimatedActor;
struct StaticActor;
struct MeshActor;
struct ShotActor;
struct ProjectileActor;
struct EffectActor;
struct ControllerActor;

struct BoneBlock;

// The resolvers of a MemPtr to a slot kept by a state still in the exe's layout (game/actor_pools.cpp): the slot (or the
// player) at that address; nullptr for 0; an address that is no slot of the kind's pools throws ResolutionError.
ActorHeader* viewAt(std::type_identity<ActorHeader>, uint32_t address);
Actor* viewAt(std::type_identity<Actor>, uint32_t address);
AnimatedActor* viewAt(std::type_identity<AnimatedActor>, uint32_t address);
StaticActor* viewAt(std::type_identity<StaticActor>, uint32_t address);
ShotActor* viewAt(std::type_identity<ShotActor>, uint32_t address);
ProjectileActor* viewAt(std::type_identity<ProjectileActor>, uint32_t address);
EffectActor* viewAt(std::type_identity<EffectActor>, uint32_t address);
ControllerActor* viewAt(std::type_identity<ControllerActor>, uint32_t address);
BoneBlock* viewAt(std::type_identity<BoneBlock>, uint32_t address);

// ---- flags -----------------------------------------------------------------------------------------------------
// +0 (flags)
constexpr uint8_t kActorLive = 0x01;       // the slot is in use (+0); with +1 bit 0: the update loops run it
constexpr uint8_t kActorDrawn = 0x02;      // drawn (with +1 bit 1); the actor collision queues need +0 & 3 != 0
constexpr uint8_t kActorQueued = kActorLive | kActorDrawn; // the actor collision queues take the actor (exe 0x414050)
constexpr uint8_t kActorLiveDrawn = kActorLive | kActorDrawn; // a slot in use and drawn (a spawned actor's +0)
constexpr uint8_t kActorScenery = 0x40;    // set by scenery statics (the ST1A movers, a sign variant, the ST09 statics
                                           // 2..4) for the draw pass (hypothesis: drawn as fixed scenery)
constexpr uint8_t kActorPoseOnRoot = 0x80; // the pose multiplies the rotation by the previous root matrix +0x74
// +1 (runFlags)
constexpr uint8_t kActorRun = 0x01;        // updated by the pool loop (0 = frozen, e.g. during a mission's message)
constexpr uint8_t kActorShown = 0x02;      // drawn by the draw pass (with +0 & kActorDrawn); a new slot gets both
constexpr uint8_t kActorWarpKept = 0x08;   // kept through an in-stage warp, moved by its offset (exe 0x5e08d0 frees the
                                           // others); the effect / shot classes set it in their first state (and some
                                           // spawners)
constexpr uint8_t kActorRunMarked = 0x10;     // set by some enemy classes at their spawn (its reader is not traced)
constexpr uint8_t kActorOffScreen = 0x20;  // the encounter's keep test found it off the screen (game/encounter)
constexpr uint8_t kActorBoneScale = 0x40;  // the pose scales each bone by its bone-block scale bytes
// +3 (loopFlags)
constexpr uint8_t kActorSweepGroup1 = 0x01; // +3 & 3: the group the stage's sweep frees (exe 0x5e05d0)
constexpr uint8_t kActorSweepGroup2 = 0x02;
constexpr uint8_t kActorLateLoop = 0x04;   // updated by the late pool loops (after the camera)
constexpr uint8_t kActorStageClass = 0x08; // the class comes from the stage's class table (else the global one)

// model flags of +0 (exe 0x5e0be0 sets them from the bound model)
constexpr uint8_t kActorMeshModel = 0x04;     // a shot / projectile with a mesh model (also set in +1)
constexpr uint8_t kActorColourTables = 0x08;  // the model has its own colour tables (model header +3 & 1)
constexpr uint8_t kActorModelOption = 0x10;   // the model header's option bit (+3 & 2; its use is not traced)

// The animated pool's tint word +0xe6 / the mesh pools' +0xb6 (RGB555 multiplier: 0x4210 = neutral)
constexpr uint16_t kTintNeutral = 0x4210;
constexpr uint16_t kTintFlash = 0x7fff; // the white flash of a hit
constexpr uint16_t kTintSpent = 0x190c; // dimmed: a body without hit points (the Birdbots' remains, a downed one)
constexpr uint8_t kScaleByteOne = 0x40; // a scale byte of 1.0
// the draw's semi-transparency bits as exe 0x5e1a00 / 0x5e1a70(actor, on, mode) store them (the animated pool's +0xeb,
// the mesh pools' +0xbb): ((on & 1) << 4 | (mode & 3)) << 1; mode 1 = additive
constexpr uint8_t semiTransparencyBits(uint32_t on, uint32_t mode) {
    return uint8_t(((on & 1) << 4 | (mode & 3)) << 1);
}
// a colour-table word of a model without colour tables
constexpr uint32_t kNoColourTable = 0x84333333;

// ---- records the slots point at -----------------------------------------------------------------------------------
// A collision box a class keeps in its own state (the blast: the actor's box pointer names it)
struct BoxExtents : DataRecord {
    static constexpr uint32_t kBytes = 12;
    int16_t minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
    template <class R> static BoxExtents load(const R& r) {
        return {{}, r.s16(0), r.s16(2), r.s16(4), r.s16(6), r.s16(8), r.s16(0xa)};
    }
    // the box in the exe's layout (a patched record of the data: game/patched_data)
    void save(std::span<uint8_t> out) const {
        const int16_t v[6] = {minX, maxX, minY, maxY, minZ, maxZ};
        for (size_t i = 0; i < 6; ++i) {
            out[2 * i] = uint8_t(uint16_t(v[i]));
            out[2 * i + 1] = uint8_t(uint16_t(v[i]) >> 8);
        }
    }
};
// One extent of the box an actor's collision box pointer names: a lane of a box record of the game's data (read
// through the data pointer) or of a class's own box (the only boxes the game writes)
class BoxLane {
public:
    BoxLane(uint32_t record, int16_t* own) : record_(record), own_(own) {}
    BoxLane(const BoxLane&) = default;
    int16_t get() const { return own_ ? *own_ : dataScalar<int16_t>(record_); }
    operator int16_t() const { return get(); }
    BoxLane& operator=(int16_t v) {
        if (!own_) throw std::logic_error("a write into a box record of the game's data (only a class's own box is written)");
        *own_ = v;
        return *this;
    }
    BoxLane& operator=(const BoxLane& o) { return *this = o.get(); }

private:
    uint32_t record_;
    int16_t* own_;
};
struct BoxAccess {
    BoxLane minX, maxX, minY, maxY, minZ, maxZ;
};
// What an actor's collision box pointer names: a box record of the game's data (its address in the loaded data), a box
// a class keeps in its own state, or none
class CollisionBoxRef {
public:
    CollisionBoxRef() = default;
    // a box record of the game's data (its address; 0: none)
    static CollisionBoxRef record(uint32_t address) {
        CollisionBoxRef r;
        r.record_ = address;
        return r;
    }
    void pointAt(uint32_t address) {
        record_ = address;
        own_ = nullptr;
    }
    // the box record of the table `id` of the data
    void pointAt(TableId id) { pointAt(stateAddress(id)); }
    void pointTo(BoxExtents& own) {
        own_ = &own;
        record_ = 0;
    }
    CollisionBoxRef& operator=(std::nullptr_t) {
        record_ = 0;
        own_ = nullptr;
        return *this;
    }
    explicit operator bool() const { return record_ != 0 || own_ != nullptr; }
    bool operator==(std::nullptr_t) const { return !*this; }
    // the record's address (0: none or an own box) / the own box (null: none or a record)
    uint32_t recordAddress() const { return record_; }
    BoxExtents* own() const { return own_; }
    // the box's lanes, read and written where they live
    BoxAccess operator*() const {
        if (own_) return {{0, &own_->minX}, {0, &own_->maxX}, {0, &own_->minY}, {0, &own_->maxY}, {0, &own_->minZ},
                          {0, &own_->maxZ}};
        return {{record_, nullptr}, {record_ + 2, nullptr}, {record_ + 4, nullptr}, {record_ + 6, nullptr},
                {record_ + 8, nullptr}, {record_ + 0xa, nullptr}};
    }
    struct Arrow {
        BoxAccess access;
        BoxAccess* operator->() { return &access; }
    };
    Arrow operator->() const { return {**this}; }

private:
    uint32_t record_ = 0;
    BoxExtents* own_ = nullptr;
};

// A class's box as a table of the data source (its typed record). The actor stores the box's address (+0x58 / +0x5c:
// DataTable::stateAddress) until the native state; the collision and the interaction queries read it through that.
struct CollisionBoxRecord : DataRecord {
    static constexpr uint32_t kBytes = 12;
    int16_t minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
    template <class R> static CollisionBoxRecord load(const R& r) {
        return {{}, r.s16(0), r.s16(2), r.s16(4), r.s16(6), r.s16(8), r.s16(0xa)};
    }
};
struct InteractionBoxRecord : DataRecord {
    static constexpr uint32_t kBytes = 12;
    int16_t centreX = 0, centreY = 0, centreZ = 0, reach = 0, minHeight = 0, maxHeight = 0;
    template <class R> static InteractionBoxRecord load(const R& r) {
        return {{}, r.s16(0), r.s16(2), r.s16(4), r.s16(6), r.s16(8), r.s16(0xa)};
    }
};
// The result box an actor hands the hit list with its entry (exe 0x5e4120 records it; the hit pass of game/hit_list
// writes the result into it): the head every submitter writes ...
struct HitBoxHead {
    uint32_t result = 0;    // damage (& 0xfff) | the other entry's flags; cleared every submit
    int16_t previousX = 0;  // the owner's previous x / z (copied into the entry)
    int16_t previousZ = 0;
};
// ... and the contact output, for the entries whose shape asks for one (the others keep their own bytes there)
struct HitBox : HitBoxHead {
    ShortVec3 contact;      // the contact point (contact mode 0x8000: x = angle, y = distance)
    int16_t contactKind = 0;

    // the contact-motion output (x = the angle, y = the distance) as the exe writes it: the angle's dword (its high
    // half cleared), then the distance ORed into the high half
    void setContactMotion(int angle, int distance) {
        contact.x = int16_t(uint16_t(uint32_t(angle)));
        contact.y = int16_t(uint16_t(uint32_t(distance)));
    }
};

// An animation script key {s8 frame, u8 ticks, u8 events, u8 flags}: the actor's copy of the current key of the
// stage's animation bank (the bank's own keys: game/animation_data AnimationKeyData)
struct AnimKey {
    int8_t frame = 0;   // the frame shown (index in the frame data)
    uint8_t ticks = 0;  // the key's ticks (on the actor's copy: ticks left)
    uint8_t events = 0; // the key's event bits (the player's footsteps / sounds; 1 = a landing frame)
    uint8_t flags = 0;  // kKeyJump | target key, kKeyHold, kKeyBlend | blend steps (& 0xf)

    void copyFrom(const AnimKey& o) { *this = o; }
};
constexpr uint8_t kKeyJump = 0x80;      // the last key of a sequence: jump to key flags & 0x7f
constexpr uint8_t kKeyHold = 0xff;      // ... or hold the current frame
constexpr uint8_t kKeyBlendMask = 0x90; // (flags & 0x90) == kKeyBlend: blend toward the next different frame
constexpr uint8_t kKeyBlend = 0x10;

// One part of an animated model's part table
struct PartEntry {
    uint8_t pointSlot = 0;  // the bone-block point the part's position goes to
    int8_t parentSlot = 0;  // the slot whose matrix / point the part hangs from
    uint8_t boneSlot = 0;   // its matrix slot = decoded angle entry = skeleton offset index
    uint8_t flags = 0;      // kPartHidden
};
constexpr uint8_t kPartHidden = 0x80; // the part is not drawn

// A bone block of the animated pool (0xb0 bytes, 32 at 0xa72660, given to the actors by the mask 0xa6bb90): four
// bones' matrices (9 x s16 + pad; +0x14..+0x1f of each unused by the pose), their points (s16 x / y / z, world
// coordinates already << stage shift; the 4th short is the bone's colour, which the pose copies with the point) and
// their scale bytes (x / y / z)
// the colour of a bone (RGB555 multiplier): the spare lane of its point, as the u16 the classes write
using BoneColour = FieldAlias<uint16_t, int16_t>;
struct BoneBlock {
    uint32_t at = 0; // the block's exe address: its identity (a class's bytes keep it)
    StateArray<Matrix3, 4> matrices{};                    // +0x00 + k * 0x20
    StateArray<int16_t, 4> matrixPads{};                  // +0x12 of each matrix slot
    StateArray<StateArray<uint32_t, 3>, 4> matrixTails{}; // +0x14..+0x1f of each slot (unused by the pose)
    StateArray<ShortVec4, 4> points{};                    // +0x80 + k * 8
    StateArray<StateArray<uint8_t, 3>, 4> scaleBytes{};   // +0xa0 + k * 4
    StateArray<uint8_t, 4> semi{};                        // +0xa3 + k * 4: the part's semi-transparency bits

    // the colour of bone k (RGB555 multiplier, kTintNeutral when allocated): the spare lane of its point
    BoneColour colour(size_t k) { return BoneColour(points[k].spare); }
    // ... its value (a read-only lookup)
    uint16_t boneColour(size_t k) const { return uint16_t(points[k].spare); }
    // the semi-transparency bits of the part on bone k (the byte after its scale bytes; the model draw's per-part mode)
    uint8_t semiBits(size_t k) const { return semi[k]; }
    // bone k's scale bytes and semi-transparency bits as one dword (the exe's dword store)
    void setScaleWord(size_t k, uint32_t v) {
        scaleBytes[k] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16)};
        semi[k] = uint8_t(v >> 24);
    }
    // the semi-transparency bits of the part on bone k set (the exe's byte store)
    void setSemiBits(size_t k, uint8_t v) { semi[k] = v; }
    // dword i of bone k's matrix slot (0x20 bytes: the matrix, its padding, the unused words; the copies of the slot
    // whole)
    uint32_t matrixWord(size_t k, size_t i) const {
        const Matrix3& m = matrices[k];
        if (i < 4) return uint32_t(uint16_t(m.e[2 * i])) | uint32_t(uint16_t(m.e[2 * i + 1])) << 16;
        if (i == 4) return uint32_t(uint16_t(m.e[8])) | uint32_t(uint16_t(matrixPads[k])) << 16;
        return matrixTails[k][i - 5];
    }
    // the exe address of bone k's point (the callers that hand it on as the exe does: a class's parameter word)
    uint32_t pointAddress(size_t k) const { return at + 0x80 + uint32_t(k) * 8; }
};

// An area of one of its bone blocks a class keeps its own bytes in (its work area, its hit boxes): the exe keeps the
// area's address in the class's bytes (kept here as that value, for the exe's layout; 0: none) and the bytes are the
// class's own (here, natively)
template <class T> class BoneBlockArea {
public:
    void pointAt(uint32_t address) { address_ = address; }
    BoneBlockArea& operator=(std::nullptr_t) {
        address_ = 0;
        return *this;
    }
    uint32_t address() const { return address_; }
    explicit operator bool() const { return address_ != 0; }
    T& operator*() { return checked(); }
    const T& operator*() const { return const_cast<BoneBlockArea*>(this)->checked(); }
    T* operator->() { return &checked(); }
    const T* operator->() const { return &const_cast<BoneBlockArea*>(this)->checked(); }

private:
    T& checked() {
        if (address_ == 0) throw std::runtime_error("a class's bone-block area read before it was set (the exe reads address 0)");
        return bytes_;
    }
    uint32_t address_ = 0;
    T bytes_{};
};

// A solid's collision mesh (exe data, *(+0xb8) of an animated actor / *(+0x9c) of a static): u32 count, then count
// volumes {s16 x0, x1, z0, z1, y0, y1; u16 shape; u16 layer mask} relative to the solid (or its bone)
struct MeshVolume : DataRecord {
    int16_t x0 = 0, x1 = 0, z0 = 0, z1 = 0, y0 = 0, y1 = 0;
    uint16_t shape = 0;     // < 0x100: the volume's push-out shape
    uint16_t layerMask = 0; // the mover layers it stops
    template <class R> static MeshVolume load(const R& r) {
        return {{}, r.s16(0), r.s16(2), r.s16(4), r.s16(6), r.s16(8), r.s16(0xa), r.u16(0xc), r.u16(0xe)};
    }
};
struct CollisionMesh : DataRecord {
    static constexpr uint32_t kMaxVolumes = 0x100;
    static constexpr uint32_t kVolumeBytes = 0x10;
    static constexpr uint32_t kBytes = 4 + kMaxVolumes * kVolumeBytes;
    uint32_t count = 0;
    DataReader bytes;
    // volume i (i < count)
    MeshVolume volume(size_t i) const { return MeshVolume::load(bytes.sub(4 + i * kVolumeBytes)); }
    static CollisionMesh load(const DataReader& r) { return {{}, r.u32(0), r}; }
};
// (a model's frame / animation offset tables and its skeleton: game/model_data, game/animation_data)
constexpr uint32_t kBonesPerBlock = 4;

// ---- references between actors ---------------------------------------------------------------------------------
// A reference to a slot of a pool (or to the player): what an actor, a script or a system keeps of another actor (the
// exe keeps the slot's address; the test translator converts). Null when it refers to none.
template <class V> class ActorRef {
public:
    ActorRef() = default;
    ActorRef(std::nullptr_t) {}
    template <class U>
        requires std::is_convertible_v<U*, V*>
    ActorRef(U* p) : p_(p) {}
    // ... of a slot read through a const reference (the slot is the pool's: the reference names the same slot)
    template <class U>
        requires std::is_convertible_v<U*, V*>
    ActorRef(const U* p) : p_(const_cast<U*>(p)) {}
    template <class U>
        requires std::is_convertible_v<U*, V*>
    ActorRef(const ActorRef<U>& o) : p_(o.get()), foreign_(o.foreignWord()) {}
    // a null reference whose word in the exe's layout names no slot (another occupant's bytes the field lies over: the
    // test translator keeps the word until the game writes the field)
    static ActorRef foreign(uint32_t word) {
        ActorRef r;
        r.foreign_ = word;
        return r;
    }
    uint32_t foreignWord() const { return foreign_; }

    V* get() const { return p_; }
    V* operator->() const { return p_; }
    V& operator*() const { return *p_; }
    explicit operator bool() const { return p_ != nullptr; }
    template <class U> bool operator==(const ActorRef<U>& o) const { return p_ == o.get(); }
    bool operator==(const V* v) const { return p_ == v; }
    bool operator==(std::nullptr_t) const { return p_ == nullptr; }

private:
    V* p_ = nullptr;
    uint32_t foreign_ = 0;
};

// ---- the class state ------------------------------------------------------------------------------------------------
// A word of its slot's head (+0x8..+0xb, +0xc..+0xf) where a class keeps a pointer of its own instead of the header's
// bytes: the exe keeps the pointer in those bytes, so a reader of the slot's bytes as the exe lays them out (the rider
// walk past the static pool, game/carry) takes the pointer there
struct HeadPointer {
    uint32_t at = 0;    // the word's offset in the slot (0: none -- the header's bytes are the exe's)
    bool known = false; // the exe's word is `word` (an actor slot's identity word, or 0: null); else the pointer is the
                        // address of a field of the game state or of a slot: never a slot's start, its byte 2 never 0
    uint32_t word = 0;

    // a pointer to an actor slot (its identity word; 0: null)
    static HeadPointer toSlot(uint32_t at, uint32_t identity) { return {at, true, identity}; }
    // a pointer to a field (null: the word 0)
    static HeadPointer toField(uint32_t at, const void* field) { return {at, field == nullptr, 0}; }
};
constexpr uint32_t kHeadStateWord = 0x8, kHeadParamsWord = 0xc; // the head words a class may keep a pointer in

// A class's own state, kept by its slot (in the exe: the slot's bytes past the shared layout, which each class lays out
// its own way). Each class declares its struct deriving from ActorClassState, constructed from the slot (a class state
// names the slot's shared fields it uses by reference); the slot holds the state of the class that last asked for one
// (classState<T>). Its members start at zero: the read map proves no class reads a byte of
// its tail it did not write in its lifetime, except the fields the slots keep for them (the slots' persistent
// members below).
struct ActorClassState {
    virtual ~ActorClassState() = default;
    // exe 0x40dc40 over the slot's class block: the members in the cleared range zeroed
    virtual void clearBlock() {}
    // the head word the class keeps a pointer in (none: the header's bytes are the exe's)
    virtual HeadPointer headPointer() const { return {}; }
    // ... the pointer cleared (the exe's dword store of 0 over that word)
    virtual void clearHeadPointer() {}
};

// What the test translator (tests/translator) is told of the class states, so that the exe's memory it keeps in the
// exe's layout follows them (nothing in the game observes them): a state created for a slot (the previous one, if any,
// given before it is destroyed), a slot's class block cleared, a slot's occupant ending its lifetime while its class
// state is current (an allocation of the slot, before it clears anything: the exe's slot keeps the bytes the class
// wrote, the shared fields over them included), every allocation of a slot (its new occupant's lifetime begins), and
// a held state taken by the class of a new occupant (the class asked for the kind the slot held)
class ClassStateObserver {
public:
    virtual ~ClassStateObserver() = default;
    virtual void created(ActorHeader& slot, ActorClassState* previous, ActorClassState& state) = 0;
    virtual void resumed(ActorHeader& slot, ActorClassState& state) = 0;
    virtual void blockCleared(ActorHeader& slot) = 0;
    // ... part of it: the slot's bytes [begin, end) (offsets in the slot)
    virtual void blockBytesCleared(ActorHeader& slot, uint32_t begin, uint32_t end) = 0;
    virtual void lifetimeEnds(ActorHeader& slot, ActorClassState& state) = 0;
    virtual void lifetimeBegins(ActorHeader& slot) = 0;
};
// the observer (none: nullptr)
ClassStateObserver* classStateObserver();
void setClassStateObserver(ClassStateObserver* observer);

// A class state of one kind asked for while the slot's current occupant holds another (two classes sharing a slot's
// tail in one lifetime: modelled explicitly, never a silent reset): a refactor error, never a port gap
class ClassStateMismatch : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
[[noreturn]] void classStateMismatch(const ActorHeader& slot, const char* held, const char* asked);

// ---- the slots ----------------------------------------------------------------------------------------------------
struct ActorHeader {
    ActorHeader() = default;
    ActorHeader(const ActorHeader&) = delete;
    ActorHeader& operator=(const ActorHeader&) = delete;
    virtual ~ActorHeader() = default;

    // kActorLive | kActorDrawn | model flags | kActorScenery | kActorPoseOnRoot
    uint8_t flags = 0;
    uint8_t runFlags = 0;   // kActorRun | drawn | kActorBoneScale | class bits
    uint8_t slot = 0;       // slot id: pool class + index (set at boot, never by an allocator)
    uint8_t loopFlags = 0;  // kActorLateLoop | kActorStageClass
    uint8_t type = 0;       // the class table index
    uint8_t subtype = 0;    // the sub-table index (pools 0x20 / 0x60)
    uint8_t variant = 0;    // model variant / the class's variant
    uint8_t spawnByte = 0;  // spawn record byte 7
    uint8_t state = 0;      // the class's state machine
    uint8_t subState = 0;
    uint8_t step = 0;
    uint8_t stepCounter = 0;
    StateArray<uint8_t, 4> params{}; // spawn record bytes 8..0xb (each class names them in its own state)

    // The slot's address in the exe (the identity the states still kept in the exe's layout -- the script block, the
    // stage's and the rooms' globals -- store for it, until they are native)
    uint32_t at = 0;

    // live: byte 0 & byte 1 & 1 (the pool loops' test)
    bool live() const { return (flags & runFlags & kActorLive) != 0; }
    // drawn or not (kActorDrawn set / cleared)
    void setDrawn(bool drawn) {
        if (drawn) flags |= kActorDrawn;
        else flags &= uint8_t(~kActorDrawn);
    }
    // state = s, sub-state = sub, step and step counter cleared (the exe's one dword store at +8)
    void enterState(uint8_t s, uint8_t sub = 0) {
        state = s;
        subState = sub;
        step = 0;
        stepCounter = 0;
    }
    // state = s, sub-state = sub, the step and the step counter kept (the exe's word store at +8)
    void enterStateKeepingStep(uint8_t s, uint8_t sub = 0) {
        state = s;
        subState = sub;
    }
    // the four parameters as one dword (a spawner's store at +0xc)
    void setParamsWord(uint32_t word) {
        for (size_t i = 0; i < 4; ++i) params[i] = uint8_t(word >> (8 * i));
    }
    // the four parameters as the exe reads them as one dword
    uint32_t paramsWord() const {
        return uint32_t(params[0]) | uint32_t(params[1]) << 8 | uint32_t(params[2]) << 16 | uint32_t(params[3]) << 24;
    }
    // the first four bytes (flags, run flags, slot id, loop flags) as one dword: what a class of the previous slot that
    // keeps a word past its own slot stores there (ActorPoolOf::following)
    uint32_t headWord() const {
        return uint32_t(flags) | uint32_t(runFlags) << 8 | uint32_t(slot) << 16 | uint32_t(loopFlags) << 24;
    }
    void setHeadWord(uint32_t word) {
        flags = uint8_t(word);
        runFlags = uint8_t(word >> 8);
        slot = uint8_t(word >> 16);
        loopFlags = uint8_t(word >> 24);
    }
    // the first two parameters as one word (a script's store at +0xc)
    void setParamsLowWord(uint16_t word) {
        params[0] = uint8_t(word);
        params[1] = uint8_t(word >> 8);
    }
    // state = s and the sub-state cleared (the exe's two byte stores at +8 / +9)
    void enterStateBytes(uint8_t s) {
        state = s;
        subState = 0;
    }
    // the step and the step counter cleared (the exe's byte stores at +0xa / +0xb)
    void clearStepBytes() {
        step = 0;
        stepCounter = 0;
    }
    // the step and the step counter cleared (the exe's word store at +0xa: a sub-state's first step)
    void restartStep() {
        step = 0;
        stepCounter = 0;
    }
    // step = n, step counter = counter (the exe's one word store at +0xa)
    void enterStep(uint8_t n, uint8_t counter = 0) {
        step = n;
        stepCounter = counter;
    }
    // a new slot's type, sub-type, variant, spawn byte, state machine and parameters cleared (the allocators' three
    // dword stores +4..+0xf)
    void clearIdentity() {
        setTypeWord(0);
        enterState(0);
        setParamsWord(0);
    }
    // the type, sub-type, variant and spawn byte cleared (the exe's dword store at +4: the player's init)
    void clearTypeWord() { setTypeWord(0); }
    // the type, sub-type, variant and spawn byte from a spawn record's word (the exe's dword store at +4)
    void setTypeWord(uint32_t word) {
        type = uint8_t(word);
        subtype = uint8_t(word >> 8);
        variant = uint8_t(word >> 16);
        spawnByte = uint8_t(word >> 24);
    }
    // the type and the sub-type as one word / the variant and the spawn byte as one word (the exe's word stores at +4
    // / +6: the player placed by a room's spawn list)
    void setTypePair(uint16_t typeAndSubtype) {
        type = uint8_t(typeAndSubtype);
        subtype = uint8_t(typeAndSubtype >> 8);
    }
    void setVariantPair(uint16_t variantAndSpawnByte) {
        variant = uint8_t(variantAndSpawnByte);
        spawnByte = uint8_t(variantAndSpawnByte >> 8);
    }

    // ---- the class state ----
    // a new occupant's lifetime begins (an allocation): the class state held stays until its class asks for another
    void beginLifetime() {
        ClassStateObserver* o = classStateObserver();
        if (o && classState_ && classStateCurrent_) o->lifetimeEnds(*this, *classState_);
        classStateCurrent_ = false;
        if (o) o->lifetimeBegins(*this);
    }
    // the class state held is the current occupant's (created or used in its lifetime)
    bool classStateCurrent() const { return classStateCurrent_; }
    // the class state held (nullptr: none yet)
    ActorClassState* heldClassState() const { return classState_.get(); }
    // the slot's class block cleared (exe 0x40dc40 over it): the state's members in that range zeroed
    void clearClassStateBlock() {
        if (classState_) classState_->clearBlock();
        if (ClassStateObserver* o = classStateObserver()) o->blockCleared(*this);
    }
    // exe 0x40dc40 over the slot's bytes [begin, end) of its class block: the state's members (all in that range)
    // zeroed
    void clearClassStateBytes(uint32_t begin, uint32_t end) {
        if (classState_) classState_->clearBlock();
        if (ClassStateObserver* o = classStateObserver()) o->blockBytesCleared(*this, begin, end);
    }

    template <class T, class S> friend T& classState(S& slot);
    template <class T, class S> friend const T& classState(const S& slot);

private:
    mutable std::unique_ptr<ActorClassState> classState_;
    mutable bool classStateCurrent_ = false; // the state held was created or used in the current occupant's lifetime
};

// The class state T of the slot (T's constructor takes the slot): the one held when it is a T; else a new one (zero)
// replacing a previous occupant's. A state of another kind held in the current lifetime: ClassStateMismatch.
template <class T, class S> T& classState(S& slot) {
    static_assert(std::is_base_of_v<ActorClassState, T>, "classState: a class state");
    ActorHeader& h = slot;
    if (T* held = dynamic_cast<T*>(h.classState_.get())) {
        if (!h.classStateCurrent_) {
            h.classStateCurrent_ = true;
            if (ClassStateObserver* o = classStateObserver()) o->resumed(h, *held);
        }
        return *held;
    }
    if (h.classState_ && h.classStateCurrent_) classStateMismatch(h, typeid(*h.classState_).name(), typeid(T).name());
    auto fresh = std::make_unique<T>(slot);
    T& state = *fresh;
    std::unique_ptr<ActorClassState> previous = std::move(h.classState_);
    h.classState_ = std::move(fresh);
    h.classStateCurrent_ = true;
    if (ClassStateObserver* o = classStateObserver()) o->created(h, previous.get(), state);
    return state;
}
// ... read through a const slot (a class state is the slot's own: asking for it may create it)
template <class T, class S> const T& classState(const S& slot) { return classState<T>(const_cast<S&>(slot)); }
// The class state T of the slot when the current occupant holds one (nullptr otherwise): a reader outside the game
// (the host's renderer, the dev tools) that must not create one
template <class T> const T* heldClassStateOf(const ActorHeader& slot) {
    return slot.classStateCurrent() ? dynamic_cast<const T*>(slot.heldClassState()) : nullptr;
}

struct Actor : ActorHeader {
    Fixed16Vec3 pos;                        // 16.16 position (Y down)
    Fixed16Vec3 prevPos;                    // the position at the start of the tick
    ShortVec4 rotation;                     // x / y = yaw / z (4096 = one turn; the pose is Ry * Rx * Rz)
    ShortVec4 prevRotation;                 // the rotation at the start of the tick
    ShortVec4 velocity;                     // per tick (<< 12 into the 16.16 position); z < 0 = forward
    ShortVec4 acceleration;                 // added to the velocity by the classes that fall / brake (y = gravity)
    ShortVec4 spin;                         // angular speed (y = the turn speed)
    uint8_t airborne = 0;
    uint8_t wasAirborne = 0;
    uint8_t onPlatform = 0;                 // standing on another actor (the solid of the actor collision pass)
    uint8_t wasOnPlatform = 0;
    ActorRef<Actor> platform;               // the actor stood on
    CollisionBoxRef collisionBox;           // a box record (the player's is written: its water sink) or a class's own box
    DataPtr<InteractionBoxRecord> interactionBox; // the box the player's talk / lift query scores
    uint8_t interaction = 0;                // kInteractTalk | kInteractLift: what the player's query may pick
    StateArray<uint8_t, 3> interactionPad{}; // the three bytes after it (cleared with the motion block; a class that
                                             // reads its slot as dwords copies them)
    uint32_t wallHit = 0;                   // the room push-out's result
    uint32_t actorHit = 0;                  // as a mover: the actor collision pass's result
    ActorRef<Actor> lastMover;              // as a solid: the last mover that hit it
    int16_t hitPoints = 0;
    int16_t maxHitPoints = 0;
    Matrix3 rootMatrix{};                   // the pose's root (rotation, scaled)
    int16_t rootPad = 0;                    // its padding short (copied with it)
    StateArray<int32_t, 3> rootTranslation{}; // the words after it (the player's pose: the root translation)

    // the copies at the top of the classes' updates: prevPos = pos, prevRotation = rotation (4 lanes)
    void savePreviousPose() {
        prevPos = pos;
        prevRotation = rotation;
    }
    // ... and the previous airborne / platform bytes
    void savePreviousContact() {
        wasAirborne = airborne;
        wasOnPlatform = onPlatform;
    }
    // the airborne / platform bytes and their previous values cleared (the exe's dword store at +0x50)
    void clearContactWord() {
        airborne = 0;
        wasAirborne = 0;
        onPlatform = 0;
        wasOnPlatform = 0;
    }
    // the root block (the root matrix, its padding and the words after) = eight dwords as the exe copies them
    void setRootWords(const uint32_t words[8]) {
        for (size_t i = 0; i < 5; ++i) {
            const int16_t lo = int16_t(uint16_t(words[i])), hi = int16_t(uint16_t(words[i] >> 16));
            if (2 * i < 9) rootMatrix.e[2 * i] = lo;
            if (2 * i + 1 < 9) rootMatrix.e[2 * i + 1] = hi;
            else rootPad = hi;
        }
        for (size_t i = 0; i < 3; ++i) rootTranslation[i] = int32_t(words[5 + i]);
    }
    // ... dword i of the root block as the exe reads it
    uint32_t rootWord(size_t i) const {
        if (i >= 5) return uint32_t(rootTranslation[i - 5]);
        const uint16_t lo = uint16_t(rootMatrix.e[2 * i]);
        const uint16_t hi = 2 * i + 1 < 9 ? uint16_t(rootMatrix.e[2 * i + 1]) : uint16_t(rootPad);
        return uint32_t(lo) | uint32_t(hi) << 16;
    }
    // the root block = bone k's matrix slot of `block` (the exe's eight dword copies: a second body riding its parent's
    // bone)
    void copyRootWordsFrom(const BoneBlock& block, size_t k);
    // the interaction box pointer and the interaction byte cleared (+0x5c..+0x60: an init's byte stores)
    void clearInteractionBytes() {
        interactionBox = nullptr;
        interaction = 0;
    }
    // exe 0x40dc40(+0x38, +0x68): the motion and contact block cleared (the velocities, the airborne / platform bytes,
    // the platform, the boxes, the interaction byte and its padding, the collision results)
    void clearMotion() {
        velocity = {};
        acceleration = {};
        spin = {};
        clearContactWord();
        platform = nullptr;
        collisionBox = nullptr;
        interactionBox = nullptr;
        interaction = 0;
        interactionPad = {};
        wallHit = 0;
        actorHit = 0;
    }
};
// +0x50 (airborne): 0 on the ground
constexpr uint8_t kAirborneFalling = 1; // in the air (jumping, falling)
constexpr uint8_t kAirborneHanging = 2; // off the ground but held up (the player hanging): the push-out keeps it
constexpr uint8_t kInteractTalk = 0x01; // +0x60: the player can talk to it (query mode 1)
constexpr uint8_t kInteractLift = 0x02; // the player can lift it (query mode 2)
constexpr uint8_t kInteractLiftTalks = 0x04; // picked by the lift query, it is talked to instead
constexpr uint8_t kInteractFacing = 0x80;    // only while it faces the player (within a quarter turn)

struct AnimatedActor : Actor {
    DataPtr<> animScript;                   // the current animation's first key (in the stage's animation bank)
    DataPtr<> animKeyAt;                    // the current key
    AnimKey animKey;                        // its copy (ticks: ticks left)
    uint8_t anim = 0;                       // the animation the class asks for
    uint8_t prevAnim = 0;                   // the animation of the last tick (0xff: none)
    uint8_t frameData = 0;                  // the animation's frame-data index (in the model's frame table)
    uint8_t keyIndex = 0;                   // keys played since the sequence (re)started
    uint32_t model = 0;                     // the model (its address in the loaded data)
    DataPtr<> frameTable;                   // the model's frame-data offsets (u32 bytes into the bank, by frameData)
    DataPtr<> animTable;                    // the model's animation offsets (u32 bytes into the bank, by animation)
    DataPtr<> skeleton;                     // the bones' offsets (s16 x / y / z per bone)
    uint32_t extraGeometry = 0;             // the model header's table after the part table (the model draw's texture
                                            // words: game/model_preview)
    DataPtr<CollisionMesh> collisionMesh; // as a solid (game/actor_collision)
    StateArray<uint32_t, 3> colourTables{}; // the model's colour tables by LOD (kNoColourTable: none)
    uint8_t boneSlotCount = 0;              // bone-block slots the model uses (kBonesPerBlock per block); the first
                                            // LOD's part count (lodParts)
    uint8_t secondLodParts = 0;             // the second / third LODs' part counts
    uint8_t thirdLodParts = 0;
    uint8_t partCount = 0;                  // parts of the part table (a decoded frame has partCount + 1 entries)
    uint32_t boneBlocks = 0;                // the bone blocks it owns: bit 31 - block index
    BoneBlock* lastBoneBlock = nullptr;     // the last block given to it
    int16_t cullWidth = 0;                  // the model header's sizes / 4 (the draw pass's culling extents;
    int16_t cullHeight = 0;                 // hypothesis)
    uint32_t texturePage = 0;               // the model's texture-page word (grid cell of its texture)
    uint32_t secondTexturePage = 0;         // the second texture-cell word (texture track 1)
    ShortVec3 scale;                        // 0x200 = 1 (<< stage shift)
    uint16_t tint = 0;                      // kTintNeutral / kTintFlash
    StateArray<uint8_t, 3> rootScaleBytes{}; // the root's scale bytes (kActorBoneScale)
    uint8_t semiTransparency = 0;           // the draw's semi-transparency bits (exe 0x5e1a00(actor, on, mode):
                                            // (on << 4 | mode) << 1; bit 0: per part)
    StateArray<PartEntry, 24> parts{};

    // the class tails' fields the read map shows read by a later occupant: kept by
    // the slot, so each reader finds what the previous occupant left
    uint32_t bossFlags = 0;       // the elephant boss's flags (+0x158)
    uint8_t guardianBits = 0;     // the guardian reaverbot's bits (+0x15f)
    uint8_t shownParts = 0;       // the shown-part bits of the Birdbots / the hover target, the town bird's bits (+0x160)
    StateArray<uint8_t, 6> hopperBytes{}; // the Manda hoppers' (and the cast guardian's) bytes +0x162..+0x167
    uint8_t townBirdHits = 0;     // the town bird's hits (+0x168)
    uint8_t townBirdHitsHigh = 0; // ... and the byte after (+0x169)
    uint8_t hopperTimer = 0;      // the Manda hoppers' timer (+0x16a)

    // the key ended its sequence (the last key carries kKeyJump: the animation played through)
    bool animEnded() const { return (animKey.flags & kKeyJump) != 0; }
    // the model header's count word: boneSlotCount and the two bytes after it (+0xcb, partCount, is set after)
    void setModelCounts(uint32_t word) {
        boneSlotCount = uint8_t(word);
        secondLodParts = uint8_t(word >> 8);
        thirdLodParts = uint8_t(word >> 16);
        partCount = uint8_t(word >> 24);
    }
    // the parts drawn at the LOD lod (0..2: the count word's bytes)
    uint8_t lodParts(uint32_t lod) const {
        return lod == 0 ? boneSlotCount : lod == 1 ? secondLodParts : lod == 2 ? thirdLodParts : partCount;
    }
    // the texture cell word k of the model draw (1 texturePage, 2 secondTexturePage; 3 reads the scale's x / y as the
    // exe does: the words from +0xd4; 0 the culling extents)
    uint32_t textureCellWord(uint32_t k) const {
        switch (k) {
        case 0: return uint32_t(uint16_t(cullWidth)) | uint32_t(uint16_t(cullHeight)) << 16;
        case 1: return texturePage;
        case 2: return secondTexturePage;
        default: return uint32_t(uint16_t(scale.x)) | uint32_t(uint16_t(scale.y)) << 16;
        }
    }
    // the root's scale bytes = 1.0 each and their pad byte cleared (the exe's one dword store at +0xe8)
    void resetRootScale() {
        rootScaleBytes = {kScaleByteOne, kScaleByteOne, kScaleByteOne};
        semiTransparency = 0;
    }
    // exe 0x40dc40 over the class block (+0x20 bytes): the class state's members there and the tail fields the slot
    // keeps cleared
    void clearClassBlock() {
        bossFlags = 0;
        guardianBits = 0;
        shownParts = 0;
        hopperBytes = {};
        townBirdHits = 0;
        townBirdHitsHigh = 0;
        hopperTimer = 0;
        clearClassStateBlock();
    }
};

// the model fields of the mesh pools (0x10 / 0x40 / 0x60), set by the model binding (game/actor_model)
struct MeshActor : Actor {
    uint32_t model = 0;                     // the model (its address in the loaded data)
    uint32_t extraGeometry = 0;             // the model header's table after the part table (use not traced)
    DataPtr<CollisionMesh> collisionMesh; // as a solid (game/actor_collision)
    StateArray<uint32_t, 3> colourTables{}; // the model's colour tables (kNoColourTable: none)
    int16_t cullWidth = 0;                  // the model header's sizes / 4 (the draw pass's culling extents;
    int16_t cullHeight = 0;                 // hypothesis)
    ShortVec3 scale;
    uint16_t tint = 0;                      // kTintNeutral / kTintFlash
    StateArray<uint8_t, 3> scaleBytes{};
    uint8_t semiTransparency = 0;           // the draw's semi-transparency bits (exe 0x5e1a70(actor, on, mode):
                                            // ((on & 1) << 4 | (mode & 3)) << 1)

    // the tint neutral, the three scale bytes 1.0 and the semi-transparency cleared (the exe's dword + word stores)
    void resetTintAndScale() {
        tint = kTintNeutral;
        scaleBytes = {kScaleByteOne, kScaleByteOne, kScaleByteOne};
        semiTransparency = 0;
    }
};

// pool 0x60: the statics
struct StaticActor : MeshActor {
    // the hit box the hit-box classes keep first in their tail (read map 3.1.3: a blast reads the previous one's); its
    // contact output (the entries that ask for one) shares its bytes with the fields below, as in the exe
    HitBox hitBox;
    // the square gunship's flight fields and the other classes' bytes there (game/gunship_flight; read map 3.1.3): the
    // bytes of the hit box's contact output
    uint8_t& gunshipPitchTarget() { return byteOf(hitBox.contact.y, 1); }
    uint8_t& gunshipTimer() { return byteOf(hitBox.contact.z, 0); }
    uint8_t& gunshipCooldown() { return byteOf(hitBox.contactKind, 0); }
    uint8_t& gunshipAttackCooldown() { return byteOf(hitBox.contactKind, 1); }

    // the class block cleared: the class state's members there and the tail fields the slot keeps
    void clearClassBlock() {
        hitBox = {};
        clearClassStateBlock();
    }

private:
    static uint8_t& byteOf(int16_t& word, size_t i) { return reinterpret_cast<uint8_t*>(&word)[i]; }

public:
};

// pool 0x10: the player's shots: their fields past the mesh layout are the same for every class (written by the arm,
// game/player/player_weapons)
struct ShotActor : MeshActor {
    HitBox hitBox;                      // its hit entry's result (contact: where a deflecting body was hit)
    ShortVec4 aimPoint;                 // the point the arm aimed it at (the exe copies it as two dwords)
    ShortVec3 aimOffset;                // the aim point relative to the target (the homing aims there)
    ActorRef<ActorHeader> target;       // the target actor (any pool; null: none)
};

// pool 0x40: the enemies' projectiles and falling debris
struct ProjectileActor : MeshActor {
    HitBoxHead hitBox;                  // the hit box the enemy shots keep first in their tail (read map 3.1.3)
    uint32_t debrisFlags = 0;           // the ST14 debris's flags (+0xc4; read map 3.1.3)

    // the class block cleared: the class state's members there and the tail fields the slot keeps
    void clearClassBlock() {
        hitBox = {};
        debrisFlags = 0;
        clearClassStateBlock();
    }
};

// pool 0xa0: effects without a model (sparks, puffs, emitters)
struct EffectActor : ActorHeader {
    ShortVec3 pos;          // s16 position (the spawn record's raw 8 bytes land at +0x10)
    int16_t posSpare = 0;   // the short after it (a spawn record's yaw; the classes that keep a value there name it)
    // the class's bytes as eight shorts, which the effect slot keeps for the next occupant: the glow trail reads its
    // screen history and its ring there and the hit spark its frames as the previous occupant left them (read map
    // 3.1.3); the classes name them in their states
    StateArray<int16_t, 8> trail{};

    // the position and the short after it copied from another effect as the exe does, as two dwords
    void copyPositionWords(const EffectActor& from) {
        pos = from.pos;
        posSpare = from.posSpare;
    }
    // ... set from an s16 point and its spare lane (a script's spark point)
    void setPositionWords(const ShortVec4& from) { setPositionWords(from.xyWord(), from.zSpareWord()); }
    // ... set from two words as the exe stores them (x | y << 16, z | spare << 16)
    void setPositionWords(uint32_t xy, uint32_t zSpare) {
        pos.x = int16_t(uint16_t(xy));
        pos.y = int16_t(uint16_t(xy >> 16));
        pos.z = int16_t(uint16_t(zSpare));
        posSpare = int16_t(uint16_t(zSpare >> 16));
    }
    // ... cleared
    void clearPositionWords() { setPositionWords(0, 0); }
    // exe 0x40dc40 over the class block (+0x18..+0x27, the trail's shorts): the slot's shorts and the class state's
    // members there cleared
    void clearClassBlock() {
        trail = {};
        clearClassStateBlock();
    }
    // ... its first n shorts only (exe 0x40dc40(+0x18, +0x18 + 2n))
    void clearClassShorts(size_t n) {
        for (size_t i = 0; i < n; ++i) trail[i] = 0;
        clearClassStateBytes(kTrailOffset, kTrailOffset + uint32_t(n) * 2);
    }
    static constexpr uint32_t kTrailOffset = 0x18;
    // a spawn record's yaw kept in the short after the position
    void setRecordYaw(int16_t yaw) { posSpare = yaw; }
    // short i (0..11) from the position on: the position, the short after it, the trail's (the classes that keep
    // points of their own there)
    int16_t& lane(size_t i) {
        if (i < 3) return i == 0 ? pos.x : i == 1 ? pos.y : pos.z;
        if (i == 3) return posSpare;
        return trail[i - 4];
    }
    // the position and the short after it as the exe reads them, two dwords (k 0: x | y << 16, 1: z | spare << 16)
    uint32_t positionWord(size_t k) const {
        return k == 0 ? uint32_t(uint16_t(pos.x)) | uint32_t(uint16_t(pos.y)) << 16
                      : uint32_t(uint16_t(pos.z)) | uint32_t(uint16_t(posSpare)) << 16;
    }
};

// An effect's shorts from its position on read three by three as points (the classes that keep up to four points of
// their own there: the first over the position)
struct EffectPoints {
    EffectActor& e;
    ShortTriple operator[](size_t k) const { return {e.lane(3 * k), e.lane(3 * k + 1), e.lane(3 * k + 2)}; }
};

// pool 0xe0: controllers (spawners, triggers, followers)
struct ControllerActor : ActorHeader {
    // the class's first eight bytes: a spawn record's point and yaw, as the exe copies them (two dwords from a room's
    // spawn list or a field encounter; the point's three shorts and the yaw's from a room script's record or an
    // encounter group's point); each controller class names them
    ShortVec4 recordPoint;
    // the actor the rider follower follows (+0x18; read map 3.1.3)
    ActorRef<Actor> rider;

    void setRecordPointWords(const ShortVec4& from) { recordPoint = from; }
    void setRecordPointShorts(int16_t x, int16_t y, int16_t z) {
        recordPoint.x = x;
        recordPoint.y = y;
        recordPoint.z = z;
    }
    void setRecordYaw(int16_t yaw) { recordPoint.spare = yaw; }
    // ... read back as the exe reads them, two dwords
    uint32_t recordPointWord(size_t k) const { return k == 0 ? recordPoint.xyWord() : recordPoint.zSpareWord(); }
    // exe 0x40dc40 over the class block (+0x18..+0x1f): the rider word and the class state's members there cleared
    void clearClassBlock() {
        rider = nullptr;
        clearClassStateBlock();
    }
};

} // namespace od::port
