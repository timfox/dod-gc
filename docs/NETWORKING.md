# Day of Defeat GameCube — broadband / online

DoD online play rides the xash3d-gc GameCube net stack (same DOL profile `dod`).

## Hardware

- Official **Broadband Adapter**, or
- **ETH2SP1** (SP1 W5500/ENC28J60) via **Swiss** BBA emulation (v0.6r2018+), or
- Dolphin BBA (TAP / XLink Kai) for LAN testing

## Usage

```text
# Early DHCP (optional)
-net

# Skip bring-up
-nonet

# Typical listen server after menu
maxplayers 8
map <map>
# Peer:
connect <ip>
```

OSReport markers: `net bring-up`, `net ready ip=…`, or soft-fail warning.

## Status

Engine glue lives in `engine/platform/gamecube/net_gamecube.*` (symlink via this
repo’s `engine/`). Lean menu discs still focus on SP bring-up; online maps need
fuller asset staging than the lean menu ISO.
