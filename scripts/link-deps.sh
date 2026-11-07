#!/usr/bin/env bash
# Recreate engine + content symlinks for this machine.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

ENGINE_DEFAULT="/run/media/tim/Expansion/xash3d-gc"
STEAM_HL_DEFAULT="/home/tim/snap/steam/common/.local/share/Steam/steamapps/common/Half-Life"

ENGINE="${DODGC_ENGINE_TARGET:-$ENGINE_DEFAULT}"
STEAM_HL="${DODGC_STEAM_HL:-$STEAM_HL_DEFAULT}"

usage() {
	cat <<EOF
usage: scripts/link-deps.sh [--engine PATH] [--steam-hl PATH]

Defaults:
  engine:   $ENGINE_DEFAULT
  steam-hl: $STEAM_HL_DEFAULT
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--engine) ENGINE="${2:-}"; shift ;;
		--steam-hl) STEAM_HL="${2:-}"; shift ;;
		-h|--help) usage; exit 0 ;;
		*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

[[ -d "$ENGINE/scripts" ]] || { echo "error: engine path invalid: $ENGINE" >&2; exit 1; }
[[ -d "$STEAM_HL/dod" ]] || { echo "error: Steam DoD missing: $STEAM_HL/dod" >&2; exit 1; }
[[ -d "$STEAM_HL/valve" ]] || { echo "error: Steam valve missing: $STEAM_HL/valve" >&2; exit 1; }

mkdir -p "$ROOT/content"
ln -sfn "$ENGINE" "$ROOT/engine"
ln -sfn "$ENGINE/3rdparty/hlsdk-portable" "$ROOT/hlsdk-portable"
ln -sfn "$STEAM_HL/dod" "$ROOT/content/dod"
ln -sfn "$STEAM_HL/valve" "$ROOT/content/valve"

echo "linked:"
printf '  %s -> %s\n' engine "$(readlink -f "$ROOT/engine")"
printf '  %s -> %s\n' hlsdk-portable "$(readlink -f "$ROOT/hlsdk-portable")"
printf '  %s -> %s\n' content/dod "$(readlink -f "$ROOT/content/dod")"
printf '  %s -> %s\n' content/valve "$(readlink -f "$ROOT/content/valve")"
