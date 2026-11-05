// A world process (see world_process.hpp).
#include "app/world_process.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "app/game_files.hpp"
#include "app/log_file.hpp"
#include "app/play_session.hpp"
#include "app/room_mesh.hpp"
#include "app/world_link.hpp"
#include "net/wire.hpp"
#include "content/registry.hpp"
#include "engine/config.hpp"
#include "engine/dat.hpp"
#include "engine/data_source.hpp"
#include "engine/lang.hpp"
#include "engine/pc_data_source.hpp"
#include "engine/ps1_data_source.hpp"
#include "game/demo_playback.hpp"
#include "game/display.hpp"
#include "game/encounter.hpp"
#include "game/stage_record.hpp"
#include "game/room_grid.hpp"
#include "game/player/net_player.hpp"
#include "game/player/player_snapshot.hpp"
#include "game/player/remote_combat.hpp"
#include "game/player/remote_player.hpp"
#include "game/title.hpp"
#include "game/world_view.hpp"
#include "game/world_digest.hpp"
#include "game/world_net_state.hpp"

namespace od::app {

namespace {

constexpr uint64_t kTickNs = 1'000'000'000ull / 30;
constexpr uint64_t kStatsEvery = 150;

struct WorldArgs {
    int stage = -1, sub = 0;
    uint64_t ticks = 0; // 0: until the process is stopped
    bool unpaced = false;
    std::string storySave;
    std::vector<uint16_t> pad; // the PS1 pad bits of each tick (past the end: none)
    struct Guest {
        size_t id = 0;
        int dx = 0, dz = 0;
    };
    // test instrument: remote players that follow player 0's state at this offset (game units); with hostAway, player 0
    // is out of the world and they stand where they were placed
    std::vector<Guest> guests;
    bool hostAway = false;
    int linkPort = 0; // the coordinator's port (app/world_link): its players, until it stops this world
    std::optional<od::port::StageRecord> stageRecord; // what the stage's other worlds hold
    int markDefeated = -1; // test instrument: this enemy id counts as defeated from the first tick on
    bool client = false;       // the receiver of a world's network state (see runClient)
    uint8_t clientId = 0;      // its client id: the player whose own shots the world does not send it
    bool keepPlayer = false;   // test instrument: on a link, the world's own player stays, driven by the pad
    int netTicks = od::port::kNetDefaultUpdateTicks; // the ticks between two updates of the network state
    std::string poseLog;       // test instrument: each actor's pose per tick, in this local file
    std::string netStats;      // test instrument: the bytes of the network state for one client, in this local file
    std::string effectLog;     // test instrument: the effects a receiver starts, in this local file
};

// "<ticks> <hex PS1 pad bits>; ..."
std::vector<uint16_t> parsePad(const std::string& text) {
    std::vector<uint16_t> pad;
    std::stringstream all(text);
    for (std::string step; std::getline(all, step, ';');) {
        std::istringstream ss(step);
        int ticks = 0;
        unsigned bits = 0;
        if (!(ss >> std::dec >> ticks >> std::hex >> bits)) throw std::invalid_argument("--world-pad: " + step);
        pad.insert(pad.end(), size_t(std::max(ticks, 0)), uint16_t(bits));
    }
    return pad;
}

WorldArgs parseArgs(int argc, char** argv) {
    WorldArgs w;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--world" && i + 2 < argc) {
            w.stage = std::stoi(argv[++i], nullptr, 16);
            w.sub = std::stoi(argv[++i]);
        } else if (a == "--world-ticks" && more) {
            w.ticks = std::stoull(argv[++i]);
        } else if (a == "--world-unpaced") {
            w.unpaced = true;
        } else if (a == "--world-pad" && more) {
            w.pad = parsePad(argv[++i]);
        } else if (a == "--world-guest" && i + 3 < argc) {
            WorldArgs::Guest g;
            g.id = size_t(std::stoul(argv[++i]));
            g.dx = std::stoi(argv[++i]);
            g.dz = std::stoi(argv[++i]);
            if (g.id == 0 || g.id >= od::port::kPlayerSlots) throw std::invalid_argument("--world-guest: id 1..7");
            w.guests.push_back(g);
        } else if (a == "--world-link" && more) {
            w.linkPort = std::stoi(argv[++i]);
        } else if (a == "--stage-record" && more) {
            const std::string hex = argv[++i];
            std::vector<uint8_t> bytes;
            for (size_t k = 0; k + 1 < hex.size(); k += 2) bytes.push_back(uint8_t(std::stoi(hex.substr(k, 2), nullptr, 16)));
            w.stageRecord = od::port::decodeStageRecord(bytes);
            if (!w.stageRecord) throw std::invalid_argument("--stage-record");
        } else if (a == "--world-mark-defeated" && more) {
            w.markDefeated = std::stoi(argv[++i]);
        } else if (a == "--world-client") {
            w.client = true;
        } else if (a == "--world-client-id" && more) {
            w.clientId = uint8_t(std::stoi(argv[++i]));
        } else if (a == "--world-keep-player") {
            w.keepPlayer = true;
        } else if (a == "--world-net-ticks" && more) {
            w.netTicks = std::max(std::stoi(argv[++i]), 1);
        } else if (a == "--world-pose-log" && more) {
            w.poseLog = argv[++i];
        } else if (a == "--world-effect-log" && more) {
            w.effectLog = argv[++i];
        } else if (a == "--world-net-stats" && more) {
            w.netStats = argv[++i];
        } else if (a == "--world-host-away") {
            w.hostAway = true;
        } else if (a == "--story-save" && more) {
            w.storySave = argv[++i];
        }
    }
    if (w.stage < 0) throw std::invalid_argument("--world <stage> <sub>");
    return w;
}

std::unique_ptr<od::DataSource> openData(od::Config& cfg, PlayerView& view) {
    if (cfg.langDir.empty()) {
        const char* base = SDL_GetBasePath();
        cfg.langDir = std::filesystem::path(base ? base : "") / "lang";
    }
    if (!cfg.disc.empty()) view.disc = std::make_shared<const od::DiscImage>(cfg.disc);
    if (cfg.data != "disc") return std::make_unique<od::PcDataSource>(cfg.datDir, cfg.exePath);
    if (!view.disc) throw std::runtime_error("data=disc needs the disc image (disc= in opendash.ini)");
    return std::make_unique<od::Ps1DataSource>(view.disc);
}

// the room a world holds: what the game's logic reads of it; the triangles and pages are the renderer's
void holdRoom(RoomMesh& held, int stage, int sub) {
    RoomMesh mesh = loadRoom(stage, sub);
    mesh.tris.clear();
    mesh.slices.clear();
    held = std::move(mesh);
}

// A file opened for the test instruments
struct LocalFile {
    std::FILE* f = nullptr;
    explicit LocalFile(const std::string& path, const char* mode = "wb") {
        if (path.empty()) return;
#ifdef _WIN32
        if (fopen_s(&f, path.c_str(), mode) != 0) f = nullptr;
#else
        f = std::fopen(path.c_str(), mode);
#endif
        if (!f) throw std::runtime_error("cannot write " + path);
    }
    ~LocalFile() {
        if (f) std::fclose(f);
    }
    LocalFile(const LocalFile&) = delete;
    LocalFile& operator=(const LocalFile&) = delete;
};

// What one client's network state takes, tick by tick (--world-net-stats): the update's period, acknowledged as late as a
// round trip of about 270 ms would (kAckLagUpdates at 15 a second, no loss)
class NetStats {
public:
    static constexpr int kAckLagUpdates = 4;
    static constexpr uint32_t kSettledTick = 30; // the room's first spawns are over: what a client that joined then is told
    static constexpr uint32_t kJoinTick = 150;   // a client joins here: what it is told at once
    NetStats(std::string path, int every) : path_(std::move(path)), every_(every) {}

    void tick(const od::port::NetWorldTable& table, uint32_t tick) {
        for (Run& r : runs_) {
            for (const auto& batch : r.sender.events(table))
                if (tick >= kSettledTick) r.laterEventBytes += batch.size();
            if (tick == kJoinTick) {
                od::port::NetStateSender joining(r.filter);
                for (const auto& batch : joining.events(table)) r.joinEventBytes += batch.size();
            }
            if (int(tick) % every_ != 0) continue;
            const std::vector<uint8_t> update = r.sender.update(table);
            r.updates.push_back(uint32_t(update.size()));
            r.lastSequence = uint16_t(r.lastSequence + 1);
            if (r.lastSequence > kAckLagUpdates) r.sender.acknowledge(uint16_t(r.lastSequence - kAckLagUpdates));
        }
        if (od::port::playerPresent(0)) {
            const od::port::NetPlayerFields fields = playerWatch_.see(od::port::player(), tick);
            if (int(tick) % every_ == 0) {
                playerUpdates_.push_back(uint32_t(playerSender_.update(0, tick, fields).size()));
                const uint16_t sent = uint16_t(playerUpdates_.size());
                if (sent > kAckLagUpdates) playerSender_.acknowledge(uint16_t(sent - kAckLagUpdates));
            }
        }
        size_t live = 0;
        for (size_t i = 0; i < od::port::kNetSlots; ++i)
            if (table.actors[i].live && od::port::netSlotVisible(uint8_t(i))) ++live;
        maxLive_ = std::max(maxLive_, live);
        occupants_ = table.occupants;
    }

    void write(uint32_t ticks, size_t playerStateBytes) {
        LocalFile out(path_);
        std::fprintf(out.f, "ticks %u every %d liveMax %zu occupants %llu playerState %zu\n", ticks, every_, maxLive_,
                     (unsigned long long)occupants_, playerStateBytes);
        std::fprintf(out.f, "playerUpdates");
        for (const uint32_t n : playerUpdates_) std::fprintf(out.f, " %u", n);
        std::fprintf(out.f, "\n");
        for (const Run& r : runs_) {
            std::fprintf(out.f, "run %s joinEvents %llu laterEvents %llu cut %llu updates", r.name,
                         (unsigned long long)r.joinEventBytes, (unsigned long long)r.laterEventBytes,
                         (unsigned long long)r.sender.updatesCut());
            for (const uint32_t n : r.updates) std::fprintf(out.f, " %u", n);
            std::fprintf(out.f, "\n");
            const od::port::NetStateSender::UpdateBytes& b = r.sender.largestUpdate();
            std::fprintf(out.f, "largest %u head %u position %u rotation %u animation %u visual %u aim %u paint %u solids %u lock %u path %u\n",
                         b.total, b.head, b.position, b.rotation, b.animation, b.visual, b.aim, b.paint, b.solids, b.lock, b.path);
        }
    }

private:
    struct Run {
        const char* name;
        od::port::NetSlotFilter filter;
        od::port::NetStateSender sender;
        std::vector<uint32_t> updates{};
        uint64_t joinEventBytes = 0, laterEventBytes = 0;
        uint16_t lastSequence = 0;
    };
    std::string path_;
    int every_;
    od::port::NetPlayerWatch playerWatch_;
    od::port::NetPlayerSender playerSender_;
    std::vector<uint32_t> playerUpdates_;
    size_t maxLive_ = 0;
    uint64_t occupants_ = 0;
    std::array<Run, 1> runs_{Run{"all", od::port::netSlotNetworked, od::port::NetStateSender(od::port::netSlotNetworked)}};
};

// test instrument: each effect a receiver starts, a line flushed at once, so that the file tells what ran before an end
class LoggedNetActors : public od::port::PoolNetActors {
public:
    explicit LoggedNetActors(std::FILE* file) : file_(file) {}
    void effect(const od::port::NetEffect& e) override {
        if (file_) {
            std::fprintf(file_, "pool %02x type %02x subtype %02x variant %02x loop %02x params %02x %02x %02x %02x\n", unsigned(e.pool),
                         unsigned(e.type), unsigned(e.subtype), unsigned(e.variant), unsigned(e.loopFlags), unsigned(e.params[0]),
                         unsigned(e.params[1]), unsigned(e.params[2]), unsigned(e.params[3]));
            std::fflush(file_);
        }
        PoolNetActors::effect(e);
    }

private:
    std::FILE* file_;
};

// The receiver of a world's network state: the room's files and each actor's animation, no world update
int runClient(const WorldArgs& w) {
    od::Config cfg = od::loadConfig();
    if (!ensureGameFiles(cfg, false)) return 1;
    auto view = std::make_unique<PlayerView>();
    const std::unique_ptr<od::DataSource> data = openData(cfg, *view);
    od::bindDataSource(data.get());
    view->stageSubs = stageSubRooms();
    view->modern = false;
    view->init(cfg);
    if (!w.storySave.empty()) {
        view->storySave = od::readFile(w.storySave);
        if (view->storySave.size() != od::port::kSaveSize || od::port::saveChecksumBad(view->storySave))
            throw std::runtime_error("--story-save: bad size / checksum");
    }
    RoomMesh current;
    view->room = &current;
    view->loadRoom = [&](int st, int sb) { holdRoom(current, st, sb); };
    holdRoom(current, w.stage, w.sub);
    if (!view->enterRoomAsClient(current)) {
        SDL_Log("client: %s", view->playError.c_str());
        return 1;
    }
    if (w.linkPort == 0) throw std::invalid_argument("--world-client needs --world-link");
    WorldLink link{uint16_t(w.linkPort)};
    const uint8_t client = w.clientId;
    link.sendReliable({WorldMessageType::Hello});
    WorldMessage join{WorldMessageType::NetJoin};
    join.playerId = client;
    link.sendReliable(join);

    LocalFile poses(w.poseLog), effects(w.effectLog);
    LoggedNetActors actors(effects.f);
    od::port::NetStateClient state(actors);
    od::port::NetRenderClock clock;
    od::port::NetLocalRunFailures failures;
    const auto acknowledge = [&](std::optional<uint16_t> sequence) {
        if (!sequence) return;
        WorldMessage ack{WorldMessageType::NetAck};
        ack.playerId = client;
        ack.netSequence = *sequence;
        link.sendLatest(ack);
    };
    bool stopped = false, shownAny = false;
    uint32_t shown = 0;
    uint64_t next = SDL_GetTicksNS(), ticks = 0;
    while (!stopped && (w.ticks == 0 || ticks < w.ticks)) {
        const uint64_t now = SDL_GetTicksNS();
        for (const WorldMessage& m : link.poll(now)) {
            switch (m.type) {
            case WorldMessageType::NetEvents:
                if (!state.applyEvents(m.netState)) throw std::runtime_error("client: a bad event batch");
                acknowledge(state.applyHeld());
                break;
            case WorldMessageType::NetUpdate:
                acknowledge(state.applyUpdate(m.netState));
                if (state.heard()) clock.heard(state.newestTick());
                break;
            case WorldMessageType::Stop: stopped = true; break;
            default: break;
            }
        }
        if (link.silent(now)) stopped = true;
        if (clock.running()) {
            const int64_t at = int64_t(clock.at(now));
            for (; at >= 0 && (!shownAny || int64_t(shown) < at);) {
                shown = shownAny ? shown + 1 : uint32_t(at);
                shownAny = true;
                state.advance(shown);
                od::port::runNetLocalActors(view->env, view->actors, false, failures);
                od::port::runNetLocalActors(view->env, view->actors, true, failures);
                od::port::actorSystem().hitCount = 0; // the world decides what hits
                actors.endTick();
                if (poses.f) od::port::writeNetPoses(poses.f, shown, od::port::netSlotVisibleOrShot, true);
            }
        }
        ++ticks;
        next += kTickNs;
        const uint64_t after = SDL_GetTicksNS();
        if (next > after) SDL_DelayNS(next - after);
        else next = after;
    }
    SDL_Log("client: ended at world tick %u, %zu actors shown, %llu effects started, %zu classes failed", shown, state.shownCount(),
            (unsigned long long)actors.effectsStarted(), failures.classes.size());
    for (const std::string& f : failures.classes) SDL_Log("client: failed: %s", f.c_str());
    return 0;
}

int run(const WorldArgs& w) {
    if (w.client) return runClient(w);
    od::Config cfg = od::loadConfig();
    if (!ensureGameFiles(cfg, false)) return 1;
    auto view = std::make_unique<PlayerView>();
    const std::unique_ptr<od::DataSource> data = openData(cfg, *view);
    od::bindDataSource(data.get());

    view->stageSubs = stageSubRooms();
    view->modern = false; // no orbit camera drives the game's camera here: the game's own camera follows
    view->init(cfg);
    view->audioInit(cfg, false);
    view->langDir = cfg.langDir;
    view->restoredVoices = cfg.restoredVoices;
    view->setLanguage(od::lang::parseLanguage(cfg.lang));
    if (!w.storySave.empty()) {
        view->storySave = od::readFile(w.storySave);
        if (view->storySave.size() != od::port::kSaveSize || od::port::saveChecksumBad(view->storySave))
            throw std::runtime_error("--story-save: bad size / checksum");
    }

    RoomMesh current;
    view->room = &current;
    view->loadRoom = [&](int st, int sb) { holdRoom(current, st, sb); };
    view->afterPad = [] { od::port::demoPadFrame(); };
    view->gameOver = [&] { od::port::gameOverModeFrame(view->fadeTask); };
    view->afterFrame = [&] { od::port::softResetFrame(view->fadeTask); };
    holdRoom(current, w.stage, w.sub);
    if (w.stageRecord) od::port::setStartingStageRecord(*w.stageRecord);

    // the boot's display state, which no title sets here (App::leaveTitle)
    od::port::display().mode = 0;
    od::port::display().maskTicks = 1;
    od::port::display().pace = 2;
    if (!view->enterPlay(current)) {
        SDL_Log("world: %s", view->playError.c_str());
        return 1;
    }
    const auto guestState = [](const WorldArgs::Guest& g) {
        od::port::PlayerSnapshot s = od::port::capturePlayerSnapshot(od::port::player());
        s.pos[0] = int32_t(uint32_t(s.pos[0]) + (uint32_t(g.dx) << 16));
        s.pos[2] = int32_t(uint32_t(s.pos[2]) + (uint32_t(g.dz) << 16));
        return s;
    };
    for (const WorldArgs::Guest& g : w.guests) od::port::remotePlayerJoin(g.id, guestState(g));
    if (w.hostAway) od::port::leavePlayer(0);
    SDL_Log("world: ST%02X sub %d running, %u live actors", w.stage, w.sub, od::port::worldDigest().liveActors);

    // on a link: no player of its own; the coordinator's come and go, and its silence ends the world
    std::unique_ptr<WorldLink> link;
    if (w.linkPort != 0) {
        link = std::make_unique<WorldLink>(uint16_t(w.linkPort));
        if (!w.keepPlayer) {
            od::port::setWorldHasNoView(true);
            od::port::leavePlayer(0);
        }
        link->sendReliable({WorldMessageType::Hello});
    }
    bool stopped = false;
    if (w.markDefeated >= 0) od::port::encounterState().defeated[size_t(w.markDefeated) >> 5] |= 0x80000000u >> (w.markDefeated & 0x1f);
    od::port::StageRecord lastRecord{}; // what the coordinator was last told; a first change goes out at once
    if (w.stageRecord) lastRecord = *w.stageRecord;
    // the network state: one sender per client
    std::map<uint8_t, od::port::NetStateSender> netClients;
    struct FiredShot {
        bool live = false;
        uint8_t player = 0, clientSlot = 0;
        uint16_t serial = 0;
    };
    std::array<FiredShot, od::port::ShotPool::size()> fired{}; // the world's shots that came from a client's fire
    // the players' states: what each owner sends, played here from this world's files; what goes on to the others
    struct RemoteMirror {
        od::port::NetPlayerReceiver from;
        std::array<od::port::NetPlayerTrack, 2> tracks{};
        uint32_t ownerTick = 0;
        bool heard = false;
    };
    std::array<RemoteMirror, od::port::kPlayerSlots> mirrors{};
    std::map<std::pair<uint8_t, uint8_t>, od::port::NetPlayerSender> playerSenders; // by client, player
    const auto playersFromOwners = [&] {
        for (uint8_t id = 0; id < od::port::kPlayerSlots; ++id) {
            RemoteMirror& m = mirrors[id];
            if (!m.heard || !od::port::playerPresent(id) || !od::port::playerIsRemote(id)) continue;
            ++m.ownerTick;
            const od::port::NetPlayerFields& f = m.from.fields();
            for (size_t c = 0; c < 2; ++c) m.tracks[c].step(f.channels[c], f.stageAnims, m.ownerTick);
            od::port::remotePlayerState(id, od::port::netPlayerSnapshot(f, m.tracks));
        }
    };
    const auto playersToOthers = [&](uint32_t netTick) {
        for (auto& [client, sender] : netClients)
            for (uint8_t id = 0; id < od::port::kPlayerSlots; ++id) {
                if (id == client || !mirrors[id].heard || !od::port::playerPresent(id)) continue;
                WorldMessage m{WorldMessageType::NetPlayer};
                m.playerId = client;
                m.netState = playerSenders[{client, id}].update(id, netTick, mirrors[id].from.fields());
                link->sendLatest(m);
            }
    };
    const auto fightTurn = [&] {
        for (uint8_t id = 0; id < od::port::kPlayerSlots; ++id) {
            if (!od::port::playerPresent(id) || !od::port::playerIsRemote(id)) continue;
            if (const auto hit = od::port::takeRemotePlayerHit(od::port::playerById(id))) {
                WorldMessage m{WorldMessageType::NetHit};
                m.playerId = id;
                m.netState = od::port::encode(*hit);
                link->sendReliable(m);
            }
        }
        for (size_t i = 0; i < fired.size(); ++i) {
            FiredShot& f = fired[i];
            if (!f.live) continue;
            const od::port::ShotActor& shot = od::port::shotPool()[i];
            if (!(shot.flags & od::port::kActorLive)) {
                f.live = false;
            } else if (shot.hitBox.result != 0) {
                const od::port::NetShotHit hit{f.clientSlot, f.serial,
                                               {shot.hitBox.result,
                                                {shot.hitBox.contact.x, shot.hitBox.contact.y, shot.hitBox.contact.z, shot.hitBox.contactKind}}};
                WorldMessage m{WorldMessageType::NetShotHit};
                m.playerId = f.player;
                m.netState = od::port::encode(hit);
                link->sendReliable(m);
            }
        }
    };
    std::unique_ptr<od::port::NetWorldTable> netTable;
    const LocalFile poses(w.poseLog);
    std::unique_ptr<NetStats> netStats;
    if (link || !w.netStats.empty()) netTable = std::make_unique<od::port::NetWorldTable>();
    if (!w.netStats.empty()) netStats = std::make_unique<NetStats>(w.netStats, w.netTicks);
    if (netTable) {
        od::port::watchNetPoses();
        // the room block as its file holds it: what the game has changed since goes to the clients
        const od::FileDataBlock pristine(od::roomBlock(*current.roomFile));
        netTable->roomShadow.assign(pristine.data(), pristine.data() + pristine.size());
    }
    const auto netTurn = [&](uint32_t netTick) {
        if (poses.f) od::port::writeNetPoses(poses.f, netTick, od::port::netSlotVisibleOrShot, false);
        if (!netTable) return;
        // every tick from the first, whoever is connected: a client that joins later is told how long ago each animation
        // started
        od::port::captureNetWorld(*netTable, netTick);
        const od::port::RoomGridState& grid = od::port::roomGrid();
        const std::span<const uint8_t> block(view->roomBlock.data(), view->roomBlock.size());
        // the two ranges of the room block that decide what collides
        const size_t cellsAt = grid.cells - grid.block, segmentsAt = grid.segments - grid.block;
        netTable->watchRoomBlock(block, cellsAt, cellsAt + size_t(grid.width) * grid.depth * od::kGridCellSize);
        netTable->watchRoomBlock(block, segmentsAt,
                                 segmentsAt + od::kListHeaderSize +
                                     size_t(od::port::SegmentListHeader(grid.segments).segmentCount) * od::kSegmentEntrySize);
        if (link) fightTurn();
        if (netStats) netStats->tick(*netTable, netTick);
        for (auto& [client, sender] : netClients) {
            for (auto& batch : sender.events(*netTable)) {
                WorldMessage events{WorldMessageType::NetEvents};
                events.playerId = client;
                events.netState = std::move(batch);
                link->sendReliable(events);
            }
            if (int(netTick) % w.netTicks != 0) continue;
            WorldMessage update{WorldMessageType::NetUpdate};
            update.playerId = client;
            update.netState = sender.update(*netTable);
            link->sendLatest(update);
        }
        if (link && int(netTick) % w.netTicks == 0) playersToOthers(netTick);
    };
    const auto linkTurn = [&](uint64_t tick) {
        const uint64_t now = SDL_GetTicksNS();
        for (const WorldMessage& m : link->poll(now)) {
            const auto state = m.playerState.empty() ? std::nullopt : od::port::decodePlayerState(m.playerState);
            switch (m.type) {
            case WorldMessageType::Join:
                if (state && m.playerId < od::port::kPlayerSlots) od::port::remotePlayerJoin(m.playerId, state->snapshot);
                break;
            case WorldMessageType::State:
                if (state && m.playerId < od::port::kPlayerSlots && od::port::playerIsRemote(m.playerId))
                    od::port::remotePlayerState(m.playerId, state->snapshot);
                break;
            case WorldMessageType::Leave:
                if (m.playerId < od::port::kPlayerSlots && od::port::playerIsRemote(m.playerId))
                    od::port::remotePlayerLeave(m.playerId);
                break;
            case WorldMessageType::StageState:
                if (const auto record = od::port::decodeStageRecord(m.stageRecord)) {
                    od::port::applyStageRecord(*record);
                    lastRecord = od::port::captureStageRecord(); // no echo of what came from the others
                }
                break;
            case WorldMessageType::NetJoin: netClients.try_emplace(m.playerId, od::port::netSlotNetworked, m.playerId); break;
            case WorldMessageType::NetFire:
                if (const auto fire = od::port::decodeFire(m.netState);
                    fire && m.playerId < od::port::kPlayerSlots && od::port::playerIsRemote(m.playerId)) {
                    if (const od::port::ShotActor* shot = od::port::startFiredShot(m.playerId, *fire))
                        fired[od::port::ShotPool::index(*shot)] = {true, m.playerId, fire->clientSlot, fire->serial};
                }
                break;
            case WorldMessageType::NetPlayer:
                if (m.playerId < od::port::kPlayerSlots) {
                    RemoteMirror& mirror = mirrors[m.playerId];
                    if (const auto heard = mirror.from.apply(m.netState)) {
                        if (!mirror.heard || int32_t(heard->tick - mirror.ownerTick) > 0) mirror.ownerTick = heard->tick;
                        mirror.heard = true;
                        od::net::ByteWriter ack;
                        ack.u8(heard->playerId);
                        ack.u16(heard->sequence);
                        WorldMessage back{WorldMessageType::NetPlayerAck};
                        back.playerId = m.playerId;
                        back.netState = ack.take();
                        link->sendLatest(back);
                    }
                }
                break;
            case WorldMessageType::NetPlayerAck: {
                od::net::ByteReader ack(m.netState);
                const uint8_t about = ack.u8();
                const uint16_t sequence = ack.u16();
                if (ack.done())
                    if (const auto s = playerSenders.find({m.playerId, about}); s != playerSenders.end()) s->second.acknowledge(sequence);
                break;
            }
            case WorldMessageType::NetReport:
                if (const auto report = od::port::decodeReport(m.netState);
                    report && m.playerId < od::port::kPlayerSlots && od::port::playerIsRemote(m.playerId))
                    od::port::applyPlayerReport(od::port::playerById(m.playerId), *report);
                break;
            case WorldMessageType::NetLeave: netClients.erase(m.playerId); break;
            case WorldMessageType::NetAck:
                if (const auto c = netClients.find(m.playerId); c != netClients.end()) c->second.acknowledge(m.netSequence);
                break;
            case WorldMessageType::Stop: stopped = true; break;
            default: break;
            }
        }
        if (link->silent(now)) {
            SDL_Log("world: the coordinator went silent");
            stopped = true;
        }
        if (const od::port::StageRecord now_ = od::port::captureStageRecord(); now_ != lastRecord) {
            lastRecord = now_;
            WorldMessage changed{WorldMessageType::StageState};
            changed.stageRecord = od::port::encodeStageRecord(now_);
            link->sendReliable(changed);
        }
        WorldMessage report{WorldMessageType::Report};
        const od::port::WorldDigest digest = od::port::worldDigest();
        report.tick = tick;
        report.liveActors = digest.liveActors;
        report.digest = digest.hash;
        for (size_t id = 0; id < od::port::kPlayerSlots; ++id)
            if (od::port::playerPresent(id)) ++report.players;
        link->sendLatest(report);
    };

    uint64_t next = SDL_GetTicksNS(), statsNs = 0, statsMaxNs = 0, tick = 0;
    while (view->play && !stopped && (w.ticks == 0 || tick < w.ticks)) {
        if (link) linkTurn(tick);
        const uint64_t at = SDL_GetTicksNS();
        const uint16_t pad = tick < w.pad.size() ? w.pad[size_t(tick)] : uint16_t(0);
        // the directions as the stick the logic reads when no stick is held, as the windowed game hands them
        const float lx = float(((pad & 0x20) != 0) - ((pad & 0x80) != 0));
        const float ly = float(((pad & 0x40) != 0) - ((pad & 0x10) != 0));
        if (!w.hostAway)
            for (const WorldArgs::Guest& g : w.guests) od::port::remotePlayerState(g.id, guestState(g));
        if (link) playersFromOwners();
        view->playTick(pad, 128, 128, lx, ly);
        netTurn(uint32_t(tick));
        if (view->assetsDirty) {
            view->assetsDirty = false;
            g_inStageSwitch = true;
            holdRoom(current, current.stage, current.sub);
            g_inStageSwitch = false;
        }
        const uint64_t cost = SDL_GetTicksNS() - at;
        statsNs += cost;
        statsMaxNs = std::max(statsMaxNs, cost);
        if (++tick % kStatsEvery == 0) {
            SDL_Log("world: tick %llu, tick cost avg %.1f us, max %.1f us, %u live actors", (unsigned long long)tick,
                    double(statsNs) / double(kStatsEvery) * 1e-3, double(statsMaxNs) * 1e-3,
                    od::port::worldDigest().liveActors);
            statsNs = statsMaxNs = 0;
        }
        if (!w.unpaced) {
            next += kTickNs;
            const uint64_t after = SDL_GetTicksNS();
            if (next > after) SDL_DelayNS(next - after);
            else next = after; // late: no burst to catch up, the tick rate falls
        }
    }
    if (netStats) netStats->write(uint32_t(tick), od::port::encodePlayerState({}).size());
    const int rc = view->playError.empty() ? 0 : 1;
    if (rc != 0) SDL_Log("world: stopped at tick %llu: %s", (unsigned long long)tick, view->playError.c_str());
    else SDL_Log("world: ended after %llu ticks", (unsigned long long)tick);
    od::port::setSoundBackend(nullptr);
    view->mixer.reset();
    return rc;
}

} // namespace

bool worldProcessRequested(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--world") return true;
    return false;
}

int runWorldProcess(int argc, char** argv) {
    int rc = 1;
    try {
        const WorldArgs w = parseArgs(argc, argv);
        char log[48];
        std::snprintf(log, sizeof log, w.client ? "opendash-client-ST%02X-%d.log" : "opendash-world-ST%02X-%d.log", w.stage,
                      w.sub);
        startLogFile(log);
        od::content::registerAll(); // the game code dispatches into the content through its registry
        if (!SDL_Init(0)) throw std::runtime_error(SDL_GetError());
        if (const char* base = SDL_GetBasePath()) od::setConfigDir(base);
        rc = run(w);
    } catch (const std::exception& e) {
        SDL_Log("world failed: %s", e.what());
    }
    SDL_Quit();
    return rc;
}

} // namespace od::app
