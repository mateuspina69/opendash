// The netplay transport (net/udp_socket, net/link_conditioner, net/wire), the protocol (net/protocol: every message's
// round trip) and the session (net/session: the refusals, reliable delivery under loss, a host forwarding to two
// clients) on loopback sockets, in process.
//   test_net_protocol
#include <chrono>
#include <cstdio>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "check.hpp"
#include "net/link_conditioner.hpp"
#include "net/protocol.hpp"
#include "net/session.hpp"
#include "net/udp_socket.hpp"
#include "net/wire.hpp"

namespace {

using od::net::ByteReader;
using od::net::ByteWriter;
using od::net::Datagram;
using od::net::Endpoint;
using od::net::LinkConditioner;
using od::net::LinkSim;
using od::net::UdpSocket;

constexpr uint64_t kMs = 1'000'000;

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// the datagrams `s` holds after the loopback has delivered them (the loopback is fast but not synchronous)
std::vector<Datagram> receiveSome(UdpSocket& s, size_t want) {
    std::vector<Datagram> got;
    for (int tries = 0; tries < 2000 && got.size() < want; ++tries) {
        for (Datagram& d : s.receive()) got.push_back(std::move(d));
        if (got.size() < want) sleepMs(1);
    }
    return got;
}

void testWire() {
    ByteWriter w;
    w.u8(0xab);
    w.u16(0x1234);
    w.u32(0xdeadbeef);
    w.u64(0x0102030405060708ull);
    w.i8(-2);
    w.i16(-300);
    w.i32(-70000);
    w.boolean(true);
    w.string("Mega Man");
    const uint8_t blob[3] = {9, 8, 7};
    w.blob(blob);
    const std::vector<uint8_t> b = w.bytes();
    CHECK(b[1] == 0x34 && b[2] == 0x12); // little endian
    ByteReader r(b);
    CHECK(r.u8() == 0xab);
    CHECK(r.u16() == 0x1234);
    CHECK(r.u32() == 0xdeadbeef);
    CHECK(r.u64() == 0x0102030405060708ull);
    CHECK(r.i8() == -2);
    CHECK(r.i16() == -300);
    CHECK(r.i32() == -70000);
    CHECK(r.boolean());
    CHECK(r.string() == "Mega Man");
    CHECK((r.blob() == std::vector<uint8_t>{9, 8, 7}));
    CHECK(r.done());
    // every prefix fails, never reads past the end
    for (size_t n = 0; n < b.size(); ++n) {
        ByteReader s(std::span<const uint8_t>(b.data(), n));
        s.u8(), s.u16(), s.u32(), s.u64(), s.i8(), s.i16(), s.i32(), s.boolean(), s.string(), s.blob();
        CHECK(!s.ok());
    }
    const uint8_t two[1] = {2};
    ByteReader bad(two);
    bad.boolean();
    CHECK(!bad.ok()); // a boolean is 0 or 1
    ByteReader extra(b);
    extra.u8();
    CHECK(extra.ok() && !extra.done());
}

void testEndpoint() {
    const Endpoint e = Endpoint::parse("10.8.0.1:27015");
    CHECK(e.address == (10u << 24 | 8u << 16 | 1u) && e.port == 27015);
    CHECK(e.text() == "10.8.0.1:27015");
    CHECK(Endpoint::parse("localhost:5000") == Endpoint::loopback(5000));
    for (const char* bad : {"10.8.0.1", "10.8.0.1:", ":5000", "10.8.0.1:0", "10.8.0.1:70000", "10.8.0.1:12a",
                            "no-such-host.invalid:5000"}) {
        bool threw = false;
        try {
            Endpoint::parse(bad);
        } catch (const std::invalid_argument& ex) {
            threw = true;
            std::printf("  refused \"%s\": %s\n", bad, ex.what());
        }
        CHECK(threw);
    }
}

void testSocket() {
    UdpSocket a(0), b(0);
    CHECK(a.port() != 0 && b.port() != 0 && a.port() != b.port());
    bool inUse = false;
    try {
        UdpSocket again(a.port());
    } catch (const std::runtime_error& ex) {
        inUse = true;
        std::printf("  second bind: %s\n", ex.what());
    }
    CHECK(inUse);
    CHECK(b.receive().empty()); // non-blocking: nothing pending
    const uint8_t one[2] = {1, 2}, two[3] = {3, 4, 5};
    CHECK(a.sendTo(Endpoint::loopback(b.port()), one));
    CHECK(a.sendTo(Endpoint::loopback(b.port()), two));
    const auto got = receiveSome(b, 2);
    CHECK(got.size() == 2);
    if (got.size() == 2) {
        CHECK(got[0].from == Endpoint::loopback(a.port()));
        CHECK((got[0].bytes == std::vector<uint8_t>{1, 2}));
        CHECK((got[1].bytes == std::vector<uint8_t>{3, 4, 5}));
    }
    std::vector<uint8_t> big(od::net::kMaxDatagram + 1, 0);
    CHECK(!a.sendTo(Endpoint::loopback(b.port()), big)); // larger than the protocol's datagrams: not sent
    uint16_t gone = 0;
    {
        UdpSocket tmp(0);
        gone = tmp.port();
    }
    // a send to a port with nobody on it (ICMP unreachable) does not break the next receive
    CHECK(a.sendTo(Endpoint::loopback(gone), one));
    sleepMs(5);
    CHECK(a.receive().empty());
}

void testConditioner() {
    UdpSocket a(0), b(0);
    // delay only: each end holds a datagram latency / 2 = 50 ms (no jitter, no loss)
    LinkConditioner ca(a, LinkSim{100, 0, 0.0}, 1), cb(b, LinkSim{100, 0, 0.0}, 2);
    const uint8_t m[1] = {42};
    uint64_t now = 1000 * kMs;
    ca.send(now, Endpoint::loopback(b.port()), m);
    CHECK(b.receive().empty()); // not on the wire yet
    ca.receive(now + 49 * kMs);  // still held
    sleepMs(5);
    CHECK(b.receive().empty());
    ca.receive(now + 50 * kMs); // sent now
    sleepMs(5);
    // cb holds what arrives another 50 ms of its clock
    CHECK(cb.receive(now + 60 * kMs).empty());
    CHECK(cb.receive(now + 109 * kMs).empty());
    const auto due = cb.receive(now + 110 * kMs);
    CHECK(due.size() == 1 && due[0].bytes == std::vector<uint8_t>{42});

    // loss: 20 % of the path = 10 % on this end; 2000 datagrams out of `a`, about 1800 sent
    LinkConditioner lossy(a, LinkSim{0, 0, 20.0}, 3);
    int sent = 0;
    for (int i = 0; i < 2000; ++i) {
        lossy.send(now, Endpoint::loopback(b.port()), m);
        if (i % 100 == 99) {
            sleepMs(1);
            for (const Datagram& d : b.receive()) sent += int(d.bytes.size());
        }
    }
    sleepMs(20);
    for (const Datagram& d : b.receive()) sent += int(d.bytes.size());
    std::printf("  20 %% loss (10 %% this end): %d of 2000 arrived\n", sent);
    CHECK(sent > 1700 && sent < 1900);

    // jitter: delays spread over latency / 2 +- jitter / 2 = 50 +- 25 ms, the order may change
    LinkConditioner jit(a, LinkSim{100, 50, 0.0}, 4);
    for (uint8_t i = 0; i < 50; ++i) {
        const uint8_t k[1] = {i};
        jit.send(now, Endpoint::loopback(b.port()), k);
    }
    jit.receive(now + 24 * kMs);
    sleepMs(5);
    CHECK(b.receive().empty()); // nothing before 25 ms
    jit.receive(now + 76 * kMs);
    const auto all = receiveSome(b, 50);
    CHECK(all.size() == 50);
    bool reordered = false;
    for (size_t i = 1; i < all.size(); ++i) reordered |= all[i].bytes[0] < all[i - 1].bytes[0];
    CHECK(reordered);
}

void testMessages() {
    using namespace od::net;
    const std::vector<std::pair<Message, uint32_t>> all = {
        {Hello{kProtocolVersion, "netplay-2 abc", 0x1111, 0x2222, 0x3333, 77, "Mateus"}, 0},
        {Welcome{3, {{0, "host"}, {1, "Ana"}, {2, "Rui"}}}, 1},
        {Refuse{RefuseReason::GameFiles, "the .DAT files differ"}, 2},
        {PeerJoined{{4, "Rui"}}, 3},
        {PeerLeft{{4, "Rui"}}, 4},
        {PlayerState{2, 1234, 99, {1, 2, 3, 4}}, 0},
        {WorldState{5, 6, {7, 8}}, 0},
        {Ping{123456789}, 0},
        {Pong{123456789}, 0},
        {Bye{}, 5},
        {Ack{42}, 0},
        {Event{{1, 2, 3}}, 6},
    };
    CHECK(all.size() == std::variant_size_v<Message>);
    for (const auto& [m, seq] : all) {
        const auto bytes = encode(m, seq);
        const auto d = decode(bytes);
        CHECK(d && d->message == m && d->version == kProtocolVersion);
        if (d && isReliable(typeOf(m))) CHECK(d->sequence == seq);
        // a cut datagram or one with a byte too many is not a message
        for (size_t n = 0; n < bytes.size(); ++n) CHECK(!decode(std::span<const uint8_t>(bytes.data(), n)));
        auto longer = bytes;
        longer.push_back(0);
        CHECK(!decode(longer));
    }
    auto wrongMagic = encode(Ping{1});
    wrongMagic[0] ^= 1;
    CHECK(!decode(wrongMagic));
    CHECK(!decode(encode(PlayerState{kMaxPlayers, 1, 1, {}}))); // a player id outside the session
    // another protocol version: a Hello yields its version, a Refuse decodes, anything else is dropped
    auto other = encode(Hello{kProtocolVersion + 1, "x", 1, 2, 3, 4, "n"});
    other[4] = uint8_t(kProtocolVersion + 1);
    const auto oh = decode(other);
    CHECK(oh && oh->version == kProtocolVersion + 1 &&
          std::get<Hello>(oh->message).protocolVersion == kProtocolVersion + 1);
    auto otherRefuse = encode(Refuse{RefuseReason::Version, "v"});
    otherRefuse[4] = uint8_t(kProtocolVersion + 1);
    CHECK(decode(otherRefuse).has_value());
    auto otherPing = encode(Ping{1});
    otherPing[4] = uint8_t(kProtocolVersion + 1);
    CHECK(!decode(otherPing));
    CHECK(sessionKeyHash("abc") != sessionKeyHash("abd") && sessionKeyHash("") == 0xcbf29ce484222325ull);
}

// sessions on loopback, polled on a fake clock (stepNs per round, a short real pause so the loopback delivers)
struct Net {
    std::vector<od::net::Session*> all;
    uint64_t now = 1'000'000'000ull;
    void run(int rounds, uint64_t stepNs = 5 * kMs) {
        for (int i = 0; i < rounds; ++i) {
            now += stepNs;
            for (od::net::Session* s : all) s->poll(now);
            sleepMs(1);
        }
    }
};

od::net::SessionConfig hostConfig() {
    od::net::SessionConfig c;
    c.role = od::net::Role::Host;
    c.name = "host";
    c.buildId = "build-1";
    c.gameFilesHash = 0xf11e5;
    c.exeHash = 0xe7e;
    c.sessionKeyHash = od::net::sessionKeyHash("friends");
    c.seed = 11;
    return c;
}
od::net::SessionConfig clientConfig(uint16_t hostPort, const char* name, uint64_t seed) {
    od::net::SessionConfig c = hostConfig();
    c.role = od::net::Role::Client;
    c.host = Endpoint::loopback(hostPort);
    c.name = name;
    c.seed = seed;
    return c;
}

void testRefusals() {
    using namespace od::net;
    Session host(hostConfig());
    struct Case {
        const char* what;
        void (*change)(SessionConfig&);
        RefuseReason reason;
        const char* names; // what the text names
    };
    const Case cases[] = {
        {"build", [](SessionConfig& c) { c.buildId = "build-2"; }, RefuseReason::Build, "build-2"},
        {"exe", [](SessionConfig& c) { c.exeHash = 1; }, RefuseReason::GameFiles, "dash2.exe"},
        {"dat files", [](SessionConfig& c) { c.gameFilesHash = 1; }, RefuseReason::GameFiles, ".DAT"},
        {"key", [](SessionConfig& c) { c.sessionKeyHash = sessionKeyHash("strangers"); }, RefuseReason::Key, "net_key"},
    };
    uint64_t seed = 100;
    for (const Case& k : cases) {
        SessionConfig cc = clientConfig(host.status(0).port, "x", ++seed);
        k.change(cc);
        Session client(cc);
        Net net{{&host, &client}};
        net.run(40);
        const SessionStatus st = client.status(net.now);
        std::printf("  %s: %s\n", k.what, st.text.c_str());
        CHECK(st.connection == Connection::Refused);
        CHECK(st.text.find(refuseReasonName(k.reason)) != std::string::npos);
        CHECK(st.text.find(k.names) != std::string::npos);
        CHECK(host.status(net.now).players.empty());
        CHECK(host.takePeerEvents().empty());
    }
    // a client of another protocol version: the host reads only the Hello's version, and refuses it
    UdpSocket raw(0);
    auto hello = encode(Hello{kProtocolVersion + 1, "?", 0, 0, 0, 1, "future"}, 0);
    hello[4] = uint8_t(kProtocolVersion + 1);
    raw.sendTo(Endpoint::loopback(host.status(0).port), hello);
    Net net{{&host}};
    std::optional<Refuse> got;
    for (int i = 0; i < 200 && !got; ++i) {
        net.run(1);
        for (const Datagram& d : raw.receive())
            if (auto m = decode(d.bytes))
                if (auto* r = std::get_if<Refuse>(&m->message)) got = *r;
    }
    CHECK(got && got->reason == RefuseReason::Version);
    if (got) std::printf("  version: %s\n", got->text.c_str());
}

void testForwarding() {
    using namespace od::net;
    Session host(hostConfig());
    const uint16_t port = host.status(0).port;
    Session a(clientConfig(port, "Ana", 21)), b(clientConfig(port, "Rui", 22));
    Net net{{&host, &a, &b}};
    net.run(40);
    const SessionStatus sa = a.status(net.now), sb = b.status(net.now), sh = host.status(net.now);
    CHECK(sa.connection == Connection::Connected && sb.connection == Connection::Connected);
    CHECK(sa.localId != sb.localId && sa.localId >= 1 && sb.localId >= 1);
    CHECK(sh.players.size() == 2);
    CHECK(sa.players.size() == 2 && sb.players.size() == 2); // the host and the other client
    const auto ev = host.takePeerEvents();
    CHECK(ev.size() == 2 && ev[0].kind == PeerEvent::Kind::Joined);
    a.takePeerEvents();
    b.takePeerEvents();
    // each player's state reaches the other two (the host forwards a client's unchanged)
    const uint8_t pa[2] = {0xa, 1}, pb[2] = {0xb, 1}, ph[2] = {0x0, 1};
    a.sendPlayerState(net.now, 10, pa);
    b.sendPlayerState(net.now, 20, pb);
    host.sendPlayerState(net.now, 30, ph);
    net.run(20);
    auto byId = [](std::vector<ReceivedState> v) {
        std::map<uint8_t, ReceivedState> m;
        for (auto& s : v) m[s.playerId] = s;
        return m;
    };
    const auto ra = byId(a.takePlayerStates()), rb = byId(b.takePlayerStates()), rh = byId(host.takePlayerStates());
    CHECK(ra.size() == 2 && ra.contains(0) && ra.contains(sb.localId));
    CHECK(ra.contains(sb.localId) && ra.at(sb.localId).payload[0] == 0xb && ra.at(sb.localId).tick == 20);
    CHECK(rb.size() == 2 && rb.contains(0) && rb.contains(sa.localId) && rb.at(sa.localId).payload[0] == 0xa);
    CHECK(rh.size() == 2 && rh.contains(sa.localId) && rh.contains(sb.localId));
    // newest wins: an older sequence of a player is dropped
    const uint8_t old[1] = {0xee};
    a.sendPlayerState(net.now, 11, old); // sequence 2: accepted
    net.run(10);
    CHECK(byId(host.takePlayerStates()).at(sa.localId).payload[0] == 0xee);
    // a stranger's PlayerState (not a client of the host) is ignored
    UdpSocket stranger(0);
    stranger.sendTo(Endpoint::loopback(port), encode(PlayerState{sa.localId, 999, 1, {9}}));
    net.run(5);
    CHECK(host.takePlayerStates().empty());
    // a client leaving (its Bye): the host and the other clients see it go
    {
        Session c(clientConfig(port, "Ze", 23));
        net.all.push_back(&c);
        net.run(40);
        CHECK(c.status(net.now).connection == Connection::Connected);
        host.takePeerEvents();
        a.takePeerEvents();
        net.all.pop_back();
    }
    net.run(40);
    const auto left = host.takePeerEvents();
    CHECK(left.size() == 1 && left[0].kind == PeerEvent::Kind::Left && left[0].peer.name == "Ze");
    const auto leftA = a.takePeerEvents();
    CHECK(leftA.size() == 1 && leftA[0].kind == PeerEvent::Kind::Left);
    CHECK(host.status(net.now).players.size() == 2);
}

void testTimeout() {
    using namespace od::net;
    // a client that falls silent (its Bye lost: 100 % loss on its end) is dropped by the host after kPeerTimeoutNs
    Session host(hostConfig());
    const uint16_t port = host.status(0).port;
    Session a(clientConfig(port, "Ana", 41));
    Net net{{&host, &a}};
    net.run(40);
    CHECK(host.status(net.now).players.size() == 1);
    host.takePeerEvents();
    net.all = {&host};
    net.run(int(kPeerTimeoutNs / (100 * kMs)) - 2, 100 * kMs);
    CHECK(host.status(net.now).players.size() == 1); // not yet
    net.run(4, 100 * kMs);
    CHECK(host.status(net.now).players.empty());
    const auto ev = host.takePeerEvents();
    CHECK(ev.size() == 1 && ev[0].kind == PeerEvent::Kind::Left);
    // and the client, its host silent as long, starts over (joins again once the host hears it)
    net.all = {&a};
    net.run(int(kPeerTimeoutNs / (100 * kMs)) + 2, 100 * kMs);
    CHECK(a.status(net.now).connection == Connection::Connecting);
    CHECK(a.status(net.now).text.find("silent") != std::string::npos);
    net.all = {&host, &a};
    net.run(40);
    CHECK(a.status(net.now).connection == Connection::Connected);
    CHECK(host.status(net.now).players.size() == 1);
}

void testReliableUnderLoss() {
    using namespace od::net;
    // 125 ms one-way, 20 ms jitter, 30 % loss on every path: the handshake and the joins still arrive, in order
    const LinkSim sim{125, 20, 30.0};
    SessionConfig hc = hostConfig();
    hc.sim = sim;
    Session host(hc);
    const uint16_t port = host.status(0).port;
    SessionConfig ac = clientConfig(port, "Ana", 31), bc = clientConfig(port, "Rui", 32);
    ac.sim = bc.sim = sim;
    Session a(ac), b(bc);
    Net net{{&host, &a, &b}};
    net.run(400, 10 * kMs); // 4 s of the fake clock
    const SessionStatus sa = a.status(net.now), sb = b.status(net.now);
    CHECK(sa.connection == Connection::Connected && sb.connection == Connection::Connected);
    CHECK(sa.players.size() == 2 && sb.players.size() == 2);
    CHECK(host.status(net.now).players.size() == 2);
    // states cross the lossy links; the measured loss and round trip are near the settings
    int got = 0;
    for (int i = 0; i < 300; ++i) {
        const uint8_t p[1] = {uint8_t(i)};
        host.sendPlayerState(net.now, uint32_t(i), p);
        net.run(1, 33 * kMs);
        for (const ReceivedState& s : a.takePlayerStates()) got += s.playerId == 0;
    }
    const PlayerStatus hp = a.status(net.now).players[0];
    std::printf("  lossy path: %d of 300 host states at Ana, loss %.1f %%, rtt %d ms\n", got, hp.lossPercent,
                hp.rttMs);
    CHECK(got > 170 && got < 250);
    CHECK(hp.rttMs > 200 && hp.rttMs < 320);
}

} // namespace

int main() {
    testWire();
    testEndpoint();
    testSocket();
    testConditioner();
    testMessages();
    testRefusals();
    testForwarding();
    testTimeout();
    testReliableUnderLoss();
    return testResult("test_net_protocol");
}
