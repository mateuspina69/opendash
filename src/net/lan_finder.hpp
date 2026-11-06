// The hosts on the local network: a Probe broadcast every second to each network this machine is on (and the limited
// broadcast), on the ports given; every host's Offer answer is kept until it falls silent. No game code.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/udp_socket.hpp"

namespace od::net {

struct LanHost {
    Endpoint endpoint; // the host's session
    std::string name, buildId;
    int players = 1;
    uint64_t lastNs = 0;
};

class LanFinder {
public:
    // throws std::runtime_error when the socket cannot be bound
    explicit LanFinder(std::vector<uint16_t> ports);
    void poll(uint64_t nowNs);
    const std::vector<LanHost>& hosts() const { return hosts_; } // oldest found first

private:
    UdpSocket socket_;
    std::vector<uint16_t> ports_;
    std::vector<LanHost> hosts_;
    uint64_t nextProbeNs_ = 0;
};

} // namespace od::net
