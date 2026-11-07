#!/usr/bin/env bash
# Stage a MEM1-friendly Day of Defeat tree for dodgc.iso / dodgc-menu.iso.
# Full Steam dod/ is far too large for GameCube boot; keep markers + UI only.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

usage() {
	cat <<'EOF'
usage: scripts/stage-lean-dod.sh [--map NAME]

  --map NAME   copy one .bsp into lean maps/ (searches content/dod then valve)
EOF
}

MAP_NAME=""
while [[ $# -gt 0 ]]; do
	case "$1" in
		--map)
			MAP_NAME="${2:-}"
			[[ -n "$MAP_NAME" ]] || { echo "error: --map needs a name" >&2; exit 2; }
			shift
			;;
		-h|--help) usage; exit 0 ;;
		*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

dodgc_require_content

CACHE="${XASH3D_GC_ISO_CACHE:-/mnt/datasets/dodgc-iso-build}"
LEAN="${DODGC_LEAN_DOD:-$CACHE/dod-menu-min}"
VMIN="${DODGC_LEAN_VALVE:-$CACHE/valve-menu-min}"
SRC="$DODGC_CONTENT_DOD"
VALVE="$DODGC_CONTENT_VALVE"

rm -rf "$LEAN" "$VMIN"
mkdir -p "$LEAN"/{media,resource,maps,gfx} "$VMIN"/{gfx,resource}

# Identity / FS markers (HasGameAssets needs marker + maps/ for non-valve).
cp -f "$SRC/liblist.gam" "$LEAN/"
cp -f "$SRC/delta.lst" "$LEAN/" 2>/dev/null || true
cp -f "$VALVE/gfx.wad" "$LEAN/"
# Smoke / map-load probes need these on the primary gamedir (not only valve/).
for f in default.cfg; do
	[[ -f "$VALVE/$f" ]] && cp -f "$VALVE/$f" "$LEAN/"
done
for f in palette.lmp colormap.lmp conback.lmp; do
	[[ -f "$VALVE/gfx/$f" ]] && cp -f "$VALVE/gfx/$f" "$LEAN/gfx/"
done
# Tiny maps placeholder — directory must exist; avoid multi-MB BSPs on menu discs.
printf 'dodgc lean maps placeholder\n' > "$LEAN/maps/.dodgc_keep"
# Do NOT stage decals.wad — Host_InitDecals enumerates every MIP into MEM1.

# DoD UI / title art (no loading-screen wallpaper farms).
mkdir -p "$LEAN/resource"
# Skip dod_english.txt (≈90 KiB) — MEM1 tip cannot Localize_AddToDict it at
# menu bring-up; builtins + valve sibling strings cover the GC menu.
for f in logo_game.tga GameMenu.res game_menu.tga game_menu_mouseover.tga \
	ClientScheme.res menu_hl_no_icon.tga
do
	[[ -f "$SRC/resource/$f" ]] && cp -f "$SRC/resource/$f" "$LEAN/resource/"
done
# Retail DoD wordmark for gc_menu bake (baker punches black→alpha, keeps olive).
if [[ -f "$LEAN/resource/logo_game.tga" ]]; then
	cp -f "$LEAN/resource/logo_game.tga" "$LEAN/resource/logo.tga"
fi

# Optional single-map stage for smoke / map-load probes.
if [[ -n "$MAP_NAME" ]]; then
	stem="${MAP_NAME%.bsp}"
	bsp=""
	for cand in "$SRC/maps/${stem}.bsp" "$VALVE/maps/${stem}.bsp"; do
		[[ -f "$cand" ]] && { bsp=$cand; break; }
	done
	if [[ -z "$bsp" ]]; then
		echo "error: map not found: ${stem}.bsp (searched dod + valve)" >&2
		exit 1
	fi
	cp -f "$bsp" "$LEAN/maps/${stem}.bsp"
	echo "staged map: $bsp ($(du -h "$bsp" | awk '{print $1}'))"
	# Map-specific + common WADs (skip decals.wad — Host_InitDecals OOM).
	for wad in "${stem}.wad" dod.wad dod_shared.wad hlbasics.wad; do
		for root in "$SRC" "$VALVE"; do
			if [[ -f "$root/$wad" ]]; then
				cp -f "$root/$wad" "$LEAN/"
				echo "staged wad: $wad"
				break
			fi
		done
	done
	# Optional overview / txt next to the BSP.
	for ext in txt res; do
		[[ -f "$SRC/maps/${stem}.${ext}" ]] && cp -f "$SRC/maps/${stem}.${ext}" "$LEAN/maps/"
		[[ -f "$VALVE/maps/${stem}.${ext}" ]] && cp -f "$VALVE/maps/${stem}.${ext}" "$LEAN/maps/"
	done
	# Create Game needs maps.lst on disc-only (refresh cannot rewrite).
	printf '%s "Local match"\n' "$stem" > "$LEAN/maps.lst"
	echo "staged maps.lst for $stem"
	if [[ ! -f "$LEAN/listenserver.cfg" ]]; then
		printf 'mp_timelimit 0\nmp_fraglimit 0\n' > "$LEAN/listenserver.cfg"
	fi
fi

# Retail DoD soldier splash tiles (same 800×600 layout as HL1 steam sheet).
mkdir -p "$LEAN/resource/background"
DOD_BG_SRC=""
if [[ -f "$SRC/resource/background/800_1_a_loading.tga" ]]; then
	DOD_BG_SRC="$SRC/resource/background"
elif [[ -f "$VALVE/resource/background/800_1_a_loading.tga" ]]; then
	DOD_BG_SRC="$VALVE/resource/background"
fi
if [[ -n "$DOD_BG_SRC" ]]; then
	cat > "$LEAN/resource/BackgroundLayout.txt" <<'EOF'
resolution	800	600

resource/background/800_1_a_loading.tga		fit		0	0
resource/background/800_1_b_loading.tga		fit		256	0
resource/background/800_1_c_loading.tga		fit		512	0
resource/background/800_1_d_loading.tga		fit		768	0

resource/background/800_2_a_loading.tga		fit		0	256
resource/background/800_2_b_loading.tga		fit		256	256
resource/background/800_2_c_loading.tga		fit		512	256
resource/background/800_2_d_loading.tga		fit		768	256

resource/background/800_3_a_loading.tga		fit		0	512
resource/background/800_3_b_loading.tga		fit		256	512
resource/background/800_3_c_loading.tga		fit		512	512
resource/background/800_3_d_loading.tga		fit		768	512
EOF
	for f in "$DOD_BG_SRC"/800_*_loading.tga; do
		[[ -f "$f" ]] && cp -f "$f" "$LEAN/resource/background/"
	done
	echo "staged DoD/menu background tiles from $DOD_BG_SRC"
fi

# Configs
cp -f "$SRC/config.cfg" "$LEAN/" 2>/dev/null || true
cp -f "$SRC/autoexec.cfg" "$LEAN/" 2>/dev/null || printf '\n' > "$LEAN/autoexec.cfg"

# HL1 Valve intro (AVI; disc bake builds .gcvid/.gcpcm companions).
if [[ "${DODGC_STAGE_INTRO:-1}" == "1" ]]; then
	cp -f "$VALVE/media/valve.avi" "$LEAN/media/" 2>/dev/null || true
	[[ -f "$VALVE/media/logo.avi" ]] && cp -f "$VALVE/media/logo.avi" "$LEAN/media/"
	if [[ -f "$VALVE/media/StartupVids.txt" ]]; then
		# Prefer retail list but force .avi (GC path); drop .webm-only lines.
		awk '/\.avi[[:space:]]*$/ {print}' "$VALVE/media/StartupVids.txt" > "$LEAN/media/StartupVids.txt"
	fi
	if [[ ! -s "$LEAN/media/StartupVids.txt" ]]; then
		printf 'media/valve.avi\n' > "$LEAN/media/StartupVids.txt"
	fi
else
	: > "$LEAN/media/StartupVids.txt"
fi

# Minimal valve basedir sibling (DoD falls back here).
cp -f "$VALVE/liblist.gam" "$VALVE/gfx.wad" "$VMIN/"
cp -f "$VALVE/valve.rc" "$VMIN/" 2>/dev/null || true
for f in palette.lmp conback.lmp colormap.lmp; do
	[[ -f "$VALVE/gfx/$f" ]] && cp -f "$VALVE/gfx/$f" "$VMIN/gfx/"
done
for f in gameui_english.txt valve_english.txt mainui_english.txt; do
	[[ -f "$VALVE/resource/$f" ]] && cp -f "$VALVE/resource/$f" "$VMIN/resource/"
done

echo "staged lean DoD: $(du -sh "$LEAN" | awk '{print $1}') → $LEAN"
echo "staged lean valve: $(du -sh "$VMIN" | awk '{print $1}') → $VMIN"
echo "LEAN=$LEAN"
echo "VMIN=$VMIN"
