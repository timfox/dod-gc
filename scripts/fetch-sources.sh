#!/usr/bin/env bash
# Clone (or update) the engine and the game-code SDK at the refs pinned in scripts/sources.lock into
# .deps/, so a build never depends on a developer's own working tree.
#
#   scripts/fetch-sources.sh                 clone what is missing, check out the pinned refs
#   scripts/fetch-sources.sh --update        also fetch and move an existing clone to the pinned refs
#                                            (the engine to the tip of its branch; the SDK clone is reset first: the
#                                            integrator edits it in place on every build, so it is disposable)
#   scripts/fetch-sources.sh --engine-ref X  use branch/tag/commit X for the engine instead of the lock
#
# It only ever touches the clones under .deps/ (or DODGC_DEPS_DIR); a checkout you point DODGC_ENGINE or
# DODGC_HLSDK at is never fetched, reset or cleaned. An engine clone with local changes is not touched either:
# the script stops and says so. DODGC_SOURCES_LOCK names another lock file (tests).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
source "${DODGC_SOURCES_LOCK:-$ROOT/scripts/sources.lock}"

UPDATE=0
while [[ $# -gt 0 ]]; do
	case "$1" in
		--update) UPDATE=1 ;;
		--engine-ref) DODGC_ENGINE_REF="${2:?--engine-ref needs a value}"; shift ;;
		-h|--help) sed -n '2,10p' "$0"; exit 0 ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done

DEPS="${DODGC_DEPS_DIR:-$ROOT/.deps}"
mkdir -p "$DEPS"

require_clean() {
	if [[ -n "$(git -C "$1" status --porcelain --untracked-files=no)" ]]; then
		echo "error: $1 has local changes; not touching it (commit, stash, or delete the directory)" >&2
		exit 1
	fi
}

# --- engine -------------------------------------------------------------------------------
ENGINE_DIR="$DEPS/xash3d-gc"
if [[ ! -d "$ENGINE_DIR/.git" ]]; then
	git clone "$DODGC_ENGINE_URL" "$ENGINE_DIR"
	UPDATE=1
fi
if (( UPDATE )); then
	require_clean "$ENGINE_DIR"
	git -C "$ENGINE_DIR" fetch --tags origin
	if git -C "$ENGINE_DIR" rev-parse --verify --quiet "origin/$DODGC_ENGINE_REF" >/dev/null; then
		git -C "$ENGINE_DIR" checkout -q -B "$DODGC_ENGINE_REF" "origin/$DODGC_ENGINE_REF"
	else
		git -C "$ENGINE_DIR" checkout -q --detach "$DODGC_ENGINE_REF"
	fi
	# Submodules at the commits this engine revision records (a few are deliberately left alone by
	# the engine's own .gitmodules, update = none).
	# DODGC_SKIP_SUBMODULES="a b" leaves those paths out (CI skips 3rdparty/dolphin: the emulator is not
	# part of the DOL build and is most of the download).
	if [[ -n "${DODGC_SKIP_SUBMODULES:-}" ]]; then
		mapfile -t want < <(git -C "$ENGINE_DIR" config -f .gitmodules --get-regexp '\.path$' | awk '{print $2}' \
			| grep -vxF -f <(tr ' ' '\n' <<<"$DODGC_SKIP_SUBMODULES"))
		git -C "$ENGINE_DIR" submodule update --init --recursive --jobs 8 -- "${want[@]}"
	else
		git -C "$ENGINE_DIR" submodule update --init --recursive --jobs 8
	fi
fi

# --- game-code SDK --------------------------------------------------------------------------
HLSDK_DIR="$DEPS/hlsdk-portable"
if [[ ! -d "$HLSDK_DIR/.git" ]]; then
	git clone -b "$DODGC_HLSDK_BRANCH" "$DODGC_HLSDK_URL" "$HLSDK_DIR"
	UPDATE=1
fi
if (( UPDATE )); then
	# The SDK clone is disposable: scripts/integrate-dod-gameplay.py edits it in place (and copies our sources
	# into it) on every build, so it always "has local changes". Reset it to the commit it is on, drop what the
	# integrator added, and move it to the pin. HLSDK_DIR is always inside DEPS: a DODGC_HLSDK you supplied is
	# never reached by this script.
	case "$HLSDK_DIR" in "$DEPS"/*) ;; *) echo "error: refusing to reset $HLSDK_DIR (not inside $DEPS)" >&2; exit 1 ;; esac
	git -C "$HLSDK_DIR" reset -q --hard HEAD
	git -C "$HLSDK_DIR" clean -fdq
	git -C "$HLSDK_DIR" fetch --tags origin
	if ! git -C "$HLSDK_DIR" cat-file -e "$DODGC_HLSDK_COMMIT^{commit}" 2>/dev/null; then
		echo "error: pinned SDK commit $DODGC_HLSDK_COMMIT is not in $DODGC_HLSDK_URL" >&2
		exit 1
	fi
	# A local branch of the same name at the pinned commit: the engine's SDK build script runs
	# `git checkout $HLSDK_GAMECUBE_BRANCH` (env.sh points it at this branch).
	git -C "$HLSDK_DIR" checkout -q -B "$DODGC_HLSDK_BRANCH" "$DODGC_HLSDK_COMMIT"
fi

echo "engine:  $ENGINE_DIR  $(git -C "$ENGINE_DIR" rev-parse HEAD)  ($(git -C "$ENGINE_DIR" branch --show-current || true))"
echo "hlsdk:   $HLSDK_DIR  $(git -C "$HLSDK_DIR" rev-parse HEAD)  ($DODGC_HLSDK_BRANCH)"
echo "scripts/env.sh uses these when DODGC_ENGINE / DODGC_HLSDK are not set."
