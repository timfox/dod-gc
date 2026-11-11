#!/usr/bin/env bash
# Stage a MEM1-friendly Day of Defeat tree for dodgc.iso / dodgc-menu.iso.
# Full Steam dod/ is far too large for GameCube boot; keep markers + UI only.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

usage() {
	cat <<'EOF'
usage: scripts/stage-lean-dod.sh [--map NAME] [--full-map] [--all-maps-audio] [--core-sfx]

  --map NAME         copy one .bsp into lean maps/ (searches content/dod then valve)
                     aliases: de_avalanche → dod_avalanche
  --full-map         with --map: also stage map cfg, mapmodels, player/weapon MDLs,
                     sprites, common DoD WADs, and core gameplay SFX packs
  --all-maps-audio   stage soundscape/ambient WAVs for every dod_*.bsp (no BSPs copied)
  --core-sfx         stage weapons/player/ui/items/doors/buttons/debris/object/common
EOF
}

MAP_NAME=""
FULL_MAP=0
ALL_MAPS_AUDIO=0
CORE_SFX=0
while [[ $# -gt 0 ]]; do
	case "$1" in
		--map)
			MAP_NAME="${2:-}"
			[[ -n "$MAP_NAME" ]] || { echo "error: --map needs a name" >&2; exit 2; }
			shift
			;;
		--full-map) FULL_MAP=1 ;;
		--all-maps-audio) ALL_MAPS_AUDIO=1 ;;
		--core-sfx) CORE_SFX=1 ;;
		-h|--help) usage; exit 0 ;;
		*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

# CS-style de_* alias → retail DoD dod_* stem.
case "$MAP_NAME" in
	de_avalanche) MAP_NAME=dod_avalanche; echo "alias: de_avalanche → dod_avalanche" ;;
	de_*)
		alt="dod_${MAP_NAME#de_}"
		if [[ -f "$DODGC_CONTENT_DOD/maps/${alt}.bsp" ]]; then
			echo "alias: $MAP_NAME → $alt"
			MAP_NAME="$alt"
		fi
		;;
esac

dodgc_require_content

CACHE="${XASH3D_GC_ISO_CACHE:-/mnt/datasets/dodgc-iso-build}"
LEAN="${DODGC_LEAN_DOD:-$CACHE/dod-menu-min}"
VMIN="${DODGC_LEAN_VALVE:-$CACHE/valve-menu-min}"
SRC="$DODGC_CONTENT_DOD"
VALVE="$DODGC_CONTENT_VALVE"

rm -rf "$LEAN" "$VMIN"
mkdir -p "$LEAN"/{media,resource,maps,gfx} "$VMIN"/{gfx,resource}

# Main-menu music + UI clicks (and optional all-map soundscapes) via shared helper.
# Retail mainui streams media/gamestartup and plays media/launch_*.wav; MP3 is
# converted to disc-streamable PCM because the Gekko MP3 path is too expensive.
# Menu + optional all-map soundscapes run here when no single --map BSP is
# staged below (per-map path invokes stage_dod_audio.py with --map itself).
if [[ -z "$MAP_NAME" ]]; then
	AUDIO_ARGS=(--dod "$SRC" --valve "$VALVE" --lean "$LEAN")
	if (( ALL_MAPS_AUDIO )); then
		AUDIO_ARGS+=(--all-maps)
	else
		AUDIO_ARGS+=(--menu-only)
	fi
	if (( CORE_SFX )) || (( FULL_MAP )); then
		AUDIO_ARGS+=(--core-sfx)
	fi
	python3 "$SCRIPT_DIR/stage_dod_audio.py" "${AUDIO_ARGS[@]}"
fi

# Identity / FS markers (HasGameAssets needs marker + maps/ for non-valve).
cp -f "$SRC/liblist.gam" "$LEAN/"
cp -f "$SRC/delta.lst" "$LEAN/" 2>/dev/null || true
cp -f "$VALVE/gfx.wad" "$LEAN/"
# Install team rules before InstallGameRules reads mp_teamplay. The listen
# config runs later, from CHalfLifeMultiplay's constructor, which is too late
# to select the teamplay gamerules class.
cat > "$LEAN/game.cfg" <<'EOF'
mp_teamplay 1
mp_teamlist "Allies;Axis"
mp_defaultteam 0
EOF
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

# CapFaces lean HUD: disc inject (inject_gc_hud_into_bootstrap) downsamples these
# valve sheets into sprites/gc_*.spr inside gamecube-bootstrap.pk3. DoD has no
# retail 320hud*.spr — without this, SPR_Load fails after MAP_READY.
mkdir -p "$LEAN/sprites"
hud_staged=0
for s in 320hud1.spr 320hud2.spr 320_pain.spr 320_train.spr crosshairs.spr; do
	if [[ -f "$VALVE/sprites/$s" ]]; then
		cp -f "$VALVE/sprites/$s" "$LEAN/sprites/"
		hud_staged=$((hud_staged + 1))
	elif [[ -f "$SRC/sprites/$s" ]]; then
		cp -f "$SRC/sprites/$s" "$LEAN/sprites/"
		hud_staged=$((hud_staged + 1))
	fi
done
echo "staged lean HUD sources: $hud_staged sprites (for gc_* bootstrap inject)"

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
	# Complete menu music/UI + this map's soundscape WAVs (case/valve/substitutes).
	MAP_AUDIO_ARGS=(--dod "$SRC" --valve "$VALVE" --lean "$LEAN" --map "$bsp")
	if (( ALL_MAPS_AUDIO )); then
		MAP_AUDIO_ARGS+=(--all-maps)
	fi
	if (( CORE_SFX )) || (( FULL_MAP )); then
		MAP_AUDIO_ARGS+=(--core-sfx)
	fi
	python3 "$SCRIPT_DIR/stage_dod_audio.py" "${MAP_AUDIO_ARGS[@]}"
	# Map-specific + common WADs (skip decals.wad — Host_InitDecals OOM).
	# Do NOT auto-mount sibling map WADs from the BSP "wad" key (e.g. flash
	# lists dod_donner.wad). Each foreign map WAD costs ~6 KiB searchpaths and
	# tipfix proved flash CapFaces drawn=365 without donner; denser maps need
	# that headroom. CapFaces packs supply world textures on -gcworldrender.
	map_wads=("${stem}.wad" hlbasics.wad)
	if (( FULL_MAP )); then
		map_wads+=(dod.wad dod_shared.wad)
	fi
	if command -v python3 >/dev/null 2>&1; then
		while IFS= read -r wad; do
			[[ -n "$wad" ]] || continue
			[[ "$wad" == "decals.wad" ]] && continue
			[[ "$wad" == "halflife.wad" ]] && continue
			# Skip other maps' WADs (dod_donner.wad while staging dod_flash, etc.).
			if [[ "$wad" == dod_*.wad && "$wad" != "${stem}.wad" ]]; then
				echo "skip foreign map wad: $wad (tip searchpaths)"
				continue
			fi
			map_wads+=("$wad")
		done < <(python3 - "$bsp" <<'PY'
import re, struct, sys
from pathlib import Path
b = Path(sys.argv[1]).read_bytes()
off, length = struct.unpack_from("<ii", b, 4)
ents = b[off:off+length].decode("latin1", "replace")
m = re.search(r'"wad"\s+"([^"]+)"', ents)
if not m:
    raise SystemExit
for part in m.group(1).split(";"):
    part = part.strip()
    if not part:
        continue
    name = part.replace("\\", "/").split("/")[-1]
    if name.lower().endswith(".wad"):
        print(name)
PY
)
	fi
	# Drop any leftover foreign map WADs from a prior lean stage.
	shopt -s nullglob
	for leftover in "$LEAN"/dod_*.wad; do
		base=$(basename "$leftover")
		[[ "$base" == "${stem}.wad" ]] && continue
		rm -f "$leftover"
		echo "removed leftover wad: $base"
	done
	shopt -u nullglob
	for wad in "${map_wads[@]}"; do
		for root in "$SRC" "$VALVE"; do
			if [[ -f "$root/$wad" ]]; then
				# Cap giant Valve WADs on lean discs (halflife.wad ~38M).
				sz=$(stat -c%s "$root/$wad" 2>/dev/null || stat -f%z "$root/$wad")
				if [[ "${sz:-0}" -gt $((12 * 1024 * 1024)) ]]; then
					echo "skip oversized wad: $wad ($(du -h "$root/$wad" | awk '{print $1}'))"
					break
				fi
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
	# Map-local DoD rules (classes / bazooka spawn / etc.).
	if [[ -f "$SRC/${stem}.cfg" ]]; then
		cp -f "$SRC/${stem}.cfg" "$LEAN/"
		echo "staged map cfg: ${stem}.cfg"
	fi
	# Per-map skybox first: CapFaces Flipper draws media/sky_<name>*.cmp before
	# world tris. Stage only this map's skyname (6×8 KiB CMPR) — not every DoD
	# sky (searchpaths tip). Prefer prebuilt CMPs; fall back to gfx/env TGAs
	# for the disc builder to encode.
	mkdir -p "$LEAN/media" "$LEAN/gfx/env"
	skyname=$(python3 - "$bsp" <<'PY'
import re, struct, sys
from pathlib import Path
b = Path(sys.argv[1]).read_bytes()
off, length = struct.unpack_from("<ii", b, 4)
ents = b[off:off+length].decode("latin1", "replace")
# Compact BSPs may not keep the entity lump at offset 0 — scan worldspawn.
m = re.search(r'"skyname"\s+"([^"]+)"', ents)
if not m:
    m = re.search(br'"skyname"\s+"([^"]+)"', b)
    print((m.group(1).decode("latin1") if m else "desert").lower())
else:
    print(m.group(1).lower())
PY
)
	skyname=${skyname:-desert}
	sky_staged=0
	CMP_ALL="${DODGC_SKY_CMP_CACHE:-/tmp/dodgc-sky-cmp-all}"
	for side in rt bk lf ft up dn; do
		dst="$LEAN/media/sky_${skyname}${side}.cmp"
		if [[ -f "$CMP_ALL/sky_${skyname}${side}.cmp" ]]; then
			cp -f "$CMP_ALL/sky_${skyname}${side}.cmp" "$dst"
			sky_staged=$((sky_staged + 1))
		elif [[ -f "$SRC/gfx/env/${skyname}${side}.tga" ]]; then
			cp -f "$SRC/gfx/env/${skyname}${side}.tga" "$LEAN/gfx/env/"
		elif [[ -f "$SRC/gfx/env/${skyname}${side}.bmp" ]]; then
			cp -f "$SRC/gfx/env/${skyname}${side}.bmp" "$LEAN/gfx/env/"
		fi
	done
	# Drop leftover skies from a prior map stage (tip searchpaths).
	shopt -s nullglob
	for leftover in "$LEAN/media"/sky_*.cmp; do
		base=$(basename "$leftover")
		case "$base" in
			sky_${skyname}rt.cmp|sky_${skyname}bk.cmp|sky_${skyname}lf.cmp|sky_${skyname}ft.cmp|sky_${skyname}up.cmp|sky_${skyname}dn.cmp) ;;
			*) rm -f "$leftover" ;;
		esac
	done
	shopt -u nullglob
	echo "staged skybox: $skyname ($sky_staged CMPR sides + gfx/env for encode)"

	# Create Game needs maps.lst on disc-only (refresh cannot rewrite).
	printf '%s "Local match"\n' "$stem" > "$LEAN/maps.lst"
	echo "staged maps.lst for $stem"
	if [[ ! -f "$LEAN/listenserver.cfg" ]]; then
		{
			printf 'mp_timelimit 0\nmp_fraglimit 0\n'
			printf 'mp_teamplay 1\nmp_teamlist "Allies;Axis"\n'
			# Mirror retail DoD listen defaults when present.
			[[ -f "$SRC/listenserver.cfg" ]] && cat "$SRC/listenserver.cfg"
			[[ -f "$SRC/${stem}.cfg" ]] && cat "$SRC/${stem}.cfg"
		} > "$LEAN/listenserver.cfg"
		echo "staged listenserver.cfg (DoD mp + map rules)"
	fi

	# CapFaces listen always needs team weapon viewmodels on disc.
	# Retail names: Kar98=v_98k, Thompson=v_tommy.
	mkdir -p "$LEAN/models"
	cap_vm=0
	for m in v_garand.mdl v_mp40.mdl v_98k.mdl v_tommy.mdl \
		v_colt.mdl v_luger.mdl v_amerk.mdl v_spade.mdl \
		p_garand.mdl p_mp40.mdl p_k98.mdl p_tommy.mdl; do
		if [[ -f "$SRC/models/$m" ]]; then
			cp -f "$SRC/models/$m" "$LEAN/models/"
			cap_vm=$((cap_vm + 1))
		fi
	done
	echo "staged CapFaces team weapon MDLs: $cap_vm files"

	if (( FULL_MAP )); then
		echo "==> full-map assets for $stem"
		# Shared DoD WADs (textures beyond the per-map wad).
		for wad in dod.wad dod_shared.wad gfx.wad; do
			if [[ -f "$SRC/$wad" ]] && [[ ! -f "$LEAN/$wad" ]]; then
				cp -f "$SRC/$wad" "$LEAN/"
				echo "staged wad: $wad"
			fi
		done
		# Mapmodels (props / objectives). Cap by size for MEM1/disc budget.
		if [[ -d "$SRC/models/mapmodels" ]]; then
			mkdir -p "$LEAN/models/mapmodels"
			# Prefer names matching the map stem; else copy all under 18 MiB budget.
			copied=0
			budget_bytes=$((18 * 1024 * 1024))
			used=0
			shopt -s nullglob
			for m in "$SRC/models/mapmodels"/*; do
				[[ -f "$m" ]] || continue
				sz=$(wc -c < "$m" | tr -d ' ')
				if (( used + sz > budget_bytes )); then
					continue
				fi
				cp -f "$m" "$LEAN/models/mapmodels/"
				used=$((used + sz))
				copied=$((copied + 1))
			done
			shopt -u nullglob
			echo "staged mapmodels: $copied files ($(numfmt --to=iec "$used" 2>/dev/null || echo "${used}B"))"
		fi
		# Player / weapon / gib stubs used by listen + client render.
		mkdir -p "$LEAN/models"
		for m in \
			player.mdl \
			allied_grunt.mdl axis_grunt.mdl \
			allied_ammo.mdl axis_ammo.mdl brit_ammo.mdl \
			allied_gibs.mdl axis_gibs.mdl \
			null.mdl \
			p_garand.mdl p_k98.mdl p_mp40.mdl p_tommy.mdl \
			p_bar.mdl p_mg34.mdl p_30cal.mdl p_mg42pr.mdl p_bazooka.mdl p_pschreck.mdl \
			p_bren.mdl p_stg44.mdl p_spring.mdl \
			p_colt.mdl p_luger.mdl p_amerk.mdl p_spade.mdl \
			v_garand.mdl v_98k.mdl v_mp40.mdl v_tommy.mdl \
			v_bar.mdl v_mg34.mdl v_30cal.mdl v_mg42.mdl v_bazooka.mdl v_panzerschreck.mdl \
			v_bren.mdl v_mp44.mdl v_spring.mdl v_scoped98k.mdl \
			v_colt.mdl v_luger.mdl v_amerk.mdl v_spade.mdl \
			w_garand.mdl w_k98.mdl w_mp40.mdl w_tommy.mdl
		do
			[[ -f "$SRC/models/$m" ]] && cp -f "$SRC/models/$m" "$LEAN/models/"
		done
		echo "staged CapFaces weapon VMs: $(ls "$LEAN/models"/v_*.mdl 2>/dev/null | wc -l) files"
		# Sprites for HUD / effects (small).
		if [[ -d "$SRC/sprites" ]]; then
			mkdir -p "$LEAN/sprites"
			# Cap: copy hud + common effect sheets only.
			shopt -s nullglob
			for s in "$SRC/sprites"/hud* "$SRC/sprites"/radar* \
				"$SRC/sprites"/scope* "$SRC/sprites"/smoke* \
				"$SRC/sprites"/explode* "$SRC/sprites"/blood* \
				"$SRC/sprites"/laser* "$SRC/sprites"/flame*
			do
				[[ -f "$s" ]] && cp -f "$s" "$LEAN/sprites/"
			done
			shopt -u nullglob
			echo "staged sprites: $(find "$LEAN/sprites" -type f | wc -l) files"
		fi
		# Soundscapes + core SFX already handled by stage_dod_audio.py above.
		echo "full-map stage size: $(du -sh "$LEAN" | awk '{print $1}')"
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
