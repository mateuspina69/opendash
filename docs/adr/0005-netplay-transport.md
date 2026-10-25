# ADR 0005 — Netplay transport: plain Winsock UDP

Date: 2026-10-24. Status: accepted.

## Context

Netplay milestone 1 (ghost players) sends each player's
state about 30 times a second to a host that forwards it. The spec planned ICE (libjuice) with a TURN relay on the
developer's São Paulo server for the developer's CGNAT. The 2026-10-24 amendment changed that: the developer and the
friends already share an OpenVPN network, so every player reaches the host at its VPN address. LAN play, Tailscale /
ZeroTier users and the two-instance loopback test need nothing more than a plain `ip:port` either.

## Decision

- **Transport:** IPv4 UDP over Winsock 2.2 (`ws2_32`, part of Windows), in `src/net/udp_socket`: one non-blocking
  socket bound to a port, `sendTo` and a `receive` that drains every pending datagram. Hostnames go through
  `getaddrinfo` (IPv4 only). No new vcpkg dependency.
- **No threads:** the session (`src/net/session`) is polled from the main thread once per sim step, between game
  ticks; nothing runs concurrently with the game.
- **Layering:** `src/net/` includes nothing of the game or the host; the app glues it to the game. It knows
  datagrams, messages and opaque player-state payloads, never game structs.
- **Reliability on top of UDP:** the session's control messages (hello, welcome, refuse, joins, leaves, bye) are
  sequenced, acknowledged and resent; player states are unreliable, newest wins.
- **Test instrument:** `src/net/link_conditioner` delays, jitters and drops datagrams (`--net-sim`) to check the
  ghosts' smoothing at Curitiba - Lisbon conditions on one machine. The session uses it only when it is configured.
- **Datagram size:** at most 1200 bytes (`kMaxDatagram`), so a VPN tunnel's smaller MTU never fragments one.

## Consequences

- Connectivity is "the host's address must be reachable": over the VPN (the OpenVPN server must allow
  client-to-client), a LAN or loopback. The host's Windows firewall must allow inbound UDP on its port
  (`docs/netplay.md`).
- ICE / signaling / relay (plan 3) remain possible later: they would hand the session a socket whose path was found by
  ICE; the protocol and the session's messages do not change.
- Windows only, like the rest of OpenDash.
