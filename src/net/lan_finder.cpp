// The hosts on the local network (see lan_finder.hpp).
#include "net/lan_finder.hpp"

#include <algorithm>

#include "net/protocol.hpp"

namespace od::net {

namespace {
constexpr uint64_t kProbeEveryNs = 1'000'000'000ull;
constexpr uint64_t kSilentNs = 3'500'000'000ull; // three probes unanswered: gone
} // namespace

LanFinder::LanFinder(std::vector<uint16_t> ports) : socket_(0), ports_(std::move(ports)) { socket_.enableBroadcast(); }

void LanFinder::poll(uint64_t nowNs) {
    if (nowNs >= nextProbeNs_) {
        nextProbeNs_ = nowNs + kProbeEveryNs;
        std::vector<uint32_t> to{0xffffffffu};
        for (const LocalNetwork& n : localNetworks())
            if (std::find(to.begin(), to.end(), n.broadcast) == to.end()) to.push_back(n.broadcast);
        const std::vector<uint8_t> probe = encode(Probe{});
        for (const uint32_t a : to)
            for (const uint16_t p : ports_) socket_.sendTo({a, p}, probe);
    }
    for (const Datagram& d : socket_.receive()) {
        const auto decoded = decode(d.bytes);
        if (!decoded || decoded->version != kProtocolVersion) continue;
        const Offer* o = std::get_if<Offer>(&decoded->message);
        if (!o) continue;
        const Endpoint at{d.from.address, o->port};
        auto it = std::find_if(hosts_.begin(), hosts_.end(), [&](const LanHost& h) { return h.endpoint == at; });
        if (it == hosts_.end()) it = hosts_.insert(hosts_.end(), LanHost{at});
        it->name = o->name;
        it->buildId = o->buildId;
        it->players = o->players;
        it->lastNs = nowNs;
    }
    std::erase_if(hosts_, [&](const LanHost& h) { return nowNs - h.lastNs > kSilentNs; });
}

} // namespace od::net
