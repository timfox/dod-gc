#!/usr/bin/env bash
# Per-subsystem static size (text/data/bss) of the dod ELF, from nm, so memory
# changes have reproducible before/after numbers.
#   scripts/report-static-size.sh [ELF] [label]
# Default ELF: $DODGC_ENGINE/OUT/bin/xash-dod. Prints a table and, with
# REPORT_OUT=file, also writes it for diffing (diff -u before.txt after.txt).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"
ELF="${1:-$DODGC_ENGINE/OUT/bin/xash-dod}"
LABEL="${2:-$(dodgc_repo_sha)}"
NM="${DEVKITPRO:-/opt/devkitpro}/devkitPPC/bin/powerpc-eabi-nm"
[[ -x "$NM" ]] || NM="/opt/devkitpro/devkitPPC/bin/powerpc-eabi-nm"
[[ -f "$ELF" ]] || { echo "error: no ELF at $ELF" >&2; exit 1; }
out="$("$NM" -S -C "$ELF" | python3 -I -c '
import sys,re
# subsystem by symbol-name pattern; first match wins
rules=[("bots","Dod_Bot|DodBots|dod_bot|RunBot|AddBot|PlanBots|g_bot"),
("dod match/rules","g_dod_|Dod_|dod_game|dod_runtime|CHalfLifeDoD|dod_round"),
("dod weapons/hud","dod_weapon|DodWeapon|CHudDod|dod_hud|Dod_Hud|DodGC_"),
("studio/viewmodel","studio|gc_studio|g_studio"),
("world/bsp","world_|gc_world|gcmap|mod_known|r_leafkeys|gc_static_|gc_gcmap"),
("video/xfb/probe","vid|gc_probe|gc_tiled|gc_early_xfb|gx_fifo|r_images"),
("server","^sv_?$|sv\\.|svs|gc_listen|gc_singleplayer|sv_"),
("client","^cl$|cls|clgame|^cl_|cl\\."),
("audio","^s_|snd|sentence|gszall")]
agg={}
for line in sys.stdin:
    p=line.split(None,3)
    if len(p)<4: continue
    addr,size,typ,name=p
    if typ not in "bBdDtT": continue
    try: n=int(size,16)
    except ValueError: continue
    kind="bss" if typ in "bB" else ("data" if typ in "dD" else "text")
    sub="other"
    for k,r in rules:
        if re.search(r,name): sub=k;break
    agg.setdefault(sub,{"text":0,"data":0,"bss":0})[kind]+=n
print("%-18s %10s %10s %10s"%("subsystem","text","data","bss"))
tot={"text":0,"data":0,"bss":0}
for k in sorted(agg,key=lambda k:-sum(agg[k].values())):
    v=agg[k]; print("%-18s %10d %10d %10d"%(k,v["text"],v["data"],v["bss"]))
    for t in tot: tot[t]+=v[t]
print("%-18s %10d %10d %10d"%("TOTAL(symbols)",tot["text"],tot["data"],tot["bss"]))
')"
printf '# static size report: %s  elf=%s\n%s\n' "$LABEL" "$ELF" "$out" | tee "${REPORT_OUT:-/dev/null}"
