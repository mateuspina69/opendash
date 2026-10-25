// IPv4 UDP over Winsock or BSD sockets (see udp_socket.hpp).
#include "net/udp_socket.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <stdexcept>

namespace od::net {

namespace {

#ifdef _WIN32
using SocketLen = int;
// Winsock 2.2, started on first use and cleaned up at exit (one WSAStartup for the process)
class Winsock {
public:
    Winsock() {
        WSADATA data;
        error_ = WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~Winsock() {
        if (error_ == 0) WSACleanup();
    }
    Winsock(const Winsock&) = delete;
    Winsock& operator=(const Winsock&) = delete;
    int error() const { return error_; }

private:
    int error_ = 0;
};

void requireWinsock() {
    static const Winsock winsock;
    if (winsock.error() != 0)
        throw std::runtime_error("net: WSAStartup failed (Winsock error " + std::to_string(winsock.error()) + ")");
}
int lastError() { return WSAGetLastError(); }
#else
using SOCKET = int;
using SocketLen = socklen_t;
constexpr SOCKET INVALID_SOCKET = -1;
constexpr int WSAEADDRINUSE = EADDRINUSE;
void requireWinsock() {}
int lastError() { return errno; }
int closesocket(SOCKET s) { return ::close(s); }
#endif

sockaddr_in toSockaddr(const Endpoint& e) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(e.address);
    a.sin_port = htons(e.port);
    return a;
}

constexpr size_t kReceiveBuffer = 2048; // larger than kMaxDatagram: an oversized datagram is seen whole and dropped

} // namespace

std::string Endpoint::text() const {
    return std::to_string(address >> 24) + "." + std::to_string((address >> 16) & 0xff) + "." +
           std::to_string((address >> 8) & 0xff) + "." + std::to_string(address & 0xff) + ":" + std::to_string(port);
}

Endpoint Endpoint::loopback(uint16_t port) { return {INADDR_LOOPBACK, port}; }

Endpoint Endpoint::parse(const std::string& text) {
    const size_t colon = text.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == text.size())
        throw std::invalid_argument("net: \"" + text + "\" is not ip:port");
    const std::string host = text.substr(0, colon), portText = text.substr(colon + 1);
    int port = 0;
    for (const char c : portText) {
        if (c < '0' || c > '9' || port > 65535) throw std::invalid_argument("net: \"" + text + "\": bad port");
        port = port * 10 + (c - '0');
    }
    if (port < 1 || port > 65535) throw std::invalid_argument("net: \"" + text + "\": port outside 1..65535");
    requireWinsock();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* found = nullptr;
    const int rc = getaddrinfo(host.c_str(), nullptr, &hints, &found);
    if (rc != 0 || !found)
        throw std::invalid_argument("net: \"" + host + "\" does not resolve to an IPv4 address (getaddrinfo error " +
                                    std::to_string(rc) + ")");
    const auto* a = reinterpret_cast<const sockaddr_in*>(found->ai_addr);
    const Endpoint e{ntohl(a->sin_addr.s_addr), uint16_t(port)};
    freeaddrinfo(found);
    return e;
}

UdpSocket::UdpSocket(uint16_t port) : socket_(uintptr_t(INVALID_SOCKET)) {
    requireWinsock();
    const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        throw std::runtime_error("net: socket() failed (socket error " + std::to_string(lastError()) + ")");
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) {
        const int err = lastError();
        closesocket(s);
        throw std::runtime_error("net: cannot bind UDP port " + std::to_string(port) + " (socket error " +
                                 std::to_string(err) + (err == WSAEADDRINUSE ? ": the port is in use)" : ")"));
    }
#ifdef _WIN32
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    // Windows reports an ICMP "port unreachable" of an earlier send as WSAECONNRESET on the next recvfrom; a peer
    // that is not up yet is normal here (the session resends), so the report is turned off
    BOOL reportReset = FALSE;
    DWORD bytes = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &reportReset, sizeof reportReset, nullptr, 0, &bytes, nullptr, nullptr);
#else
    // an unconnected UDP socket gets no ICMP errors here, so there is nothing to turn off
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    sockaddr_in bound{};
    SocketLen len = sizeof bound;
    getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len);
    socket_ = uintptr_t(s);
    port_ = ntohs(bound.sin_port);
}

UdpSocket::~UdpSocket() {
    if (SOCKET(socket_) != INVALID_SOCKET) closesocket(SOCKET(socket_));
}

bool UdpSocket::sendTo(const Endpoint& to, std::span<const uint8_t> bytes) {
    if (bytes.size() > kMaxDatagram) return false;
    const sockaddr_in a = toSockaddr(to);
    const int sent = ::sendto(SOCKET(socket_), reinterpret_cast<const char*>(bytes.data()), int(bytes.size()), 0,
                              reinterpret_cast<const sockaddr*>(&a), sizeof a);
    return sent == int(bytes.size());
}

std::vector<Datagram> UdpSocket::receive() {
    std::vector<Datagram> out;
    char buf[kReceiveBuffer];
#ifdef _WIN32
    constexpr int kFlags = 0;
#else
    constexpr int kFlags = MSG_TRUNC; // n is the datagram's real size: an oversized one is dropped below
#endif
    for (;;) {
        sockaddr_in from{};
        SocketLen len = sizeof from;
        const int n = int(::recvfrom(SOCKET(socket_), buf, int(sizeof buf), kFlags, reinterpret_cast<sockaddr*>(&from),
                                     &len));
        if (n < 0) {
#ifdef _WIN32
            if (lastError() == WSAEMSGSIZE) continue; // larger than any datagram of the protocol: dropped
#endif
            break; // EWOULDBLOCK: drained (any other error: nothing more to read now)
        }
        if (size_t(n) > kMaxDatagram) continue;
        out.push_back({{ntohl(from.sin_addr.s_addr), ntohs(from.sin_port)},
                       std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(buf),
                                            reinterpret_cast<const uint8_t*>(buf) + n)});
    }
    return out;
}

} // namespace od::net
