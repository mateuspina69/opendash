// The network state of a world's actors (game/world_net_state). With no arguments: the rules of the data path, on tables
// built by hand. With arguments: a real world process and a real receiver process, joined through this test as the
// coordinator is (with and without a link's delay and loss), in rooms with enemies. Each process writes each actor's pose per
// tick in a local file (a pose never crosses the network) and the files are compared afterwards.
//   test_world_net [<opendash.exe> <save image> <folder for the files> [<ST:SUB> ...]]
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "app/world_link.hpp"
#include "check.hpp"
#include "game/player/net_player.hpp"
#include "game/player/remote_combat.hpp"
#include "game/global_state.hpp"
#include "game/story_table.hpp"
#include "game/world_net_state.hpp"

using namespace od::port;
using od::app::WorldLink;
using od::app::WorldMessage;
using od::app::WorldMessageType;

namespace {

// ---- the rules, on tables built by hand ---------------------------------------------------------------------------------

constexpr uint8_t kSlotA = 0x20, kSlotB = 0x21, kStatic = 0x60;

NetWorldTable::Seen seenAt(int x, uint8_t anim = 3, uint32_t script = 0x1000) {
    NetWorldTable::Seen s;
    s.type = 7;
    s.modelEntry = 2;
    s.model = 0x5000;
    s.animTable = 0x6000;
    s.animScript = script;
    s.pos = {int16_t(x), 10, 20};
    s.rot = {0, 100, 0};
    s.anim = anim;
    s.shown = true;
    return s;
}

struct Recorded : NetClientActors {
    std::map<uint8_t, NetActorIdentity> live;
    std::map<uint8_t, NetActorState> last;
    std::vector<std::pair<uint8_t, AnimStart>> starts;
    void appear(uint8_t slot, const NetActorIdentity& identity, const NetActorState&) override { live[slot] = identity; }
    void go(uint8_t slot) override {
        live.erase(slot);
        last.erase(slot);
    }
    void coordinateShift(uint8_t) override {}
    std::vector<NetRoomPatch> patches;
    void roomPatch(const NetRoomPatch& p) override { patches.push_back(p); }
    std::vector<NetEffect> effects;
    void effect(const NetEffect& e) override { effects.push_back(e); }
    void tick(uint8_t slot, const NetActorState& state, const std::optional<AnimStart>& start) override {
        last[slot] = state;
        if (start) starts.emplace_back(slot, *start);
    }
};

constexpr size_t kHeader = 10;

void testChangesOnly() {
    NetWorldTable t;
    NetStateSender sender;
    Recorded rec;
    NetStateClient client(rec);
    t.tick = 0;
    t.seen(kSlotA, seenAt(100));
    t.seen(kStatic, seenAt(500));
    for (const auto& batch : sender.events(t)) CHECK(client.applyEvents(batch));
    // told by the appearance: nothing to say
    auto u0 = sender.update(t);
    CHECK(u0.size() == kHeader);
    const auto seq0 = client.applyUpdate(u0);
    CHECK(seq0.has_value());
    client.advance(0);
    CHECK(rec.live.size() == 2);
    CHECK(rec.live[kSlotA].modelEntry == 2 && rec.live[kSlotA].type == 7);
    CHECK(rec.starts.size() == 2); // both animated in this table; the animation started on tick 0
    // one axis moved: slot, mask, one coordinate
    t.tick = 2;
    t.seen(kSlotA, seenAt(104));
    t.seen(kStatic, seenAt(500));
    auto u1 = sender.update(t);
    CHECK(u1.size() == kHeader + 2 + 2);
    const auto seq1 = client.applyUpdate(u1);
    CHECK(seq1.has_value());
    // not acknowledged: it goes again; acknowledged: it does not
    t.tick = 4;
    CHECK(sender.update(t).size() == kHeader + 2 + 2);
    sender.acknowledge(*seq1);
    t.tick = 6;
    CHECK(sender.update(t).size() == kHeader);
    // the position between two states
    client.advance(1);
    CHECK(rec.last[kSlotA].pos[0] == 102);
    client.advance(2);
    CHECK(rec.last[kSlotA].pos[0] == 104);
}

void testLostUpdate() {
    NetWorldTable t;
    NetStateSender sender;
    Recorded rec;
    NetStateClient client(rec);
    t.seen(kSlotA, seenAt(100));
    for (const auto& batch : sender.events(t)) client.applyEvents(batch);
    sender.acknowledge(*client.applyUpdate(sender.update(t)));
    t.tick = 2;
    t.seen(kSlotA, seenAt(110));
    const auto lost = sender.update(t); // never arrives
    t.tick = 4;
    t.seen(kSlotA, seenAt(120, 5, 0x2000)); // and an animation starts
    t.tick = 5;
    const auto late = sender.update(t);
    t.tick = 6;
    t.seen(kSlotA, seenAt(130, 5, 0x2000));
    const auto newest = sender.update(t);
    const auto seq = client.applyUpdate(newest);
    CHECK(seq.has_value());
    // a stale update is simply replaced
    CHECK(!client.applyUpdate(late).has_value());
    CHECK(!client.applyUpdate(lost).has_value());
    rec.starts.clear();
    client.advance(6);
    CHECK(rec.last[kSlotA].pos[0] == 130);
    CHECK(rec.starts.size() == 1);
    // the animation has been in play since the tick it was chosen, whenever the client hears of it
    CHECK(!rec.starts.empty() && rec.starts[0].second.anim == 5 && rec.starts[0].second.steps == 2);
    // a value that goes back to what is acknowledged, while the client holds another, goes at once
    sender.acknowledge(*seq);
    t.tick = 8;
    t.seen(kSlotA, seenAt(100, 5, 0x2000));
    const auto a = sender.update(t);
    CHECK(a.size() > kHeader);
    t.tick = 10;
    t.seen(kSlotA, seenAt(130, 5, 0x2000)); // back to what is acknowledged
    const auto back = sender.update(t);
    CHECK(back.size() > kHeader);
    CHECK(client.applyUpdate(a).has_value());
    const auto seqBack = client.applyUpdate(back);
    CHECK(seqBack.has_value());
    client.advance(10);
    CHECK(rec.last[kSlotA].pos[0] == 130);
    // and once the client holds it, nothing more goes
    sender.acknowledge(*seqBack);
    t.tick = 12;
    CHECK(sender.update(t).size() == kHeader);
}

void testEventsFirstAndReuse() {
    NetWorldTable t;
    NetStateSender sender;
    Recorded rec;
    NetStateClient client(rec);
    t.seen(kSlotA, seenAt(100));
    const auto first = sender.events(t);
    const auto u = sender.update(t);
    // the update arrives before the actors it counts on: it waits for them
    CHECK(!client.applyUpdate(u).has_value());
    for (const auto& batch : first) CHECK(client.applyEvents(batch));
    CHECK(client.applyHeld().has_value());
    client.advance(0);
    CHECK(rec.live.count(kSlotA) == 1);
    // the slot's occupant changes: goes and appears, in order, and an update built before says nothing of the new one
    t.tick = 2;
    t.seen(kSlotA, seenAt(150));
    const auto before = sender.update(t);
    t.tick = 4;
    t.gone(kSlotA);
    t.tick = 6;
    NetWorldTable::Seen other = seenAt(300);
    other.type = 9;
    other.modelEntry = 4;
    t.seen(kSlotA, other);
    const auto batches = sender.events(t);
    CHECK(batches.size() == 1 && batches[0].size() == 8 + 2 + 26);
    for (const auto& batch : batches) CHECK(client.applyEvents(batch));
    CHECK(client.applyUpdate(before).has_value());
    client.advance(5);
    CHECK(rec.live[kSlotA].type == 7);
    client.advance(6);
    CHECK(rec.live[kSlotA].type == 9 && rec.last[kSlotA].pos[0] == 300);
    // an actor goes
    t.tick = 8;
    t.gone(kSlotA);
    for (const auto& batch : sender.events(t)) CHECK(client.applyEvents(batch));
    client.advance(7);
    CHECK(rec.live.count(kSlotA) == 1);
    client.advance(8);
    CHECK(rec.live.count(kSlotA) == 0);
}

void testFitsOneDatagram() {
    NetWorldTable t;
    NetStateSender sender;
    const auto fill = [&](int d) {
        for (int i = 0; i < 24; ++i) {
            NetWorldTable::Seen s = seenAt(i * 8 + d);
            s.pos = {int16_t(i + d), int16_t(i + d), int16_t(i + d)};
            s.rot = {int16_t(d), int16_t(d), int16_t(d)};
            t.seen(uint8_t(0x20 + i), s);
        }
        for (int i = 0; i < 64; ++i) {
            NetWorldTable::Seen s = seenAt(i * 8 + d);
            s.pos = {int16_t(i + d), int16_t(i + d), int16_t(i + d)};
            s.rot = {int16_t(d), int16_t(d), int16_t(d)};
            t.seen(uint8_t(0x60 + i), s);
        }
    };
    t.tick = 0;
    fill(0);
    for (const auto& batch : sender.events(t)) CHECK(batch.size() <= kNetMaxEventBatch);
    t.tick = 2;
    fill(1);
    std::map<uint8_t, int> told;
    for (int round = 0; round < 3; ++round) {
        const auto u = sender.update(t);
        CHECK(u.size() <= kNetMaxPayload);
        // walks past what the last one held: every actor is in some update (the mask of a whole entry: 14 bytes)
        for (size_t at = kHeader; at + 14 <= u.size(); at += 14) ++told[u[at]];
    }
    CHECK(sender.updatesCut() >= 1);
    CHECK(told.size() == 88);
}

// what a class changes of its model, and a pose held: small fields, only when they change
void testVisualFields() {
    NetWorldTable t;
    NetStateSender sender;
    Recorded rec;
    NetStateClient client(rec);
    NetWorldTable::Seen s = seenAt(100);
    s.bound.scale = {0x200, 0x200, 0x200};
    s.bound.tint = 0x4210;
    s.visual = s.bound;
    t.seen(kSlotA, s);
    for (const auto& batch : sender.events(t)) CHECK(client.applyEvents(batch));
    sender.acknowledge(*client.applyUpdate(sender.update(t))); // what the bind gives is not told
    t.tick = 2;
    s.visual.scale = {0x220, 0x1e0, 0x220};
    s.visual.tint = 0x7fff;
    s.visual.hiddenParts = 0x10004;
    s.aim = {{2, {0, 40, 0}}, {5, {7, 0, -3}}};
    t.seen(kSlotA, s);
    const auto u = sender.update(t);
    // slot, mask, second mask, scale, tint, parts, count, an entry of one lane, an entry of two
    CHECK(u.size() == kHeader + 3 + 6 + 2 + 3 + 1 + 3 + 5);
    const auto seq = client.applyUpdate(u);
    CHECK(seq.has_value());
    client.advance(2);
    const NetActorState& shown = rec.last[kSlotA];
    CHECK(shown.visual.scale[1] == 0x1e0 && shown.visual.tint == 0x7fff && shown.visual.hiddenParts == 0x10004);
    CHECK(shown.told == (kNetToldScale | kNetToldTint | kNetToldParts));
    CHECK(shown.aim == s.aim);
    sender.acknowledge(*seq);
    t.tick = 4;
    t.seen(kSlotA, s);
    CHECK(sender.update(t).size() == kHeader);
    // the animation stays where it is: one flag, with the key
    t.tick = 6;
    s.stepped = false;
    s.stayed = true;
    s.key = 9;
    s.ticksLeft = 2;
    s.atStart = false;
    t.seen(kSlotA, s);
    const auto h = sender.update(t);
    CHECK(h.size() == kHeader + 3 + 1 + 1 + 3); // slot, masks, animation, what it needs more: held, the key
    rec.starts.clear();
    const auto seqHeld = client.applyUpdate(h);
    CHECK(seqHeld.has_value());
    client.advance(6);
    CHECK(rec.starts.size() == 1 && rec.starts[0].second.held && rec.starts[0].second.keyed &&
          rec.starts[0].second.key == 9 && rec.starts[0].second.ticks == 2);
    sender.acknowledge(*seqHeld);
    // it stays: nothing goes; it steps again: told once
    t.tick = 8;
    t.seen(kSlotA, s);
    CHECK(sender.update(t).size() == kHeader);
    t.tick = 10;
    s.stepped = true;
    s.stayed = false;
    t.seen(kSlotA, s);
    rec.starts.clear();
    CHECK(client.applyUpdate(sender.update(t)).has_value());
    client.advance(10);
    CHECK(rec.starts.size() == 1 && !rec.starts[0].second.held);
}

// a shot moves by itself from a point and its steps; a new path only when it leaves that one
void testPaths() {
    constexpr uint8_t kShot = 0x45;
    NetWorldTable t;
    NetStateSender sender;
    Recorded rec;
    NetStateClient client(rec);
    const auto shotAt = [](int x16) { // x in 1/16 unit
        NetWorldTable::Seen s;
        s.type = 3;
        s.onPath = true;
        s.fine = {x16 << 12, 0, 0};
        s.pos = {int16_t(x16 >> 4), 0, 0};
        return s;
    };
    t.seen(kShot, shotAt(0));
    for (const auto& batch : sender.events(t)) CHECK(client.applyEvents(batch));
    t.tick = 1;
    t.seen(kShot, shotAt(10 * 16)); // leaves the still point: a path, from the tick before
    t.tick = 2;
    t.seen(kShot, shotAt(20 * 16));
    const auto u = sender.update(t);
    CHECK(u.size() == kHeader + 3 + 2 + 2); // slot, masks, age, which steps, x's step
    CHECK(client.applyUpdate(u).has_value());
    client.advance(0);
    client.advance(1);
    CHECK(rec.last[kShot].pos[0] == 10);
    client.advance(2);
    CHECK(rec.last[kShot].pos[0] == 20);
    sender.acknowledge(1);
    // straight on: nothing goes, and the client moves it
    for (uint32_t tick = 3; tick <= 12; ++tick) {
        t.tick = tick;
        t.seen(kShot, shotAt(int(tick) * 160));
        if (tick % 2 == 0) CHECK(sender.update(t).size() == kHeader);
        client.advance(tick);
    }
    CHECK(rec.last[kShot].pos[0] == 120);
    // it turns: a new path
    t.tick = 13;
    t.seen(kShot, shotAt(120 * 16));
    t.tick = 14;
    t.seen(kShot, shotAt(120 * 16));
    CHECK(sender.update(t).size() > kHeader);
}

void testEffects() {
    NetWorldTable t;
    NetStateSender sender;
    Recorded rec;
    NetStateClient client(rec);
    NetWorldTable::Seen s;
    s.type = 7;
    s.effect = true;
    s.effectGoes = true;
    s.effectStart.pool = 0x60;
    s.effectStart.type = 7;
    s.effectStart.pos = {1, 2, 3};
    s.effectStart.params = {4, 5, 6, 7};
    s.pos = {1, 2, 3};
    t.seen(0x61, s);
    auto batches = sender.events(t);
    CHECK(batches.size() == 1 && batches[0].size() == 8 + 35);
    for (const auto& batch : batches) CHECK(client.applyEvents(batch));
    CHECK(sender.update(t).size() == kHeader); // no state for it
    client.advance(0);
    CHECK(rec.effects.size() == 1 && rec.effects[0] == s.effectStart);
    CHECK(rec.live.empty());
    // it ends on the world: nothing to say. Another starts in the slot: one more event.
    t.tick = 1;
    t.gone(0x61);
    CHECK(sender.events(t).empty());
    t.tick = 2;
    t.seen(0x61, s);
    CHECK(sender.events(t).size() == 1);
    // one that follows what only the world holds does not go
    NetStateSender second;
    s.effectGoes = false;
    NetWorldTable t2;
    t2.seen(0x62, s);
    CHECK(second.events(t2).empty());
}

// a lock-on point: told when it moves past the tolerance, then its position alone
void testLockPoints() {
    NetWorldTable t;
    NetStateSender sender;
    Recorded rec;
    NetStateClient client(rec);
    NetWorldTable::Seen s = seenAt(100);
    s.lock = {{{4, -40, 0}, 0x1234, 0x210000, false}};
    t.seen(kSlotA, s);
    for (const auto& batch : sender.events(t)) CHECK(client.applyEvents(batch));
    const auto full = sender.update(t);
    CHECK(full.size() == kHeader + 3 + 1 + 4 + 3 + 2); // slot, masks, count, owner, offset, shape
    const auto seq = client.applyUpdate(full);
    CHECK(seq.has_value());
    client.advance(0);
    CHECK(rec.last[kSlotA].lock == s.lock);
    sender.acknowledge(*seq);
    // a fall needs no new path each tick (checked below in testPaths)
    t.tick = 2;
    s.lock[0].offset[1] = -43; // within the tolerance
    t.seen(kSlotA, s);
    CHECK(sender.update(t).size() == kHeader);
    t.tick = 4;
    s.lock[0].offset[1] = -60;
    t.seen(kSlotA, s);
    const auto moved = sender.update(t);
    CHECK(moved.size() == kHeader + 3 + 1 + 3);
    CHECK(client.applyUpdate(moved).has_value());
    client.advance(4);
    CHECK(rec.last[kSlotA].lock == s.lock);
}

// a client's own shots do not come back to it
void testOwnShotsWithheld() {
    constexpr uint8_t kShot = 0x12;
    NetWorldTable t;
    NetStateSender mine(netSlotNetworked, 3), other(netSlotNetworked, 4);
    NetWorldTable::Seen s;
    s.type = 1;
    s.owner = 3;
    s.onPath = true;
    t.seen(kShot, s);
    CHECK(mine.events(t).empty());
    CHECK(other.events(t).size() == 1);
}

void testFightMessages() {
    const NetHitResult hit{0x8001234, {1, -2, 3, 4}};
    CHECK(decodeHitResult(encode(hit)) == hit);
    NetFire fire;
    fire.clientSlot = 7;
    fire.serial = 0x1234;
    fire.type = 2;
    fire.aimPoint = {1, 2, 3, 4};
    fire.target = 0x25;
    CHECK(decodeFire(encode(fire)) == fire);
    const NetPlayerReport report{-1, 80, true};
    CHECK(decodeReport(encode(report)) == report);
    const NetShotHit shotHit{3, 9, hit};
    CHECK(decodeShotHit(encode(shotHit)) == shotHit);
}

void testRoomPatches() {
    NetWorldTable t;
    t.roomShadow = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    std::vector<uint8_t> live = t.roomShadow;
    t.watchRoomBlock(live);
    CHECK(t.roomPatches.empty());
    live[2] = 99;
    live[4] = 98;
    live[18] = 97;
    t.watchRoomBlock(live);
    CHECK(t.roomPatches.size() == 2);
    CHECK(t.roomShadow == live);
    NetStateSender sender;
    Recorded rec;
    NetStateClient client(rec);
    for (const auto& batch : sender.events(t)) CHECK(client.applyEvents(batch));
    client.advance(0);
    CHECK(rec.patches.size() == 2 && rec.patches[0].offset == 2 && rec.patches[0].bytes.size() == 3 && rec.patches[1].offset == 18);
    // a joiner hears them too; an old client only once
    NetStateSender joiner;
    CHECK(!joiner.events(t).empty());
    CHECK(sender.events(t).empty());
}

// a player's state: only what changed, and the animation's byte when it changes
void testPlayerState() {
    NetPlayerSender sender;
    NetPlayerReceiver receiver;
    NetPlayerFields f;
    f.pos = {100, 0, 200};
    f.look.costume = 2;
    f.channels[0].anim = 5;
    f.channels[0].tick = 1;
    const auto first = sender.update(3, 1, f);
    const auto h = receiver.apply(first);
    CHECK(h && h->playerId == 3);
    CHECK(receiver.fields() == f);
    sender.acknowledge(h->sequence);
    CHECK(sender.update(3, 2, f).size() == 9); // id, sequence, tick, mask: nothing else
    f.pos[0] = 104;
    const auto moved = sender.update(3, 3, f);
    CHECK(moved.size() == 9 + 2);
    f.channels[0].anim = 6;
    f.channels[0].tick = 5;
    const auto anim = sender.update(3, 5, f);
    CHECK(anim.size() == 9 + 2 + 2);
    CHECK(receiver.apply(moved).has_value());
    CHECK(receiver.apply(anim).has_value());
    CHECK(receiver.fields() == f);
    CHECK(!receiver.apply(moved).has_value()); // stale
}

void testLinkMessages() {
    WorldMessage ack{WorldMessageType::NetAck};
    ack.playerId = 3;
    ack.netSequence = 0xbeef;
    const auto a = od::app::decodeWorldMessage(od::app::encodeWorldMessage(ack));
    CHECK(a && a->type == WorldMessageType::NetAck && a->playerId == 3 && a->netSequence == 0xbeef);
    WorldMessage update{WorldMessageType::NetUpdate};
    update.playerId = 2;
    update.netState = {1, 2, 3, 4};
    const auto u = od::app::decodeWorldMessage(od::app::encodeWorldMessage(update));
    CHECK(u && u->type == WorldMessageType::NetUpdate && u->playerId == 2 && u->netState == update.netState);
}

// ---- two real processes -------------------------------------------------------------------------------------------------

// tools/world_compare.py's fighting pad
const char* const kFightingPad =
    "45 10; 20 8000; 40 8010; 12 4000; 30 8080; 25 8000; 40 8020; 12 4000; 40 8040; 30 8000; "
    "45 10; 20 8000; 40 8010; 12 4000; 30 8080; 25 8000; 40 8020; 12 4000; 40 8040; 30 8000";
int gTicks = 588;

struct LinkFaults {
    const char* name;
    int delayMs; // one way
    int lossPercent;
};

// One way of the link: messages held for the delay, what the next one replaces lost at random
struct Way {
    struct Held {
        uint64_t at;
        WorldMessage message;
        bool reliable;
    };
    std::deque<Held> held;
    void push(uint64_t at, const WorldMessage& m, bool reliable) { held.push_back({at, m, reliable}); }
    void flush(uint64_t now, WorldLink& to) {
        while (!held.empty() && held.front().at <= now) {
            if (held.front().reliable) to.sendReliable(held.front().message);
            else to.sendLatest(held.front().message);
            held.pop_front();
        }
    }
};

struct Tally {
    uint64_t both = 0, onlyWorld = 0, onlyClient = 0;
    uint64_t posExact = 0, posWithin2 = 0, posWithin8 = 0, posWithin32 = 0;
    int posWorst = 0;
    uint64_t rotExact = 0, rotWithin16 = 0;
    uint64_t animated = 0, animSame = 0, keySame = 0, tintSame = 0, partsSame = 0, paintSame = 0;
    uint64_t stateTicks = 0, stateKeySame = 0, statePoseSame = 0, stateKeySamePoseSame = 0;
};

struct TypeTally {
    uint64_t records = 0, animSame = 0, keySame = 0, poseSame = 0, keySamePoseDiffers = 0;
};

std::string runRoom(const char* exe, const char* save, const std::filesystem::path& dir, const std::string& room,
                    const LinkFaults& faults) {
    const std::string stage = room.substr(0, room.find(':')), sub = room.substr(room.find(':') + 1);
    const std::filesystem::path worldPoses = dir / ("poses-world-" + stage + "-" + sub + ".bin"),
                                clientPoses = dir / ("poses-client-" + stage + "-" + sub + ".bin");
    std::filesystem::remove(worldPoses);
    std::filesystem::remove(clientPoses);
    WorldLink toWorld, toClient;
    char worldPort[8], clientPort[8], ticks[16];
    std::snprintf(worldPort, sizeof worldPort, "%u", unsigned(toWorld.port()));
    std::snprintf(clientPort, sizeof clientPort, "%u", unsigned(toClient.port()));
    std::snprintf(ticks, sizeof ticks, "%d", gTicks);
    const std::string worldLog = worldPoses.string(), clientLog = clientPoses.string();
    const char* worldArgs[] = {exe, "--world", stage.c_str(), sub.c_str(), "--world-link", worldPort, "--world-keep-player",
                               "--world-ticks", ticks, "--world-pad", kFightingPad, "--world-pose-log", worldLog.c_str(),
                               "--story-save", save, nullptr};
    const std::string effectLog = (dir / ("effects-" + stage + "-" + sub + ".txt")).string();
    const char* clientArgs[] = {exe, "--world", stage.c_str(), sub.c_str(), "--world-client", "--world-link", clientPort,
                                "--world-pose-log", clientLog.c_str(), "--world-effect-log", effectLog.c_str(), "--world-client-id", "7",
                                "--story-save", save, nullptr};
    SDL_Process* client = SDL_CreateProcess(clientArgs, false);
    CHECK(client != nullptr);
    SDL_Process* world = SDL_CreateProcess(worldArgs, false);
    CHECK(world != nullptr);
    if (!world || !client) return "no process";

    std::mt19937 random(12345);
    Way toWorldWay, toClientWay;
    const uint64_t delay = uint64_t(faults.delayMs) * 1'000'000ull;
    const auto lost = [&] { return faults.lossPercent > 0 && int(random() % 100) < faults.lossPercent; };
    uint64_t lastKeepAlive = 0, updates = 0, updateBytes = 0, eventBytes = 0;
    bool worldEnded = false;
    int code = -1;
    for (const uint64_t until = SDL_GetTicksNS() + 120'000'000'000ull; !worldEnded && SDL_GetTicksNS() < until;) {
        const uint64_t now = SDL_GetTicksNS();
        for (const WorldMessage& m : toWorld.poll(now)) {
            if (m.type == WorldMessageType::NetEvents) {
                eventBytes += m.netState.size();
                toClientWay.push(now + delay, m, true);
            } else if (m.type == WorldMessageType::NetUpdate) {
                ++updates;
                updateBytes += m.netState.size();
                if (!lost()) toClientWay.push(now + delay, m, false);
            }
        }
        for (const WorldMessage& m : toClient.poll(now)) {
            if (m.type == WorldMessageType::NetJoin) toWorldWay.push(now + delay, m, true);
            else if (m.type == WorldMessageType::NetAck && !lost()) toWorldWay.push(now + delay, m, false);
        }
        toWorldWay.flush(now, toWorld);
        toClientWay.flush(now, toClient);
        if (now - lastKeepAlive > 500'000'000ull) {
            lastKeepAlive = now;
            toWorld.sendLatest({WorldMessageType::KeepAlive});
            toClient.sendLatest({WorldMessageType::KeepAlive});
        }
        worldEnded = SDL_WaitProcess(world, false, &code);
        SDL_DelayNS(1'000'000);
    }
    CHECK(worldEnded && code == 0);
    if (!worldEnded) SDL_KillProcess(world, true);
    SDL_DestroyProcess(world);
    toClient.sendReliable({WorldMessageType::Stop});
    for (const uint64_t until = SDL_GetTicksNS() + 3'000'000'000ull; SDL_GetTicksNS() < until && !toClient.settled();) {
        toClient.poll(SDL_GetTicksNS());
        SDL_DelayNS(5'000'000);
    }
    int clientCode = -1;
    bool clientEnded = false;
    for (const uint64_t until = SDL_GetTicksNS() + 10'000'000'000ull; !clientEnded && SDL_GetTicksNS() < until; SDL_DelayNS(20'000'000))
        clientEnded = SDL_WaitProcess(client, false, &clientCode);
    if (!clientEnded || clientCode != 0) std::printf("the receiver %s, code %d\n", clientEnded ? "ended" : "did not end", clientCode);
    CHECK(clientEnded && clientCode == 0);
    if (!clientEnded) SDL_KillProcess(client, true);
    SDL_DestroyProcess(client);

    // ---- the two pose files, tick by tick ---------------------------------------------------------------------------
    const std::vector<NetPoseRecord> w = readNetPoses(worldPoses), c = readNetPoses(clientPoses);
    CHECK(w.empty() == c.empty()); // a room can hold no actor at all
    if (w.empty() || c.empty()) return "no poses";
    std::map<uint64_t, const NetPoseRecord*> atWorld, atClient;
    const auto key = [](const NetPoseRecord& r) { return uint64_t(r.tick) << 8 | r.slot; };
    for (const NetPoseRecord& r : w) atWorld[key(r)] = &r;
    for (const NetPoseRecord& r : c) atClient[key(r)] = &r;
    uint32_t first = 0xffffffffu, last = 0;
    for (const NetPoseRecord& r : c) {
        first = std::min(first, r.tick);
        last = std::max(last, r.tick);
    }
    last = std::min(last, w.back().tick);
    Tally t;
    std::map<int, TypeTally> types;
    for (const NetPoseRecord& r : w)
        if (r.tick >= first && r.tick <= last && !atClient.count(key(r))) ++t.onlyWorld;
    for (const NetPoseRecord& r : c) {
        if (r.tick > last) continue;
        const auto found = atWorld.find(key(r));
        if (found == atWorld.end() || found->second->type != r.type) {
            ++t.onlyClient;
            continue;
        }
        const NetPoseRecord& v = *found->second;
        ++t.both;
        int d = 0;
        for (size_t i = 0; i < 3; ++i) d = std::max(d, std::abs(int(r.pos[i]) - int(v.pos[i])));
        t.posExact += d == 0;
        t.posWithin2 += d <= 2;
        t.posWithin8 += d <= 8;
        t.posWithin32 += d <= 32;
        t.posWorst = std::max(t.posWorst, d);
        int a = 0;
        for (size_t i = 0; i < 3; ++i) {
            int e = (int(r.rot[i]) - int(v.rot[i])) & 0xfff;
            if (e > 0x800) e = 0x1000 - e;
            a = std::max(a, e);
        }
        t.rotExact += a == 0;
        t.rotWithin16 += a <= 16;
        if (v.bones.empty() && r.bones.empty()) continue; // a static, or an actor with no animation
        ++t.animated;
        TypeTally& type = types[r.type];
        ++type.records;
        const bool animSame = r.anim == v.anim;
        const bool keySame = animSame && r.key == v.key && r.frame == v.frame && r.ticksLeft == v.ticksLeft;
        t.tintSame += r.tint == v.tint;
        t.partsSame += r.hiddenParts == v.hiddenParts;
        t.paintSame += r.paint == v.paint;
        const bool poseSame = keySame && r.bones == v.bones;
        t.animSame += animSame;
        type.animSame += animSame;
        t.keySame += keySame;
        type.keySame += keySame;
        if (r.tick % uint32_t(kNetDefaultUpdateTicks) == 0) { // a state's tick: position and rotation are not in between
            ++t.stateTicks;
            t.stateKeySame += keySame;
            t.statePoseSame += poseSame;
            t.stateKeySamePoseSame += poseSame;
            type.poseSame += poseSame;
            type.keySamePoseDiffers += keySame && !poseSame;
        }
    }
    const auto pct = [](uint64_t a, uint64_t b) { return b ? 100.0 * double(a) / double(b) : 0.0; };
    std::printf("ST%s-%s [%s]: ticks %u..%u, update %.0f bytes avg (%llu updates), events %llu bytes\n", stage.c_str(), sub.c_str(),
                faults.name, first, last, updates ? double(updateBytes) / double(updates) : 0.0, (unsigned long long)updates,
                (unsigned long long)eventBytes);
    std::printf("  actors: %llu held by both, %llu only the world (%.2f%%), %llu only the receiver\n",
                (unsigned long long)t.both, (unsigned long long)t.onlyWorld, pct(t.onlyWorld, t.both + t.onlyWorld),
                (unsigned long long)t.onlyClient);
    std::printf("  position: exact %.1f%%, within 2 %.1f%%, within 8 %.1f%%, within 32 %.1f%%, worst %d units; rotation exact "
                "%.1f%%, within 16/4096 %.1f%%\n",
                pct(t.posExact, t.both), pct(t.posWithin2, t.both), pct(t.posWithin8, t.both), pct(t.posWithin32, t.both),
                t.posWorst, pct(t.rotExact, t.both), pct(t.rotWithin16, t.both));
    std::printf("  animated records %llu: same animation %.1f%%, same key (animation, key, frame, ticks left) %.1f%%, same tint "
                "%.1f%%, same hidden parts %.1f%%, same part paint %.1f%%\n",
                (unsigned long long)t.animated, pct(t.animSame, t.animated), pct(t.keySame, t.animated),
                pct(t.tintSame, t.animated), pct(t.partsSame, t.animated), pct(t.paintSame, t.animated));
    std::printf("  at a state's tick (%llu): same key %.1f%%, pose bit-identical %.1f%%\n", (unsigned long long)t.stateTicks,
                pct(t.stateKeySame, t.stateTicks), pct(t.statePoseSame, t.stateTicks));
    for (const auto& [type, n] : types)
        std::printf("    type %02x: %llu records, same animation %.1f%%, same key %.1f%%, state-tick poses identical %llu, key "
                    "same but pose differs %llu\n",
                    type, (unsigned long long)n.records, pct(n.animSame, n.records), pct(n.keySame, n.records),
                    (unsigned long long)n.poseSame, (unsigned long long)n.keySamePoseDiffers);
    std::fflush(stdout);
    // what the data path promises: the same actors, within the interpolation, and the animation the class chose
    CHECK(t.both > 0);
    CHECK(pct(t.onlyWorld, t.both + t.onlyWorld) < 2.0);
    CHECK(pct(t.onlyClient, t.both + t.onlyClient) < 2.0);
    return "";
}

} // namespace

void testStoryTable() {
    using namespace od::port;
    CHECK(storyPlaceOf(0x25, 6, 3) == StoryPlaceKind::Boss);
    CHECK(storyPlaceOf(0x25, 6, 4) == StoryPlaceKind::Free); // past its story
    CHECK(storyPlaceOf(0x25, 5, 3) == StoryPlaceKind::Free);
    CHECK(storyPlaceOf(0x55, 2, 1) == StoryPlaceKind::Mission);
    CHECK(storyDoorGated(0x37, 0x37, 6, 1));
    CHECK(!storyDoorGated(0x37, 0x37, 5, 1));
    CHECK(storyDoorGated(0x2a, 0x2b, 0, 12)); // into a stage listed whole
    CHECK(!storyDoorGated(0x2b, 0x2b, 1, 12)); // ... not inside it
}

void testGlobalState() {
    using namespace od::port;
    CHECK(flagScope(0x2f0) == FlagScope::Shared);
    CHECK(flagScope(0x690) == FlagScope::Stage);
    CHECK(flagScope(0x712) == FlagScope::Stage);
    CHECK(flagScope(0x700) == FlagScope::Local);
    CHECK(flagScope(0x6ff) == FlagScope::Local);
    GlobalMaster master(100);
    GlobalChange a;
    a.stage = 0x41;
    a.flags.push_back({0x5e, 0x01, 0}); // shared
    a.flags.push_back({0x690 >> 3, 0x01, 0}); // of the stage
    a.flags.push_back({0x700 >> 3, 0x01, 0}); // this process's only: never goes out
    a.zennyDelta = 50;
    const GlobalMaster::Out out = master.merge(a);
    CHECK(out.shared.flags.size() == 1 && out.shared.flags[0].byte == 0x5e);
    CHECK(out.stage.flags.size() == 1);
    CHECK(out.zenny == 150);
    GlobalChange b;
    b.stage = 0x42;
    b.flags.push_back({0x5e, 0x02, 0x01});
    b.zennyDelta = -1000; // no less than nothing
    CHECK(master.merge(b).zenny == 0);
    const GlobalChange here = master.all(0x41), there = master.all(0x42);
    CHECK(here.flags.size() == 2 && there.flags.size() == 1);
    CHECK(there.flags[0].set == 0x02 && there.flags[0].clear == 0x01);
    master.dropStage(0x41);
    CHECK(master.all(0x41).flags.size() == 1);
    const auto round = decodeGlobalChange(encode(here));
    CHECK(round && *round == here);
    CHECK(!decodeGlobalChange(std::vector<uint8_t>{1, 2}));
}

int main(int argc, char** argv) {
    testGlobalState();
    testStoryTable();
    testChangesOnly();
    testLostUpdate();
    testEventsFirstAndReuse();
    testFitsOneDatagram();
    testVisualFields();
    testPaths();
    testEffects();
    testLockPoints();
    testRoomPatches();
    testPlayerState();
    testOwnShotsWithheld();
    testFightMessages();
    testLinkMessages();
    if (argc >= 4) {
        SDL_Init(0);
        std::vector<std::string> rooms;
        bool quick = false; // --quick: 10 s of each room with a clean link, for a sweep of the rooms
        for (int i = 4; i < argc; ++i) {
            if (std::string(argv[i]) == "--quick") quick = true;
            else rooms.push_back(argv[i]);
        }
        if (rooms.empty()) rooms = {"12:1", "42:2", "41:2"};
        if (quick) gTicks = 300;
        const LinkFaults clean{"no delay, no loss", 0, 0}, far{"125 ms each way, 3% lost", 125, 3};
        for (const std::string& room : rooms) {
            runRoom(argv[1], argv[2], argv[3], room, clean);
            if (!quick) runRoom(argv[1], argv[2], argv[3], room, far);
        }
        SDL_Quit();
    }
    return testResult("test_world_net");
}
