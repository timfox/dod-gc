#!/usr/bin/env bash
# Build Day of Defeat GameCube artifacts into this repo's out/ tree only.
# Produces: out/dodgc.dol + out/dodgc.iso
# Does NOT overwrite HL1 OUT/xash3d-gc.iso, boot.dol, or halflife1.dol.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

usage() {
	cat <<'EOF'
usage: scripts/build-dodgc.sh [options]

  --dol-only     build/link dodgc.dol only (skip ISO)
  --iso-only     package out/dodgc.iso from an existing out/dodgc.dol
  --skip-hlsdk   skip HLSDK archive rebuild (use existing engine OUT/hlsdk-gamecube/dod)
  --play         stamp play-oriented gamecube.cfg with BSP world rendering
  --smoke MAP    lean smoke disc for MAP (e.g. dod_avalanche)
  -h, --help     this help

Environment:
  DODGC_ENGINE, DODGC_CONTENT_DOD, DODGC_ISO, XASH3D_GC_ISO_CACHE
EOF
}

DOL_ONLY=0
ISO_ONLY=0
SKIP_HLSDK=0
PLAY=0
SMOKE_MAP=""

while [[ $# -gt 0 ]]; do
	case "$1" in
		--dol-only) DOL_ONLY=1 ;;
		--iso-only) ISO_ONLY=1 ;;
		--skip-hlsdk) SKIP_HLSDK=1 ;;
		--play) PLAY=1 ;;
		--smoke)
			SMOKE_MAP="${2:-}"
			[[ -n "$SMOKE_MAP" ]] || { echo "error: --smoke needs a map name" >&2; exit 2; }
			shift
			;;
		-h|--help) usage; exit 0 ;;
		*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

dodgc_require_content

ENGINE_DOL="$DODGC_ENGINE/OUT/bin/dodgc.dol"
ENGINE_HLSDK_DOD="$DODGC_ENGINE/OUT/hlsdk-gamecube/dod"

if (( ! ISO_ONLY )); then
	echo "==> [dod-gc] building DoD HLSDK + engine DOL (profile=dod → dodgc.dol)"
	# Modern Ethernet adapters require libogc2's native EXI drivers.
	export XASH_GAMECUBE_OGC_STACK="${XASH_GAMECUBE_OGC_STACK:-libogc2}"
	# CapFaces tip: default BBA off (~200 KiB MEM1 → tip≈390 KiB on caen).
	# Set XASH_GAMECUBE_LINK_BBA=1 for Ethernet/net probes only.
	export XASH_GAMECUBE_LINK_BBA="${XASH_GAMECUBE_LINK_BBA:-0}"
	cd "$DODGC_ENGINE"
	python3 "$SCRIPT_DIR/integrate-dod-gameplay.py" \
		--hlsdk-dir "$DODGC_HLSDK" --engine-dir "$DODGC_ENGINE"

	# Keep DoD HLSDK archives under out/network-hlsdk. Never clobber a fresh
	# private build with stale OUT/hlsdk-gamecube copies (hud19 ScreenWidth miss).
	SDK_PRIVATE="$DODGC_OUT/network-hlsdk"
	export HLSDK_GAMECUBE_DESTDIR="$SDK_PRIVATE"

	if (( ! SKIP_HLSDK )); then
		echo "==> HLSDK GameCube archives for gamedir=dod → $SDK_PRIVATE"
		echo "    note: Steam DoD client is vgui2-blocked upstream; this tree uses"
		echo "    HL1SDK deathmatch as the code base while mirroring DoD assets."
		HLSDK_GAMECUBE_DESTDIR="$SDK_PRIVATE" \
		HLSDK_GAMECUBE_GAMEDIR=dod \
			bash scripts/hlsdk-gamecube-build.sh || {
			echo "warning: hlsdk-gamecube-build for dod failed or is incomplete." >&2
			echo "  Continuing only if archives already exist; otherwise DOL link will fail." >&2
		}
	fi

	SDK_SOURCE="${HLSDK_GAMECUBE_FALLBACK_SOURCE:-$DODGC_ENGINE/OUT/hlsdk-gamecube}"
	if [[ ! -s "$SDK_PRIVATE/dod/cl_dlls/libclient_gamecube_ppc.a" ]] \
		&& [[ -s "$SDK_SOURCE/dod/cl_dlls/libclient_gamecube_ppc.a" ]]; then
		mkdir -p "$SDK_PRIVATE/lib"
		cp -a "$SDK_SOURCE/dod" "$SDK_PRIVATE/"
		[[ ! -d "$SDK_SOURCE/lib" ]] || cp -a "$SDK_SOURCE/lib/." "$SDK_PRIVATE/lib/"
	fi
	for archive in "$SDK_PRIVATE/dod/dlls/libhl_gamecube_ppc.a" "$SDK_PRIVATE/dod/cl_dlls/libclient_gamecube_ppc.a"; do
		[[ -s "$archive" ]] || continue
		"${DEVKITPRO:-/opt/devkitpro}/devkitPPC/bin/powerpc-eabi-objcopy" \
			--remove-section=.eh_frame --remove-section='.eh_frame.*' "$archive"
	done

	# Build DOL only inside the engine tree; disc packaging happens here with dodgc.iso.
	XASH3D_GC_SKIP_DISC_BUILD=1 \
	HLSDK_GAMECUBE_GAMEDIR=dod \
		bash scripts/build-gamecube.sh --game-profile=dod

	if [[ ! -f "$ENGINE_DOL" ]]; then
		echo "error: expected $ENGINE_DOL after build" >&2
		exit 1
	fi

	cp -f "$ENGINE_DOL" "$DODGC_DOL"
	if [[ -f "$DODGC_ENGINE/OUT/bin/gamecube-handoff.txt" ]]; then
		cp -f "$DODGC_ENGINE/OUT/bin/gamecube-handoff.txt" "$DODGC_HANDOFF"
	fi
	echo "==> [dod-gc] DOL ready: $DODGC_DOL"
fi

if (( DOL_ONLY )); then
	exit 0
fi

if [[ ! -f "$DODGC_DOL" ]]; then
	echo "error: missing $DODGC_DOL (run without --iso-only first)" >&2
	exit 1
fi

echo "==> [dod-gc] packaging isolated ISO: $DODGC_ISO"
echo "    data=$DODGC_CONTENT_DOD  gamedir=dod  cache=$XASH3D_GC_ISO_CACHE"

# Stage content into cache so disc tooling never writes into Steam or HL1 trees.
BUILD_DATA="$XASH3D_GC_ISO_CACHE/dod"
BUILD_VALVE="$XASH3D_GC_ISO_CACHE/valve"
mkdir -p "$BUILD_DATA"
rsync -a --delete "$DODGC_CONTENT_DOD/" "$BUILD_DATA/"
cat > "$BUILD_DATA/game.cfg" <<'EOF'
mp_teamplay 1
mp_teamlist "Allies;Axis"
mp_defaultteam 0
EOF
if [[ -d "$DODGC_CONTENT_VALVE" ]]; then
	mkdir -p "$BUILD_VALVE"
	rsync -a --delete \
		--exclude 'SAVE' --exclude 'logs' --exclude '*.tmp' \
		"$DODGC_CONTENT_VALVE/" "$BUILD_VALVE/"
fi

tmp_iso="$XASH3D_GC_ISO_CACHE/dodgc-$$.iso"
trap 'rm -f "$tmp_iso"' EXIT

DISC_ARGS=(
	--dol "$DODGC_DOL"
	--output "$tmp_iso"
	--data "$BUILD_DATA"
	--gamedir dod
)
if (( PLAY )); then
	DISC_ARGS+=(--play --world-render)
fi
if [[ -n "$SMOKE_MAP" ]]; then
	DISC_ARGS+=(--smoke-map "$SMOKE_MAP")
fi

TMPDIR="$XASH3D_GC_ISO_CACHE" \
	python3 "$DODGC_ENGINE/scripts/build-gamecube-disc.py" "${DISC_ARGS[@]}"

publish_tmp="$DODGC_ISO.tmp.$$"
cp --reflink=auto "$tmp_iso" "$publish_tmp"
mv -f "$publish_tmp" "$DODGC_ISO"
trap - EXIT
rm -f "$tmp_iso"

{
	echo "dod-gc disc handoff"
	echo "built_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "iso=$DODGC_ISO"
	echo "iso_bytes=$(wc -c < "$DODGC_ISO" | tr -d ' ')"
	echo "dol=$DODGC_DOL"
	echo "gamedir=dod"
	echo "engine_commit=$(dodgc_engine_sha)"
	echo "dodgc_commit=$(dodgc_repo_sha)"
	echo "content_dod=$DODGC_CONTENT_DOD"
	echo "note=does_not_overwrite_hl1_xash3d-gc.iso"
} | tee "$DODGC_OUT/dodgc-iso-handoff.txt"

echo "==> [dod-gc] ISO ready: $DODGC_ISO"
echo "    playtest: scripts/playtest.sh"
echo "    HL1 remains untouched (engine OUT/xash3d-gc.iso / boot.dol)."
