#!/usr/bin/env bash
# Package MEM1-friendly lean DoD discs into out/ (does not touch HL1 ISOs).
# Uses stage-lean-dod.sh + existing out/dodgc.dol.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

usage() {
	cat <<'EOF'
usage: scripts/build-lean-iso.sh [options]

  --menu-only      only out/dodgc-menu.iso (Valve intro + DoD menu)
  --intro-only     only out/dodgc.iso (same lean tree; larger full pack path)
  --skip-startup-vids
                   omit Valve intro AVI/gcvid (faster menu-only smoke)
  --net            stage gamecube.cfg "net" for early BBA/eth2sp1 DHCP (-net)
  --map NAME       stage one BSP into lean maps/ (DoD or valve)
  --smoke MAP      disc --smoke-map MAP (auto map load; implies staging MAP)
  --listen MAP     playable listen host (local player plus one peer)
  --headless       server only; requires --listen (optional third Cube)
  --connect ADDR   peer client (gamecube.cfg connect ADDR); use with --net
  --net-ip IP      static IPv4 address (DHCP when omitted)
  --net-mask MASK  static subnet mask (default 255.255.255.0)
  --net-gateway IP static router address (default same subnet .1)
  --full-map       stage mapmodels/weapons/sprites/WADs + core SFX with --map/--smoke/--listen
  --all-maps-audio stage every map's soundscape/ambient WAVs + menu music/UI
  --core-sfx       stage weapons/player/ui/items/doors/buttons/debris/object/common
  --world-render   gamecube.cfg gcworldrender (Flipper world; smoke/listen/connect)
  --dumpframes     gamecube.cfg dumpframes (-gcdumpframes stills; with --smoke)
  --dumpeye "x y z [pitch] [yaw]"
                   CapFaces camera override in gamecube.cfg (spawn atlas)
  --tas NAME       stage dod/tas/NAME.tas + gamecube.cfg tas NAME (pad replay)
  --skip-stage     reuse existing lean cache trees
  -h, --help

Requires out/dodgc.dol (scripts/build-intro-dod-menu.sh or build-dodgc.sh --dol-only).
EOF
}

MENU=1
INTRO=1
SKIP_STAGE=0
SKIP_STARTUP_VIDS=0
WANT_NET=0
FULL_MAP=0
WORLD_RENDER=0
DUMPFRAMES=0
DUMPEYE=""
TAS=""
MAP=""
SMOKE=""
LISTEN=""
CONNECT=""
NET_IP=""
NET_MASK="255.255.255.0"
NET_GW=""
HEADLESS=0
STAGE_ARGS=()

while [[ $# -gt 0 ]]; do
	case "$1" in
		--menu-only) INTRO=0; MENU=1 ;;
		--intro-only) INTRO=1; MENU=0 ;;
		--skip-startup-vids) SKIP_STARTUP_VIDS=1 ;;
		--net) WANT_NET=1 ;;
		--headless) HEADLESS=1 ;;
		--net-mask) NET_MASK="${2:?--net-mask needs MASK}"; shift ;;
		--net-gateway) NET_GW="${2:?--net-gateway needs IP}"; shift ;;
		--full-map) FULL_MAP=1; STAGE_ARGS+=(--full-map) ;;
		--all-maps-audio) STAGE_ARGS+=(--all-maps-audio) ;;
		--core-sfx) STAGE_ARGS+=(--core-sfx) ;;
		--world-render) WORLD_RENDER=1 ;;
		--dumpframes) DUMPFRAMES=1 ;;
		--dumpeye)
			DUMPEYE="${2:-}"
			[[ -n "$DUMPEYE" ]] || { echo "error: --dumpeye needs coords" >&2; exit 2; }
			shift
			;;
		--tas)
			TAS="${2:-}"
			[[ -n "$TAS" ]] || { echo "error: --tas needs NAME" >&2; exit 2; }
			if [[ "$TAS" == */* || "$TAS" == *\\* || "$TAS" == *.* ]]; then
				echo "error: --tas NAME must be a simple token" >&2
				exit 2
			fi
			shift
			;;
		--net-ip)
			NET_IP="${2:-}"
			[[ -n "$NET_IP" ]] || { echo "error: --net-ip needs IP" >&2; exit 2; }
			shift
			;;
		--map) MAP="${2:-}"; shift; STAGE_ARGS+=(--map "$MAP") ;;
		--smoke)
			SMOKE="${2:-}"
			[[ -n "$SMOKE" ]] || { echo "error: --smoke needs a map" >&2; exit 2; }
			MAP="$SMOKE"
			STAGE_ARGS+=(--map "$SMOKE")
			shift
			;;
		--listen)
			LISTEN="${2:-}"
			[[ -n "$LISTEN" ]] || { echo "error: --listen needs a map" >&2; exit 2; }
			MAP="$LISTEN"
			STAGE_ARGS+=(--map "$LISTEN")
			shift
			;;
		--connect)
			CONNECT="${2:-}"
			[[ -n "$CONNECT" ]] || { echo "error: --connect needs ADDR" >&2; exit 2; }
			shift
			;;
		--skip-stage) SKIP_STAGE=1 ;;
		-h|--help) usage; exit 0 ;;
		*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done
if (( DUMPFRAMES )) && [[ -z "$SMOKE" && -z "$LISTEN" && -z "$CONNECT" ]]; then
	echo "error: --dumpframes requires --smoke, --listen, or --connect" >&2
	exit 2
fi
if (( WORLD_RENDER )) && [[ -z "$SMOKE" && -z "$LISTEN" && -z "$CONNECT" ]]; then
	echo "error: --world-render needs --smoke, --listen, or --connect" >&2
	exit 2
fi
if [[ -n "$SMOKE" && -n "$LISTEN" ]]; then
	echo "error: --smoke and --listen are mutually exclusive" >&2
	exit 2
fi
if [[ -n "$LISTEN" && -n "$CONNECT" ]]; then
	echo "error: --listen and --connect are mutually exclusive" >&2
	exit 2
fi
if [[ -n "$CONNECT" && $WANT_NET -eq 0 ]]; then
	echo "error: --connect requires --net" >&2
	exit 2
fi
if [[ -n "$NET_IP" && $WANT_NET -eq 0 ]]; then
	WANT_NET=1
fi

if (( HEADLESS )) && [[ -z "$LISTEN" ]]; then
	echo "error: --headless requires --listen" >&2; exit 2
fi
# Validate before staging or invoking disc tooling. Derive a conventional router
# only for explicit /24 addresses; other subnets must supply their gateway.
if [[ -n "$NET_IP" ]]; then
	if [[ -z "$NET_GW" ]]; then
		[[ "$NET_MASK" == 255.255.255.0 ]] || { echo "error: supply --net-gateway with a non-/24 mask" >&2; exit 2; }
		NET_GW="${NET_IP%.*}.1"
	fi
	python3 - "$NET_IP" "$NET_MASK" "$NET_GW" <<'PYNET'
import ipaddress, sys
try:
    ip, mask, gateway = sys.argv[1:]
    address = ipaddress.IPv4Address(ip)
    network = ipaddress.IPv4Network(f"{ip}/{mask}", strict=False)
    router = ipaddress.IPv4Address(gateway)
    assert network.prefixlen in range(1, 31)
    assert address not in (network.network_address, network.broadcast_address)
    assert not address.is_multicast and not address.is_loopback and int(address) >> 24 != 0
    assert int(router) != 0 and router in network and router not in (network.network_address, network.broadcast_address, address)
except (ValueError, AssertionError):
    sys.exit("error: invalid static IPv4 address, mask or gateway")
PYNET
elif [[ -n "$NET_GW" || "$NET_MASK" != 255.255.255.0 ]]; then
	echo "error: --net-mask/--net-gateway require --net-ip" >&2; exit 2
fi

write_net_config() {
	if [[ -n "$NET_IP" ]]; then
		printf 'netstatic %s %s %s\n' "$NET_IP" "$NET_MASK" "$NET_GW"
	else
		echo net
	fi
}

dodgc_require_content
[[ -f "$DODGC_DOL" ]] || {
	echo "error: missing $DODGC_DOL — build DOL first" >&2
	exit 1
}

if (( ! SKIP_STAGE )); then
	bash "$SCRIPT_DIR/stage-lean-dod.sh" "${STAGE_ARGS[@]+"${STAGE_ARGS[@]}"}"
fi

LEAN="${DODGC_LEAN_DOD:-$XASH3D_GC_ISO_CACHE/dod-menu-min}"
VMIN="${DODGC_LEAN_VALVE:-$XASH3D_GC_ISO_CACHE/valve-menu-min}"
[[ -d "$LEAN" ]] || { echo "error: lean DoD tree missing: $LEAN" >&2; exit 1; }
[[ -d "$VMIN" ]] || { echo "error: lean valve tree missing: $VMIN" >&2; exit 1; }

pack() {
	local mode=$1
	local out=$2
	local tmp="$XASH3D_GC_ISO_CACHE/dodgc-lean-${mode}-$$.iso"
	local args=(
		--dol "$DODGC_DOL"
		--output "$tmp"
		--data "$LEAN"
		--gamedir dod
		--extra-data "valve=$VMIN"
		--no-auto-extra-gamedirs
	)
	# Solo listen / peer-connect boots own gamecube.cfg; otherwise use play defaults.
	if [[ -z "$LISTEN" && -z "$CONNECT" ]]; then
		args+=(--play)
	fi
	# Keep Valve intro (valve.avi → .gcvid) on lean discs unless explicitly skipped.
	(( SKIP_STARTUP_VIDS )) && args+=(--skip-startup-vids)
	if [[ -n "$SMOKE" ]]; then
		args+=(--smoke-map "$SMOKE")
		(( WORLD_RENDER )) && args+=(--world-render)
		(( DUMPFRAMES )) && args+=(--probe-dumpframes)
	fi
	if [[ -n "$LISTEN" ]]; then
		if [[ -n "$TAS" ]]; then
			tas_src="$DODGC_ENGINE/scripts/tas/${TAS}.tas"
			[[ -f "$tas_src" ]] || { echo "error: missing TAS script $tas_src" >&2; exit 2; }
			mkdir -p "$LEAN/tas"
			cp -f "$tas_src" "$LEAN/tas/${TAS}.tas"
		fi
		{
			echo "video prog"
			echo "listenserver $LISTEN"
			(( WANT_NET )) && write_net_config
			(( HEADLESS )) && echo headless
			# Flipper world tris (otherwise R_EdgeDrawing skips as gcmap smoke).
			(( WORLD_RENDER )) && echo "gcworldrender"
			# CapFaces GX display lists cost 128 KiB BSS tip — charlie tip-OOM'd
			# after MAP_READY with gcworlddl (heap_free=104). Skip for lean listen;
			# PVS/frustum CapFaces path does not need a static DL.
			# Solo listen needs pad → engfuncs; without this CapFaces is view-only.
			echo "gcplayerinput"
			(( DUMPFRAMES )) && echo "dumpframes"
			[[ -n "$DUMPEYE" ]] && echo "dumpeye $DUMPEYE"
			[[ -n "$TAS" ]] && echo "tas $TAS"
		} > "$LEAN/gamecube.cfg"
		echo "==> listenserver cfg: $LISTEN net=$([[ $WANT_NET -eq 1 ]] && echo "${NET_IP:-dhcp}" || echo off) world_render=$WORLD_RENDER dumpframes=$DUMPFRAMES tas=${TAS:-off}"
	elif [[ -n "$CONNECT" ]]; then
		{
			echo "video prog"
			write_net_config
			echo "connect $CONNECT"
			# Peer BSP tip path needs -gcmap (high-slot surfaces); without it
			# Mod_LoadWorld OOMs on planes/surfaces (dual IPC 20261107-005100).
			if [[ -n "$MAP" ]]; then
				echo "map $MAP"
			fi
			(( WORLD_RENDER )) && echo "gcworldrender"
			(( DUMPFRAMES )) && echo "dumpframes"
			# Keep the peer CapFaces capture aligned with the listen host's
			# explicitly selected spawn/atlas eye.
			[[ -n "$DUMPEYE" ]] && echo "dumpeye $DUMPEYE"
		} > "$LEAN/gamecube.cfg"
		echo "==> connect cfg: $CONNECT net=${NET_IP:-dhcp} map=${MAP:-} world_render=$WORLD_RENDER dumpframes=$DUMPFRAMES"
	fi
	echo "==> packaging $out ($mode) startup_vids=$([[ $SKIP_STARTUP_VIDS -eq 0 ]] && echo on || echo off) net=$([[ $WANT_NET -eq 1 ]] && echo on || echo off) listen=${LISTEN:-} connect=${CONNECT:-} net_ip=${NET_IP:-}"
	if ! TMPDIR="$XASH3D_GC_ISO_CACHE" \
		XASH_GC_PROBE_NET="$([[ $WANT_NET -eq 1 ]] && echo 1 || echo 0)" \
		python3 "$DODGC_ENGINE/scripts/build-gamecube-disc.py" "${args[@]}"
	then
		rm -f "$tmp"
		echo "error: disc build failed for $mode" >&2
		exit 1
	fi
	[[ -f "$tmp" ]] || { echo "error: missing temp ISO $tmp" >&2; exit 1; }
	# CapFaces pack: build-gamecube-disc.py fails smoke staging if cf_*.gcp is
	# missing; hybrid GC ISOs are not reliably listed by 7z, so trust the disc build.
	if (( WORLD_RENDER )) && [[ -n "${SMOKE:-$LISTEN}" ]]; then
		local stem
		stem="$(echo "${SMOKE:-$LISTEN}" | tr '[:upper:]' '[:lower:]')"
		echo "==> CapFaces pack required for ${stem} (enforced by disc build)"
	fi
	local pub="$out.tmp.$$"
	cp --reflink=auto "$tmp" "$pub"
	mv -f "$pub" "$out"
	rm -f "$tmp"
	ls -lh "$out"
}

(( INTRO )) && pack intro "$DODGC_ISO"
(( MENU )) && pack menu "$DODGC_OUT/dodgc-menu.iso"

{
	echo "dod-gc lean disc"
	echo "built_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "iso=$DODGC_ISO"
	echo "menu_iso=$DODGC_OUT/dodgc-menu.iso"
	echo "dol=$DODGC_DOL"
	echo "lean=$LEAN"
	echo "valve_min=$VMIN"
	echo "smoke=${SMOKE:-}"
	echo "listen=${LISTEN:-}"
	echo "connect=${CONNECT:-}"
	echo "map=${MAP:-}"
	echo "engine_commit=$(dodgc_engine_sha)"
	echo "dodgc_commit=$(dodgc_repo_sha)"
} | tee "$DODGC_OUT/dodgc-iso-handoff.txt"

echo "==> lean ISOs ready"
echo "    probe: scripts/probe-playtest.sh --route intro-menu out/dodgc.iso"
