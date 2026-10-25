#include "app/app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actors.hpp"
#include "game/angles.hpp"
#include "game/collision.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_movement.hpp"
#include "game/player/player_state.hpp"
#include "content/nino/platform_boss.hpp"
#include "content/nino/repair_machine_state.hpp"
#include "game/door_actor.hpp"
#include "game/encounter.hpp"
#include "game/event_state.hpp"
#include "game/game_state.hpp"
#include "game/room_grid.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"
namespace od::app {

namespace {

// the player's yaw turned toward the 16.16 point (x, z) at the player's own height (exe 0x5e3bc0's turn)
void faceToward(int32_t x, int32_t z) {
    od::port::Player& pl = od::port::player();
    od::port::Fixed16Vec3 aim;
    aim.x = x;
    aim.y = pl.pos.y;
    aim.z = z;
    pl.rotation.y = int16_t(od::port::angleOf(aim, pl.pos));
}
// the integer x / z of an actor's position (the bot's distances)
int actorX(uint32_t a) { return od::port::actorAt(a).pos.x.whole; }
int actorZ(uint32_t a) { return od::port::actorAt(a).pos.z.whole; }
// the smoke the spray segment watches on its anchor: the ST18 repair machine's smoke ticks (+0x15c); no state of the
// machine's yet: no smoke. An anchor of another class holding its own state: std::logic_error (spray names the machine)
bool anchorSmoking(uint32_t anchor) {
    const od::port::ActorHeader& slot = od::port::actorHeaderAt(anchor);
    if (const auto* m = od::port::heldClassStateOf<od::port::repair_machine_state::MachineFields>(slot))
        return m->smokeTicks > 0;
    if (slot.classStateCurrent() && slot.heldClassState())
        throw std::logic_error("playscript spray: the anchor is not the ST18 repair machine");
    return false;
}
// a playscript names a class by its update function's pointer in the source's encoding (the PC exe's address): the
// source names the class's routine; a class with no C++ routine matches nothing
bool isScriptClass(const od::RoutineRef& h, uint32_t scriptClass) {
    const od::RoutineRef c = od::stateRoutine(scriptClass);
    return h && c.mapped() && h.is(c.id());
}

} // namespace

void App::parsePlayScript(const std::string& text) {
    std::stringstream all(text);
    for (std::string step; std::getline(all, step, ';');) {
        std::istringstream ss(step);
        std::string w;
        if (!(ss >> w)) continue;
        if (w == "tp") {
            std::array<int, 4> v{};
            if (ss >> v[0] >> v[1] >> v[2] >> v[3]) playScriptTeleports[playScript.size()] = v;
            continue;
        }
        if (w == "goto") { // "goto N X Z [BUTTONS]": walk to (X, Z) (turned toward it every tick), BUTTONS held
            int n = 0;
            ss >> n;
            AimSeg seg{playScript.size(), playScript.size() + size_t(std::max(0, n)), {}};
            seg.go = true;
            ss >> seg.tgt[0] >> seg.tgt[2];
            std::string names;
            uint16_t mask = 0;
            if (ss >> names) {
                static const std::map<std::string, uint16_t> kB = {{"cross", 0x4000}, {"square", 0x8000}, {"circle", 0x2000},
                                                                   {"triangle", 0x1000}, {"r2", 0x200}, {"r1", 0x800}};
                std::stringstream ns(names);
                for (std::string nm; std::getline(ns, nm, '+');)
                    if (const auto it = kB.find(nm); it != kB.end()) mask |= it->second;
            }
            for (int t = 0; t < n; ++t) playScript.push_back(mask);
            playScriptAims.push_back(std::move(seg));
            continue;
        }
        if (w == "route") { // "route N ST/SUB ... [| CLASS ...]": walk the exits room by room, fighting CLASS on the way
            int n = 0;
            ss >> n;
            AimSeg seg{playScript.size(), playScript.size() + size_t(std::max(0, n)), {}};
            seg.route = true;
            bool enemies = false;
            for (std::string c; ss >> c;) {
                if (c == "|") { enemies = true; continue; }
                if (enemies) seg.cls.push_back(uint32_t(std::stoul(c, nullptr, 16)));
                else if (c == "skate") seg.skate = true; // roller dash (circle held: bit 0x2 of +0x10e) under water with the aqua shoes
                else if (const size_t k = c.find('/'); k != std::string::npos)
                    seg.rooms.push_back({int(std::stoul(c.substr(0, k), nullptr, 16)), std::stoi(c.substr(k + 1))});
            }
            for (int t = 0; t < n; ++t) playScript.push_back(0);
            playScriptAims.push_back(std::move(seg));
            continue;
        }
        if (w == "lift") { // "lift N X Z": walk at the object at (X, Z), face it, tap triangle until it is held
            int n = 0;
            ss >> n;
            AimSeg seg{playScript.size(), playScript.size() + size_t(std::max(0, n)), {}};
            ss >> seg.tgt[0] >> seg.tgt[2];
            seg.lift = true;
            for (int t = 0; t < n; ++t) playScript.push_back(0);
            playScriptAims.push_back(std::move(seg));
            continue;
        }
        if (w == "walkto") { // "walkto N X Z": the route planner's path to (X, Z) in this room (no fights, no use)
            int n = 0;
            ss >> n;
            AimSeg seg{playScript.size(), playScript.size() + size_t(std::max(0, n)), {}};
            ss >> seg.tgt[0] >> seg.tgt[2];
            seg.route = seg.walk = true;
            for (int t = 0; t < n; ++t) playScript.push_back(0);
            playScriptAims.push_back(std::move(seg));
            continue;
        }
        if (w == "talk") { // "talk N CLASS [VAR]": walk to the pool-0x20 actor CLASS (variant VAR), face it, tap circle
            int n = 0;
            ss >> n;
            AimSeg seg{playScript.size(), playScript.size() + size_t(std::max(0, n)), {}};
            std::string c;
            ss >> c;
            seg.cls.push_back(uint32_t(std::stoul(c, nullptr, 16)));
            seg.tgt[0] = -1;
            if (std::string v; ss >> v) seg.tgt[0] = std::stoi(v, nullptr, 0);
            seg.talk = true;
            for (int t = 0; t < n; ++t) playScript.push_back(0);
            playScriptAims.push_back(std::move(seg));
            continue;
        }
        if (w == "ride") { // "ride N CLASS X Y Z": board CLASS (jump onto it), ride it, jump off onto (X, Y, Z)
            int n = 0;
            ss >> n;
            AimSeg seg{playScript.size(), playScript.size() + size_t(std::max(0, n)), {}};
            std::string c;
            ss >> c >> seg.tgt[0] >> seg.tgt[1] >> seg.tgt[2];
            seg.cls.push_back(uint32_t(std::stoul(c, nullptr, 16)));
            seg.ride = true;
            for (int t = 0; t < n; ++t) playScript.push_back(0);
            playScriptAims.push_back(std::move(seg));
            continue;
        }
        if (w == "aim" || w == "catch" || w == "guard" || w == "spray") {
            int n = 0;
            ss >> n;
            AimSeg seg{playScript.size(), playScript.size() + size_t(std::max(0, n)), {}};
            seg.catcher = w == "catch" || w == "guard";
            seg.guard = w == "guard";
            seg.spray = w == "spray";
            seg.catcher = seg.catcher || seg.spray;
            for (std::string c; ss >> c;)
                if (c == "!") seg.protect = true;
                else if (c == "&") seg.strafe = true; // aim: side-step while locked on (dodging)
                else if (c == "*") continue;
                else if (c[0] == '~') seg.reach = int(std::stoul(c.substr(1), nullptr, 16));
                else if (c[0] == '@') seg.anchor = uint32_t(std::stoul(c.substr(1), nullptr, 16));
                else if (c[0] == '^') seg.facing = uint32_t(std::stoul(c.substr(1), nullptr, 16));
                else seg.cls.push_back(uint32_t(std::stoul(c, nullptr, 16)));
            const bool special = text.find('*', 0) != std::string::npos && step.find('*') != std::string::npos;
            for (int t = 0; t < n; ++t) // "*": fire the special weapon (triangle, held) instead of buster taps
                playScript.push_back(seg.catcher ? uint16_t(0)
                                     : special  ? uint16_t(0x200 | (t % 8 < 5 ? 0x1000 : 0))
                                                : uint16_t(0x200 | (t % 6 < 2 ? 0x8000 : 0)));
            playScriptAims.push_back(std::move(seg));
            continue;
        }
        if (w == "calm") { playScript.push_back(kPtCalm); continue; }
        if (w == "mash") { playScript.push_back(kPtMash); continue; }
        int ticks = 0;
        try { ticks = std::stoi(w); } catch (...) { SDL_Log("pt error: bad playscript step '%s'", step.c_str()); continue; }
        std::string names;
        ss >> names;
        static const std::map<std::string, uint16_t> kNames = {
            {"up", 0x10}, {"right", 0x20}, {"down", 0x40}, {"left", 0x80}, {"cross", 0x4000}, {"square", 0x8000},
            {"circle", 0x2000}, {"triangle", 0x1000}, {"l1", 0x400}, {"r1", 0x800}, {"l2", 0x100},
            {"r2", 0x200}, {"start", 0x8}, {"select", 0x1}, {"l3", 0x2}, {"r3", 0x4}};
        uint16_t mask = 0;
        std::stringstream ns(names);
        for (std::string n; std::getline(ns, n, '+');) {
            const auto it = kNames.find(n);
            if (it != kNames.end()) mask |= it->second;
        }
        for (int t = 0; t < ticks; ++t) playScript.push_back(mask);
    }
}

std::string App::ptState() {
    const od::port::GameState& game = od::port::gameState();
    const od::port::Player& pl = od::port::player();
    char b[200];
    std::snprintf(b, sizeof b, "mode %02x%02x stage %02x%02x story %02x script %02x req %02x ev %02x pl %02x at %d %d %d",
                  unsigned(game.mode), unsigned(game.modeStep), unsigned(game.stage), unsigned(game.room),
                  unsigned(game.story), unsigned(od::port::scriptState().flags),
                  unsigned(od::port::transitionRequest().state), unsigned(od::port::eventState().active),
                  unsigned(pl.state), int32_t(pl.pos.x) >> 24, int32_t(pl.pos.y) >> 24, int32_t(pl.pos.z) >> 24);
    return b;
}

// the game runs nothing that holds the pad: field play, no transition / script / event, the player playing
static bool gameIdle() {
    return od::port::transitionRequest().state == od::port::kTransitionNone &&
           !(od::port::scriptState().flags & od::port::kScriptRunning) && od::port::eventState().active == 0 &&
           od::port::player().playerState() == od::port::PlayerState::Playing;
}

bool App::ptCalm() {
    const od::port::GameState& game = od::port::gameState();
    return game.gameMode() == od::port::GameMode::Field && game.fieldStep() == od::port::FieldStep::Playing &&
           gameIdle();
}

uint16_t App::ptNextPad() {
    if (playScriptPos >= playScript.size()) return 0;
    const uint16_t m = playScript[playScriptPos];
    if (m != kPtCalm && m != kPtMash) {
        ++playScriptPos;
        return m;
    }
    ptCalmRun = ptCalm() ? ptCalmRun + 1 : 0;
    ++ptWait;
    if (ptCalmRun >= 4 || ptWait > kPtWaitLimit) {
        if (ptWait > kPtWaitLimit) SDL_Log("pt wait timeout at %zu (never calm)", playScriptPos);
        ++playScriptPos;
        ptCalmRun = ptWait = 0;
        return ptNextPad();
    }
    return m == kPtMash && ptWait % 10 == 1 ? uint16_t(0x2000) : uint16_t(0);
}

void App::botStep(bool& aimApproach, bool& aimBack, uint16_t& aimSide, uint16_t& aimPad) {
    for (const AimSeg& seg : playScriptAims) {
        if (playScriptPos < seg.from || playScriptPos >= seg.to) continue;
        od::port::Player& pl = od::port::player();
        if (seg.talk) {
            const bool busy = !gameIdle();
            if (busy) {
                aimPad = (playScriptPos % 10 == 1) ? 0x2000 : 0; // advance the messages
                continue;
            }
            uint32_t npc = 0;
            int64_t nd = INT64_MAX;
            for (uint32_t i = 0; i < 24; ++i) {
                const uint32_t a = od::port::kAnimatedPool + i * 0x16c;
                if (!isScriptClass(od::port::fieldActorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Animated), seg.cls[0])) continue;
                if (seg.tgt[0] >= 0 && od::port::actorHeaderAt(a).variant != uint32_t(seg.tgt[0])) continue;
                const int64_t dx = actorX(a) - pl.pos.x.whole, dz = actorZ(a) - pl.pos.z.whole;
                if (dx * dx + dz * dz < nd) { nd = dx * dx + dz * dz; npc = a; }
            }
            if (npc) {
                const od::port::Actor& target = od::port::actorAt(npc);
                faceToward(target.pos.x, target.pos.z);
                if (nd > int64_t(0xa0) * 0xa0) aimApproach = true;
                else if (playScriptPos % 8 < 2) aimPad = 0x2000;
            }
            continue;
        }
        if (seg.lift) {
            if (pl.carryFlags != 0) continue; // held
            faceToward(int32_t(seg.tgt[0] * 65536), int32_t(seg.tgt[2] * 65536));
            const int64_t dx = seg.tgt[0] - pl.pos.x.whole, dz = seg.tgt[2] - pl.pos.z.whole;
            static int64_t lastD = 0;
            const bool close = dx * dx + dz * dz < int64_t(0xe0) * 0xe0 || lastD == dx * dx + dz * dz;
            lastD = dx * dx + dz * dz;
            if (!close) aimApproach = true;
            else if (pl.subState == 0 && playScriptPos % 10 < 2) aimPad = 0x1000;
            continue;
        }
        if (seg.go) {
            const int64_t dx = seg.tgt[0] - pl.pos.x.whole, dz = seg.tgt[2] - pl.pos.z.whole;
            if (dx * dx + dz * dz > int64_t(0x30) * 0x30) {
                faceToward(int32_t(seg.tgt[0] * 65536), int32_t(seg.tgt[2] * 65536));

                aimApproach = true;
            }
            continue;
        }
        if (seg.route) {
            static int stuck = 0, hop = 0, side = 0, lastX = 0, lastZ = 0, useT = 0;
            static uint32_t fightFoe = 0;
            static int leap = 0; // a running jump over a gap: ticks of cross held
            static int fightT = 0, fightHp = 0;
            static std::map<uint32_t, size_t> ignoreUntil; // foes that took no damage: left alone a while
            const auto faceTo = [](int x, int z) { faceToward(int32_t(x * 65536), int32_t(z * 65536)); };
            const int st = od::port::gameState().stage, sb = od::port::gameState().room;
            const int px = pl.pos.x.whole, pz = pl.pos.z.whole;
            const uint32_t exits = od::port::stage().exitTable;
            // the exit entries from e on (the table's end: kExitEnd, or the entries mapped)
            // a pseudo exit record at the walk target (the bot's own: kWalkEx names it)
            constexpr uint32_t kWalkEx = 0x7f300000;
            static od::port::ExitEntry walkExit;
            const auto exitAt = [](uint32_t e) { return e == kWalkEx ? walkExit : od::port::exitRecordAt(e); };
            const auto exitMapped = [](uint32_t e) { return od::port::dataMapped(e, od::port::kExitEntrySize); };
            if (static int exitsKey = -1; std::getenv("OD_EXITS") && exitsKey != ((st << 8) | sb) &&
                                          exitMapped(exits)) { // dev: the room's exit records
                exitsKey = (st << 8) | sb;
                for (uint32_t e = exits; exitMapped(e) && exitAt(e).id != od::port::kExitEnd; e += od::port::kExitEntrySize) {
                    const od::port::ExitEntry exit = exitAt(e);
                    SDL_Log("EXIT ST%02X/%d -> ST%02X/%d at %d %d %d", st, sb, unsigned(exit.destStage),
                            int(exit.destRoom), int(exit.trigger.x), int(exit.trigger.y), int(exit.trigger.z));
                }
            }
            if (static int doorsKey = -1; std::getenv("OD_DOORS") && doorsKey != ((st << 8) | sb)) { // dev: door actors
                doorsKey = (st << 8) | sb;
                for (uint32_t i = 0; i < 64; ++i) {
                    const uint32_t a = od::port::kStaticPool + i * 0xcc;
                    const od::port::StaticActor& act = od::port::staticActorAt(a);
                    if (!(act.flags & od::port::kActorLive)) continue;
                    od::RoutineRef h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Statics);
                    if (!h) h = od::port::actorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::StaticsLate);
                    SDL_Log("STATIC ST%02X/%d slot %u type %02x sub %02x var %02x st %u/%u c %02x d %02x e %02x f %02x pos %d %d %d class %06x %s",
                            st, sb, i, unsigned(act.type), unsigned(act.subtype), unsigned(act.variant),
                            unsigned(act.state), unsigned(act.subState), unsigned(act.params[0]), unsigned(act.params[1]),
                            unsigned(act.params[2]), unsigned(act.params[3]), int(act.pos.x.whole), int(act.pos.y.whole),
                            int(act.pos.z.whole), h.pointerValue(), od::port::portedActorClass(h.id()) ? "ported" : "NOT PORTED");
                }
                SDL_Log("FLAGS710 %02x %02x", unsigned(od::port::storyFlags().bytes[0x710 >> 3]),
                        unsigned(od::port::storyFlags().bytes[(0x710 >> 3) + 1]));
            }
            if (static int segsKey = -1; std::getenv("OD_SEGS") && segsKey != ((st << 8) | sb)) { // dev: breakable segments
                segsKey = (st << 8) | sb;
                const od::port::RoomGridState& grid = od::port::roomGrid();
                const uint32_t seg = grid.segments;
                const uint32_t segCount = od::port::SegmentListHeader(seg).segmentCount;
                SDL_Log("SEGS ST%02X/%d flags %02x count %u", st, sb, unsigned(grid.roomFlags), segCount);
                const uint32_t list = seg + segCount * od::kSegmentEntrySize + od::kListHeaderSize;
                SDL_Log("SEGS grid ox %02x oz %02x w %02x", unsigned(grid.originX), unsigned(grid.originZ),
                        unsigned(grid.width));
                for (int cz = 0x30; cz < 0x50; ++cz)
                    for (int cx = 0x30; cx < 0x50; ++cx) {
                        const int gx = cx - grid.originX, gz = cz - grid.originZ;
                        if (gx < 0 || gz < 0 || gx >= grid.width) continue;
                        const uint32_t ca = grid.cells + uint32_t((gz * grid.width) + gx) * 0xc;
                        if (!od::port::dataMapped(ca, 0xc)) continue;
                        for (int k = 0; k < 1; ++k) {
                            const uint32_t w = od::port::GridCell(ca).head;
                            if (!(w & 0x8000) || (w & 0x4000)) continue;
                            const uint32_t i = w & 0x7ff, e1 = seg + i * od::kSegmentEntrySize + od::kListHeaderSize;
                            const od::port::SegmentEntry entry(e1);
                            const od::port::SegmentPoolEntry pool(grid.pool + od::kListHeaderSize +
                                                                  uint32_t(entry.poolIndex) * od::kPoolEntrySize);
                            const od::port::BreakableWord breakable(list + i * od::kBreakableWordSize);
                            SDL_Log("SEG cell %02x %02x k%d idx %u hp %d var %x pool %u poolhp %d kind %d react %x list %08x", cx, cz, k, i,
                                    int(entry.hitPoints), unsigned(entry.flags), unsigned(entry.poolIndex),
                                    int(pool.hitPoints), int(pool.breakFlags() & 3), unsigned(pool.reaction & 0xf),
                                    uint32_t(uint16_t(breakable.counter)) | uint32_t(uint16_t(breakable.pendingDamage)) << 16);
                        }
                    }
            }
            // the next room of the list after the current one
            int next = -1;
            for (size_t k = 0; k + 1 < seg.rooms.size(); ++k)
                if (seg.rooms[k].first == st && seg.rooms[k].second == sb) next = int(k + 1);
            uint32_t ex = 0;
            if (next >= 0 && exitMapped(exits))
                for (uint32_t e = exits; exitMapped(e) && exitAt(e).id != od::port::kExitEnd; e += od::port::kExitEntrySize)
                    if (exitAt(e).destStage == seg.rooms[next].first && exitAt(e).destRoom == seg.rooms[next].second) {
                        ex = e;
                        break;
                    }
            if (seg.walk) { // the pseudo exit record at the walk target
                walkExit.trigger.x = int16_t(seg.tgt[0]);
                walkExit.trigger.z = int16_t(seg.tgt[2]);
                ex = kWalkEx;
                next = 0x40 + int((uint32_t(seg.tgt[0]) * 31u + uint32_t(seg.tgt[2])) & 0x3f);
            }
            // the nearest live enemy of the listed classes
            uint32_t foe = 0;
            int64_t fd = INT64_MAX;
            for (uint32_t i = 0; i < 24; ++i) {
                const uint32_t a = od::port::kAnimatedPool + i * 0x16c;
                const od::RoutineRef h = od::port::fieldActorHandler(od::port::actorHeaderAt(a), od::port::ActorPool::Animated);
                if (std::none_of(seg.cls.begin(), seg.cls.end(), [&](uint32_t c) { return isScriptClass(h, c); }) ||
                    od::port::actorAt(a).hitPoints <= 0)
                    continue;
                if (const auto ig = ignoreUntil.find(a); ig != ignoreUntil.end() && ig->second > playScriptPos) continue;
                const int64_t dx = actorX(a) - px, dz = actorZ(a) - pz;
                if (dx * dx + dz * dz < fd) { fd = dx * dx + dz * dz; foe = a; }
            }
            const bool busy = !gameIdle();
            if (busy) {
                aimPad = (playScriptPos % 10 == 1) ? 0x2000 : 0; // messages / scripts: tap circle
            } else if (foe && (fd < int64_t(0x600) * 0x600 || (ex && (od::port::storyFlags().bytes[0x710 >> 3] & 1)))) {
                // (a foe behind a wall would pin the bot there: at most 600 ticks per foe, then 900 away)
                if (foe != fightFoe) { fightFoe = foe; fightT = 0; fightHp = od::port::actorAt(foe).hitPoints; }
                if (++fightT > 600) { ignoreUntil[foe] = playScriptPos + 900; fightT = 0; }
                faceTo(actorX(foe), actorZ(foe));
                aimPad = 0x200 | (playScriptPos % 6 < 2 ? 0x8000 : 0);
                aimApproach = fd > int64_t(0x200) * 0x200;
                aimBack = fd < int64_t(0x140) * 0x140;
            } else if (ex) {
                const int tx = exitAt(ex).trigger.x, tz = exitAt(ex).trigger.z;
                const int64_t dx = tx - px, dz = tz - pz;
                uint32_t door = 0; // a door actor (static type 0x19) at the exit: face it and talk
                for (uint32_t i = 0; i < 64; ++i) {
                    const uint32_t a = od::port::kStaticPool + i * 0xcc;
                    const od::port::ActorHeader& header = od::port::actorHeaderAt(a);
                    if ((header.flags & od::port::kActorLive) && header.type == od::port::kDoorActorType &&
                        std::abs(actorX(a) - tx) < 0x200 && std::abs(actorZ(a) - tz) < 0x200)
                        door = a;
                }
                // a door without an actor (a house door volume): face the exit and tap circle at it
                const bool doorVol = !door && dx * dx + dz * dz < int64_t(0x140) * 0x140 && stuck > 20;
                if (doorVol) {
                    faceTo(tx, tz);
                    if (playScriptPos % 6 < 2) aimPad = 0x2000;
                } else if (door && dx * dx + dz * dz < int64_t(0x200) * 0x200) {
                    faceTo(actorX(door), actorZ(door));
                    const int64_t ddx = actorX(door) - px, ddz = actorZ(door) - pz;
                    aimApproach = ddx * ddx + ddz * ddz > int64_t(0xc0) * 0xc0;
                    if (playScriptPos % 6 < 2) aimPad = 0x2000;
                } else if (dx * dx + dz * dz > int64_t(0x60) * 0x60 && useT == 0) {
                    // the path: a BFS over a 0x80 grid of the room's floor (probe + move test), once per
                    // room / exit; walk to the next waypoint of it, straight at the trigger when none
                    static int pathKey = -1;
                    static std::set<size_t> blockedCells; // waypoints the bot made no progress to (this room)
                    static int blockedRoom = -1;
                    if (blockedRoom != ((st << 8) | sb)) { blockedRoom = (st << 8) | sb; blockedCells.clear(); }
                    static std::vector<std::pair<int, int>> path;
                    static std::vector<char> pathWall; // the step crosses an obstacle
                    static size_t pathAt = 0;
                    const int key = (st << 16) | (sb << 8) | next;
                    if (key != pathKey) {
                        pathKey = key;
                        path.clear();
                        pathWall.clear();
                        pathAt = 0;
                        constexpr int kStep = 0x40, kHalf = 64; // +-0x1000
                        const int py0 = pl.pos.y.whole;
                        const uint32_t routeMask = od::port::collisionLayerMask(pl.slot); // the player's layers
                        std::vector<int> floorY(size_t(4 * kHalf * kHalf), 0x7fff);
                        const auto idx = [&](int i, int j) { return size_t(j * 2 * kHalf + i); };
                        const auto unblocked = [&](size_t c) { return !blockedCells.count(c); };
                        for (int j = 0; j < 2 * kHalf; ++j)
                            for (int i = 0; i < 2 * kHalf; ++i) {
                                const int16_t q[3] = {int16_t((i - kHalf) * kStep + 0x10), int16_t(py0 - 0x300),
                                                      int16_t((j - kHalf) * kStep + 0x10)}; // off the 0x400 seams
                                int g = od::port::probeGroundAt(playerView.env, q, od::port::CollisionBoxRef{}, 0, routeMask);
                                if (g == 0x7fff || g == -0x4000) { // a low ceiling (tunnel): probe from just above the floor
                                    const int16_t q2[3] = {q[0], int16_t(py0 - 0x60), q[2]};
                                    g = od::port::probeGroundAt(playerView.env, q2, od::port::CollisionBoxRef{}, 0, routeMask);
                                }
                                if (g != 0x7fff && g != -0x4000 && g > py0 - 0x300 && g < py0 + 0x1000)
                                    floorY[idx(i, j)] = g;
                            }
                        const auto cell = [&](int x, int z) {
                            return std::pair<int, int>{std::clamp((x + kHalf * kStep + kStep / 2) / kStep, 0, 2 * kHalf - 1),
                                                       std::clamp((z + kHalf * kStep + kStep / 2) / kStep, 0, 2 * kHalf - 1)};
                        };
                        const auto [si, sj] = cell(px, pz);
                        if (std::getenv("OD_ROUTEMAP")) { // dev: the room's trigger volumes (type, flags, centre)
                            std::set<std::tuple<int, int, int, int, int>> seen;
                            od::CollisionState cst;
                            for (int j = 0; j < 2 * kHalf; ++j)
                                for (int i = 0; i < 2 * kHalf; ++i) {
                                    for (int lv = 0; lv < 12; ++lv) { // several heights (raised platforms)
                                        od::TriggerHit th;
                                        const std::array<int16_t, 3> q{int16_t((i - kHalf) * kStep + 0x10), int16_t(py0 + 0x200 - lv * 0x100),
                                                                       int16_t((j - kHalf) * kStep + 0x10)};
                                        for (int yw = 0; yw < 4; ++yw)
                                            if (od::triggerScan(playerView.env.room, cst, q, int16_t(yw * 0x400), int16_t(yw * 0x400), th))
                                                seen.insert({th.type, th.flags, th.x, th.y, th.z});
                                    }
                                }
                            for (const auto& [ty, fl, x, y, z] : seen)
                                SDL_Log("ROUTETRIG ST%02X/%d type %02x flags %04x at %d %d %d", st, sb, ty, fl, x, y, z);
                        }
                        if (const char* px2 = std::getenv("OD_ROUTEPROBEX")) { // dev: a probe line along z at x
                            int x0 = 0, x1 = 0, z0 = 0, z1 = 0, y0 = 0; // "x0 x1 z0 z1 probeY": a height grid
                            std::sscanf(px2, "%d %d %d %d %d", &x0, &x1, &z0, &z1, &y0);
                            for (int z = z0; z <= z1; z += 0x40) {
                                std::string row;
                                for (int x = x0; x <= x1; x += 0x40) {
                                    const int16_t q1[3] = {int16_t(x + 0x10), int16_t(y0), int16_t(z + 0x10)};
                                    const int g1 = od::port::probeGroundAt(playerView.env, q1, od::port::CollisionBoxRef{}, 0, routeMask);
                                    char t[16];
                                    std::snprintf(t, sizeof t, " %5d", g1);
                                    row += t;
                                }
                                SDL_Log("ROUTEPROBEX z %5d:%s", z, row.c_str());
                            }
                        }
                        if (const char* pz = std::getenv("OD_ROUTEPROBE")) { // dev: a probe line at z = OD_ROUTEPROBE
                            const int zz = std::atoi(pz);
                            std::string row;
                            for (int x = -0x800; x < 0; x += 0x10) {
                                const int16_t q1[3] = {int16_t(x), int16_t(py0 - 0x300), int16_t(zz)};
                                const int16_t q2[3] = {int16_t(x), int16_t(py0 - 0x60), int16_t(zz)};
                                const int g1 = od::port::probeGroundAt(playerView.env, q1, od::port::CollisionBoxRef{}, 0, routeMask);
                                const int g2 = od::port::probeGroundAt(playerView.env, q2, od::port::CollisionBoxRef{}, 0, routeMask);
                                char t[24];
                                std::snprintf(t, sizeof t, " %d:%d/%d", x, g1, g2);
                                row += t;
                            }
                            SDL_Log("ROUTEPROBE z %d:%s", zz, row.c_str());
                        }
                        if (std::getenv("OD_ROUTECELLS")) { // dev: the grid cell words (0x200 cells) around the player
                            const od::port::RoomGridState& grid = od::port::roomGrid();
                            const int gx0 = grid.originX, gz0 = grid.originZ, gw = grid.width;
                            SDL_Log("ROUTECELL grid origin %02x %02x width %u flags %02x", gx0, gz0, gw,
                                    unsigned(grid.roomFlags));
                            for (uint32_t sgi : {0x43u, 0x44u, 0x4cu, 0x4du, 0x47u, 0x48u, 0x42u}) {
                                // the raw bytes at the list's entry offset sgi * 8 and the pool's at its byte 6 * 0xc
                                // (the dump as it was traced: from the lists' starts, their headers included)
                                const od::port::DataReader segBytes = od::port::dataAt(grid.segments + sgi * 8, 8);
                                const uint8_t poolIndex = segBytes.u8(6);
                                const od::port::DataReader poolBytes = od::port::dataAt(grid.pool + uint32_t(poolIndex) * 0xc, 12);
                                std::string b1, b2;
                                for (size_t k = 0; k < 8; ++k) { char t[4]; std::snprintf(t, 4, "%02x", unsigned(segBytes.u8(k))); b1 += t; }
                                for (size_t k = 0; k < 12; ++k) { char t[4]; std::snprintf(t, 4, "%02x", unsigned(poolBytes.u8(k))); b2 += t; }
                                SDL_Log("ROUTESEG %02x seg %s pool[%02x] %s", sgi, b1.c_str(), unsigned(poolIndex), b2.c_str());
                            }
                            for (int cz = 0x38; cz < 0x48; ++cz) {
                                std::string row;
                                for (int cx = 0x38; cx < 0x48; ++cx) {
                                    const int k = (cz - gz0) * gw - gx0 + cx;
                                    char buf[8];
                                    const uint32_t ca = grid.cells + uint32_t(k) * 0xc;
                                    if (k < 0 || !od::port::dataMapped(ca, 4)) std::snprintf(buf, sizeof buf, " ----");
                                    else std::snprintf(buf, sizeof buf, " %04x", unsigned(od::port::GridCell(ca).word));
                                    row += buf;
                                }
                                SDL_Log("ROUTECELL z %02x:%s", cz, row.c_str());
                            }
                        }
                        if (std::getenv("OD_ROUTEMAP")) // dev: the floor map of the room (relative heights)
                            for (int j = 0; j < 2 * kHalf; ++j) {
                                std::string row;
                                for (int i = 0; i < 2 * kHalf; ++i) {
                                    const int v = floorY[idx(i, j)];
                                    char c = v == 0x7fff ? '#' : v < py0 - 0x200 ? 'H' : v < py0 - 0x40 ? '^' : v > py0 + 0x40 ? 'v' : '.';
                                    { // a switchable segment cell (breakable walls / gates): 'B'
                                        const int wx = (i - kHalf) * kStep + 0x10, wz = (j - kHalf) * kStep + 0x10;
                                        const int cx = (wx >> 9) + 0x40, cz = (wz >> 9) + 0x40;
                                        const od::port::RoomGridState& grid = od::port::roomGrid();
                                        const int ci2 = (cz - grid.originZ) * grid.width + cx - grid.originX;
                                        const uint32_t ca = grid.cells + uint32_t(ci2) * 0xc;
                                        if (ci2 >= 0 && od::port::dataMapped(ca, 4)) {
                                            const uint32_t cw = od::port::GridCell(ca).head;
                                            if ((grid.roomFlags & od::port::kRoomSwitchableSegments) && (cw & 0x8000) &&
                                                !(cw & 0x4000))
                                                c = c == '#' ? 'B' : 'b';
                                        }
                                    }
                                    if (i == si && j == sj) c = '@';
                                    for (uint32_t e = exits; exitAt(e).id != od::port::kExitEnd; e += od::port::kExitEntrySize)
                                        if (cell(exitAt(e).trigger.x, exitAt(e).trigger.z) == std::pair<int, int>{i, j})
                                            c = char('0' + exitAt(e).destRoom % 10);
                                    row += c;
                                }
                                SDL_Log("ROUTEMAP %02d %s", j, row.c_str());
                            }
                        const auto [gi, gj] = cell(tx, tz);
                        // Dijkstra: floor steps cost 1, a wall / unreachable cell 40 (the way through the thinnest
                        // obstacle: a breakable wall, a block to jump); wallAt marks the path's wall cells
                        std::vector<int> prev(floorY.size(), -1), dist(floorY.size(), INT32_MAX);
                        std::vector<int> qu; // the cells settled (for the log)
                        using QE = std::pair<int, int>;
                        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
                        dist[idx(si, sj)] = 0;
                        prev[idx(si, sj)] = int(idx(si, sj));
                        pq.push({0, int(idx(si, sj))});
                        static constexpr int kD[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
                        while (!pq.empty()) {
                            const auto [dc, c] = pq.top();
                            pq.pop();
                            if (dc != dist[size_t(c)]) continue;
                            qu.push_back(c);
                            const int ci = c % (2 * kHalf), cj = c / (2 * kHalf);
                            if (ci == gi && cj == gj) break;
                            for (const auto& d : kD) {
                                const int ni = ci + d[0], nj = cj + d[1];
                                if (ni < 0 || nj < 0 || ni >= 2 * kHalf || nj >= 2 * kHalf) continue;
                                const size_t n = idx(ni, nj);
                                const int y0 = floorY[size_t(c)] == 0x7fff ? py0 : floorY[size_t(c)];
                                const int up = pl.inWater ? 0x240 : 0x60; // underwater jumps reach far higher
                                bool open = floorY[n] != 0x7fff && floorY[n] >= y0 - up && floorY[n] <= y0 + 0x1000 && unblocked(n);
                                if (open) {
                                    const int16_t mq[3] = {int16_t(((ci + ni) * kStep) / 2 - kHalf * kStep + 0x10), int16_t(y0 - 0x300),
                                                           int16_t(((cj + nj) * kStep) / 2 - kHalf * kStep + 0x10)};
                                    int mg = od::port::probeGroundAt(playerView.env, mq, od::port::CollisionBoxRef{}, 0, routeMask);
                                    if (mg == 0x7fff || mg == -0x4000) {
                                        const int16_t mq2[3] = {mq[0], int16_t(y0 - 0x60), mq[2]};
                                        mg = od::port::probeGroundAt(playerView.env, mq2, od::port::CollisionBoxRef{}, 0, routeMask);
                                    }
                                    open = !(mg == 0x7fff || mg == -0x4000 || mg < y0 - up);
                                }
                                const int cost = dc + (open ? (d[0] && d[1] ? 3 : 2) : 80);
                                if (cost < dist[n]) {
                                    dist[n] = cost;
                                    prev[n] = c;
                                    pq.push({cost, int(n)});
                                }
                            }
                        }
                        const int goal = int(idx(gi, gj));
                        if (prev[size_t(goal)] != -1 && goal != int(idx(si, sj))) {
                            for (int c = goal; c != int(idx(si, sj)); c = prev[size_t(c)]) {
                                path.push_back({(c % (2 * kHalf) - kHalf) * kStep, (c / (2 * kHalf) - kHalf) * kStep});
                                pathWall.push_back(char(dist[size_t(c)] - dist[size_t(prev[size_t(c)])] >= 80));
                            }
                            std::reverse(path.begin(), path.end());
                            std::reverse(pathWall.begin(), pathWall.end());
                        }
                        SDL_Log("ROUTE path ST%02X/%d -> %d: %zu steps (floor %d cells, reached %zu, start %d %d goal %d %d, py %d)", st, sb, next, path.size(), int(std::count_if(floorY.begin(), floorY.end(), [](int v) { return v != 0x7fff; })), qu.size(), si, sj, gi, gj, py0);
                    }
                    while (pathAt < path.size()) {
                        const int64_t wx = path[pathAt].first - px, wz = path[pathAt].second - pz;
                        if (wx * wx + wz * wz > int64_t(0x30) * 0x30) break;
                        ++pathAt;
                    }
                    { // no progress to the waypoint for 200 ticks: block its cell and plan again
                        static size_t progAt = SIZE_MAX;
                        static int64_t bestD = 0;
                        static int noProg = 0;
                        if (pathAt < path.size()) {
                            const int64_t wx = path[pathAt].first - px, wz = path[pathAt].second - pz;
                            const int64_t d2 = wx * wx + wz * wz;
                            if (progAt != pathAt || d2 + 0x400 < bestD) { progAt = pathAt; bestD = d2; noProg = 0; }
                            else if (++noProg > 200) {
                                constexpr int kStep = 0x40, kHalf = 64;
                                blockedCells.insert(size_t((path[pathAt].second / kStep + kHalf) * 2 * kHalf + path[pathAt].first / kStep + kHalf));
                                SDL_Log("ROUTE blocked cell %d %d (no progress), replanning", path[pathAt].first, path[pathAt].second);
                                pathKey = -1;
                                noProg = 0;
                                progAt = SIZE_MAX;
                            }
                        }
                    }
                    if (pathAt < path.size()) {
                        size_t aimAt = pathAt; // across an obstacle run: straight at its far side
                        if (pathWall[pathAt])
                            while (aimAt + 1 < path.size() && pathWall[aimAt]) ++aimAt;
                        faceTo(path[aimAt].first, path[aimAt].second);
                        aimApproach = true;
                        if (pathWall[pathAt]) { // skip the obstacle steps already passed
                            for (size_t k = pathAt; k < aimAt; ++k) {
                                const int64_t ox = path[k].first - px, oz = path[k].second - pz;
                                const int64_t fx = path[aimAt].first - px, fz = path[aimAt].second - pz;
                                if (fx * fx + fz * fz < ox * ox + oz * oz) pathAt = k + 1;
                            }
                        }
                        // an obstacle ahead (no floor / wall): a running jump at its edge; stuck there: shoot it
                        if (pathWall[pathAt] && leap == 0 && pl.subState != 7 && pl.subState != 8)
                            leap = 30;
                        if (pathWall[pathAt] && stuck > 15) aimPad |= (playScriptPos % 6 < 2) ? 0x8000 : 0;
                    } else { // the path ends short of the trigger: walk at it, shooting what is in the way
                        faceTo(tx, tz);    // (breakable walls / floors open to buster hits)
                        aimApproach = true;
                        if (stuck > 20) aimPad |= (playScriptPos % 6 < 2) ? 0x8000 : 0;
                    }
                } else if (seg.walk) { // arrived
                } else { // at the trigger: stand on it facing the exit's yaw and use; then step along it
                    pl.rotation.y = exitAt(ex).trigger.yaw;
                    if (playScriptPos % 6 < 2) aimPad = 0x2000;
                    if (useT > 40) aimApproach = (useT % 4) == 0; // slow steps (passages)
                    if (++useT > 80) useT = 0;
                }
                // stuck: hop, then side-step
                if (std::abs(px - lastX) + std::abs(pz - lastZ) < 2) ++stuck;
                else stuck = 0;
                if (stuck > 30 && hop == 0) hop = 20;
                if (stuck > 90 && side == 0) side = 40;
                if (hop > 0) { aimPad |= 0x4000; --hop; }
                if (leap > 0) {
                    aimPad |= 0x4000;
                    aimApproach = true;
                    --leap;
                }
                if (side > 0) { aimPad |= (playScriptPos / 200) % 2 ? 0x20 : 0x80; aimApproach = false; if (--side == 0) stuck = 0; }
            }
            if (seg.skate && pl.inWater != 0 && uint8_t(pl.legPart) == od::port::kLegsWaterSkates &&
                od::port::eventState().active == 0)
                aimPad |= 0x2000; // held circle sets the 0x2 (skate) bit of +0x10e
            lastX = px;
            lastZ = pz;
            if (playScriptPos % 200 == 0)
                SDL_Log("ROUTE %zu ST%02X/%d next %d exit %08x foe %08x lock %d pos %d %d", playScriptPos, st, sb, next,
                        ex, foe, int(od::port::storyFlags().bytes[0x710 >> 3] & 1), px, pz);

            continue;
        }
        if (seg.ride) {
            static int jumpHold = 0, jumpRest = 0; // a jump = 25 ticks held, then 8 released
            uint32_t mount = 0;
            const auto look = [&](uint32_t base, uint32_t n, uint32_t stride, od::port::ActorPool p) {
                for (uint32_t i = 0; i < n; ++i)
                    if (isScriptClass(od::port::fieldActorHandler(od::port::actorHeaderAt(base + i * stride), p), seg.cls[0])) mount = base + i * stride;
            };
            look(od::port::kAnimatedPool, 24, 0x16c, od::port::ActorPool::Animated);
            const auto faceTo = [](int x, int z) { faceToward(int32_t(x * 65536), int32_t(z * 65536)); };
            const int px = pl.pos.x.whole, py = pl.pos.y.whole, pz = pl.pos.z.whole;
            const int64_t tdx = seg.tgt[0] - px, tdz = seg.tgt[2] - pz;
            const od::port::Actor& mountActor = od::port::actorAt(mount);
            const bool onMount =
                mount && pl.onPlatform != 0 && (od::port::slotIdentityWord(pl.platform) & 0xffffff) == (mount & 0xffffff);
            const bool atTarget = tdx * tdx + tdz * tdz < int64_t(0x180) * 0x180 && py < seg.tgt[1] + 0x40;
            if (atTarget) {
                faceTo(seg.tgt[0], seg.tgt[2]);
            } else if (onMount) { // ride; leap toward the target once the mount passes near it
                const int64_t mx = seg.tgt[0] - mountActor.pos.x.whole, mz = seg.tgt[2] - mountActor.pos.z.whole;
                faceTo(seg.tgt[0], seg.tgt[2]);
                const int64_t md = mx * mx + mz * mz;
                if (md < int64_t(0x400) * 0x400) aimApproach = true; // run toward it along the back ...
                if (md < int64_t(0x3a0) * 0x3a0 && jumpHold == 0 && jumpRest == 0) jumpHold = 30; // ... and leap
            } else if (mount && pl.airborne == 0 && pl.subState != 7 && pl.subState != 8 && pl.subState != 9 &&
                       py > -0x100) { // on the floor: wait ahead of it, jump as it comes
                const double vx = mountActor.pos.x.whole - mountActor.prevPos.x.whole,
                             vz = mountActor.pos.z.whole - mountActor.prevPos.z.whole, vl = std::sqrt(vx * vx + vz * vz);
                // wait where it heads: its waypoint (the ST38 boss: content/nino/platform_boss), else ahead along
                // its course
                int ax = mountActor.pos.x.whole + (vl > 0.5 ? int(vx / vl * 0x600) : 0),
                    az = mountActor.pos.z.whole + (vl > 0.5 ? int(vz / vl * 0x600) : 0);
                if (od::stateRoutine(seg.cls[0]).is(od::port::kPlatformBossClass)) {
                    const od::port::PlatformBossTarget w = od::port::platformBossTarget(od::port::animatedActorAt(mount));
                    ax = w.x;
                    az = w.z;
                }
                const int64_t dx = mountActor.pos.x.whole - px, dz = mountActor.pos.z.whole - pz,
                              wx = ax - px, wz = az - pz;
                const int64_t d2 = dx * dx + dz * dz;
                const bool coming = -(dx * vx + dz * vz) > 0; // it moves toward the player
                if (coming && d2 < int64_t(0x170) * 0x170 && d2 > int64_t(0x100) * 0x100) {
                    faceTo(mountActor.pos.x.whole, mountActor.pos.z.whole);
                    aimApproach = true; // a running jump at it (FUN_0057a420 wants a direction held)
                    if (jumpHold == 0 && jumpRest == 0) jumpHold = 25;
                } else if (coming && d2 < int64_t(0x700) * 0x700) { // it comes: stand and face it
                    faceTo(mountActor.pos.x.whole, mountActor.pos.z.whole);
                } else {
                    faceTo(ax, az);
                    aimApproach = wx * wx + wz * wz > int64_t(0x40) * 0x40;
                }
            } else if (!mount) {
                faceTo(seg.tgt[0], seg.tgt[2]);
            } else if (py < mountActor.pos.y.whole - 0x40) { // airborne above the mount: held jump toward the target
                faceTo(seg.tgt[0], seg.tgt[2]);
                aimPad = 0x4000;
                aimApproach = true;
            } else { // airborne, not above it yet: keep the jump held, no steering into its underside
                faceTo(mountActor.pos.x.whole, mountActor.pos.z.whole);
                aimPad = 0x4000;
            }
            if (jumpHold > 0) {
                aimPad |= 0x4000;
                if (--jumpHold == 0) jumpRest = 8;
            } else if (jumpRest > 0) {
                aimPad &= uint16_t(~0x4000u);
                --jumpRest;
            }
            if (playScriptPos % 100 == 0 || (!onMount && py < -0x40))
                SDL_Log("RIDE %zu p %d %d %d mount %08x %d %d %d sub %u on %d at %d", playScriptPos, px, py, pz, mount,
                        mount ? int(mountActor.pos.x.whole) : 0, mount ? int(mountActor.pos.y.whole) : 0,
                        mount ? int(mountActor.pos.z.whole) : 0, mount ? unsigned(mountActor.subState) : 0u,
                        int(onMount), int(atTarget));
            continue;
        }
        if (seg.catcher) {
            const auto find = [&](uint32_t cls, auto&& take) {
                const auto pool = [&](uint32_t base, uint32_t n, uint32_t stride, od::port::ActorPool p) {
                    for (uint32_t i = 0; i < n; ++i)
                        if (isScriptClass(od::port::fieldActorHandler(od::port::actorHeaderAt(base + i * stride), p), cls)) take(base + i * stride);
                };
                pool(od::port::kAnimatedPool, 24, 0x16c, od::port::ActorPool::Animated);
                pool(od::port::kStaticPool, 64, 0xcc, od::port::ActorPool::Statics);
            };
            uint32_t anchor = 0, face = 0, near = 0;
            if (seg.anchor) find(seg.anchor, [&](uint32_t a) { anchor = a; });
            if (seg.facing) find(seg.facing, [&](uint32_t a) { face = a; });
            const auto faceTo = [](int x, int z) { faceToward(int32_t(x * 65536), int32_t(z * 65536)); };
            if (seg.protect && anchor) od::port::actorAt(anchor).hitPoints = od::port::actorAt(anchor).maxHitPoints;
            const int px = pl.pos.x.whole, pz = pl.pos.z.whole;
            const bool carrying = pl.interactionTarget.hasMode(od::port::kInteractLiftMode);
            aimPad = 0;
            if (seg.spray && anchor) { // smoke on the anchor (+0x15c): run to it and spray (triangle)
                double gx = actorX(anchor), gz = actorZ(anchor);
                const bool smoke = anchorSmoking(anchor);
                if (face && !smoke) { // else wait out of the way, toward FACING
                    const double fx = actorX(face) - gx, fz = actorZ(face) - gz,
                                 fl = std::max(1.0, std::sqrt(fx * fx + fz * fz));
                    gx += fx / fl * 0x900;
                    gz += fz / fl * 0x900;
                }
                const double d2 = (gx - px) * (gx - px) + (gz - pz) * (gz - pz);
                if (smoke && d2 < double(0x280) * 0x280) {
                    faceTo(actorX(anchor), actorZ(anchor));
                    aimPad = 0x1000;
                } else if (smoke) {
                    faceTo(int(gx), int(gz));
                    aimApproach = true;
                } else { // keep moving: circle the waiting point (homing shots turn slowly)
                    const double ang = double(playScriptPos % 240) * 6.2831853 / 240.0;
                    faceTo(int(gx + std::sin(ang) * 0x280), int(gz + std::cos(ang) * 0x280));
                    aimApproach = true;
                }
                continue;
            }
            if (seg.guard) {
                uint32_t tgt = 0;
                int64_t tdist = INT64_MAX;
                for (uint32_t c : seg.cls)
                    find(c, [&](uint32_t a) {
                        const od::port::ActorHeader& header = od::port::actorHeaderAt(a);
                        if (!anchor || header.state != 1 || header.variant == 1) return;
                        const int64_t ax = actorX(a) - px, az = actorZ(a) - pz;
                        if (ax * ax + az * az < tdist) { tdist = ax * ax + az * az; tgt = a; }
                    });
                double gx = anchor ? actorX(anchor) : px, gz = anchor ? actorZ(anchor) : pz;
                if (anchor && face) {
                    const double fx = actorX(face) - gx, fz = actorZ(face) - gz,
                                 fl = std::max(1.0, std::sqrt(fx * fx + fz * fz));
                    gx += fx / fl * 0x700;
                    gz += fz / fl * 0x700;
                }
                if (tgt) {
                    faceTo(actorX(tgt), actorZ(tgt));
                    aimPad = 0x200 | (playScriptPos % 6 < 3 ? 0x1000 : 0);
                } else if ((gx - px) * (gx - px) + (gz - pz) * (gz - pz) > 0x100 * 0x100) {
                    faceTo(int(gx), int(gz));
                    aimApproach = true;
                } else if (face) {
                    faceTo(actorX(face), actorZ(face));
                }
                continue;
            }
            if (carrying) { // lock (R1) on a missile heading for the anchor, else the facing actor; throw
                uint32_t tgt = 0;
                int64_t tdist = INT64_MAX;
                for (uint32_t c : seg.cls)
                    find(c, [&](uint32_t a) {
                        const od::port::ActorHeader& header = od::port::actorHeaderAt(a);
                        if (!anchor || header.variant != 2 || header.state != 1) return;
                        const int64_t ax = actorX(a) - actorX(anchor), az = actorZ(a) - actorZ(anchor);
                        if (ax * ax + az * az < tdist) { tdist = ax * ax + az * az; tgt = a; }
                    });
                if (!tgt) tgt = face;
                if (tgt) faceTo(actorX(tgt), actorZ(tgt));
                aimPad = 0x200;
                if (playScriptPos % 10 < 2) aimPad |= 0x1000;
            } else {
                int64_t nd = INT64_MAX;
                uint32_t threat = 0; // the nearest one heading for the anchor (static variant 2) close to it
                int64_t td = INT64_MAX;
                for (uint32_t c : seg.cls)
                    find(c, [&](uint32_t a) {
                        const int64_t dx = actorX(a) - px, dz = actorZ(a) - pz;
                        if (dx * dx + dz * dz < nd) { nd = dx * dx + dz * dz; near = a; }
                        const od::port::ActorHeader& header = od::port::actorHeaderAt(a);
                        if (anchor && header.variant == 2 && header.state == 1) {
                            const int64_t ax = actorX(a) - actorX(anchor), az = actorZ(a) - actorZ(anchor);
                            if (ax * ax + az * az < int64_t(0x900) * 0x900 && ax * ax + az * az < td) {
                                td = ax * ax + az * az;
                                threat = a;
                            }
                        }
                    });
                if (near && nd < int64_t(0x180) * 0x180) {
                    faceTo(actorX(near), actorZ(near));
                    aimPad = 0x1000; // held: the lift query reads the held buttons
                } else if (anchor) { // the guard point: from the anchor toward the facing actor
                    double gx = actorX(anchor), gz = actorZ(anchor);
                    if (face) {
                        const double fx = actorX(face) - gx, fz = actorZ(face) - gz,
                                     fl = std::max(1.0, std::sqrt(fx * fx + fz * fz));
                        gx += fx / fl * 0xa00;
                        gz += fz / fl * 0xa00;
                    }
                    if ((gx - px) * (gx - px) + (gz - pz) * (gz - pz) > 0x100 * 0x100) {
                        faceTo(int(gx), int(gz));
                        aimApproach = true;
                    } else if (face) {
                        faceTo(actorX(face), actorZ(face));
                    }
                }
            }
            continue;
        }
        uint32_t best = 0, from = pl.at;
        int64_t bestD = INT64_MAX;
        const auto scan = [&](const std::vector<uint32_t>& cls, auto&& take) {
            const auto pool = [&](uint32_t base, uint32_t n, uint32_t stride, od::port::ActorPool p) {
                for (uint32_t i = 0; i < n; ++i) {
                    const uint32_t a = base + i * stride;
                    const od::RoutineRef h = od::port::fieldActorHandler(od::port::actorHeaderAt(a), p);
                    if (std::any_of(cls.begin(), cls.end(), [&](uint32_t c) { return isScriptClass(h, c); })) take(a);
                }
            };
            pool(od::port::kAnimatedPool, 24, 0x16c, od::port::ActorPool::Animated);
            pool(od::port::kStaticPool, 64, 0xcc, od::port::ActorPool::Statics);
        };
        if (seg.anchor) scan({seg.anchor}, [&](uint32_t a) { from = a; });
        scan(seg.cls, [&](uint32_t a) {
            const int64_t dx = actorX(a) - actorX(from), dz = actorZ(a) - actorZ(from);
            if (dx * dx + dz * dz < bestD) {
                bestD = dx * dx + dz * dz;
                best = a;
            }
        });
        if (from != pl.at) { // guarding the anchor: stay by it, shoot what comes near it
            if (bestD > int64_t(0xa00) * 0xa00) best = 0;
            double gx = actorX(from), gz = actorZ(from); // the guard point
            uint32_t face = 0;
            if (seg.facing) scan({seg.facing}, [&](uint32_t a) { face = a; });
            if (face) {
                const double fx = actorX(face) - gx, fz = actorZ(face) - gz,
                             fl = std::max(1.0, std::sqrt(fx * fx + fz * fz));
                gx += fx / fl * 0x400;
                gz += fz / fl * 0x400;
            }
            const int plx = pl.pos.x.whole, plz = pl.pos.z.whole;
            const double dx = gx - plx, dz = gz - plz;
            const bool away = dx * dx + dz * dz > double(0x180) * 0x180;
            if (away || !best) {
                const double ax = away ? dx : (face ? actorX(face) - plx : dx),
                             az = away ? dz : (face ? actorZ(face) - plz : dz);
                    faceToward(int32_t(plx + int(ax)) * 65536, int32_t(plz + int(az)) * 65536);
                aimApproach = away;
                best = 0;
            } else {
                bestD = 0;
            }
        }
        if (best) {
            const int yaw = od::port::angleOf(od::port::actorAt(best).pos, pl.pos);
            pl.rotation.y = int16_t(yaw);

            aimApproach = bestD > int64_t(seg.reach) * seg.reach; // far: walk toward it
            aimBack = !aimApproach && seg.reach > 0x100 && bestD < int64_t(0x140) * 0x140; // point blank: back off
            if (seg.strafe && !aimApproach) aimSide = (playScriptPos / 90) % 2 ? 0x20 : 0x80;
        }
    }
}

void App::playscriptLog() {
    if (!playScript.empty()) { // --playscript trace: the player's state / lift target / room script on change
        const od::port::Player& pl = od::port::player();
        static std::string lastTrace;
        char tr[256];
        std::snprintf(tr, sizeof tr, "state %u/%u held %04x target %08x script %08x ev %02x flag27 %u sw %02x mode %02x leg %02x skate %04x be %02x 13c %02x 149 %02x h %04x",
                      unsigned(pl.state), unsigned(pl.subState), unsigned(pl.carryFlags),
                      unsigned(pl.interactionTarget.word()), unsigned(od::port::scriptState().headBytesWord()),
                      unsigned(od::port::eventState().active), unsigned(od::port::storyFlag(0x27)),
                      unsigned(uint8_t(pl.activeSpecial)), unsigned(od::port::gameState().frontEnd),
                      unsigned(uint8_t(pl.legPart)), unsigned(pl.button(od::port::PlayerButton::Walk)),
                      unsigned(pl.inWater), unsigned(uint8_t(pl.armFlags)), unsigned(uint8_t(pl.skateHoldTicks)),
                      unsigned(pl.held));
        static uint32_t lifted = 0; // the last lift target (0x2000000 | actor): where it is
        if ((pl.interactionTarget.word() & 0xff000000u) == od::port::kInteractLiftMode)
            lifted = pl.interactionTarget.word() & od::port::kInteractActorMask;
        if (lifted) {
            const size_t n = std::strlen(tr);
            const od::port::Actor& liftedActor = od::port::actorAt(lifted);
            std::snprintf(tr + n, sizeof tr - n, " | lifted %06x %d %d %d st %u/%u", lifted,
                          int(liftedActor.pos.x.whole), int(liftedActor.pos.y.whole), int(liftedActor.pos.z.whole),
                          unsigned(liftedActor.state), unsigned(liftedActor.subState));
        }
        if (lastTrace != tr || std::getenv("OD_POSLOG")) {
            SDL_Log("playscript tick %zu pos %d %d %d %s", playScriptPos, int(pl.pos.x.whole), int(pl.pos.y.whole),
                    int(pl.pos.z.whole), tr);
            lastTrace = tr;
        }
    }
    if (!playScript.empty()) { // playthrough timeline
        const std::string st = ptState();
        if (st != ptLast) {
            SDL_Log("pt %llu %zu %s", (unsigned long long)ptTicks, playScriptPos, st.c_str());
            ptLast = st;
        }
        ++ptTicks;
    }
    static bool scriptLogged = false; // end of --playscript: where the player ended up (once)
    if (!scriptLogged && !playScript.empty() && playScriptPos == playScript.size() && playerView.play) {
        const od::port::Player& pl = od::port::player();
        SDL_Log("playscript end pos %d %d %d state %d/%d on actor %d (%08x)", int(pl.pos.x.whole), int(pl.pos.y.whole),
                int(pl.pos.z.whole), int(pl.state), int(pl.subState), int(pl.onPlatform), unsigned(od::port::slotIdentityWord(pl.platform)));
        scriptLogged = true;
        // the posed player (bone points +0x490, Maybe_UpdatePlayer's skeleton): where the model is drawn
        const od::port::ShortVec3& bone0 = pl.bonePoints[od::port::kBodyBone];
        const od::port::ShortVec3& bone5 = pl.bonePoints[od::port::kRightUpperArmBone];
        SDL_Log("playscript end player anim %02x bone0 %d %d %d bone5 %d %d %d visible %u", unsigned(pl.body.anim),
                int(bone0.x), int(bone0.y), int(bone0.z), int(bone5.x), int(bone5.y), int(bone5.z),
                unsigned(pl.flags & od::port::kActorDrawn));
        for (uint32_t i = 0; i < 24; ++i) { // the live pool-0x20 actors at the end (stall diagnosis)
            const uint32_t q = od::port::kAnimatedPool + i * 0x16c;
            const od::port::AnimatedActor& act = od::port::animatedActorAt(q);
            if (!act.live()) continue;
            SDL_Log("endactor %u class %06x b0 %02x b1 %02x st %u/%u/%u hp %d f160 %02x var %u pos %d %d %d model %08x",
                    i, od::port::fieldActorHandler(od::port::actorHeaderAt(q), od::port::ActorPool::Animated).pointerValue(),
                    unsigned(act.flags), unsigned(act.runFlags), unsigned(act.state), unsigned(act.subState),
                    unsigned(act.step), int(act.hitPoints), unsigned(act.shownParts), unsigned(act.variant),
                    int(act.pos.x.whole), int(act.pos.y.whole), int(act.pos.z.whole), unsigned(act.model));
        }
        for (uint32_t i = 0; i < 64; ++i) { // the live static-pool actors (traps, platforms, doors)
            const uint32_t q = od::port::kStaticPool + i * 0xcc;
            const od::port::StaticActor& act = od::port::staticActorAt(q);
            if (!(act.flags & od::port::kActorLive)) continue;
            od::RoutineRef h = od::port::actorHandler(od::port::actorHeaderAt(q), od::port::ActorPool::Statics, false);
            if (!h) h = od::port::actorHandler(od::port::actorHeaderAt(q), od::port::ActorPool::StaticsLate, false);
            SDL_Log("endstatic %u class %06x type %02x var %u st %u/%u live %d pos %d %d %d", i, h.pointerValue(), unsigned(act.type),
                    unsigned(act.variant), unsigned(act.state), unsigned(act.subState),
                    int(playerView.actorLive(q, false)), int(act.pos.x.whole), int(act.pos.y.whole),
                    int(act.pos.z.whole));
        }
        SDL_Log("endcounter a7ad90 %u", unsigned(od::port::encounterState().waveEnemies));
        {
            for (const od::DynamicObject& o : playerView.liveActors(current))
                SDL_Log("playscript end live actor object %08x: %zu triangles", o.id, o.tris.size() / 3);
        }
        playerView.logTownNpcs();
        {
            SDL_Log("pt end %llu %zu %s pos %d %d %d", (unsigned long long)ptTicks, playScriptPos,
                    ptState().c_str(), int(pl.pos.x.whole), int(pl.pos.y.whole), int(pl.pos.z.whole));
            std::string hex;
            char h[3];
            for (size_t k = 0; k < od::port::kStoryFlagBytes; ++k) {
                std::snprintf(h, sizeof h, "%02x", unsigned(od::port::storyFlags().bytes[k]));
                hex += h;
            }

            SDL_Log("pt flags %s", hex.c_str());
            if (!saveFileArg.empty()) playerView.saveGame(saveFileArg, 0);
        }
        if (saveGameArg >= 0 && saveGameArg <= 99) {
            char nm[24];
            std::snprintf(nm, sizeof nm, "DASH2-%02d.DAT", saveGameArg);
            playerView.saveGame(savesDir / nm, uint8_t(saveGameArg));
        }
        if (shotPath.empty()) scriptEnded = true; // --playscript without --shot: quit after this frame
    }
}

} // namespace od::app
