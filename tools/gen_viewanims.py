#!/usr/bin/env python3
"""Which sequence of each retail DoD view model plays for which weapon animation.

The game code sends generic animation numbers (idle 0, fire 1-3, reload 4, draw 5, ...) but every retail view
model lays its sequences out differently (the Garand's reload is sequence 5, the Thompson's sequence 1 is its
reload, the grenade's draw is 1), so a fixed number plays the wrong animation. MODELS below names, per model, the
sequence label that serves each role; this tool resolves the labels against the retail models and writes
src/dod_viewanims.h, which dod_weapons.c uses to translate.

  tools/gen_viewanims.py --models <dod>/models --write src/dod_viewanims.h
  tools/gen_viewanims.py --models <dod>/models --check src/dod_viewanims.h   # the header is what MODELS renders
  tools/gen_viewanims.py --models <dod>/models --verify         # every label exists in the retail model

Roles: idle, idle_empty, fire (up to three variants), fire_empty, reload, draw, holster, pinpull (grenades).
A missing role is -1 and the caller falls back (idle_empty -> idle, fire_empty -> fire, others -> idle).
"""
import argparse
import struct
import sys
from pathlib import Path

ROLES = ("idle", "idle_empty", "fire", "fire_empty", "reload", "draw", "holster", "pinpull")

# Per model: role -> label, or a list of labels (fire variants). Labels are what the retail model calls them.
SEMI = {"idle": "idle", "idle_empty": "empty_idle"}
MODELS = {
    "v_garand": {"idle": "idle", "idle_empty": "empty_idle", "fire": ["shoot1", "shoot2", "shoot3"], "fire_empty": "shoot_empty",
                 "reload": "reload", "draw": "draw"},
    "v_spring": {"idle": "idle", "idle_empty": "empty_idle", "fire": ["shoot1", "shoot2"], "reload": "reload", "draw": "draw"},
    "v_98k": {"idle": "idle", "fire": ["shoot"], "reload": "reload", "draw": "draw"},
    "v_scoped98k": {"idle": "idle", "fire": ["shoot"], "reload": "reload", "draw": "draw"},
    "v_tommy": {"idle": "idle", "idle_empty": "empty_idle", "fire": ["shoot1", "shoot2"], "reload": "reload", "draw": "draw"},
    "v_mp40": {"idle": "idle", "idle_empty": "empty_idle", "fire": ["shoot1", "shoot2"], "reload": "reload", "draw": "draw"},
    "v_mp44": {"idle": "idle", "idle_empty": "empty_idle", "fire": ["shoot1", "shoot2"], "reload": "reload", "draw": "draw"},
    "v_greasegun": {"idle": "idle", "idle_empty": "empty_idle", "fire": ["shoot1", "shoot2"], "reload": "reload", "draw": "draw"},
    "v_m1carbine": {"idle": "idle", "fire": ["shoot"], "reload": "reload", "draw": "draw"},
    "v_sten": {"idle": "idle", "fire": ["shoot"], "reload": "reload", "draw": "draw"},
    "v_k43": {"idle": "idle", "idle_empty": "empty_idle", "fire": ["shoot1", "shoot2"], "reload": "reload", "draw": "draw"},
    # bipod guns: the bipod-up set (the guns are not deployed in these animations yet)
    "v_bar": {"idle": "up_idle", "fire": ["up_shoot"], "reload": "up_reload", "draw": "up_draw"},
    "v_bren": {"idle": "up_idle", "fire": ["up_shoot"], "reload": "up_reload", "draw": "up_draw"},
    "v_fg42": {"idle": "up_idle", "fire": ["up_shoot"], "reload": "up_reload", "draw": "up_draw"},
    "v_30cal": {"idle": "upidle", "idle_empty": "upidleempty", "fire": ["upshoot"], "reload": "reload", "draw": "upidle"},
    "v_mg42": {"idle": "upidle", "idle_empty": "upidleempty", "fire": ["upshoot"], "reload": "reload", "draw": "upidle"},
    "v_enfield": {"idle": "idle", "fire": ["shoot"], "reload": "reload", "draw": "draw"},
    "v_enfield_scoped": {"idle": "idle", "fire": ["shoot"], "reload": "reload", "draw": "draw"},
    "v_colt": {"idle": "idle", "idle_empty": "empty_idle", "fire": ["shoot1", "shoot2"], "fire_empty": "shoot_empty",
               "reload": "reload", "draw": "draw"},
    "v_luger": {"idle": "idle", "idle_empty": "empty_idle", "fire": ["shoot"], "fire_empty": "shoot_empty", "reload": "reload",
                "draw": "draw"},
    "v_webley": {"idle": "idle", "fire": ["shoot"], "reload": "reload", "draw": "draw"},
    "v_bazooka": {"idle": "idle", "fire": ["launch"], "reload": "reload_idle", "draw": "draw"},
    "v_panzerschreck": {"idle": "idle", "fire": ["launch"], "reload": "reload_idle", "draw": "draw"},
    "v_piat": {"idle": "idle", "idle_empty": "idle_empty", "fire": ["launch"], "reload": "reload_idle", "draw": "draw"},
    # melee: the two slashes are the fire variants
    "v_amerk": {"idle": "idle", "fire": ["slash1", "slash2"], "draw": "draw"},
    "v_spade": {"idle": "idle", "fire": ["slash1", "slash2"], "draw": "draw"},
    "v_fairbairn": {"idle": "idle", "fire": ["slash1", "slash2"], "draw": "draw"},
    "v_paraknife": {"idle": "idle", "fire": ["slash1", "slash2"], "draw": "draw"},
    # grenades: pin pull, throw (fire), holster
    "v_grenade": {"idle": "idle", "fire": ["throw"], "draw": "draw", "holster": "holster", "pinpull": "pinpull"},
    "v_stick": {"idle": "idle", "fire": ["throw"], "draw": "draw", "holster": "holster", "pinpull": "pinpull"},
    "v_mills": {"idle": "idle", "fire": ["throw"], "draw": "draw", "holster": "holster", "pinpull": "pinpull"},
}


def sequence_labels(path):
    """The sequence labels of a GoldSrc studio model, in order (studiohdr: numseq at 164, seqindex at 168)."""
    data = Path(path).read_bytes()
    if data[:4] != b"IDST":
        raise ValueError(f"{path}: not a studio model")
    numseq, seqindex = struct.unpack_from("<ii", data, 164)
    return [data[seqindex + i * 176:seqindex + i * 176 + 32].split(b"\0")[0].decode("latin1") for i in range(numseq)]


def resolve(model, spec, labels):
    """role -> index (fire -> list of up to 3 indexes), -1 when the model has no such role."""
    out = {}
    for role in ROLES:
        want = spec.get(role)
        if want is None:
            out[role] = [] if role == "fire" else -1
            continue
        names = want if isinstance(want, list) else [want]
        idx = []
        for name in names:
            if name not in labels:
                raise ValueError(f"{model}: no sequence {name!r} for {role} (has {labels})")
            idx.append(labels.index(name))
        out[role] = idx if role == "fire" else idx[0]
    return out


def render(resolved):
    lines = ["/* Generated by tools/gen_viewanims.py from the retail DoD view models. Do not edit.",
             " * model, idle, idle_empty, fire count, fire[3], fire_empty, reload, draw, holster, pinpull (-1 = none). */"]
    for model in sorted(resolved):
        r = resolved[model]
        fire = r["fire"] + [-1] * (3 - len(r["fire"]))
        lines.append('\t{ "models/%s.mdl", %d, %d, %d, { %d, %d, %d }, %d, %d, %d, %d, %d },' % (
            model, r["idle"], r["idle_empty"], len(r["fire"]), fire[0], fire[1], fire[2], r["fire_empty"], r["reload"], r["draw"],
            r["holster"], r["pinpull"]))
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--models", type=Path, help="the retail dod/models directory")
    ap.add_argument("--write", type=Path, help="write the C table here")
    ap.add_argument("--check", type=Path, help="exit 1 when this header is not what MODELS renders (needs --models)")
    ap.add_argument("--verify", action="store_true", help="every label of every model exists in the retail model")
    args = ap.parse_args(argv)
    if not args.models:
        ap.error("--models is required")
    resolved = {}
    for model, spec in MODELS.items():
        resolved[model] = resolve(model, spec, sequence_labels(args.models / f"{model}.mdl"))
    text = render(resolved)
    if args.write:
        args.write.write_text(text)
        print(f"wrote {args.write}: {len(resolved)} models")
    if args.check and args.check.read_text() != text:
        print(f"{args.check} is out of date; run with --write", file=sys.stderr)
        return 1
    if args.verify:
        print(f"{len(resolved)} models, every label found")
    return 0


if __name__ == "__main__":
    sys.exit(main())
