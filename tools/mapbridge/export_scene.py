"""mayapy (needs a batch Maya; none on the host after 2026 was removed): open a Maya scene and export its .map. The release build no longer calls this.   mayapy export_scene.py <scene.mb> <out.map>"""
import maya.standalone; maya.standalone.initialize()
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import maya.cmds as cmds, maya_export as me
scene, out = sys.argv[1], sys.argv[2]
cmds.file(scene, open=True, force=True)
for m in (me.validate() or []):
    print("[validate] %s: %s" % m)
me.WORKDIR = os.path.dirname(out) or "."
me.UNITS_PER_CM = 1.0
me.export_now(out)
print("EXPORT OK", out)
