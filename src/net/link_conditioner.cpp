// The --net-sim link conditioner (see link_conditioner.hpp).
#include "net/link_conditioner.hpp"

#include <algorithm>

namespace od::net {

namespace {
constexpr uint64_t kNsPerMs = 1'000'000;
} // namespace

LinkConditioner::LinkConditioner(UdpSocket& socket, const LinkSim& sim, uint64_t seed)
    : socket_(socket), sim_(sim), rng_(seed) {}

uint64_t LinkConditioner::delayNs() {
    // this end's half: latency / 2 +- jitter / 2
    const double half = double(sim_.latencyMs) * 0.5, spread = double(sim_.jitterMs) * 0.5;
    std::uniform_real_distribution<double> jitter(-spread, spread);
    const double ms = std::max(0.0, half + (spread > 0.0 ? jitter(rng_) : 0.0));
    return uint64_t(ms * double(kNsPerMs));
}

bool LinkConditioner::lost() {
    if (sim_.lossPercent <= 0.0) return false;
    std::uniform_real_distribution<double> roll(0.0, 100.0);
    return roll(rng_) < sim_.lossPercent * 0.5;
}

void LinkConditioner::send(uint64_t nowNs, const Endpoint& to, std::span<const uint8_t> bytes) {
    if (lost()) return;
    outgoing_.push_back({nowNs + delayNs(), order_++, to, std::vector<uint8_t>(bytes.begin(), bytes.end())});
    flushOutgoing(nowNs);
}

void LinkConditioner::flushOutgoing(uint64_t nowNs) {
    std::stable_sort(outgoing_.begin(), outgoing_.end(), [](const Pending& a, const Pending& b) {
        return a.dueNs != b.dueNs ? a.dueNs < b.dueNs : a.order < b.order;
    });
    size_t sent = 0;
    while (sent < outgoing_.size() && outgoing_[sent].dueNs <= nowNs) {
        socket_.sendTo(outgoing_[sent].peer, outgoing_[sent].bytes);
        ++sent;
    }
    outgoing_.erase(outgoing_.begin(), outgoing_.begin() + std::ptrdiff_t(sent));
}

std::vector<Datagram> LinkConditioner::receive(uint64_t nowNs) {
    flushOutgoing(nowNs);
    for (Datagram& d : socket_.receive()) {
        if (lost()) continue;
        incoming_.push_back({nowNs + delayNs(), order_++, d.from, std::move(d.bytes)});
    }
    std::stable_sort(incoming_.begin(), incoming_.end(), [](const Pending& a, const Pending& b) {
        return a.dueNs != b.dueNs ? a.dueNs < b.dueNs : a.order < b.order;
    });
    std::vector<Datagram> due;
    size_t n = 0;
    while (n < incoming_.size() && incoming_[n].dueNs <= nowNs) {
        due.push_back({incoming_[n].peer, std::move(incoming_[n].bytes)});
        ++n;
    }
    incoming_.erase(incoming_.begin(), incoming_.begin() + std::ptrdiff_t(n));
    return due;
}

} // namespace od::net
