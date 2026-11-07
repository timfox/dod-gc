#!/usr/bin/env bash
# Prove DoD Avalanche visuals: stage full-map assets, smoke+worldrender+dumpframes,
# boot Dolphin, collect DumpFrames stills as a fly-through evidence pack.
#
# Retail map stem is dod_avalanche (CS-style de_avalanche is accepted as an alias).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

MAP="${1:-dod_avalanche}"
case "$MAP" in
	de_avalanche) MAP=dod_avalanche ;;
esac

DURATION="${FLYTHROUGH_DURATION:-90}"
# Success signals only — FATAL/OOM must not count as ready (false pass 20261107).
READY_RE='world_ready=1|V_RenderView path present|post-G36 sustained world present|MAP READY'
# "map-load arena too small" is expected once before compact (no-lighting) load.
FAIL_RE='Xash Error:|Host_Error|Could not load model|fatal message=|_Mem_Realloc: out of memory'

dodgc_require_content
[[ -f "$DODGC_DOL" ]] || {
	echo "==> building DOL"
	export XASH_GAMECUBE_LINK_BBA="${XASH_GAMECUBE_LINK_BBA:-1}"
	bash "$SCRIPT_DIR/build-dodgc.sh" --dol-only --skip-hlsdk
}

ISO="$DODGC_OUT/dodgc-avalanche-fly.iso"
echo "==> lean smoke ISO: $MAP (full-map + world-render + dumpframes)"
bash "$SCRIPT_DIR/build-lean-iso.sh" \
	--menu-only \
	--smoke "$MAP" \
	--full-map \
	--world-render \
	--dumpframes \
	--skip-startup-vids
cp -f "$DODGC_OUT/dodgc-menu.iso" "$ISO"

STAMP="$(date +%Y%m%d-%H%M%S)"
SHOT_DIR="$DODGC_SHOTS/${STAMP}-avalanche-fly"
NOTE="$DODGC_NOTES/${STAMP}-avalanche-flythrough.md"
USER_DIR="${XDG_RUNTIME_DIR:-/tmp}/dodgc-fly-${STAMP}"
LOG="/tmp/dodgc-fly-${STAMP}.log"
DUMP_DIR="$USER_DIR/DumpFrames"
mkdir -p "$SHOT_DIR" "$DUMP_DIR" "$USER_DIR/Config" "$DODGC_VIDEOS"

DOLPHIN=""
if [[ -x "$DODGC_ENGINE/3rdparty/dolphin/build/Binaries/dolphin-emu" ]]; then
	DOLPHIN="$DODGC_ENGINE/3rdparty/dolphin/build/Binaries/dolphin-emu"
elif command -v dolphin-emu >/dev/null 2>&1; then
	DOLPHIN="$(command -v dolphin-emu)"
else
	echo "error: Dolphin not found" >&2
	exit 2
fi

PROFILE="${XDG_DATA_HOME:-$HOME/.local/share}/dod-gc/dolphin"
[[ -f "$PROFILE/Config/GCPadNew.ini" ]] && cp -f "$PROFILE/Config/GCPadNew.ini" "$USER_DIR/Config/" || true

cat > "$USER_DIR/Config/Dolphin.ini" <<EOF
[Core]
CPUCore = 1
CPUThread = True
DSPHLE = True
FastDiscSpeed = True
MMU = True
AccurateCPUCache = True
SIDevice0 = 6
[DSP]
Muted = True
[Interface]
ConfirmStop = False
[Display]
KeepWindowOnTop = False
EOF

# Match dual-bba Dolphin bring-up (DISPLAY/EGL). Missing X11 → instant SIGSEGV.
export EGL_PLATFORM=x11 QT_QPA_PLATFORM=xcb
unset WAYLAND_DISPLAY
: "${DISPLAY:=:0}"
export DISPLAY

# OpenGL + DumpFrames stills (Null backend produced black TGAs in prior probes).
cat > "$USER_DIR/Config/GFX.ini" <<EOF
[Settings]
DumpFrames = True
DumpFramesSilent = True
DumpFramesAsImages = True
DumpPath = $DUMP_DIR
Backend = OGL
ImmediateXFBEnable = False
EFBCopyEnable = True
SkipEFBAccessFromCPU = False
DisableCopyToVRAM = True
ShowFPS = True
EOF

cat > "$USER_DIR/Config/Logger.ini" <<'EOF'
[Logs]
BOOT = True
OSREPORT = True
[Options]
Verbosity = 3
WriteToFile = True
EOF

{
	echo "# Avalanche fly-through $STAMP"
	echo
	echo "- Map: \`$MAP\` (DoD retail; \`de_avalanche\` alias)"
	echo "- ISO: \`$ISO\`"
	echo "- DOL: \`$DODGC_DOL\`"
	echo "- Engine: \`$(dodgc_engine_sha)\`"
	echo "- DumpFrames: \`$DUMP_DIR\` → copied to \`$SHOT_DIR\`"
	echo
} > "$NOTE"

echo "==> Dolphin fly-through ${DURATION}s ready-regex=$READY_RE"
# Only prior dod-gc flythrough profiles — never bare dolphin-emu (kills other projects).
dodgc_pkill_own_dolphin "${XDG_RUNTIME_DIR:-/tmp}/dodgc-fly-"
sleep 1
set +e
"$DOLPHIN" -u "$USER_DIR" -e "$ISO" >"$LOG" 2>&1 &
DPID=$!
set -e

deadline=$((SECONDS + DURATION))
ready=0
failed=0
while (( SECONDS < deadline )); do
	if ! kill -0 "$DPID" 2>/dev/null; then
		break
	fi
	if rg -a -q "$FAIL_RE" "$LOG" 2>/dev/null; then
		failed=1
		break
	fi
	if rg -a -q "$READY_RE" "$LOG" 2>/dev/null; then
		ready=1
		# Keep sampling DumpFrames for a fly-through window after world ready.
		sleep 25
		break
	fi
	sleep 2
done

kill "$DPID" 2>/dev/null || true
wait "$DPID" 2>/dev/null || true
sleep 1

# Harvest stills
shopt -s nullglob
frames=( "$DUMP_DIR"/*.{png,tga,jpg} )
shopt -u nullglob
copied=0
for f in "${frames[@]+"${frames[@]}"}"; do
	cp -f "$f" "$SHOT_DIR/"
	copied=$((copied + 1))
done
# Also grab any DumpFrames under Dolphin user dir variants
find "$USER_DIR" -type f \( -name '*.png' -o -name '*.tga' \) 2>/dev/null \
	| while read -r f; do
		cp -f "$f" "$SHOT_DIR/" 2>/dev/null || true
	done

tr -d '\000' < "$LOG" | rg -n 'world_ready|V_RenderView|sustained world|MAP READY|FATAL|out of memory|surfaces using|peer disc|gcmap|DumpFrames|CapFaces|G151' \
	| head -80 >> "$NOTE" || true

{
	echo
	echo "## Result"
	echo "- ready=$ready"
	echo "- failed=$failed"
	echo "- dumpframes_copied=$copied"
	echo "- shot_dir=\`$SHOT_DIR\`"
	echo "- log=\`$LOG\`"
} >> "$NOTE"

echo "==> fly-through done ready=$ready failed=$failed frames=$copied"
echo "    shots: $SHOT_DIR"
echo "    note:  $NOTE"
echo "    log:   $LOG"
ls -la "$SHOT_DIR" 2>/dev/null | head -20
# Exit non-zero if world never ready or fatal boot
(( ready && !failed && copied > 0 )) || exit 1
