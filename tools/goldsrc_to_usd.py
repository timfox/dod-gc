#!/usr/bin/env python3
"""Convert GoldSrc / Day of Defeat assets to USD so they open in Maya (mayaUsd).

  goldsrc_to_usd.py mdl  model.mdl [model2.mdl ...] -o OUTDIR [--no-anims]
  goldsrc_to_usd.py spr  sprite.spr ...             -o OUTDIR
  goldsrc_to_usd.py bsp  map.bsp ... --wad a.wad [--wad b.wad]  -o OUTDIR
  goldsrc_to_usd.py all  /path/to/dod               -o OUTDIR   (everything)

Reads only; the source files are never modified. Requires: pip install usd-core numpy pillow

MDL  -> <name>/<name>.usdc  UsdSkel: one Mesh per body-part sub-model (rigid
        single-joint skin weights), Skeleton with bind/rest transforms, one
        SkelAnimation per sequence (named after the sequence label), materials
        with PNG textures. Textures in <name>/textures/*.png. Models that keep
        their textures in nameT.mdl are handled automatically.
SPR  -> <name>/<name>.usdc  a textured quad plus <name>/frames/frame_NN.png.
BSP  -> <name>.usdc  world + brush entities as meshes per texture, with PNG
        textures from the BSP itself or the given WADs (WAD3).

Units: GoldSrc units are about one inch; stage metersPerUnit is 0.0254, upAxis
is Z (GoldSrc native); Maya's USD import converts to its own up axis.
Quirks kept as customData on the prims (bone controllers, events, attachments,
hitboxes, skin families, texture flags) so nothing is silently lost.
"""
import argparse
import json
import math
import os
import re
import struct
import sys

import numpy as np
from PIL import Image
from pxr import Gf, Sdf, Usd, UsdGeom, UsdShade, UsdSkel, Vt

# ---------------------------------------------------------------- helpers

def cstr(b):
	return b.split(b"\0", 1)[0].decode("latin-1")


def safe(name):
	s = re.sub(r"[^A-Za-z0-9_]", "_", name)
	if not s or s[0].isdigit():
		s = "_" + s
	return s


def new_stage(path, up="Z"):
	stage = Usd.Stage.CreateNew(path)
	UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.z if up == "Z" else UsdGeom.Tokens.y)
	UsdGeom.SetStageMetersPerUnit(stage, 0.0254)
	return stage


def palette_png(pix, pal, w, h, path, masked):
	idx = np.frombuffer(pix, dtype=np.uint8).reshape(h, w)
	p = np.frombuffer(pal, dtype=np.uint8).reshape(256, 3)
	rgb = p[idx]
	if masked:
		a = np.where(idx == 255, 0, 255).astype(np.uint8)
		img = Image.fromarray(np.dstack([rgb, a]), "RGBA")
	else:
		img = Image.fromarray(rgb, "RGB")
	img.save(path)


def make_material(stage, root, name, tex_rel, masked, additive=False, chrome=False, flags=0):
	mat = UsdShade.Material.Define(stage, "%s/Materials/%s" % (root, safe(name)))
	sh = UsdShade.Shader.Define(stage, "%s/Surface" % mat.GetPath())
	sh.CreateIdAttr("UsdPreviewSurface")
	sh.CreateInput("roughness", Sdf.ValueTypeNames.Float).Set(1.0)
	sh.CreateInput("metallic", Sdf.ValueTypeNames.Float).Set(0.0)
	st = UsdShade.Shader.Define(stage, "%s/st" % mat.GetPath())
	st.CreateIdAttr("UsdPrimvarReader_float2")
	st.CreateInput("varname", Sdf.ValueTypeNames.Token).Set("st")
	tx = UsdShade.Shader.Define(stage, "%s/tex" % mat.GetPath())
	tx.CreateIdAttr("UsdUVTexture")
	tx.CreateInput("file", Sdf.ValueTypeNames.Asset).Set(Sdf.AssetPath(tex_rel))
	tx.CreateInput("st", Sdf.ValueTypeNames.Float2).ConnectToSource(st.CreateOutput("result", Sdf.ValueTypeNames.Float2))
	tx.CreateInput("wrapS", Sdf.ValueTypeNames.Token).Set("repeat")
	tx.CreateInput("wrapT", Sdf.ValueTypeNames.Token).Set("repeat")
	tx.CreateInput("sourceColorSpace", Sdf.ValueTypeNames.Token).Set("sRGB")
	tx.CreateOutput("rgb", Sdf.ValueTypeNames.Float3)
	sh.CreateInput("diffuseColor", Sdf.ValueTypeNames.Color3f).ConnectToSource(tx.GetOutput("rgb"))
	if masked:
		tx.CreateOutput("a", Sdf.ValueTypeNames.Float)
		sh.CreateInput("opacity", Sdf.ValueTypeNames.Float).ConnectToSource(tx.GetOutput("a"))
		sh.CreateInput("opacityThreshold", Sdf.ValueTypeNames.Float).Set(0.5)
	mat.CreateSurfaceOutput().ConnectToSource(sh.CreateOutput("surface", Sdf.ValueTypeNames.Token))
	mat.GetPrim().SetCustomDataByKey("goldsrc:textureFlags", int(flags))
	if chrome:
		mat.GetPrim().SetCustomDataByKey("goldsrc:chrome", True)
	if additive:
		mat.GetPrim().SetCustomDataByKey("goldsrc:additive", True)
	return mat


# ---------------------------------------------------------------- MDL

def angle_quat(a):
	sr, cr = math.sin(a[0] * .5), math.cos(a[0] * .5)
	sp, cp = math.sin(a[1] * .5), math.cos(a[1] * .5)
	sy, cy = math.sin(a[2] * .5), math.cos(a[2] * .5)
	x = sr * cp * cy - cr * sp * sy
	y = cr * sp * cy + sr * cp * sy
	z = cr * cp * sy - sr * sp * cy
	w = cr * cp * cy + sr * sp * sy
	return (w, x, y, z)


def quat_matrix(q, t):
	w, x, y, z = q
	m = np.eye(4)
	m[0, 0] = 1 - 2 * (y * y + z * z); m[1, 0] = 2 * (x * y + w * z); m[2, 0] = 2 * (x * z - w * y)
	m[0, 1] = 2 * (x * y - w * z); m[1, 1] = 1 - 2 * (x * x + z * z); m[2, 1] = 2 * (y * z + w * x)
	m[0, 2] = 2 * (x * z + w * y); m[1, 2] = 2 * (y * z - w * x); m[2, 2] = 1 - 2 * (x * x + y * y)
	m[:3, 3] = t
	return m


def gf_matrix(m):
	"""numpy column-vector 4x4 -> Gf.Matrix4d (USD row-vector convention)."""
	return Gf.Matrix4d(*[float(v) for v in m.T.flatten()])


class Mdl:
	def __init__(self, path):
		self.path = path
		self.name = os.path.splitext(os.path.basename(path))[0]
		d = open(path, "rb").read()
		self.d = d
		if d[:4] != b"IDST" or struct.unpack_from("<i", d, 4)[0] != 10:
			raise ValueError("not a GoldSrc studio v10 model")
		(self.length,) = struct.unpack_from("<i", d, 72)
		f = struct.unpack_from("<30i", d, 140)
		(self.numbones, self.boneindex, self.numbc, self.bcindex, self.numhb, self.hbindex,
		 self.numseq, self.seqindex, self.numseqgroups, self.seqgroupindex, self.numtex,
		 self.texindex, self.texdata, self.numskinref, self.numskinfam, self.skinindex,
		 self.numbp, self.bpindex, self.numatt, self.attindex, _a, _b, _c, _e,
		 self.numtrans, self.transindex) = f[:26]
		self.td = d  # texture data source
		if self.numtex == 0:
			tp = os.path.join(os.path.dirname(path), self.name + "T.mdl")
			if os.path.exists(tp):
				self.td = open(tp, "rb").read()
				f2 = struct.unpack_from("<30i", self.td, 140)
				self.numtex, self.texindex, self.texdata = f2[10], f2[11], f2[12]
		self._bones()

	def _bones(self):
		d = self.d
		self.bones = []
		for i in range(self.numbones):
			o = self.boneindex + i * 112
			name = cstr(d[o:o + 32])
			parent, flags = struct.unpack_from("<ii", d, o + 32)
			bc = struct.unpack_from("<6i", d, o + 40)
			val = struct.unpack_from("<6f", d, o + 64)
			sc = struct.unpack_from("<6f", d, o + 88)
			self.bones.append(dict(name=name, parent=parent, flags=flags, bc=bc, val=val, scale=sc))
		# unique joint names for USD
		seen = {}
		for b in self.bones:
			n = safe(b["name"])
			seen[n] = seen.get(n, 0) + 1
			b["usd"] = n if seen[n] == 1 else "%s_%d" % (n, seen[n])
		for i, b in enumerate(self.bones):
			b["path"] = (self.bones[b["parent"]]["path"] + "/" if b["parent"] >= 0 else "") + b["usd"]
		self.rest_local = []
		self.world = []
		for i, b in enumerate(self.bones):
			m = quat_matrix(angle_quat(b["val"][3:6]), b["val"][:3])
			self.rest_local.append(m)
			self.world.append(self.world[b["parent"]] @ m if b["parent"] >= 0 else m)

	# --- textures
	def textures(self):
		out = []
		for i in range(self.numtex):
			o = self.texindex + i * 80
			name = cstr(self.td[o:o + 64])
			flags, w, h, idx = struct.unpack_from("<4i", self.td, o + 64)
			pix = self.td[idx:idx + w * h]
			pal = self.td[idx + w * h:idx + w * h + 768]
			out.append(dict(name=name, flags=flags, w=w, h=h, pix=pix, pal=pal))
		return out

	def skins(self):
		n = self.numskinref * self.numskinfam
		v = struct.unpack_from("<%dh" % n, self.d, self.skinindex) if n else ()
		return [list(v[i * self.numskinref:(i + 1) * self.numskinref]) for i in range(self.numskinfam)]

	# --- geometry
	def bodyparts(self):
		d = self.d
		parts = []
		for i in range(self.numbp):
			o = self.bpindex + i * 76
			name = cstr(d[o:o + 64])
			nm, base, mi = struct.unpack_from("<3i", d, o + 64)
			models = []
			for j in range(nm):
				models.append(self._model(mi + j * 112))
			parts.append(dict(name=name, models=models))
		return parts

	def _model(self, o):
		d = self.d
		name = cstr(d[o:o + 64])
		(_t, _r, nmesh, meshidx, nverts, vinfo, vidx, nnorms, ninfo, nidx, _ng, _gi) = struct.unpack_from("<ifiiiiiiiiii", d, o + 64)[:12]
		verts = np.frombuffer(d, dtype="<f4", count=nverts * 3, offset=vidx).reshape(-1, 3).astype(np.float64)
		vbone = np.frombuffer(d, dtype=np.uint8, count=nverts, offset=vinfo).astype(int)
		norms = np.frombuffer(d, dtype="<f4", count=nnorms * 3, offset=nidx).reshape(-1, 3).astype(np.float64)
		nbone = np.frombuffer(d, dtype=np.uint8, count=nnorms, offset=ninfo).astype(int)
		wv = np.empty_like(verts)
		for b in set(vbone.tolist()):
			sel = vbone == b
			wv[sel] = verts[sel] @ self.world[b][:3, :3].T + self.world[b][:3, 3]
		wn = np.empty_like(norms)
		for b in set(nbone.tolist()):
			sel = nbone == b
			wn[sel] = norms[sel] @ self.world[b][:3, :3].T
		meshes = []
		for k in range(nmesh):
			mo = meshidx + k * 20
			ntri, triidx, skinref, _nn, _ni = struct.unpack_from("<5i", d, mo)
			meshes.append(dict(tris=self._tris(triidx, ntri), skinref=skinref))
		return dict(name=name, verts=wv, vbone=vbone, norms=wn, meshes=meshes)

	def _tris(self, p, ntri):
		d = self.d
		out = []  # (vi, ni, s, t) triples
		while True:
			(n,) = struct.unpack_from("<h", d, p)
			p += 2
			if n == 0:
				break
			fan = n < 0
			n = abs(n)
			vs = np.frombuffer(d, dtype="<i2", count=n * 4, offset=p).reshape(n, 4).astype(int)
			p += n * 8
			if fan:
				for i in range(1, n - 1):
					out.append((vs[0], vs[i], vs[i + 1]))
			else:
				for i in range(n - 2):
					out.append((vs[i], vs[i + 1], vs[i + 2]) if i % 2 == 0 else (vs[i + 1], vs[i], vs[i + 2]))
		return out

	# --- animation
	def seqs(self):
		d = self.d
		out = []
		for i in range(self.numseq):
			o = self.seqindex + i * 176
			label = cstr(d[o:o + 32])
			fps, flags, act, actw, nev, evi, nfr = struct.unpack_from("<fiiiiii", d, o + 32)
			numblends, animindex = struct.unpack_from("<ii", d, o + 120)
			seqgroup = struct.unpack_from("<i", d, o + 156)[0]
			events = []
			for e in range(nev):
				eo = evi + e * 76
				fr, ev, ty = struct.unpack_from("<3i", d, eo)
				events.append((fr, ev, cstr(d[eo + 12:eo + 76])))
			out.append(dict(label=label, fps=fps, flags=flags, activity=act, nframes=nfr,
			                numblends=numblends, animindex=animindex, seqgroup=seqgroup, events=events))
		return out

	def seqgroup_data(self, g):
		if g == 0:
			return self.d
		if not hasattr(self, "_sg"):
			self._sg = {}
		if g not in self._sg:
			p = os.path.join(os.path.dirname(self.path), "%s%02d.mdl" % (self.name, g))
			self._sg[g] = open(p, "rb").read() if os.path.exists(p) else None
		return self._sg[g]

	def anim_frames(self, seq, blend=0):
		"""Return (nframes, trans[nf,nb,3], quat[nf,nb,4 wxyz]) or None."""
		data = self.seqgroup_data(seq["seqgroup"])
		if data is None:
			return None
		nf = max(seq["nframes"], 1)
		nb = self.numbones
		base = seq["animindex"] + blend * nb * 12
		trans = np.zeros((nf, nb, 3))
		quat = np.zeros((nf, nb, 4))
		for b, bone in enumerate(self.bones):
			offs = struct.unpack_from("<6H", data, base + b * 12)
			vals = np.zeros((nf, 6))
			for c in range(6):
				if offs[c] == 0:
					vals[:, c] = bone["val"][c]
					continue
				ptr = base + b * 12 + offs[c]
				chan = self._decode(data, ptr, nf)
				vals[:, c] = bone["val"][c] + chan * bone["scale"][c]
			for f in range(nf):
				trans[f, b] = vals[f, :3]
				quat[f, b] = angle_quat(vals[f, 3:6])
		return nf, trans, quat

	@staticmethod
	def _decode(data, ptr, nf):
		out = np.zeros(nf)
		# expand run-length: walk spans
		f = 0
		while f < nf:
			valid, total = data[ptr], data[ptr + 1]
			if total == 0:
				break
			vals = struct.unpack_from("<%dh" % valid, data, ptr + 2) if valid else (0,)
			for k in range(total):
				if f >= nf:
					break
				out[f] = vals[k] if k < valid else vals[-1]
				f += 1
			ptr += 2 + valid * 2
		return out


def ground_shift(m, tr, qu, foot_ids, rest_foot):
	"""Z shift that puts the lowest foot bone of frame 0 at the rest-pose foot height."""
	W = []
	for b, bone in enumerate(m.bones):
		loc = quat_matrix(tuple(qu[0, b]), tr[0, b])
		W.append(W[bone["parent"]] @ loc if bone["parent"] >= 0 else loc)
	return rest_foot - min(W[i][2, 3] for i in foot_ids)


def convert_mdl(path, outdir, anims=True, log=print):
	m = Mdl(path)
	root_dir = os.path.join(outdir, safe(m.name))
	tex_dir = os.path.join(root_dir, "textures")
	os.makedirs(tex_dir, exist_ok=True)
	stage = new_stage(os.path.join(root_dir, safe(m.name) + ".usdc"))
	rname = safe(m.name)
	root = UsdSkel.Root.Define(stage, "/" + rname)
	stage.SetDefaultPrim(root.GetPrim())
	rp = "/" + rname
	# skeleton
	skel = UsdSkel.Skeleton.Define(stage, rp + "/Skeleton")
	joints = [b["path"] for b in m.bones]
	skel.CreateJointsAttr(joints)
	skel.CreateJointNamesAttr([b["name"] for b in m.bones])
	skel.CreateBindTransformsAttr([gf_matrix(w) for w in m.world])
	skel.CreateRestTransformsAttr([gf_matrix(r) for r in m.rest_local])
	skel.GetPrim().SetCustomDataByKey("goldsrc:boneControllers", json.dumps(
		{b["name"]: list(b["bc"]) for b in m.bones if any(c >= 0 for c in b["bc"])}))
	# textures + materials
	texs = m.textures()
	mats = []
	for i, t in enumerate(texs):
		fn = "%02d_%s.png" % (i, safe(os.path.splitext(t["name"])[0]))
		masked = bool(t["flags"] & 0x40)
		palette_png(t["pix"], t["pal"], t["w"], t["h"], os.path.join(tex_dir, fn), masked)
		mats.append(make_material(stage, rp, "%02d_%s" % (i, os.path.splitext(t["name"])[0]),
		                          "./textures/" + fn, masked, bool(t["flags"] & 0x20), bool(t["flags"] & 2), t["flags"]))
	skins = m.skins()
	fam0 = skins[0] if skins else list(range(len(texs)))
	if len(skins) > 1:
		root.GetPrim().SetCustomDataByKey("goldsrc:skinFamilies", json.dumps([list(map(int, s)) for s in skins]))
	# meshes
	geo = UsdGeom.Xform.Define(stage, rp + "/Geometry")
	nmesh = 0
	for bi, bp in enumerate(m.bodyparts()):
		bpx = UsdGeom.Xform.Define(stage, "%s/Geometry/%s" % (rp, safe(bp["name"])))
		for mi, mod in enumerate(bp["models"]):
			if not mod["meshes"]:
				continue
			mp = "%s/Geometry/%s/%s_%d" % (rp, safe(bp["name"]), safe(mod["name"] or "model"), mi)
			mesh = UsdGeom.Mesh.Define(stage, mp)
			mesh.CreateOrientationAttr(UsdGeom.Tokens.leftHanded)
			mesh.CreateSubdivisionSchemeAttr(UsdGeom.Tokens.none)
			pts = mod["verts"]
			fvi, uvs, nrm, subs = [], [], [], {}
			ntri = 0
			for me in mod["meshes"]:
				tex = fam0[me["skinref"]] if me["skinref"] < len(fam0) else 0
				tw, th = (texs[tex]["w"], texs[tex]["h"]) if tex < len(texs) else (1, 1)
				for tri in me["tris"]:
					for v in tri:
						fvi.append(int(v[0]))
						uvs.append((v[2] / tw, 1.0 - v[3] / th))
						nrm.append(tuple(mod["norms"][v[1]]) if v[1] < len(mod["norms"]) else (0, 0, 1))
					subs.setdefault(tex, []).append(ntri)
					ntri += 1
			mesh.CreatePointsAttr(Vt.Vec3fArray.FromNumpy(pts.astype(np.float32)))
			mesh.CreateFaceVertexCountsAttr(Vt.IntArray([3] * ntri))
			mesh.CreateFaceVertexIndicesAttr(Vt.IntArray(fvi))
			pv = UsdGeom.PrimvarsAPI(mesh)
			st = pv.CreatePrimvar("st", Sdf.ValueTypeNames.TexCoord2fArray, UsdGeom.Tokens.faceVarying)
			st.Set(Vt.Vec2fArray([Gf.Vec2f(float(a), float(b)) for a, b in uvs]))
			mesh.CreateNormalsAttr(Vt.Vec3fArray([Gf.Vec3f(*[float(c) for c in n]) for n in nrm]))
			mesh.SetNormalsInterpolation(UsdGeom.Tokens.faceVarying)
			for tex, faces in subs.items():
				sub = UsdShade.MaterialBindingAPI.Apply(
					UsdGeom.Subset.Define(stage, mp + "/tex_%02d" % tex).GetPrim())
				ss = UsdGeom.Subset(sub.GetPrim())
				ss.CreateElementTypeAttr("face")
				ss.CreateIndicesAttr(Vt.IntArray(faces))
				ss.CreateFamilyNameAttr("materialBind")
				if tex < len(mats):
					sub.Bind(mats[tex])
			# skinning (rigid): one joint per vertex
			b = UsdSkel.BindingAPI.Apply(mesh.GetPrim())
			b.CreateSkeletonRel().SetTargets([skel.GetPath()])
			b.CreateJointIndicesPrimvar(False, 1).Set(Vt.IntArray(mod["vbone"].astype(int).tolist()))
			b.CreateJointWeightsPrimvar(False, 1).Set(Vt.FloatArray([1.0] * len(pts)))
			b.CreateGeomBindTransformAttr(Gf.Matrix4d(1))
			if mi != 0:  # default body group only; others hidden (still editable)
				mesh.CreateVisibilityAttr(UsdGeom.Tokens.invisible)
			nmesh += 1
	b = UsdSkel.BindingAPI.Apply(root.GetPrim())
	# attachments + hitboxes as metadata / locators
	d = m.d
	att = UsdGeom.Xform.Define(stage, rp + "/Attachments")
	for i in range(m.numatt):
		o = m.attindex + i * 88
		name = cstr(d[o:o + 32])
		_t, bone = struct.unpack_from("<ii", d, o + 32)
		org = struct.unpack_from("<3f", d, o + 40)
		pos = m.world[bone] @ np.array([*org, 1.0]) if 0 <= bone < m.numbones else np.array([*org, 1.0])
		a = UsdGeom.Xform.Define(stage, "%s/Attachments/att%d_%s" % (rp, i, safe(name)))
		a.AddTranslateOp().Set(Gf.Vec3d(*[float(x) for x in pos[:3]]))
		a.GetPrim().SetCustomDataByKey("goldsrc:bone", m.bones[bone]["name"] if 0 <= bone < m.numbones else "")
	# animations
	nanim = 0
	if anims and m.numseq:
		seqs = m.seqs()
		adir = UsdGeom.Scope.Define(stage, rp + "/Animations")
		# Player animations are authored around the hull centre (feet about 36 units
		# below the origin). Shift each sequence so its feet stand on the origin like
		# the rest pose does.
		ground = "/player/" in path.replace("\\", "/") or m.name == "player"
		foot_ids = [i for i, b in enumerate(m.bones) if re.search(r"foot|toe", b["name"], re.I)]
		rest_foot = min(m.world[i][2, 3] for i in foot_ids) if foot_ids else 0.0
		ground = ground and bool(foot_ids)
		stage.SetTimeCodesPerSecond(30.0)
		first = None
		used = set()
		for si, s in enumerate(seqs):
			for bl in range(max(s["numblends"], 1)):
				r = m.anim_frames(s, bl)
				if r is None:
					continue
				nf, tr, qu = r
				if ground:
					tr = tr.copy()
					tr[:, [i for i, b in enumerate(m.bones) if b["parent"] < 0], 2] += ground_shift(m, tr, qu, foot_ids, rest_foot)
				nm = safe(s["label"]) + ("_b%d" % bl if bl else "")
				if nm in used:
					nm = "%s_%d" % (nm, si)
				used.add(nm)
				ap = "%s/Animations/%s" % (rp, nm)
				a = UsdSkel.Animation.Define(stage, ap)
				a.CreateJointsAttr(joints)
				fps = s["fps"] or 30.0
				k = 30.0 / fps
				ta = a.CreateTranslationsAttr()
				ra = a.CreateRotationsAttr()
				sa = a.CreateScalesAttr()
				ones = Vt.Vec3hArray([Gf.Vec3h(1, 1, 1)] * m.numbones)
				for f in range(nf):
					t = f * k
					ta.Set(Vt.Vec3fArray.FromNumpy(tr[f].astype(np.float32)), t)
					ra.Set(Vt.QuatfArray([Gf.Quatf(float(q[0]), Gf.Vec3f(float(q[1]), float(q[2]), float(q[3]))) for q in qu[f]]), t)
				sa.Set(ones)
				p = a.GetPrim()
				p.SetCustomDataByKey("goldsrc:fps", float(fps))
				p.SetCustomDataByKey("goldsrc:frames", int(nf))
				p.SetCustomDataByKey("goldsrc:looping", bool(s["flags"] & 1))
				p.SetCustomDataByKey("goldsrc:activity", int(s["activity"]))
				if s["events"]:
					p.SetCustomDataByKey("goldsrc:events", json.dumps([{"frame": e[0], "event": e[1], "options": e[2]} for e in s["events"]]))
				if first is None:
					first = ap
				nanim += 1
		stage.SetStartTimeCode(0)
		if first:
			stage.SetEndTimeCode(max(0, (m.anim_frames(seqs[0])[0] - 1) * 30.0 / (seqs[0]["fps"] or 30)))
			b.CreateAnimationSourceRel().SetTargets([Sdf.Path(first)])
	stage.GetRootLayer().Save()
	log("  %s: %d bones, %d meshes, %d textures, %d animations" % (m.name, m.numbones, nmesh, len(texs), nanim))
	return dict(bones=m.numbones, meshes=nmesh, textures=len(texs), anims=nanim)


# ---------------------------------------------------------------- SPR

def convert_spr(path, outdir, log=print):
	d = open(path, "rb").read()
	if d[:4] != b"IDSP":
		raise ValueError("not a sprite")
	ver, stype, tex, rad, mw, mh, nframes, beam, sync = struct.unpack_from("<ii i f iii f i", d, 4)
	o = 40
	(npal,) = struct.unpack_from("<h", d, o)
	pal = d[o + 2:o + 2 + npal * 3]
	pal = pal + bytes(768 - len(pal))
	o += 2 + npal * 3
	name = safe(os.path.splitext(os.path.basename(path))[0])
	fd = os.path.join(outdir, name, "frames")
	os.makedirs(fd, exist_ok=True)
	n = 0
	first = None
	for i in range(nframes):
		(grp,) = struct.unpack_from("<i", d, o)
		if grp:  # frame group: take the first frame
			(cnt,) = struct.unpack_from("<i", d, o + 4)
			o += 8 + cnt * 4
		fo = o + 4
		ox, oy, w, h = struct.unpack_from("<4i", d, fo)
		pix = d[fo + 16:fo + 16 + w * h]
		fn = "frame_%02d.png" % i
		palette_png(pix, pal, w, h, os.path.join(fd, fn), tex == 3)
		first = first or (fn, w, h)
		o = fo + 16 + w * h
		n += 1
	stage = new_stage(os.path.join(outdir, name, name + ".usdc"))
	rp = "/" + name
	x = UsdGeom.Xform.Define(stage, rp)
	stage.SetDefaultPrim(x.GetPrim())
	mat = make_material(stage, rp, name, "./frames/" + first[0], True)
	mesh = UsdGeom.Mesh.Define(stage, rp + "/Quad")
	hw, hh = first[1] / 2.0, first[2] / 2.0
	mesh.CreatePointsAttr([(0, -hw, -hh), (0, hw, -hh), (0, hw, hh), (0, -hw, hh)])
	mesh.CreateFaceVertexCountsAttr([4])
	mesh.CreateFaceVertexIndicesAttr([0, 1, 2, 3])
	st = UsdGeom.PrimvarsAPI(mesh).CreatePrimvar("st", Sdf.ValueTypeNames.TexCoord2fArray, UsdGeom.Tokens.faceVarying)
	st.Set(Vt.Vec2fArray([(0, 0), (1, 0), (1, 1), (0, 1)]))
	UsdShade.MaterialBindingAPI.Apply(mesh.GetPrim()).Bind(mat)
	x.GetPrim().SetCustomDataByKey("goldsrc:spriteType", int(stype))
	x.GetPrim().SetCustomDataByKey("goldsrc:frames", int(n))
	stage.GetRootLayer().Save()
	log("  %s: %d frames" % (name, n))
	return dict(frames=n)


# ---------------------------------------------------------------- WAD / BSP

def read_wad(path):
	d = open(path, "rb").read()
	if d[:4] != b"WAD3":
		return {}
	n, off = struct.unpack_from("<ii", d, 4)
	out = {}
	for i in range(n):
		po, dsize, size, typ, comp = struct.unpack_from("<iiibb", d, off + i * 32)
		name = cstr(d[off + i * 32 + 16:off + i * 32 + 32])
		if typ == 0x43:
			out[name.lower()] = d[po:po + dsize]
	return out


def miptex_decode(b):
	name = cstr(b[:16])
	w, h = struct.unpack_from("<II", b, 16)
	offs = struct.unpack_from("<4I", b, 24)
	if offs[0] == 0:
		return None
	pix = b[offs[0]:offs[0] + w * h]
	po = offs[3] + (w // 8) * (h // 8) + 2
	pal = b[po:po + 768]
	return dict(name=name, w=w, h=h, pix=pix, pal=pal)


def parse_entities(raw):
	text = raw.split(b"\0", 1)[0].decode("latin-1")
	out = []
	for blk in re.findall(r"\{(.*?)\}", text, re.S):
		out.append(dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', blk)))
	return out


def set_entity_xform(x, e, brush):
	"""Translate by 'origin', rotate by 'angles' (pitch yaw roll) or 'angle' (yaw)."""
	try:
		org = [float(v) for v in e.get("origin", "0 0 0").split()[:3]]
	except ValueError:
		org = [0, 0, 0]
	if "angles" in e:
		try:
			pitch, yaw, roll = [float(v) for v in e["angles"].split()[:3]]
		except ValueError:
			pitch = yaw = roll = 0
	else:
		pitch = roll = 0
		try:
			yaw = float(e.get("angle", 0))
		except ValueError:
			yaw = 0
		if yaw == -1:
			yaw = 0
	x.AddTranslateOp().Set(Gf.Vec3d(*org))
	if yaw:
		x.AddRotateZOp().Set(yaw)
	if pitch:
		x.AddRotateYOp().Set(pitch)
	if roll:
		x.AddRotateXOp().Set(roll)


def convert_bsp(path, outdir, wads, log=print):
	d = open(path, "rb").read()
	ver = struct.unpack_from("<i", d, 0)[0]
	if ver != 30:
		raise ValueError("BSP version %d unsupported (need 30)" % ver)
	L = [struct.unpack_from("<ii", d, 4 + i * 8) for i in range(15)]

	def lump(i):
		return d[L[i][0]:L[i][0] + L[i][1]]
	verts = np.frombuffer(lump(3), dtype="<f4").reshape(-1, 3).astype(np.float64)
	edges = np.frombuffer(lump(12), dtype="<u2").reshape(-1, 2)
	surfe = np.frombuffer(lump(13), dtype="<i4")
	fl = np.frombuffer(lump(7), dtype=np.dtype([("plane", "<u2"), ("side", "<u2"), ("first", "<i4"), ("n", "<i2"),
		("ti", "<i2"), ("st", "u1", 4), ("lofs", "<i4")]))
	ti = np.frombuffer(lump(6), dtype=np.dtype([("s", "<f4", 4), ("t", "<f4", 4), ("mip", "<i4"), ("flags", "<i4")]))
	mt = lump(2)
	nm = struct.unpack_from("<i", mt, 0)[0]
	mips = []
	for i in range(nm):
		(o,) = struct.unpack_from("<i", mt, 4 + i * 4)
		mips.append(None if o < 0 else mt[o:])
	models = np.frombuffer(lump(14), dtype=np.dtype([("mins", "<f4", 3), ("maxs", "<f4", 3), ("origin", "<f4", 3),
		("head", "<i4", 4), ("vis", "<i4"), ("first", "<i4"), ("n", "<i4")]))
	name = safe(os.path.splitext(os.path.basename(path))[0])
	odir = os.path.join(outdir, name)
	tdir = os.path.join(odir, "textures")
	os.makedirs(tdir, exist_ok=True)
	stage = new_stage(os.path.join(odir, name + ".usdc"))
	rp = "/" + name
	x = UsdGeom.Xform.Define(stage, rp)
	stage.SetDefaultPrim(x.GetPrim())
	texinfo = {}
	missing = set()
	mats = {}

	def get_tex(mi):
		if mi in texinfo:
			return texinfo[mi]
		b = mips[mi] if mi < len(mips) else None
		t = None
		nme = cstr(b[:16]) if b else ""
		if b and struct.unpack_from("<I", b, 24)[0]:
			t = miptex_decode(b)
		else:
			for w in wads:
				if nme.lower() in w:
					t = miptex_decode(w[nme.lower()])
					break
		if t is None:
			missing.add(nme)
			texinfo[mi] = (nme, None)
		else:
			fnm = safe(nme).lower() + ".png"
			masked = nme.startswith("{")
			if nme.startswith("!") or nme.lower().startswith("sky"):
				masked = False
			palette_png(t["pix"], t["pal"], t["w"], t["h"], os.path.join(tdir, fnm), masked)
			mats[mi] = make_material(stage, rp, nme, "./textures/" + fnm, masked, flags=0)
			texinfo[mi] = (nme, t)
		return texinfo[mi]
	ents = parse_entities(lump(0))
	place = {}  # brush model index -> entity
	for e in ents:
		mdlkey = e.get("model", "")
		if mdlkey.startswith("*") and mdlkey[1:].isdigit():
			place.setdefault(int(mdlkey[1:]), e)
	for mi_, mdl in enumerate(models):
		mp = "%s/%s" % (rp, "world" if mi_ == 0 else "brush_%d" % mi_)
		xf = UsdGeom.Xform.Define(stage, mp)
		if mi_ in place:
			set_entity_xform(xf, place[mi_], brush=True)
			xf.GetPrim().SetCustomDataByKey("goldsrc:classname", place[mi_].get("classname", ""))
			if place[mi_].get("targetname"):
				xf.GetPrim().SetCustomDataByKey("goldsrc:targetname", place[mi_]["targetname"])
		mesh_pts, buckets = [], {}
		tp = {}
		for fi in range(mdl["first"], mdl["first"] + mdl["n"]):
			f = fl[fi]
			t = ti[f["ti"]]
			nme, tex = get_tex(int(t["mip"]))
			if nme.lower() in ("aaatrigger", "null", "clip", "origin", "skip", "hint", "sky"):
				pass
			se = surfe[f["first"]:f["first"] + f["n"]]
			vi = [int(edges[e][0]) if e >= 0 else int(edges[-e][1]) for e in se]
			w, h = (tex["w"], tex["h"]) if tex else (64, 64)
			pts = verts[vi]
			u = (pts @ np.array(t["s"][:3]) + t["s"][3]) / w
			v = 1.0 - (pts @ np.array(t["t"][:3]) + t["t"][3]) / h
			bk = buckets.setdefault(int(t["mip"]), dict(fv=[], fc=[], pts=[], uv=[], ix={}))
			base = len(bk["pts"])
			bk["pts"].extend(map(tuple, pts))
			bk["uv"].extend(zip(u.tolist(), v.tolist()))
			bk["fc"].append(len(vi))
			bk["fv"].extend(range(base, base + len(vi)))
		for mip, bk in buckets.items():
			nme = texinfo[mip][0]
			mesh = UsdGeom.Mesh.Define(stage, "%s/%s" % (mp, safe(nme)) if True else mp)
			mesh.CreateOrientationAttr(UsdGeom.Tokens.leftHanded)
			mesh.CreateSubdivisionSchemeAttr(UsdGeom.Tokens.none)
			mesh.CreatePointsAttr(Vt.Vec3fArray([Gf.Vec3f(*[float(c) for c in p]) for p in bk["pts"]]))
			mesh.CreateFaceVertexCountsAttr(Vt.IntArray(bk["fc"]))
			mesh.CreateFaceVertexIndicesAttr(Vt.IntArray(bk["fv"]))
			st = UsdGeom.PrimvarsAPI(mesh).CreatePrimvar("st", Sdf.ValueTypeNames.TexCoord2fArray, UsdGeom.Tokens.vertex)
			st.Set(Vt.Vec2fArray([Gf.Vec2f(float(a), float(b)) for a, b in bk["uv"]]))
			if nme.lower() in ("aaatrigger", "null", "clip", "origin", "skip", "hint", "sky") or nme.lower().startswith("aaa"):
				mesh.CreateVisibilityAttr(UsdGeom.Tokens.invisible)  # tool textures kept but hidden
			if mip in mats:
				UsdShade.MaterialBindingAPI.Apply(mesh.GetPrim()).Bind(mats[mip])
	# point/model entities: references to converted MDLs, locators for the rest
	n_props = 0
	grp = UsdGeom.Xform.Define(stage, rp + "/entities")
	for ei, e in enumerate(ents):
		cls = e.get("classname", "")
		mdlname = e.get("model", "")
		if mdlname.lower().endswith(".mdl") and "origin" in e:
			rel = re.sub(r"^models/", "", mdlname.replace("\\", "/")).lower()  # converted files are lowercase
			stem = os.path.splitext(os.path.basename(rel))[0]
			rel_usd = "../../models/%s/%s/%s.usdc" % (os.path.dirname(rel), safe(stem), safe(stem))
			rel_usd = rel_usd.replace("//", "/")
			x = UsdGeom.Xform.Define(stage, "%s/entities/%s_%d" % (rp, safe(cls), ei))
			set_entity_xform(x, e, brush=False)
			x.GetPrim().GetReferences().AddReference(rel_usd)
			x.GetPrim().SetCustomDataByKey("goldsrc:model", mdlname)
			n_props += 1
		elif "origin" in e and cls and not mdlname.startswith("*") and cls not in ("worldspawn",):
			x = UsdGeom.Xform.Define(stage, "%s/entities/%s_%d" % (rp, safe(cls), ei))
			set_entity_xform(x, e, brush=False)
			x.GetPrim().SetCustomDataByKey("goldsrc:classname", cls)
			if e.get("targetname"):
				x.GetPrim().SetCustomDataByKey("goldsrc:targetname", e["targetname"])
	stage.GetRootLayer().Save()
	log("  %s: %d brush entities placed, %d model props referenced" % (name, len(place), n_props))
	if missing:
		log("  %s: textures not found (need a WAD): %s" % (name, ", ".join(sorted(missing))[:200]))
	log("  %s: %d models, %d faces" % (name, len(models), len(fl)))
	return dict(models=len(models), faces=len(fl), missing=sorted(missing))


# ---------------------------------------------------------------- CLI

def walk(root, ext):
	for dp, _, fs in os.walk(root):
		for f in sorted(fs):
			if f.lower().endswith(ext):
				yield os.path.join(dp, f)


def main():
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("kind", choices=["mdl", "spr", "bsp", "all"])
	ap.add_argument("inputs", nargs="+")
	ap.add_argument("-o", "--out", required=True)
	ap.add_argument("--wad", action="append", default=[])
	ap.add_argument("--no-anims", action="store_true")
	a = ap.parse_args()
	ok = fail = 0
	failed = []

	def run(fn, p, *args):
		nonlocal ok, fail
		try:
			fn(p, *args)
			ok += 1
		except Exception as e:  # keep going; report at end
			fail += 1
			failed.append((p, str(e)))
			print("  FAILED %s: %s" % (p, e))
	if a.kind == "all":
		root = a.inputs[0]
		wads = {}
		# the base game's WADs (halflife.wad, liquids.wad, ...) hold many map textures
		extra = os.path.join(os.path.dirname(os.path.abspath(root)), "valve")
		for w in list(walk(extra, ".wad")) + list(walk(root, ".wad")):
			wads.update(read_wad(w))
		wads_l = [wads]
		for p in walk(os.path.join(root, "models"), ".mdl"):
			if re.search(r"T\.mdl$|\d\d\.mdl$", p) and os.path.exists(re.sub(r"(T|\d\d)\.mdl$", ".mdl", p)):
				continue
			print("mdl", p)
			run(convert_mdl, p, os.path.join(a.out, "models", os.path.relpath(os.path.dirname(p), os.path.join(root, "models"))), not a.no_anims)
		for p in walk(os.path.join(root, "sprites"), ".spr"):
			print("spr", p)
			run(convert_spr, p, os.path.join(a.out, "sprites"))
		for p in walk(os.path.join(root, "maps"), ".bsp"):
			print("bsp", p)
			run(convert_bsp, p, os.path.join(a.out, "maps"), wads_l)
	else:
		wads = {}
		for w in a.wad:
			wads.update(read_wad(w))
		for p in a.inputs:
			print(a.kind, p)
			if a.kind == "mdl":
				run(convert_mdl, p, a.out, not a.no_anims)
			elif a.kind == "spr":
				run(convert_spr, p, a.out)
			else:
				run(convert_bsp, p, a.out, [wads])
	print("done: %d ok, %d failed" % (ok, fail))
	for p, e in failed:
		print("  failed:", p, e)
	return 1 if fail else 0


if __name__ == "__main__":
	sys.exit(main())
