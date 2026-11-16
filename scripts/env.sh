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

# Pinned sources (scripts/sources.lock): the engine ref, and the game-code SDK branch + commit.
# shellcheck disable=SC1091
source "$DODGC_ROOT/scripts/sources.lock"

# Engine and SDK: the checkouts named by DODGC_ENGINE / DODGC_HLSDK, or the pinned clones that
# scripts/fetch-sources.sh makes under .deps/. Never a symlink into somebody's working tree (the
# tracked engine/ and hlsdk-portable/ symlinks used to point at one developer's external drive, so a
# default build compiled whatever state that tree happened to be in).
if [[ -z "${DODGC_ENGINE:-}" && -d "$DODGC_ROOT/.deps/xash3d-gc/scripts" ]]; then
	DODGC_ENGINE="$DODGC_ROOT/.deps/xash3d-gc"
fi
if [[ -z "${DODGC_HLSDK:-}" && -d "$DODGC_ROOT/.deps/hlsdk-portable" ]]; then
	DODGC_HLSDK="$DODGC_ROOT/.deps/hlsdk-portable"
fi
if [[ -z "${DODGC_ENGINE:-}" || ! -d "${DODGC_ENGINE}/scripts" ]]; then
	echo "error: no engine / SDK sources selected." >&2
	echo "  engine: ${DODGC_ENGINE:-<unset>}   hlsdk: ${DODGC_HLSDK:-<unset>}" >&2
	echo "  Either run scripts/fetch-sources.sh (pinned clones under .deps/), or point" >&2
	echo "  DODGC_ENGINE at an xash3d-gc checkout and DODGC_HLSDK at the game-code SDK checkout." >&2
	exit 1
fi
export DODGC_ENGINE
export DODGC_HLSDK="${DODGC_HLSDK:-}"

# Scripts that build the game DLL call this; ones that only package a disc do not need the SDK.
dodgc_require_hlsdk() {
	if [[ -z "$DODGC_HLSDK" || ! -d "$DODGC_HLSDK/dlls" ]]; then
		echo "error: no SDK source selected (hlsdk: ${DODGC_HLSDK:-<unset>})." >&2
		echo "  Run scripts/fetch-sources.sh, or point DODGC_HLSDK at the game-code SDK checkout." >&2
		exit 1
	fi
}
# The engine's SDK build script runs `git checkout $HLSDK_GAMECUBE_BRANCH` in $HLSDK_PORTABLE_DIR and
# defaults to a local branch name (mobile_hacks) that a fresh clone does not have; name the pinned one.
export HLSDK_PORTABLE_DIR="${HLSDK_PORTABLE_DIR:-$DODGC_HLSDK}"
export HLSDK_GAMECUBE_BRANCH="${HLSDK_GAMECUBE_BRANCH:-$DODGC_HLSDK_BRANCH}"
# Host tools (split_studio_mdl.py ...): this repo's own copy, not a stale one lying around in /tmp.
export DODGC_TOOLS="${DODGC_TOOLS:-$DODGC_ROOT/tools}"

# Legally obtained retail Day of Defeat + HL1 basedir (DoD falls back to valve/).
export DODGC_CONTENT_DOD="${DODGC_CONTENT_DOD:-$DODGC_ROOT/content/dod}"
export DODGC_CONTENT_VALVE="${DODGC_CONTENT_VALVE:-$DODGC_ROOT/content/valve}"

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
		echo "  symlink content/dod → retail Half-Life/dod" >&2
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
