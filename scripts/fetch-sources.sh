#!/usr/bin/env bash
# Clone (or update) the engine and the game-code SDK at the refs pinned in scripts/sources.lock into
# .deps/, so a build never depends on a developer's own working tree.
#
#   scripts/fetch-sources.sh                 clone what is missing, check out the pinned refs
#   scripts/fetch-sources.sh --update        also fetch and move an existing clone to the pinned refs
#   scripts/fetch-sources.sh --engine-ref X  use branch/tag/commit X for the engine instead of the lock
#
# Existing clones with local changes are never touched: the script stops and says so.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
source "$ROOT/scripts/sources.lock"

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

DEPS="$ROOT/.deps"
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
	git -C "$ENGINE_DIR" submodule update --init --recursive --jobs 8
fi

# --- game-code SDK --------------------------------------------------------------------------
HLSDK_DIR="$DEPS/hlsdk-portable"
if [[ ! -d "$HLSDK_DIR/.git" ]]; then
	git clone -b "$DODGC_HLSDK_BRANCH" "$DODGC_HLSDK_URL" "$HLSDK_DIR"
	UPDATE=1
fi
if (( UPDATE )); then
	require_clean "$HLSDK_DIR"
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
