# GameCube DoD networking

This port targets matching **GameCube DoD builds**, with two players per game.
The connection handshake carries `gc_dod=1` in both directions. A missing or
incompatible marker rejects the connection before loading the game. This marker
checks software compatibility; it does not authenticate physical hardware.
Desktop Steam DoD servers use a different game DLL/client and are outside this
network version. Dolphin remains a development test target.

## Native ETH2SP1 build

ETH2SP1 uses a W5500 controller in **Serial Port 1**. The build uses libogc2's
native W5500 driver, alongside the retail BBA and ENC28J60 drivers. Swiss's SDK
BBA emulation does not replace native drivers for homebrew. The retail Nintendo
BBA also occupies SP1, not Memory Card Slot B. Dolphin's EthernetIPC/BuiltIn
adapters emulate the retail BBA and cannot validate a physical W5500.

Sources: [libogc2 interface selection](https://github.com/extremscorner/libogc2/blob/30134d2f98b1c0d65023089b8cd4ac9de3cbe2c7/lwip/network.c),
[Swiss supported devices](https://github.com/emukidid/swiss-gc),
[Nintendo BBA manual](https://csassets.nintendo.com/noaext/image/private/t_KA_PDF/broadband_english).

```bash
# Creates a private toolchain in out/network-toolchain; leaves /opt unchanged.
scripts/setup-network-toolchain.sh
scripts/build-dodgc.sh --dol-only
```

The setup pins libogc2 and libfat revisions and records compiler/archive hashes
in `out/network-toolchain/versions.txt`. Normal DoD builds require libogc2 and
link networking by default. Explicit `XASH_GAMECUBE_OGC_STACK=libogc` retains the
older retail-BBA build path; it cannot drive ETH2SP1.

The engine adapts libogc2's four-argument `if_config`, native socket-address
layout, `getsockname`, and negative error codes. Receive calls stay nonblocking.
A failed interface initialization stays failed for that boot: libogc2 retains
internal initialization state after some failures, so retrying could otherwise
report false success. Fix the cable/configuration and reboot to retry.

The native build budgets MEM1 for two players: 460 server/client entities,
a 256 KiB kernel workspace, a 128 KiB filesystem arena, a 256 KiB GX command FIFO, and one 320-pixel-wide
scanout framebuffer scaled by VI using the selected television timing. Private
copies of the SDK archives omit unused asynchronous unwind tables; the SDK is
built with exceptions disabled. Maps exceeding the entity or memory budget need
separate optimization and validation. Console/kernel heap telemetry accompanies
network readiness and gameplay activation.

## Two GameCubes on a LAN

```bash
scripts/build-network-pair.sh --map dod_flash \
  --host-ip 10.0.0.2 --client-ip 10.0.0.3
```

The outputs are under `out/network-lan/`:

- `dodgc.dol`: the same executable used in both images.
- `host/dodgc-menu.iso`: local player plus one peer.
- `client/dodgc-menu.iso`: connects to the host and stages the same map.
- `SHA256SUMS`: verify with `cd out/network-lan && sha256sum -c SHA256SUMS`.

Connect both ETH2SP1 adapters to the same switch/router and load the role's image
through Swiss. Choose unused addresses on your actual LAN. The defaults are an
isolated `10.0.0.0/24` test network, not an automatic home-router configuration.
The host now loads a local player by default. `--headless` explicitly builds a
server-only host for an optional third Cube.

For DHCP on a normal LAN, build the roles separately:

```bash
scripts/build-lean-iso.sh --menu-only --listen dod_flash --net \
  --full-map --world-render --skip-startup-vids
# Preserve out/dodgc-menu.iso as the host image before packaging the peer.
scripts/build-lean-iso.sh --menu-only --connect HOST_LAN_IPV4:27015 --net \
  --map dod_flash --full-map --world-render --skip-startup-vids
```

Use the host's DHCP address shown in its network log, or reserve an address on
its router. Direct connection currently accepts numeric IPv4 addresses.

## Internet play between GameCubes

```bash
scripts/build-network-pair.sh --map dod_flash --online HOST_PUBLIC_IPV4:27015
```

The matched images are placed in `out/network-online/`. Both Cubes use DHCP to
obtain their own local IP, subnet mask and gateway. Replace `HOST_PUBLIC_IPV4`
with the host connection's actual public IPv4 address before building the peer.
Forward **UDP 27015** on the host router to the host Cube's reserved DHCP address.
The peer connects outbound and ordinarily needs no forwarding. Test from a
separate internet connection; connecting through the public address from the
same LAN depends on router hairpin support.

This is direct IPv4 play. There is no relay, matchmaking service, automatic
port forwarding or CGNAT traversal. A host behind CGNAT needs a reachable network
or separately configured routing. The pair generator supports an explicit
external port if the router translates it to the host's UDP 27015.

## Boot configuration

`gamecube.cfg` is read before networking starts, from the selected FAT game tree
or the image. Keep host and peer game/map assets identical.

```text
# Host, DHCP:
video prog
net
listenserver dod_flash
gcworldrender

# Peer, static address on a home LAN:
video prog
netstatic 192.168.1.43 255.255.255.0 192.168.1.1
connect 192.168.1.42:27015
map dod_flash
gcworldrender
```

`netstatic IP` retains the legacy `/24` lab defaults when written manually;
prefer the full `IP MASK GATEWAY` form. Packaging with `--net-ip` derives `.1`
from the selected /24 address; `--net-mask` and `--net-gateway` override it.
Addresses, contiguous masks and gateway/subnet relationships are checked.
`-nonet` / `-noip` suppress interface initialization. `headless` in the boot
configuration requests a server-only Cube. Player routes select real PAD input
instead of the map probe's synthetic input.

## Verification and remaining hardware gate

```bash
python3 -m unittest discover -s tests -v
scripts/probe-network.py out/network-lan/host/dodgc-menu.iso \
  out/network-lan/client/dodgc-menu.iso
```

The regression launches fresh Dolphin profiles on SP1 EthernetIPC (EXI 14),
requires **both** host-local and peer `GameCube DoD: gameplay active` markers,
exercises both controller pipes and holds the session before returning success.
Each run preserves logs, profiles and image hashes under `out/network-probe-*`.
It terminates only its own child processes. An interface or challenge response
alone does not pass this test.

Historical `20261107-092212-dual-ipc-join` evidence used a headless host and an
older classic-libogc build. It is not evidence for two local players or ETH2SP1.

Physical ETH2SP1 and separate-network internet play require on-device evidence.
On each Cube, record the local address, completed signon, movement and firing
against the other player, and sustained play without disconnects. When writable
FAT storage is available, network/gameplay breadcrumbs are appended to
`xash3d/dod/logs/swiss-evidence.txt`. Dolphin cannot prove W5500 detection,
physical link behavior, Swiss storage coexistence or internet routing.
