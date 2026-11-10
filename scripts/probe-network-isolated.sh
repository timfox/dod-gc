#!/usr/bin/env bash
# Build/package/probe DoD networking without touching the shared engine tree.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="${DODGC_NETWORK_ENGINE:-$ROOT/out/network-engine}"
OUT="${DODGC_NETWORK_OUT:-$ROOT/out/network-isolated}"
TOOLCHAIN="${DODGC_NETWORK_DEVKITPRO:-$ROOT/out/network-toolchain/devkitpro}"
REBUILD=0
RUN_PROBE=1
TIMEOUT=90
PAIR_ARGS=()

while (( $# )); do
	case "$1" in
		--rebuild) REBUILD=1 ;;
		--no-probe) RUN_PROBE=0 ;;
		--timeout) TIMEOUT="${2:?--timeout needs seconds}"; shift ;;
		-h|--help)
			echo "usage: scripts/probe-network-isolated.sh [--rebuild] [--no-probe] [--timeout SECONDS] [build-network-pair options]"
			exit 0 ;;
		--online)
			echo "--online is not supported by the isolated probe; this workflow is GameCube-only LAN" >&2
			exit 2 ;;
		*) PAIR_ARGS+=("$1") ;;
	esac
	shift
done

[[ -x "$ENGINE/scripts/build-gamecube.sh" ]] || { echo "missing isolated engine: $ENGINE" >&2; exit 1; }
[[ -d "$TOOLCHAIN/libogc2/gamecube/include" && -d "$TOOLCHAIN/libogc2/gamecube/lib" ]] \
	|| { echo "missing isolated libogc2 toolchain: $TOOLCHAIN" >&2; exit 1; }
[[ "$TIMEOUT" =~ ^[1-9][0-9]*$ ]] || { echo "--timeout must be positive seconds" >&2; exit 2; }

if (( REBUILD )); then
	mkdir -p "$ENGINE/.ai"
	flock "$ENGINE/.ai/dodgc-network-build.lock" \
		env DEVKITPRO="$TOOLCHAIN" XASH_GAMECUBE_OGC_STACK=libogc2 \
		XASH_GAMECUBE_LINK_BBA=1 HLSDK_GAMECUBE_GAMEDIR=dod \
		bash -c 'cd "$1" && bash scripts/build-gamecube.sh --game-profile=dod' _ "$ENGINE"
fi

DOL="$ENGINE/OUT/bin/dodgc.dol"
[[ -s "$DOL" ]] || { echo "missing $DOL; re-run with --rebuild" >&2; exit 1; }

mkdir -p "$OUT"
DODGC_ENGINE="$ENGINE" DODGC_OUT="$OUT" DODGC_DOL="$DOL" \
	"$ROOT/scripts/build-network-pair.sh" "${PAIR_ARGS[@]}"

if (( RUN_PROBE )); then
	"$ROOT/scripts/probe-network.py" \
		"$OUT/network-lan/host/dodgc-menu.iso" \
		"$OUT/network-lan/client/dodgc-menu.iso" --timeout "$TIMEOUT"
fi
