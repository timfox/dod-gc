#!/usr/bin/env bash
# Recreate engine + content symlinks for this machine.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

ENGINE_DEFAULT="/run/media/tim/Expansion/xash3d-gc"
RETAIL_HL_DEFAULT="${DODGC_STEAM_HL_DEFAULT:-/home/tim/snap/steam/common/.local/share/Steam/steamapps/common/Half-Life}"

ENGINE="${DODGC_ENGINE_TARGET:-$ENGINE_DEFAULT}"
RETAIL_HL="${DODGC_RETAIL_HL:-${DODGC_STEAM_HL:-$RETAIL_HL_DEFAULT}}" # DODGC_STEAM_HL: legacy name

usage() {
	cat <<EOF
usage: scripts/link-deps.sh [--engine PATH] [--retail-hl PATH]

Defaults:
  engine:   $ENGINE_DEFAULT
  retail-hl: $RETAIL_HL_DEFAULT
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--engine) ENGINE="${2:-}"; shift ;;
		--retail-hl|--steam-hl) RETAIL_HL="${2:-}"; shift ;;
		-h|--help) usage; exit 0 ;;
		*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

[[ -d "$ENGINE/scripts" ]] || { echo "error: engine path invalid: $ENGINE" >&2; exit 1; }
[[ -d "$RETAIL_HL/dod" ]] || { echo "error: retail DoD missing: $RETAIL_HL/dod" >&2; exit 1; }
[[ -d "$RETAIL_HL/valve" ]] || { echo "error: retail valve missing: $RETAIL_HL/valve" >&2; exit 1; }

mkdir -p "$ROOT/content"
ln -sfn "$ENGINE" "$ROOT/engine"
ln -sfn "$ENGINE/3rdparty/hlsdk-portable" "$ROOT/hlsdk-portable"
ln -sfn "$RETAIL_HL/dod" "$ROOT/content/dod"
ln -sfn "$RETAIL_HL/valve" "$ROOT/content/valve"

echo "linked:"
printf '  %s -> %s\n' engine "$(readlink -f "$ROOT/engine")"
printf '  %s -> %s\n' hlsdk-portable "$(readlink -f "$ROOT/hlsdk-portable")"
printf '  %s -> %s\n' content/dod "$(readlink -f "$ROOT/content/dod")"
printf '  %s -> %s\n' content/valve "$(readlink -f "$ROOT/content/valve")"
