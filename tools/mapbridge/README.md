# mapbridge quick start
```
# 1. Maya: import maya_export; maya_export.install()  (save -> writes ~/.cache/gsbridge/live.map)
# 2. ceres: python3 watch.py --iso /path/to/slot-disc.iso [--vis --rad]
# self-test without Maya: python3 selftest_map.py /tmp/x.map ; bin/sdHLCSG ... (see watch.py)
```
Design and status: DESIGN.md. Engine hook: engine-livemap.patch (superseded: the poll lives on xash3d-gc branch claude/livemap-poll, cvar gc_livemap, default off); build_compiler.sh builds bin/.
