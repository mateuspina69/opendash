// The actor system: the per-tick update loops of the actor pools in the frame body (exe 0x542280), the dispatch of an
// actor to its class (the class tables the stage setup points at) and the registry of the classes whose update is
// ported; the enemy list. The pools and their views: game/actor_pools, game/actor_state. Bit-exact against
// tests/data/act_*.bin.
//
// Pools and their loops (an actor is live when byte 0 & byte 1 & 1):
//   0x20  animated     exe 0x55bb60  [[*0xa6d300 + type * 4] + sub * 4]
//   0x10  shots        exe 0x55bc20  [0x8b1fb0 + type * 4]
//   0x40  projectiles  exe 0x55bc70  +3 & 8 ? [*0xa6bc58 + type * 4] : [0x8b1fb0 + type * 4]
//   0x60  statics      exe 0x55bce0 (+3 & 4 clear) / 0x55bd60 (+3 & 4 set):
//                        +3 & 8 ? [[*0xa6bb8c + type * 4] + sub * 4] : [0x8b1ff4 + type * 4]
//   0xa0  effects      exe 0x55bde0 / 0x55be50 (+3 & 4 clear / set):
//                        +3 & 8 ? [*0xa72588 + type * 4] : [0x8b205c + type * 4]
//   0xe0  controllers  exe 0x55bec0 (type != 0, +3 & 4 clear) / 0x55bf20 (+3 & 4 set; type 0 = free when the byte its
//                        +0xc points at is 0): [*0xa6c650 + type * 4]
// Frame body order: script runner, 0x50f350, 0x58cc10, pool e0, player input + dispatch, pool 20, pool 40, pool a0,
// pool 60, actor collision pass, the player's update, pool 10, camera, pool a0 (late), pool 60 (late), pool e0 (late),
// 0x415560, 0x55c140, 0x42a270, the hit list pass 0x5e4260, 0x511720.
//
// A class is identified by its update routine (the per-stage tables map the same type number to different
// functions; the source names each table entry's routine: engine/routines.csv). Ported classes run their
// transcription; unported ones are skipped (the actor keeps the state of the room snapshot) and reported once per
// class pointer through ActorContext::log. A ported class whose transcription meets an unported branch (logic_error
// "not ported") is disabled for the session.
#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

enum class ActorPool : uint8_t {
    Controllers,     // pool 0xe0, exe 0x55bec0
    Animated,        // pool 0x20, exe 0x55bb60
    Projectiles,     // pool 0x40, exe 0x55bc70
    Effects,         // pool 0xa0, exe 0x55bde0
    Statics,         // pool 0x60, exe 0x55bce0
    Shots,           // pool 0x10, exe 0x55bc20
    EffectsLate,     // pool 0xa0 after the camera, exe 0x55be50
    StaticsLate,     // pool 0x60 after the camera, exe 0x55bd60
    ControllersLate, // pool 0xe0 after the camera, exe 0x55bf20
};

struct ActorContext {
    std::function<void(const std::string&)> log; // optional
    std::set<uint32_t> reported;                 // unported class pointers already logged (the gap's message)
    std::set<RoutineId> disabled;                // ported classes that threw "not ported" this session
    std::vector<uint32_t> skipped;               // actors skipped by the last updatePool call (unported classes)
};

// ---- the class tables -----------------------------------------------------------------------------------------------
// A class table: u32 per type, the pointer to the class's update function (0: none; the source names its routine). A
// table of class tables (the animated and static pools' stage tables): u32 per type, its class table by sub-type.
// entry `index` of a class table (or of a table of them)
inline uint32_t classTableEntry(uint32_t table, uint32_t index) { return dataScalar<uint32_t>(table + index * 4); }
// the stage's class tables (the stage setup points them; game/spawn); the title and the status sub-screen point the
// animated pool's at their own
struct StageClassTables {
    DataPtr<> animated; // tables of class tables
    DataPtr<> statics;
    DataPtr<> projectiles; // class tables
    DataPtr<> effects;
    DataPtr<> controllers;
    // the field's animated table while the status sub-screen has swapped in its own (exe 0x541bf0 step 1 saves it,
    // step 3 restores it)
    DataPtr<> fieldAnimated;
};
// the exe's global class tables (exe data 0x8b1fb0 / 0x8b1ff4 / 0x8b205c, a pointer per type): the classes' update
// routines
using GlobalClassTable = DataTable<RoutineTable<0x100>>;
inline const GlobalClassTable globalShotClasses{TableId::GlobalShotClasses};     // the shots, and the projectiles without kActorStageClass
inline const GlobalClassTable globalStaticClasses{TableId::GlobalStaticClasses}; // the statics without kActorStageClass
inline const GlobalClassTable globalEffectClasses{TableId::GlobalEffectClasses}; // the effects without kActorStageClass
// the stage's class tables (one set, for the life of the program)

inline StageClassTables& stageClassTables() {
    static StageClassTables t;
    return t;
}

// A class's update: the slot's address (Phase 1: a converted class starts by taking its view, animatedActorAt ...)
using ActorUpdateFn = void (*)(const PlayerEnv& env, uint32_t actor);

// The class routine the pool loop would call for this actor (a null RoutineRef: none -- dead, or skipped by the
// loop's flags; a pointer with no C++ routine: not mapped). updating = false: also for a frozen actor (byte 1 bit 0
// off, e.g. exe 0x5e05d0(0xffe, 1) during a mission's opening message): the exe's draw pass 0x5f3fd0 tests byte 0 &
// byte 1 & 2 (6), not bit 0
RoutineRef actorHandler(const ActorHeader& actor, ActorPool pool, bool updating = true);
// actorHandler for host-side queries (draw passes, logs) of the FIELD's actors: while the status sub-screen has
// swapped the pool-0x20 class table (game mode 5 steps 2..3, table 0x91a7cc) the field's table is the one saved in
// 0xa5a018; an empty entry resolves to none instead of reading address 0
RoutineRef fieldActorHandler(const ActorHeader& actor, ActorPool pool, bool updating = true);
// The controller slots (pool 0xe0) whose class is `cls`, in slot order (early or late loop): the host's pictures of
// the stage controllers' screen elements walk them
std::vector<ControllerActor*> controllersRunning(RoutineId cls);
// The ported transcription of a class, or nullptr
ActorUpdateFn portedActorClass(RoutineId cls);
// One of the pool loops of the frame body: every live actor of the pool, in slot order, with its class update
// (ported) or skipped (unported: ctx.skipped). Returns the number of actors updated.
int updatePool(const PlayerEnv& env, ActorContext& ctx, ActorPool pool);

} // namespace od::port
