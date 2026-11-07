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
  --listen MAP     solo local listen server (gamecube.cfg listenserver MAP)
  --connect ADDR   peer client (gamecube.cfg connect ADDR); use with --net
  --net-ip IP      optional static guest IP (gamecube.cfg netstatic IP)
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
MAP=""
SMOKE=""
LISTEN=""
CONNECT=""
NET_IP=""
STAGE_ARGS=()

while [[ $# -gt 0 ]]; do
	case "$1" in
		--menu-only) INTRO=0; MENU=1 ;;
		--intro-only) INTRO=1; MENU=0 ;;
		--skip-startup-vids) SKIP_STARTUP_VIDS=1 ;;
		--net) WANT_NET=1 ;;
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
	fi
	if [[ -n "$LISTEN" ]]; then
		{
			echo "video prog"
			echo "listenserver $LISTEN"
			# BBA/eth2sp1 / TapServer static bring-up for joinable host probes.
			if (( WANT_NET )); then
				if [[ -n "$NET_IP" ]]; then
					echo "netstatic $NET_IP"
				else
					echo "netstatic"
				fi
			fi
		} > "$LEAN/gamecube.cfg"
		echo "==> listenserver cfg: $LISTEN net=$([[ $WANT_NET -eq 1 ]] && echo netstatic${NET_IP:+ $NET_IP} || echo off)"
	elif [[ -n "$CONNECT" ]]; then
		{
			echo "video prog"
			if [[ -n "$NET_IP" ]]; then
				echo "netstatic $NET_IP"
			else
				echo "netstatic"
			fi
			echo "connect $CONNECT"
		} > "$LEAN/gamecube.cfg"
		echo "==> connect cfg: $CONNECT net=netstatic${NET_IP:+ $NET_IP}"
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