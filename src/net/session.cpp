// The netplay session (see session.hpp).
#include "net/session.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace od::net {

namespace {

constexpr uint64_t kNsPerMs = 1'000'000;
constexpr uint64_t kPingIntervalNs = 1'000'000'000ull;     // one ping a second per link
constexpr uint64_t kInitialResendNs = 300 * kNsPerMs;      // before a round trip is measured
constexpr uint64_t kMinResendNs = 100 * kNsPerMs, kMaxResendNs = 1'000 * kNsPerMs;
constexpr uint64_t kStatsWindowNs = 2'000'000'000ull;      // the loss figure covers this much play
constexpr double kRttSmoothing = 0.2;                      // weight of a new round-trip sample
constexpr size_t kMaxEarly = 64;                           // reliable messages held ahead of a gap

// a reliable message is resent after twice the smoothed round trip (bounded)
uint64_t resendAfterNs(double rttMs) {
    if (rttMs < 0) return kInitialResendNs;
    return std::clamp(uint64_t(2.0 * rttMs * double(kNsPerMs)), kMinResendNs, kMaxResendNs);
}

// xorshift64* step: the Hello nonces (a new one per connection attempt)
uint32_t nextNonce(uint64_t& s) {
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return uint32_t((s * 0x2545f4914f6cdd1dull) >> 32);
}

std::string hex64(uint64_t v) {
    char buf[20];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

} // namespace

const char* connectionName(Connection c) {
    switch (c) {
    case Connection::Hosting: return "hosting";
    case Connection::Connecting: return "connecting";
    case Connection::Connected: return "connected";
    case Connection::Refused: return "refused";
    }
    return "?";
}

Session::Session(const SessionConfig& config)
    : config_(config), socket_(config.port),
      connection_(config.role == Role::Host ? Connection::Hosting : Connection::Connecting),
      nonceState_(config.seed | 1) {
    if (config_.sim) conditioner_.emplace(socket_, *config_.sim, config_.seed);
    if (config_.role == Role::Client) hostLink_.endpoint = config_.host;
}

Session::~Session() {
    if (config_.role == Role::Host) {
        for (auto& [e, c] : clients_)
            if (c.accepted) sendReliable(lastNowNs_, c.link, Bye{});
    } else if (connection_ == Connection::Connected) {
        sendReliable(lastNowNs_, hostLink_, Bye{});
    }
}

void Session::transmit(uint64_t nowNs, const Endpoint& to, std::span<const uint8_t> bytes) {
    if (conditioner_) conditioner_->send(nowNs, to, bytes);
    else socket_.sendTo(to, bytes);
}

void Session::sendReliable(uint64_t nowNs, Link& link, const Message& m) {
    const uint32_t seq = link.nextOut++;
    std::vector<uint8_t> bytes = encode(m, seq);
    transmit(nowNs, link.endpoint, bytes);
    link.unacked.push_back({seq, std::move(bytes), nowNs});
}

void Session::sendUnreliable(uint64_t nowNs, const Link& link, const Message& m) {
    transmit(nowNs, link.endpoint, encode(m));
}

std::vector<Message> Session::accept(uint64_t nowNs, Link& link, uint32_t sequence, Message m) {
    std::vector<Message> ready;
    if (sequence == link.nextIn) {
        ready.push_back(std::move(m));
        ++link.nextIn;
        for (auto it = link.early.find(link.nextIn); it != link.early.end(); it = link.early.find(link.nextIn)) {
            ready.push_back(std::move(it->second));
            link.early.erase(it);
            ++link.nextIn;
        }
    } else if (sequence > link.nextIn && link.early.size() < kMaxEarly) {
        link.early.emplace(sequence, std::move(m)); // a duplicate of an early one keeps the first
    }
    // below nextIn: a resend of something delivered (its ack was lost); acked again below
    sendUnreliable(nowNs, link, Ack{link.nextIn});
    return ready;
}

void Session::handleLinkMessage(uint64_t nowNs, Link& link, const Message& m) {
    if (const Ack* a = std::get_if<Ack>(&m)) {
        std::erase_if(link.unacked, [&](const Unacked& u) { return u.sequence < a->next; });
    } else if (const Ping* p = std::get_if<Ping>(&m)) {
        sendUnreliable(nowNs, link, Pong{p->sentNs});
    } else if (const Pong* echo = std::get_if<Pong>(&m)) {
        if (echo->echoNs <= nowNs) {
            const double sample = double(nowNs - echo->echoNs) / double(kNsPerMs);
            link.rttMs = link.rttMs < 0 ? sample : link.rttMs + kRttSmoothing * (sample - link.rttMs);
        }
    }
}

void Session::serviceLink(uint64_t nowNs, Link& link) {
    const uint64_t after = resendAfterNs(link.rttMs);
    for (Unacked& u : link.unacked)
        if (nowNs - u.sentNs >= after) {
            transmit(nowNs, link.endpoint, u.bytes);
            u.sentNs = nowNs;
        }
    if (nowNs - link.lastPingNs >= kPingIntervalNs) {
        link.lastPingNs = nowNs;
        sendUnreliable(nowNs, link, Ping{nowNs});
    }
}

bool Session::acceptState(uint64_t nowNs, const PlayerState& s) {
    StateStats& st = stats_[s.playerId];
    if (st.any && s.sequence <= st.lastSequence) return false; // older or a duplicate: newest wins
    st.any = true;
    st.lastSequence = s.sequence;
    st.lastNs = nowNs;
    if (st.windowReceived == 0) {
        st.windowFirst = s.sequence;
        st.windowStartNs = nowNs;
    }
    st.windowLast = s.sequence;
    ++st.windowReceived;
    if (nowNs - st.windowStartNs >= kStatsWindowNs) {
        const double expected = double(st.windowLast - st.windowFirst) + 1.0;
        st.lossPercent = float(100.0 * (1.0 - double(st.windowReceived) / expected));
        st.windowReceived = 0;
    }
    states_.push_back({s.playerId, s.sequence, s.tick, s.payload});
    return true;
}

void Session::poll(uint64_t nowNs) {
    lastNowNs_ = nowNs;
    if (config_.role == Role::Client && !started_) {
        started_ = true;
        clientStart(nowNs);
    }
    std::vector<Datagram> in = conditioner_ ? conditioner_->receive(nowNs) : socket_.receive();
    for (Datagram& d : in) {
        auto decoded = decode(d.bytes);
        if (!decoded) continue; // not ours, or malformed
        if (config_.role == Role::Host) hostReceive(nowNs, d.from, std::move(*decoded));
        else clientReceive(nowNs, d.from, std::move(*decoded));
    }
    if (config_.role == Role::Host) {
        std::vector<Endpoint> silent;
        for (auto& [e, c] : clients_) {
            if (nowNs - c.link.lastHeardNs >= kPeerTimeoutNs) silent.push_back(e);
            else serviceLink(nowNs, c.link);
        }
        for (const Endpoint& e : silent) hostDrop(nowNs, e);
    } else if (connection_ != Connection::Refused) {
        if (nowNs - hostLink_.lastHeardNs >= kPeerTimeoutNs) clientLost(nowNs, "the host is silent");
        else serviceLink(nowNs, hostLink_);
    }
}

void Session::sendPlayerState(uint64_t nowNs, uint32_t tick, std::span<const uint8_t> payload) {
    const PlayerState s{localId_, ++stateSequence_, tick, std::vector<uint8_t>(payload.begin(), payload.end())};
    if (config_.role == Role::Host) {
        const std::vector<uint8_t> bytes = encode(s);
        for (auto& [e, c] : clients_)
            if (c.accepted) transmit(nowNs, e, bytes);
    } else if (connection_ == Connection::Connected) {
        sendUnreliable(nowNs, hostLink_, s);
    }
}

std::vector<ReceivedState> Session::takePlayerStates() { return std::exchange(states_, {}); }
std::vector<PeerEvent> Session::takePeerEvents() { return std::exchange(events_, {}); }

// ---- host ----------------------------------------------------------------------------------------------------------

void Session::hostReceive(uint64_t nowNs, const Endpoint& from, Decoded d) {
    auto it = clients_.find(from);
    if (const Hello* h = std::get_if<Hello>(&d.message)) {
        if (it != clients_.end() && it->second.nonce != h->nonce) { // the same address, a new connection
            hostDrop(nowNs, from);
            it = clients_.end();
        }
        if (it == clients_.end()) {
            Client c;
            c.link.endpoint = from;
            c.nonce = h->nonce;
            it = clients_.emplace(from, std::move(c)).first;
        }
    }
    if (it == clients_.end()) return; // not a client, and not a Hello
    Client& c = it->second;
    c.link.lastHeardNs = nowNs;
    if (d.version != kProtocolVersion) {
        // only a Hello decodes from another version: refuse it (once; its acks are in its own layout, the record
        // falls silent and goes)
        if (c.link.nextOut == 0) {
            const Hello& h = std::get<Hello>(d.message);
            sendReliable(nowNs, c.link,
                         Refuse{RefuseReason::Version, "host protocol " + std::to_string(kProtocolVersion) +
                                                           ", yours " + std::to_string(h.protocolVersion)});
        }
        return;
    }
    const MessageType t = typeOf(d.message);
    if (isReliable(t)) {
        for (const Message& m : accept(nowNs, c.link, d.sequence, std::move(d.message))) {
            hostDeliver(nowNs, c, m);
            if (!clients_.contains(from)) return; // a Bye dropped it
        }
        return;
    }
    if (const PlayerState* s = std::get_if<PlayerState>(&d.message)) {
        // a client's own state only (its id), from a welcomed client; forwarded unchanged to the others
        if (!c.accepted || s->playerId != c.info.playerId) return;
        if (!acceptState(nowNs, *s)) return;
        const std::vector<uint8_t> bytes = encode(*s);
        for (auto& [e, other] : clients_)
            if (other.accepted && e != from) transmit(nowNs, e, bytes);
        return;
    }
    // WorldState is the host's own message (co-op milestones): a client sending one is ignored
    handleLinkMessage(nowNs, c.link, d.message);
}

void Session::hostDeliver(uint64_t nowNs, Client& c, const Message& m) {
    if (const Hello* h = std::get_if<Hello>(&m)) {
        if (!c.accepted) hostWelcomeOrRefuse(nowNs, c, *h);
    } else if (std::holds_alternative<Bye>(m)) {
        hostDrop(nowNs, c.link.endpoint);
    }
    // Welcome / Refuse / PeerJoined / PeerLeft are the host's own messages: from a client they are ignored
}

void Session::hostWelcomeOrRefuse(uint64_t nowNs, Client& c, const Hello& h) {
    std::optional<Refuse> refuse;
    if (h.protocolVersion != kProtocolVersion)
        refuse = Refuse{RefuseReason::Version, "host protocol " + std::to_string(kProtocolVersion) + ", yours " +
                                                   std::to_string(h.protocolVersion)};
    else if (h.buildId != config_.buildId)
        refuse = Refuse{RefuseReason::Build, "host build " + config_.buildId + ", yours " + h.buildId};
    else if (h.exeHash != config_.exeHash)
        refuse = Refuse{RefuseReason::GameFiles, "dash2.exe differs (host " + hex64(config_.exeHash) + ", yours " +
                                                     hex64(h.exeHash) + ")"};
    else if (h.gameFilesHash != config_.gameFilesHash)
        refuse = Refuse{RefuseReason::GameFiles, "the .DAT files differ (host " + hex64(config_.gameFilesHash) +
                                                     ", yours " + hex64(h.gameFilesHash) + ")"};
    else if (h.sessionKeyHash != config_.sessionKeyHash)
        refuse = Refuse{RefuseReason::Key, "the session key (net_key) differs"};
    uint8_t id = 0;
    if (!refuse) {
        for (uint8_t candidate = 1; candidate < kMaxPlayers && id == 0; ++candidate) {
            const bool used = std::any_of(clients_.begin(), clients_.end(), [&](const auto& e) {
                return e.second.accepted && e.second.info.playerId == candidate;
            });
            if (!used) id = candidate;
        }
        if (id == 0) refuse = Refuse{RefuseReason::Full, std::to_string(kMaxPlayers) + " players already"};
    }
    if (refuse) {
        text_ = c.link.endpoint.text() + " (" + h.name + ") refused: " + refuseReasonName(refuse->reason) + ": " +
                refuse->text;
        sendReliable(nowNs, c.link, *refuse);
        return;
    }
    c.accepted = true;
    c.info = {id, h.name};
    Welcome w{id, {{kHostPlayerId, config_.name}}};
    for (auto& [e, other] : clients_)
        if (other.accepted && &other != &c) {
            w.peers.push_back(other.info);
            sendReliable(nowNs, other.link, PeerJoined{c.info});
        }
    sendReliable(nowNs, c.link, w);
    events_.push_back({PeerEvent::Kind::Joined, c.info});
}

void Session::hostDrop(uint64_t nowNs, const Endpoint& e) {
    auto it = clients_.find(e);
    if (it == clients_.end()) return;
    const bool accepted = it->second.accepted;
    const PeerInfo info = it->second.info;
    clients_.erase(it);
    if (!accepted) return;
    stats_.erase(info.playerId);
    for (auto& [oe, other] : clients_)
        if (other.accepted) sendReliable(nowNs, other.link, PeerLeft{info});
    events_.push_back({PeerEvent::Kind::Left, info});
}

// ---- client --------------------------------------------------------------------------------------------------------

void Session::clientStart(uint64_t nowNs) {
    hostLink_ = Link{};
    hostLink_.endpoint = config_.host;
    hostLink_.lastHeardNs = nowNs; // the timeout counts from the Hello
    hostLink_.lastPingNs = nowNs;
    connection_ = Connection::Connecting;
    Hello h;
    h.buildId = config_.buildId;
    h.gameFilesHash = config_.gameFilesHash;
    h.exeHash = config_.exeHash;
    h.sessionKeyHash = config_.sessionKeyHash;
    h.nonce = nextNonce(nonceState_);
    h.name = config_.name;
    sendReliable(nowNs, hostLink_, h);
}

void Session::clientLost(uint64_t nowNs, const char* why) {
    for (const auto& [id, p] : players_) events_.push_back({PeerEvent::Kind::Left, p});
    players_.clear();
    stats_.clear();
    localId_ = kHostPlayerId;
    text_ = std::string(why) + "; joining " + config_.host.text() + " again";
    clientStart(nowNs);
}

void Session::clientReceive(uint64_t nowNs, const Endpoint& from, Decoded d) {
    if (from != config_.host || connection_ == Connection::Refused) return;
    hostLink_.lastHeardNs = nowNs;
    if (d.version != kProtocolVersion && !std::holds_alternative<Refuse>(d.message)) return;
    const MessageType t = typeOf(d.message);
    if (isReliable(t)) {
        for (const Message& m : accept(nowNs, hostLink_, d.sequence, std::move(d.message))) {
            clientDeliver(nowNs, m);
            if (connection_ == Connection::Refused) return;
        }
        return;
    }
    if (const PlayerState* s = std::get_if<PlayerState>(&d.message)) {
        if (connection_ == Connection::Connected && s->playerId != localId_) acceptState(nowNs, *s);
        return;
    }
    // WorldState: the host's shared world of the co-op milestones; this milestone's host never sends it
    handleLinkMessage(nowNs, hostLink_, d.message);
}

void Session::clientDeliver(uint64_t nowNs, const Message& m) {
    if (const Welcome* w = std::get_if<Welcome>(&m)) {
        if (connection_ != Connection::Connecting) return;
        connection_ = Connection::Connected;
        localId_ = w->playerId;
        text_.clear();
        for (const PeerInfo& p : w->peers) {
            if (p.playerId == localId_) continue;
            players_[p.playerId] = p;
            events_.push_back({PeerEvent::Kind::Joined, p});
        }
    } else if (const Refuse* r = std::get_if<Refuse>(&m)) {
        connection_ = Connection::Refused;
        text_ = std::string("refused by the host: ") + refuseReasonName(r->reason) + ": " + r->text;
        hostLink_.unacked.clear();
    } else if (const PeerJoined* j = std::get_if<PeerJoined>(&m)) {
        if (j->peer.playerId == localId_) return;
        players_[j->peer.playerId] = j->peer;
        events_.push_back({PeerEvent::Kind::Joined, j->peer});
    } else if (const PeerLeft* l = std::get_if<PeerLeft>(&m)) {
        if (players_.erase(l->peer.playerId)) {
            stats_.erase(l->peer.playerId);
            events_.push_back({PeerEvent::Kind::Left, l->peer});
        }
    } else if (std::holds_alternative<Bye>(m)) {
        clientLost(nowNs, "the host left");
    }
}

// ---- status --------------------------------------------------------------------------------------------------------

SessionStatus Session::status(uint64_t nowNs) const {
    SessionStatus st;
    st.role = config_.role;
    st.port = socket_.port();
    st.localId = localId_;
    st.connection = connection_;
    st.text = text_;
    auto fill = [&](PlayerStatus& p) {
        if (auto s = stats_.find(p.playerId); s != stats_.end() && s->second.any) {
            p.lossPercent = s->second.lossPercent;
            p.lastHeardMs = int64_t((nowNs - s->second.lastNs) / kNsPerMs);
        }
    };
    if (config_.role == Role::Host) {
        for (const auto& [e, c] : clients_) {
            if (!c.accepted) continue;
            PlayerStatus p{c.info.playerId, c.info.name, e.text(), int(c.link.rttMs)};
            if (c.link.rttMs < 0) p.rttMs = -1;
            fill(p);
            st.players.push_back(p);
        }
    } else {
        for (const auto& [id, info] : players_) {
            PlayerStatus p{id, info.name};
            if (id == kHostPlayerId) {
                p.address = config_.host.text();
                p.rttMs = hostLink_.rttMs < 0 ? -1 : int(hostLink_.rttMs);
            }
            fill(p);
            st.players.push_back(p);
        }
    }
    std::sort(st.players.begin(), st.players.end(),
              [](const PlayerStatus& a, const PlayerStatus& b) { return a.playerId < b.playerId; });
    return st;
}

} // namespace od::net
