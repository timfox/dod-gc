"""tools/vehicle/obj_to_mdl.py winds its triangles as GoldSrc studio models are wound.

Retail studio models (sherman.mdl, panzer.mdl, us-inf.mdl) store every triangle so that
(v1-v0)x(v2-v0) points against its vertex normals. Wound the other way, the engine culls the
outside of each face and the model draws inside-out (the first Panzer IV in game).
"""
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools", "vehicle"))
import obj_to_mdl  # noqa: E402

# A box, every face counter-clockwise seen from outside (the OBJ convention); a hull and a turret group.
BOX_OBJ = """
v -2 -1 0
v 2 -1 0
v 2 1 0
v -2 1 0
v -2 -1 1
v 2 -1 1
v 2 1 1
v -2 1 1
v -0.5 -0.5 1
v 0.5 -0.5 1
v 0.5 0.5 1
v -0.5 0.5 1
v -0.5 -0.5 1.6
v 0.5 -0.5 1.6
v 0.5 0.5 1.6
v -0.5 0.5 1.6
vt 0 0
g hull
f 1/1 4/1 3/1 2/1
f 5/1 6/1 7/1 8/1
f 1/1 2/1 6/1 5/1
f 2/1 3/1 7/1 6/1
f 3/1 4/1 8/1 7/1
f 4/1 1/1 5/1 8/1
g turret
f 13/1 14/1 15/1 16/1
f 9/1 10/1 14/1 13/1
f 10/1 11/1 15/1 14/1
f 11/1 12/1 16/1 15/1
f 12/1 9/1 13/1 16/1
"""


def _triangles(mdl):
	"""(positions, normals) per triangle, in stored order, in model space (bone offsets added)."""
	numbones, boneindex = struct.unpack_from("<ii", mdl, 140)
	bones = []
	for b in range(numbones):
		at = boneindex + b * 112
		parent = struct.unpack_from("<i", mdl, at + 32)[0]
		pos = struct.unpack_from("<3f", mdl, at + 64)
		if parent >= 0:
			pos = tuple(pos[i] + bones[parent][i] for i in range(3))
		bones.append(pos)
	bodyindex = struct.unpack_from("<i", mdl, 208)[0]
	modelindex = struct.unpack_from("<i", mdl, bodyindex + 72)[0]
	(nummesh, meshindex, numverts, vinfo, vindex, numnorms, ninfo, nindex) = struct.unpack_from(
		"<iiiiiiii", mdl, modelindex + 72)
	vbone = mdl[vinfo:vinfo + numverts]
	verts = [struct.unpack_from("<3f", mdl, vindex + 12 * i) for i in range(numverts)]
	verts = [tuple(v[j] + bones[vbone[i]][j] for j in range(3)) for i, v in enumerate(verts)]
	norms = [struct.unpack_from("<3f", mdl, nindex + 12 * i) for i in range(numnorms)]
	out = []
	for m in range(nummesh):
		numtris, triindex = struct.unpack_from("<ii", mdl, meshindex + 20 * m)
		at = triindex
		while True:
			n = struct.unpack_from("<h", mdl, at)[0]
			at += 2
			if n == 0:
				break
			corners = [struct.unpack_from("<4h", mdl, at + 8 * k) for k in range(abs(n))]
			at += 8 * abs(n)
			if n == 3:
				out.append(([verts[c[0]] for c in corners], [norms[c[1]] for c in corners]))
	return out


class WindingTest(unittest.TestCase):
	def test_triangles_wind_against_their_normals_like_retail(self):
		# The skin is not what this checks, and the CI runner has no Pillow: a flat 8x8 one.
		real_quantise = obj_to_mdl.quantise
		obj_to_mdl.quantise = lambda png, size: (bytes(size * size), bytes(768))
		self.addCleanup(setattr, obj_to_mdl, "quantise", real_quantise)
		with tempfile.TemporaryDirectory() as tmp:
			obj = os.path.join(tmp, "box.obj")
			skin = os.path.join(tmp, "skin.png")
			with open(obj, "w") as fh:
				fh.write(BOX_OBJ)
			parts, vs, vts, _ = obj_to_mdl.read_obj(obj)
			vs = obj_to_mdl.to_goldsrc(vs, "z", "x")
			mdl, ntris = obj_to_mdl.build(parts, vs, vts, skin, "box", 100.0, 8, {"turret"})
		tris = _triangles(mdl)
		self.assertEqual(len(tris), ntris)
		self.assertGreater(ntris, 0)
		for pts, norms in tris:
			e1 = [pts[1][j] - pts[0][j] for j in range(3)]
			e2 = [pts[2][j] - pts[0][j] for j in range(3)]
			geo = obj_to_mdl._cross(e1, e2)
			nsum = [sum(n[j] for n in norms) for j in range(3)]
			self.assertLess(obj_to_mdl._dot(geo, nsum), 0.0)


if __name__ == "__main__":
	unittest.main()
