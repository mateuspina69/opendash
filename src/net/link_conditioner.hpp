// A test instrument between the netplay session and its socket (--net-sim): datagrams are delayed (latency + jitter)
// and dropped (loss) in both directions, on a monotonic clock the caller passes in, so the smoothing of the ghosts can
// be checked at Curitiba - Lisbon conditions on one machine. Not a path of normal play: the session sends straight
// through the socket unless a simulation is configured.
//
// One conditioner models this instance's half of the path: every datagram it sends and every one it receives waits
// latency / 2 (+- jitter / 2, uniform) and is lost with probability loss / 2. Two instances with the same settings
// therefore give a one-way delay of `latency`, a round trip of 2 * latency and about `loss` per one-way trip
// (`--net-sim 125 20 3` on both ends: ~250 ms round trip, ~3 % loss). Jitter can reorder datagrams, as a real path can.
#pragma once

#include <cstdint>
#include <random>
#include <span>
#include <vector>

#include "net/udp_socket.hpp"

namespace od::net {

struct LinkSim {
    int latencyMs = 0;   // one-way delay of the whole path (half of it on each end)
    int jitterMs = 0;    // the spread of that delay (+- half of it on each end)
    double lossPercent = 0.0; // one-way loss of the whole path (half of it on each end)
};

class LinkConditioner {
public:
    LinkConditioner(UdpSocket& socket, const LinkSim& sim, uint64_t seed);

    // queues the datagram (or drops it); sent once its delay has passed (in a later receive / send call)
    void send(uint64_t nowNs, const Endpoint& to, std::span<const uint8_t> bytes);
    // sends the outgoing datagrams that are due, drains the socket into the incoming queue (dropping some) and
    // returns the incoming datagrams that are due, in their delivery order
    std::vector<Datagram> receive(uint64_t nowNs);

    const LinkSim& sim() const { return sim_; }

private:
    struct Pending {
        uint64_t dueNs;
        uint64_t order; // ties of dueNs keep the queue order
        Endpoint peer;
        std::vector<uint8_t> bytes;
    };
    uint64_t delayNs();
    bool lost();
    void flushOutgoing(uint64_t nowNs);

    UdpSocket& socket_;
    LinkSim sim_;
    std::mt19937_64 rng_;
    uint64_t order_ = 0;
    std::vector<Pending> outgoing_, incoming_;
};

} // namespace od::net
