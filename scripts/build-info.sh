#!/usr/bin/env bash
# Print what a disc was built from: every repo and the pinned submodules, with a dirty flag.
#
#   scripts/build-info.sh            full report (written next to the ISO)
#   scripts/build-info.sh --short    the few lines build-lean-iso.sh puts on the disc as build-info.txt;
#                                    the engine prints them in the boot log
#
# Optional: DODGC_DOLPHIN_SRC = the Dolphin source tree the emulator binary was built from. When it is
# set, its commit is compared with the one the engine records for 3rdparty/dolphin and a mismatch is
# flagged (the emulator is not part of the disc, but a disc tested on an emulator that master does not
# pin is not "the same page").
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/tip-status.sh"

SHORT=0
[[ "${1:-}" == "--short" ]] && SHORT=1

sha() { git -C "$1" rev-parse HEAD 2>/dev/null || echo "unknown"; }
branch() { git -C "$1" branch --show-current 2>/dev/null || true; }
dirty() { git -C "$1" status --porcelain --untracked-files=no 2>/dev/null | wc -l; }

hlsdk_head="none"
[[ -z "$DODGC_HLSDK" ]] || hlsdk_head="$(sha "$DODGC_HLSDK")"
hlsdk_pin="OK (pinned $DODGC_HLSDK_COMMIT)"
[[ "$hlsdk_head" == "$DODGC_HLSDK_COMMIT" ]] || hlsdk_pin="MISMATCH: pinned $DODGC_HLSDK_COMMIT"

dolphin_line="not checked (set DODGC_DOLPHIN_SRC to the tree the emulator was built from)"
if [[ -n "${DODGC_DOLPHIN_SRC:-}" && -d "$DODGC_DOLPHIN_SRC" ]]; then
	dol_pin="$(git -C "$DODGC_ENGINE" rev-parse HEAD:3rdparty/dolphin 2>/dev/null || echo unknown)"
	dol_have="$(sha "$DODGC_DOLPHIN_SRC")"
	if [[ "$dol_pin" == "$dol_have" ]]; then
		dolphin_line="OK ($dol_have)"
	else
		dolphin_line="MISMATCH: emulator built from $dol_have, engine master records $dol_pin"
	fi
fi

# Whether each source is at the tip of the ref it tracks. A checkout you supplied yourself is not checked.
engine_tip="user-supplied (not checked)"
hlsdk_tip="user-supplied (not checked)"
if [[ "$DODGC_ENGINE" == "$DODGC_ROOT/.deps/xash3d-gc" ]]; then
	engine_tip="$(dodgc_tip_status "$DODGC_ENGINE" "$DODGC_ENGINE_REF")"
fi
if [[ -n "$DODGC_HLSDK" && "$DODGC_HLSDK" == "$DODGC_ROOT/.deps/hlsdk-portable" ]]; then
	# the SDK is pinned to a commit: it is "at the tip" when the pinned branch has not moved past the pin
	hlsdk_tip="$(dodgc_tip_status "$DODGC_HLSDK" "$DODGC_HLSDK_BRANCH")"
fi

tool="$DODGC_TOOLS/split_studio_mdl.py"
tool_sha="missing"
[[ -f "$tool" ]] && tool_sha="$(sha256sum "$tool" | cut -c1-16)"

echo "build-info 1"
echo "built $(date -u +%Y-%m-%dT%H:%M:%SZ) on $(hostname)"
echo "dod-gc $(sha "$DODGC_ROOT") $(branch "$DODGC_ROOT") dirty=$(dirty "$DODGC_ROOT")"
echo "xash3d-gc $(sha "$DODGC_ENGINE") $(branch "$DODGC_ENGINE") dirty=$(dirty "$DODGC_ENGINE") $engine_tip"
if [[ -n "$DODGC_HLSDK" ]]; then
	echo "hlsdk $hlsdk_head $(branch "$DODGC_HLSDK") modified-by-integration=$(dirty "$DODGC_HLSDK") pin=$hlsdk_pin branch-$hlsdk_tip"
else
	echo "hlsdk none (not selected for this build)"
fi
echo "mainui $(git -C "$DODGC_ENGINE" rev-parse HEAD:3rdparty/mainui 2>/dev/null || echo unknown)"
echo "tools $DODGC_TOOLS split_studio_mdl.py sha256=$tool_sha"
echo "dolphin $dolphin_line"

if (( ! SHORT )); then
	echo
	echo "== xash3d-gc submodules (recorded commit; leading '-' = not initialised, e.g. engine .gitmodules update=none)"
	git -C "$DODGC_ENGINE" submodule status --recursive 2>/dev/null || true
	echo
	echo "== toolchain"
	echo "devkitPPC: $("${DEVKITPRO:-/opt/devkitpro}/devkitPPC/bin/powerpc-eabi-gcc" --version 2>/dev/null | head -1)"
	echo "recipe: XASH_GAMECUBE_OGC_STACK=${XASH_GAMECUBE_OGC_STACK:-unset} XASH_GC_DEFAULT_SLOTS=${XASH_GC_DEFAULT_SLOTS:-default}"
	echo "iso cache: ${XASH3D_GC_ISO_CACHE:-unset}"
fi
