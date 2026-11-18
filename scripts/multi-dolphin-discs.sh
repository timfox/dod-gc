#!/usr/bin/env bash
# Build host + client1 + client2 lean discs for the multi-Dolphin test (needs out/dodgc.dol).
#   MAP=dod_flash scripts/multi-dolphin-discs.sh
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
MAP="${MAP:-dod_flash}"; PAIR="$DODGC_OUT/multi-dolphin"; mkdir -p "$PAIR"
export XASH3D_GC_ISO_CACHE="$PAIR/staging" DODGC_LEAN_DOD="$PAIR/staging/dod-menu-min" DODGC_LEAN_VALVE="$PAIR/staging/valve-menu-min"
mkdir -p "$XASH3D_GC_ISO_CACHE"
COMMON=(--menu-only --full-map --world-render --net --skip-startup-vids)
M='dodgc_master http://10.0.0.1:8080'
DODGC_EXTRA_CFG="$M" DODGC_OUT=$PAIR/host DODGC_ISO=$PAIR/host/dodgc.iso bash scripts/build-lean-iso.sh "${COMMON[@]}" --listen "$MAP" --net-ip 10.0.0.2 --net-gateway 10.0.0.1
for n in 1 2; do
	DODGC_EXTRA_CFG="$M\nlocalservers\ninternetservers" DODGC_OUT=$PAIR/client$n DODGC_ISO=$PAIR/client$n/dodgc.iso \
		bash scripts/build-lean-iso.sh "${COMMON[@]}" --map "$MAP" --net-ip 10.0.0.$((n+2)) --net-gateway 10.0.0.1 --connect 10.0.0.2:27015
done
