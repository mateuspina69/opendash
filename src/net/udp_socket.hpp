// Plain IPv4 UDP over Winsock / BSD sockets (docs/adr/0005-netplay-transport.md): a non-blocking socket bound to a port, datagrams
// sent to and drained from `ip:port` endpoints. No game code: the netplay session (net/session) runs on it.
#pragma once

#include <compare>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace od::net {

// Largest datagram the session sends or accepts (below the 1280-byte IPv6 minimum MTU minus headers, so a VPN
// tunnel's smaller MTU never fragments it)
constexpr size_t kMaxDatagram = 1200;

// An IPv4 address and port, both in host byte order
struct Endpoint {
    uint32_t address = 0; // a.b.c.d = a << 24 | b << 16 | c << 8 | d
    uint16_t port = 0;

    // "a.b.c.d:port"
    std::string text() const;
    // "a.b.c.d:port" or "hostname:port" (resolved with getaddrinfo, IPv4 only); throws std::invalid_argument with
    // the text and the reason (no port, port outside 1..65535, the name does not resolve to an IPv4 address)
    static Endpoint parse(const std::string& text);
    // 127.0.0.1:port
    static Endpoint loopback(uint16_t port);

    friend auto operator<=>(const Endpoint&, const Endpoint&) = default;
};

struct Datagram {
    Endpoint from;
    std::vector<uint8_t> bytes;
};

class UdpSocket {
public:
    // binds 0.0.0.0:port (0 = any free port) non-blocking; throws std::runtime_error naming the port and the
    // socket error when it cannot (the port in use, Winsock missing)
    explicit UdpSocket(uint16_t port);
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // the bound port (the one the system chose for port 0)
    uint16_t port() const { return port_; }
    // sends one datagram (at most kMaxDatagram bytes); false when the system did not take it (a full send buffer,
    // an unreachable network): UDP, the caller's protocol resends what must arrive
    bool sendTo(const Endpoint& to, std::span<const uint8_t> bytes);
    // every datagram waiting on the socket, oldest first (empty when none)
    std::vector<Datagram> receive();

private:
    uintptr_t socket_; // the Winsock SOCKET or the file descriptor
    uint16_t port_ = 0;
};

} // namespace od::net
