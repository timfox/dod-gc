# Multi-Dolphin matchmaking / online test (one machine, no sudo)

Several Dolphin instances play each other on one computer: one listen-server host, N clients, plus a
local copy of the REST master (`scripts/dodgc_master.py`).

## Why not the built-in BBA

Dolphin's BuiltIn BBA is a per-instance NAT: a guest can reach the outside world but nothing can
reach a guest, so two instances never see each other. The **EthernetTapServer** BBA (EXI type 11,
`BBA_TAPSERVER_DESTINATION`) instead sends raw Ethernet frames over a unix socket. `scripts/dolphin-tap-hub.py`
is the other end: it fans frames between all connected Dolphins (one virtual switch).

## The rootless gateway (what the master needs)

Guests reach the master over HTTP, so the virtual switch needs a real IP host on it. Without sudo:
`unshare -Urn` gives a private user+net namespace where we may create a tap device. `scripts/multi-dolphin-net.sh start`:

- creates tap `gw0` = **10.0.0.1/24** in that namespace,
- runs `dodgc_master.py --bind 10.0.0.1 --port 8080 --allow-private` there (`--allow-private` because guests are 10.0.0.x),
- runs the hub with `--tap gw0`, so guest frames and the namespace's kernel stack are on one L2 segment.

Guests: static `10.0.0.2` (host), `10.0.0.3`, `10.0.0.4` ... netmask /24, gateway 10.0.0.1, unique
`BBA_MAC` (`00:09:bf:00:00:0N`). The discs get `dodgc_master http://10.0.0.1:8080` in `gamecube.cfg`.
Inspect the master with `scripts/multi-dolphin-net.sh curl /v1/servers`.

## Run

```bash
scripts/fetch-sources.sh && scripts/build-dodgc.sh --dol-only          # fresh DOL
scripts/multi-dolphin-discs.sh                                          # host + client1 + client2 ISOs, out/multi-dolphin/
scripts/multi-dolphin-net.sh start
scripts/multi-dolphin-play.sh out/multi-dolphin/{host,client1,client2}/dodgc-menu.iso
```

The play script takes the ceres-wide `/tmp/dolphin-ceres.lock` first (set `MDT_LOCK=0` elsewhere), uses a
separate `-u` dir per instance, only ever kills the PIDs it started, and logs to `$RUN/<name>.log`.
Stop with `scripts/multi-dolphin-net.sh stop`.

## Registration and listing

- The listen host (`maxclients > 1`, `dodgc_master` set) POSTs `/v1/servers` `{port,gamedir,map,players,max,name}` at start,
  on map change and every 5 min; the master records the TCP peer address (10.0.0.2) and rejects private ones unless `--allow-private`.
- Clients with `dodgc_master` run `internetservers`: GET `/v1/servers?gamedir=dod`, then `info` query to each `ip:port`;
  `localservers` does the UDP broadcast leg, which the hub carries as Ethernet broadcast.
- The test discs join with `connect 10.0.0.2:27015` from `gamecube.cfg` (no menu navigation needed); the
  cfg also runs `localservers`/`internetservers` first so the list requests appear in the logs.

See the results section appended by the test run in `out/multi-dolphin/` and PR notes.

## Results, 2026-11-18 (dod-gc 7ddc1b3 + xash3d-gc cac957c on master d23b9c58)

Works:
- Fresh DOL builds once two master build breaks are fixed (dod-gc `DodGC_TeamForPlayer` forward declaration,
  xash3d-gc#234 `gc_iso9660_small_reads`). `XASH_GAMECUBE_LINK_BBA=1` must be exported: `build-dodgc.sh` defaults it to 0
  (no libbba, "net ensure skipped"), and the network DOL needs the private toolchain in `out/network-toolchain`.
- Rootless virtual LAN + local master (`curl` from the namespace answers `/healthz`, `/v1/servers`).
- Host Dolphin on the TAP-server BBA: `net ready ip=10.0.0.2 mask=255.255.255.0 gw=10.0.0.1`, `Server IPv4 address 10.0.0.2:27015`.

Blocked (not an infrastructure problem):
- The BBA-linked listen host runs out of MEM1 heap on current master: after `net ready`, `SV_InitGame` and the game DLL
  cvar registration fail with `_Mem_Realloc: out of memory` (`heap_free` 100-200 B, `unclaimed=0`).
  Shrinking lwIP further (PBUF_POOL_SIZE 8, MEM_SIZE 32 KiB, saves 58 KiB of libbba .bss) moved the failure earlier
  (Zone alloc in console.c at pre-Con_Init), so this needs an engine-side MEM1 budget fix, not a toolchain tweak.
- Consequently no two-instance play, master registration from a guest, client listing, or gameplay media were produced.
  The hub also never showed a connected Dolphin (`clients=0`) before the host aborted; re-check once the host stays up.
