#!/usr/bin/env bash
# Package matching GameCube DoD host/client discs from one DOL.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/env.sh"
MAP=dod_flash
MODE=lan
HOST_IP=10.0.0.2
CLIENT_IP=10.0.0.3
REMOTE=""
HEADLESS=0
DUMPEYE=""
while (( $# )); do
	case "$1" in
		--map) MAP="${2:?--map needs NAME}"; shift ;;
		--online) MODE=online; REMOTE="${2:?--online needs HOST_PUBLIC_IPV4[:PORT]}"; shift ;;
		--host-ip) HOST_IP="${2:?--host-ip needs IP}"; shift ;;
		--client-ip) CLIENT_IP="${2:?--client-ip needs IP}"; shift ;;
		--dumpeye) DUMPEYE="${2:?--dumpeye needs 'x y z [pitch] [yaw]'}"; shift ;;
		--headless) HEADLESS=1 ;;
		-h|--help)
			echo "usage: scripts/build-network-pair.sh [--map dod_flash] [--host-ip IP --client-ip IP] [--online HOST_PUBLIC_IPV4[:PORT]] [--dumpeye 'x y z [pitch] [yaw]'] [--headless]"
			exit 0 ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done
[[ "$MAP" =~ ^[A-Za-z0-9_-]+$ ]] || { echo "invalid map name" >&2; exit 2; }
[[ "$HOST_IP" != "$CLIENT_IP" ]] || { echo "host and peer need distinct LAN addresses" >&2; exit 2; }
if [[ "$MODE" == online ]]; then
	python3 - "$REMOTE" <<'PY'
import ipaddress, sys
try:
    parts = sys.argv[1].split(":")
    assert len(parts) in (1, 2)
    ip = ipaddress.IPv4Address(parts[0])
    assert not ip.is_multicast and not ip.is_unspecified and not ip.is_loopback
    if len(parts) == 2: assert parts[1].isdigit() and 1 <= int(parts[1]) <= 65535
except (ValueError, AssertionError):
    sys.exit("--online requires a unicast IPv4 address and optional UDP port")
PY
fi
[[ -s "$DODGC_DOL" ]] || { echo "build DOL first: scripts/build-dodgc.sh --dol-only" >&2; exit 1; }
PAIR="$DODGC_OUT/network-$MODE"
mkdir -p "$PAIR"
# Keep map/config staging independent from other DoD and HL1 packaging jobs.
export XASH3D_GC_ISO_CACHE="${DODGC_NETWORK_CACHE:-$PAIR/staging}"
export DODGC_LEAN_DOD="$XASH3D_GC_ISO_CACHE/dod-menu-min"
export DODGC_LEAN_VALVE="$XASH3D_GC_ISO_CACHE/valve-menu-min"
mkdir -p "$XASH3D_GC_ISO_CACHE"

COMMON=(--menu-only --full-map --world-render --net --skip-startup-vids)
HOST_ARGS=(--listen "$MAP")
CLIENT_ARGS=(--map "$MAP")
if [[ "$MODE" == lan ]]; then
	HOST_ARGS+=(--net-ip "$HOST_IP")
	CLIENT_ARGS+=(--net-ip "$CLIENT_IP" --connect "$HOST_IP:27015")
else
	# Obtain the real local IP/mask/router with DHCP on each separate LAN.
	CLIENT_ARGS+=(--connect "$REMOTE")
fi
(( HEADLESS )) && HOST_ARGS+=(--headless)
[[ -n "$DUMPEYE" ]] && { HOST_ARGS+=(--dumpeye "$DUMPEYE"); CLIENT_ARGS+=(--dumpeye "$DUMPEYE"); }
DODGC_OUT="$PAIR/host" DODGC_ISO="$PAIR/host/dodgc.iso" bash "$ROOT/scripts/build-lean-iso.sh" "${COMMON[@]}" "${HOST_ARGS[@]}"
DODGC_OUT="$PAIR/client" DODGC_ISO="$PAIR/client/dodgc.iso" bash "$ROOT/scripts/build-lean-iso.sh" "${COMMON[@]}" "${CLIENT_ARGS[@]}"
cp "$DODGC_DOL" "$PAIR/dodgc.dol"
( cd "$PAIR" && sha256sum dodgc.dol host/dodgc-menu.iso client/dodgc-menu.iso ) > "$PAIR/SHA256SUMS"
printf 'mode=%s\nmap=%s\nheadless=%s\nconnect=%s\n' "$MODE" "$MAP" "$HEADLESS" "${REMOTE:-$HOST_IP:27015}" > "$PAIR/session.txt"
echo "Matched $MODE pair ready in $PAIR (host/client/dodgc-menu.iso)"
