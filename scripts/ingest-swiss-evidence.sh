#!/usr/bin/env bash
# Poll mounted FAT volumes for Cube swiss-evidence.txt and archive a pass/fail note.
# Completes only when MAP_READY + CapFaces playability markers are present.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
source "$ROOT/scripts/env.sh"

TIMEOUT="${SD_WAIT_TIMEOUT:-7200}"
POLL="${SD_POLL_SECS:-5}"
SKIP_RE='Expansion$|Expansion1|Stream|Storage|Production|datasets'
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG="/tmp/dodgc-swiss-evidence-ingest-${STAMP}.log"
NOTES="$DODGC_NOTES"
mkdir -p "$NOTES"

echo "==> ingest swiss-evidence (timeout ${TIMEOUT}s) log=$LOG" | tee "$LOG"
START=$(date +%s)

pass_markers() {
	local f="$1"
	rg -q 'MAP_READY' "$f" || return 1
	rg -q 'CapFaces (pack loaded|display list|drawn=|prefetch|G151)' "$f" || return 1
	return 0
}

while true; do
	now=$(date +%s)
	if (( now - START >= TIMEOUT )); then
		echo "TIMEOUT: no swiss-evidence within ${TIMEOUT}s" | tee -a "$LOG"
		exit 1
	fi
	for mount in /run/media/tim/*; do
		[[ -d "$mount" ]] || continue
		base=$(basename "$mount")
		echo "$base" | rg -q "$SKIP_RE" && continue
		ev="$mount/xash3d/dod/logs/swiss-evidence.txt"
		[[ -f "$ev" ]] || continue
		echo "==> found $ev" | tee -a "$LOG"
		dest="$NOTES/${STAMP}-swiss-evidence-cube.txt"
		cp -f "$ev" "$dest"
		cp -f "$ev" "$NOTES/swiss-evidence-latest.txt"
		echo "archived $dest" | tee -a "$LOG"
		if pass_markers "$ev"; then
			{
				echo "# Cube swiss-evidence PASS — $STAMP"
				echo
				echo "- Source: \`$ev\`"
				echo "- Mount: \`$mount\`"
				echo
				echo '## Evidence'
				echo '```'
				cat "$ev"
				echo '```'
			} > "$NOTES/${STAMP}-swiss-evidence-pass.md"
			echo "PASS: CapFaces playability markers present" | tee -a "$LOG"
			exit 0
		fi
		echo "INCOMPLETE: markers missing in $ev (waiting for fuller Cube run)" | tee -a "$LOG"
	done
	sleep "$POLL"
done
