#!/usr/bin/env bash
# Shared paths for the isolated Day of Defeat GameCube workspace.
# Source from other scripts:  source "$(dirname "$0")/env.sh"

set -euo pipefail

DODGC_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export DODGC_ROOT

# Prefer the private native-Ethernet toolchain once setup has completed.
DODGC_NETWORK_TOOLCHAIN="${DODGC_NETWORK_TOOLCHAIN:-$DODGC_ROOT/out/network-toolchain}"
if [[ -z "${DEVKITPRO:-}" && -f "$DODGC_NETWORK_TOOLCHAIN/versions.txt" ]]; then
	export DEVKITPRO="$DODGC_NETWORK_TOOLCHAIN/devkitpro"
fi

# Engine lives outside this repo (read/build there; do not treat as HL1 workspace).
export DODGC_ENGINE="${DODGC_ENGINE:-$DODGC_ROOT/engine}"
if [[ ! -d "$DODGC_ENGINE/scripts" ]]; then
	echo "error: engine symlink missing or incomplete: $DODGC_ENGINE" >&2
	echo "  expected scripts/build-gamecube.sh under the xash3d-gc tree" >&2
	exit 1
fi

# Legally obtained Steam Day of Defeat + HL1 basedir (DoD falls back to valve/).
export DODGC_CONTENT_DOD="${DODGC_CONTENT_DOD:-$DODGC_ROOT/content/dod}"
export DODGC_CONTENT_VALVE="${DODGC_CONTENT_VALVE:-$DODGC_ROOT/content/valve}"
export DODGC_HLSDK="${DODGC_HLSDK:-$DODGC_ROOT/hlsdk-portable}"

# All DoD artifacts stay under this repo — never OUT/xash3d-gc.iso / boot.dol.
export DODGC_OUT="${DODGC_OUT:-$DODGC_ROOT/out}"
export DODGC_DOL="${DODGC_DOL:-$DODGC_OUT/dodgc.dol}"
export DODGC_ISO="${DODGC_ISO:-$DODGC_OUT/dodgc.iso}"
export DODGC_HANDOFF="${DODGC_HANDOFF:-$DODGC_OUT/gamecube-handoff.txt}"

# Playtest evidence (screenshots + videos) stays local to dod-gc.
export DODGC_PLAYTESTS="${DODGC_PLAYTESTS:-$DODGC_ROOT/playtests}"
export DODGC_SHOTS="${DODGC_SHOTS:-$DODGC_PLAYTESTS/screenshots}"
export DODGC_VIDEOS="${DODGC_VIDEOS:-$DODGC_PLAYTESTS/videos}"
export DODGC_NOTES="${DODGC_NOTES:-$DODGC_PLAYTESTS/notes}"

# Scratch for disc staging — prefer datasets volume, then /dev/shm, then local.
if [[ -z "${XASH3D_GC_ISO_CACHE:-}" ]]; then
	if [[ -d /mnt/datasets ]]; then
		export XASH3D_GC_ISO_CACHE="/mnt/datasets/dodgc-iso-build"
	elif [[ -d /dev/shm ]]; then
		export XASH3D_GC_ISO_CACHE="/dev/shm/dodgc-iso-build"
	else
		export XASH3D_GC_ISO_CACHE="$DODGC_OUT/iso-cache"
	fi
fi

mkdir -p "$DODGC_OUT" "$DODGC_SHOTS" "$DODGC_VIDEOS" "$DODGC_NOTES" "$XASH3D_GC_ISO_CACHE"

dodgc_require_content() {
	if [[ ! -f "$DODGC_CONTENT_DOD/liblist.gam" ]]; then
		echo "error: DoD content missing at $DODGC_CONTENT_DOD" >&2
		echo "  symlink content/dod → Steam Half-Life/dod" >&2
		exit 1
	fi
	if [[ ! -d "$DODGC_CONTENT_VALVE" ]]; then
		echo "warning: valve basedir missing at $DODGC_CONTENT_VALVE (DoD often falls back to it)" >&2
	fi
}

dodgc_engine_sha() {
	git -C "$DODGC_ENGINE" rev-parse --short=10 HEAD 2>/dev/null || echo "unknown"
}

dodgc_repo_sha() {
	git -C "$DODGC_ROOT" rev-parse --short=10 HEAD 2>/dev/null || echo "unknown"
}

# Kill only Dolphin processes whose argv includes -u <dod-gc profile path>.
# NEVER pkill -f dolphin-emu — that closes every Dolphin on the machine
# (including sandboxed trials in other projects).
dodgc_pkill_own_dolphin() {
	local pattern
	for pattern in "$@"; do
		[[ -n "$pattern" ]] || continue
		pkill -f -- "-u ${pattern}" 2>/dev/null || true
	done
}

# Kill only this repo's tap hub (by script path and optional --path).
dodgc_pkill_own_tap_hub() {
	local tap="${1:-}"
	if [[ -n "$tap" ]]; then
		pkill -f -- "dolphin-tap-hub.py --path ${tap}" 2>/dev/null || true
	fi
	pkill -f -- "${DODGC_ROOT}/scripts/dolphin-tap-hub.py" 2>/dev/null || true
}
