#!/usr/bin/env bash
# Snapshot progress evidence: note + optional screenshot/video still into playtests/.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

usage() {
	cat <<'EOF'
usage: scripts/capture-progress.sh [options] [message...]

  --label NAME     short label for filenames (default: progress)
  --screenshot     grab a full-desktop screenshot now
  --from-file PATH copy an existing image/video into playtests/
  --no-note        skip markdown note
  -h, --help

Examples:
  scripts/capture-progress.sh --screenshot "boot reaches DoD menu"
  scripts/capture-progress.sh --from-file /tmp/frame.png "map load smoke"
EOF
}

LABEL="progress"
SHOT=0
FROM_FILE=""
WRITE_NOTE=1
MSG=()

while [[ $# -gt 0 ]]; do
	case "$1" in
		--label)
			LABEL="${2:-progress}"
			shift
			;;
		--screenshot) SHOT=1 ;;
		--from-file)
			FROM_FILE="${2:-}"
			shift
			;;
		--no-note) WRITE_NOTE=0 ;;
		-h|--help) usage; exit 0 ;;
		--)
			shift
			MSG+=("$@")
			break
			;;
		-*)
			echo "unknown option: $1" >&2
			usage >&2
			exit 2
			;;
		*) MSG+=("$1") ;;
	esac
	shift
done

STAMP="$(date +%Y%m%d-%H%M%S)"
TEXT="${MSG[*]:-progress checkpoint}"
SAFE_LABEL="$(echo "$LABEL" | tr -cs 'A-Za-z0-9._-' '-' | sed 's/^-//;s/-$//')"
NOTE_FILE="$DODGC_NOTES/${STAMP}-${SAFE_LABEL}.md"
ARTIFACTS=()

if (( SHOT )); then
	SHOT_PATH="$DODGC_SHOTS/${STAMP}-${SAFE_LABEL}.png"
	if command -v import >/dev/null 2>&1; then
		import -window root "$SHOT_PATH"
		ARTIFACTS+=("$SHOT_PATH")
	elif command -v gnome-screenshot >/dev/null 2>&1; then
		gnome-screenshot -f "$SHOT_PATH"
		ARTIFACTS+=("$SHOT_PATH")
	else
		echo "warning: no screenshot tool (ImageMagick import / gnome-screenshot)" >&2
	fi
fi

if [[ -n "$FROM_FILE" ]]; then
	[[ -f "$FROM_FILE" ]] || { echo "error: not a file: $FROM_FILE" >&2; exit 1; }
	ext="${FROM_FILE##*.}"
	case "${ext,,}" in
		mp4|mkv|webm|mov)
			dest="$DODGC_VIDEOS/${STAMP}-${SAFE_LABEL}.${ext}"
			;;
		*)
			dest="$DODGC_SHOTS/${STAMP}-${SAFE_LABEL}.${ext}"
			;;
	esac
	cp -f "$FROM_FILE" "$dest"
	ARTIFACTS+=("$dest")
fi

if (( WRITE_NOTE )); then
	{
		echo "# ${SAFE_LABEL} — $STAMP"
		echo
		echo "- Repo: \`$(dodgc_repo_sha)\`"
		echo "- Engine: \`$(dodgc_engine_sha)\`"
		echo "- ISO present: $([[ -f $DODGC_ISO ]] && echo yes || echo no)"
		echo "- DOL present: $([[ -f $DODGC_DOL ]] && echo yes || echo no)"
		if ((${#ARTIFACTS[@]})); then
			echo "- Artifacts:"
			for a in "${ARTIFACTS[@]}"; do
				echo "  - \`${a#$DODGC_ROOT/}\`"
			done
		fi
		echo
		echo "$TEXT"
	} > "$NOTE_FILE"
	echo "==> note: $NOTE_FILE"
fi

for a in "${ARTIFACTS[@]}"; do
	echo "==> saved: $a"
done

python3 "$SCRIPT_DIR/record-session.py" index >/dev/null || true
