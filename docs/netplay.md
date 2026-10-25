# Netplay (a shared world over the network)

OpenDash players in the same room see each other's Mega Man as a **ghost**: the real model, look, animation and
movement, smoothed over the network. And they play in **one world, the host's**:

- **The host's world is the only one that simulates.** Every actor of the field happens in the host's game: the
  enemies, their shots, their deaths and their drops, the NPCs walking their routes, the critters. A client shows the
  host's (its own copies of them are removed) and sends the host its hits on them. The host applies each hit through the game's own hit code, so the enemy flinches, loses health and
  dies the same way on every screen.
- **Everyone travels together.** When the host changes room (a door, the F1 *Cenarios* tab, PgUp/PgDn, `[` `]`), every
  client loads the same room and is placed beside the host. Only the host can change the room: a client's *Cenarios*
  tab is locked ("só o host muda o cenário"), and its keys do nothing. A client that starts or loads a game after
  joining (the title's new game or continue) is sent to the host's room as soon as it is in play.
- **The host's story.** The rooms load as in the host's game: its story progress and event flags pick the stage files,
  the room's actors and its scripts, whatever the client's own save says. The client's own story is kept aside: its
  saves write its own progress, and it comes back when the host leaves.
- **Drops: each player gets their own copy.** The host rolls the drop, and every player sees it in the same place.
  Picking it up is local, so nobody takes it from anyone else. Chests work the same way: everyone opens their own.
- **Breakable walls are shared.** A client's hit on a wall goes to the host, and the wall breaks on every screen.
- **Cutscenes.** When a story cutscene starts on the host, it starts on every client too. Each player watches or skips
  it at their own pace, and the host's world waits ("aguardando <nomes>") until everyone has finished, for 60 s at
  most. Talking to NPCs and shops stays local: while a client talks to
  an NPC, that NPC stops and faces it on the client's screen (on the host's it keeps going), then follows the host's
  again. On a client, the host's actors cannot be lifted.
- **Friendly fire** (F1 → Netplay → Combate, the host's setting): with *fogo amigo* on, the players' shots hit each
  other, with knockback, for *dano entre jogadores %* (25-100 %) of their damage. With it off, shots go through the
  other players. A client sees the host's setting, greyed out ("definido pelo host").
- **Dying** is not a game over in netplay: a downed player gets up again 3 s later at the room's arrival point, with
  full health.
- **If the host leaves**, every client goes back to its own world: the room loads again with its own enemies, and the
  player keeps their health, items and position. F1 → Netplay shows "host saiu".

Each player keeps their own items and save (the story shown is the host's, the one saved is the player's). The actors are shared only in the host's room; a cutscene's own cast
stays each player's.

One player **hosts** and the others **join** the host's `ip:port` over UDP. Up to 8 players: the host and 7 others.
Everyone talks only to the host, and the host forwards each player's state to the others.

Transport: `docs/adr/0005-netplay-transport.md`.

## What every player needs

- **The same OpenDash build.** The host refuses a different build and names both. The build id is a hash of the
  sources of the game state, the content scripts, the data loading and the wire (`src/game`, `src/content`,
  `src/engine`, `src/net`, `src/app/netplay*`; `cmake/build_id.cmake`), so anyone who builds the same sources gets
  the same id, and builds that differ only in rendering, upscalers or UI play together.
- **The same game files.** That means `dash2.exe` and the whole `.DAT` set. The host refuses different files and says
  whether the exe or the `.DAT` set differs. Language packs (`opendash/lang`) do not count.
- **The same session key** (`net_key=`). Only its hash travels. The host refuses a different key.
- **A path to the host.** For the Lisbon friends this is the developer's OpenVPN network (below).

## Settings

Put these in `opendash.ini` next to where OpenDash runs. Command-line flags override them.

`--config <file.ini>` reads another ini instead of `opendash.ini`, and its `log=<file>` names the log file (relative to
the ini's folder; default `opendash.log` next to the executable). Two copies on one PC, from the same build:

```ini
# host.ini                      # client.ini
net=host                        net=join
net_name=Host                   net_join=127.0.0.1:27460
log=host.log                    net_name=Client
                                log=client.log
```

`opendash --config host.ini`, then `opendash --config client.ini`. Copy your `opendash.ini`'s lines (`dat_dir=`,
`exe=`, `display=` ...) into both if you set any. Settings changed in the app are saved to the ini given.

| `opendash.ini` | flag | meaning |
|---|---|---|
| `net=off\|host\|join` | `--host [port]` / `--join ip:port` | off (default), host a session, or join one |
| `net_port=27460` | `--host <port>` | the UDP port the host listens on (default 27460). Clients use any free port. |
| `net_join=10.8.0.1:27460` | `--join 10.8.0.1:27460` | the host's address and port (only for `net=join`) |
| `net_name=Mateus` | `--net-name Mateus` | your name, shown to the others (default: your Windows user name) |
| `net_key=...` | `--net-key ...` | the session key; it must match the host's |
| `net_friendly_fire=0\|1` | (F1 → Netplay) | the host's friendly fire (default 0: off) |
| `net_ff_scale=100` | (F1 → Netplay) | the % of a player's damage on another player, 25-100 (default 100) |

Host example:

```ini
net=host
net_port=27460
net_name=Mateus
net_key=our-secret
```

Friend example (the host's VPN address is `10.8.0.1`):

```ini
net=join
net_join=10.8.0.1:27460
net_name=Rui
net_key=our-secret
```

When netplay is on, OpenDash hashes the game files at startup: under a second when they are in the disk cache, a few seconds
the first time.

## The friends test over OpenVPN

1. **OpenVPN server: allow client-to-client traffic.** Add `client-to-client` to the server's config
   (e.g. `/etc/openvpn/server/server.conf`) and restart the service (`sudo systemctl restart openvpn-server@server`).
   Without it, VPN clients can reach the server but not each other.
   If the server also filters forwarded traffic (iptables / ufw), allow UDP between the VPN clients on the tunnel
   interface.
2. **Everyone connects to the VPN.** The host finds their VPN address: `ipconfig` shows it under the TAP / OpenVPN
   adapter (e.g. `10.8.0.6`). That address goes in the friends' `net_join=`.
3. **Host: open the port in the Windows firewall.** In an administrator prompt:
   `netsh advfirewall firewall add rule name="OpenDash netplay" dir=in action=allow protocol=UDP localport=27460`.
   Windows may also ask the first time OpenDash listens. Allow it for private networks, or for public networks
   too if the VPN adapter is classed as public. Joining players need no firewall rule (their traffic is an answer to
   what they send).
4. **Same build, same files, same key:** see above.
5. **Start:** the host starts OpenDash with `net=host`, then the friends start theirs with `net=join`. Load a game and
   go to the same room (e.g. ST08 sub 2). Each sees the others.
6. **Check:** press **F1 → Netplay**. It shows your role and port, then per player: id, name, round trip (RTT),
   loss of player states, time since last heard, and ghost slot. Under "fantasmas" it says why each ghost is or is
   not shown.

## Why a ghost is not shown (F1 → Netplay)

| text | meaning |
|---|---|
| `not in field play` | you or they are in a menu, a cutscene, a transition or the title |
| `other room` | different stage / sub-room |
| `loaded data differs ...` | same room, but the story state loaded other stage files or script model banks (e.g. ST53 before / after its flag) |
| `not drawn (draw flags ...)` | their game hides their Mega Man right now (as it would on their screen) |
| `character not supported ...` | a character other than Mega Man (not in this milestone) |
| `player file missing` / `look not built: ...` | a body / weapon file of their look could not be read (should not happen with matching game files) |

A ghost silent for 3 seconds disappears. A player silent for 5 seconds is dropped by the host. A client whose host
goes silent keeps trying to join again.

## Refusals

The joining player's F1 → Netplay shows the host's reason: `protocol version differs`, `OpenDash build differs` (both
builds named), `game files differ` (dash2.exe or the .DAT files), `session key differs`, `session full`.

## Test instruments

- `--net-sim <latency ms> <jitter ms> <loss %>` delays, jitters and drops this end's datagrams. Each end takes half
  the latency and half the loss. With `--net-sim 125 20 3` on both ends: about 250 ms round trip and 3 % loss each
  way, like Curitiba to Lisbon.
- `od_net_echo` (built with OpenDash) is a peer without the game. It hosts or joins like OpenDash and sends back every
  state it receives, moved sideways, so one OpenDash instance sees its own Mega Man as a ghost that crossed the
  network:
  `od_net_echo --host 27461 --dat-dir <dat> --exe <dash2.exe> --net-sim 125 20 3`, then
  `opendash --join 127.0.0.1:27461 --net-sim 125 20 3 --stage 08 --play 1`.
- `--ghost-mirror <delay ticks> <dx> <dz>` (and `--ghost-look <costume> <variant> <weapon>`) shows your own Mega Man
  as a ghost through the same smoothing, without a network.
- `tests/test_net_protocol.cpp`, `tests/test_ghost_buffer.cpp`, `tests/test_ghost_view.cpp` cover the protocol, the
  session, the smoothing and the ghost view.
