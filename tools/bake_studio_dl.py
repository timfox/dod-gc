#!/usr/bin/env python3
"""Bake static GoldSrc studio mapmodels into GX display lists (GCsd).

Rest-pose skinned geometry in model space → media/sd_<map>.gsd. Runtime
(ref/gx/r_gx_studiodl.c) loads the pack into ARAM, applies the entity
transform via GX_PNMTX0, binds live studio skins, and GX_CallDispList —
same tris/UVs/textures, no CPU bone skin or FIFO rebuild per frame.

Only mapmodels with ordinary (non-weighted) verts are baked. Bodygroup
variants are separate entries keyed by GoldSrc body value + skin family.
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

GX_DRAW_TRIANGLESTRIP = 0x98
GX_DRAW_TRIANGLEFAN = 0xA0
GX_VTXFMT = 6
ST_FRAC = 9
STUDIO_NF_MASKED = 0x0040
STUDIO_NF_CHROME = 0x0002
STUDIO_HAS_BONEWEIGHTS = 1 << 31

# Struct sizes (GoldSrc / Xash studio.h)
BONE_SZ = 112
BODYPART_SZ = 76
TEXTURE_SZ = 80
MODEL_SZ = 112
MESH_SZ = 20


def fnv1a32(s: str) -> int:
	h = 2166136261
	for ch in s.lower().replace("\\", "/"):
		h ^= ord(ch) & 0xFF
		h = (h * 16777619) & 0xFFFFFFFF
	return h


def pad32(b: bytearray) -> None:
	b += b"\0" * ((-len(b)) % 32)


def rgb565(r: int, g: int, b: int) -> int:
	return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def angle_quaternion_studio(angles) -> tuple[float, float, float, float]:
	"""Match AngleQuaternion(..., studio=true) — bone values are radians."""
	# Engine: SinCos(ROLL→sy/cy), SinCos(YAW→sp/cp), SinCos(PITCH→sr/cr)
	sy, cy = math.sin(angles[2] * 0.5), math.cos(angles[2] * 0.5)
	sp, cp = math.sin(angles[1] * 0.5), math.cos(angles[1] * 0.5)
	sr, cr = math.sin(angles[0] * 0.5), math.cos(angles[0] * 0.5)
	qx = sr * cp * cy - cr * sp * sy
	qy = cr * sp * cy + sr * cp * sy
	qz = cr * cp * sy - sr * sp * cy
	qw = cr * cp * cy + sr * sp * sy
	return (qx, qy, qz, qw)


def mat_from_quat_origin(q, origin) -> list[list[float]]:
	"""Matrix3x4_FromOriginQuat."""
	x, y, z, w = q
	out = [[0.0] * 4 for _ in range(3)]
	out[0][0] = 1.0 - 2.0 * y * y - 2.0 * z * z
	out[1][0] = 2.0 * x * y + 2.0 * w * z
	out[2][0] = 2.0 * x * z - 2.0 * w * y
	out[0][1] = 2.0 * x * y - 2.0 * w * z
	out[1][1] = 1.0 - 2.0 * x * x - 2.0 * z * z
	out[2][1] = 2.0 * y * z + 2.0 * w * x
	out[0][2] = 2.0 * x * z + 2.0 * w * y
	out[1][2] = 2.0 * y * z - 2.0 * w * x
	out[2][2] = 1.0 - 2.0 * x * x - 2.0 * y * y
	out[0][3] = origin[0]
	out[1][3] = origin[1]
	out[2][3] = origin[2]
	return out


def mat_concat(a, b) -> list[list[float]]:
	"""out = a * b (Matrix3x4_ConcatTransforms)."""
	out = [[0.0] * 4 for _ in range(3)]
	for i in range(3):
		for j in range(3):
			out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j]
		out[i][3] = a[i][0] * b[0][3] + a[i][1] * b[1][3] + a[i][2] * b[2][3] + a[i][3]
	return out


def mat_xform(m, v) -> tuple[float, float, float]:
	return (
		m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2] + m[0][3],
		m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2] + m[1][3],
		m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2] + m[2][3],
	)


def mat_rotate(m, v) -> tuple[float, float, float]:
	return (
		m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
		m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
		m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2],
	)


class StudioMdl:
	def __init__(self, path: Path):
		self.path = path
		self.data = path.read_bytes()
		d = self.data
		ident, ver = struct.unpack_from("<2i", d, 0)
		if ident != 0x54534449 and d[0:4] != b"IDST":
			# little-endian 'IDST'
			if struct.unpack_from("<I", d, 0)[0] != 0x54534449:
				raise ValueError(f"{path}: not IDST")
		if ver != 10:
			raise ValueError(f"{path}: studio version {ver} (need 10)")
		self.name = d[8:72].split(b"\0", 1)[0].decode("latin1", "replace")
		(
			self.flags,
			self.numbones,
			self.boneindex,
			_nbc,
			_bci,
			_nhb,
			_hbi,
			self.numseq,
			self.seqindex,
			_nsg,
			_sgi,
			self.numtextures,
			self.textureindex,
			_tdi,
			self.numskinref,
			self.numskinfamilies,
			self.skinindex,
			self.numbodyparts,
			self.bodypartindex,
		) = struct.unpack_from("<19i", d, 136)

	def bones(self):
		out = []
		for i in range(self.numbones):
			off = self.boneindex + i * BONE_SZ
			parent, _unused = struct.unpack_from("<ii", self.data, off + 32)
			ctrl = struct.unpack_from("<6i", self.data, off + 40)
			value = struct.unpack_from("<6f", self.data, off + 64)
			out.append({"parent": parent, "value": value, "ctrl": ctrl})
		return out

	def textures(self):
		out = []
		for i in range(self.numtextures):
			off = self.textureindex + i * TEXTURE_SZ
			name = self.data[off : off + 64].split(b"\0", 1)[0].decode("latin1", "replace")
			flags, w, h, _idx = struct.unpack_from("<iiii", self.data, off + 64)
			out.append({"name": name, "flags": flags, "width": max(1, w), "height": max(1, h)})
		return out

	def skinref(self, family: int = 0) -> list[int]:
		if self.numskinref <= 0 or self.skinindex <= 0:
			return list(range(max(1, self.numtextures)))
		fam = max(0, min(family, max(0, self.numskinfamilies - 1)))
		off = self.skinindex + fam * self.numskinref * 2
		return list(struct.unpack_from(f"<{self.numskinref}h", self.data, off))

	def bodyparts(self):
		out = []
		for i in range(self.numbodyparts):
			off = self.bodypartindex + i * BODYPART_SZ
			name = self.data[off : off + 64].split(b"\0", 1)[0].decode("latin1", "replace")
			nmodels, base, modelindex = struct.unpack_from("<iii", self.data, off + 64)
			models = []
			for mi in range(nmodels):
				moff = modelindex + mi * MODEL_SZ
				mname = self.data[moff : moff + 64].split(b"\0", 1)[0].decode("latin1", "replace")
				(
					_unused,
					_unused2,
					nmesh,
					meshindex,
					nverts,
					vertinfoindex,
					vertindex,
					nnorms,
					norminfoindex,
					normindex,
					_blendv,
					_blendn,
				) = struct.unpack_from("<ifiiiiiiiiii", self.data, moff + 64)
				meshes = []
				for mei in range(nmesh):
					meoff = meshindex + mei * MESH_SZ
					ntris, triindex, skinref, nnorms_m, _u = struct.unpack_from(
						"<iiiii", self.data, meoff
					)
					meshes.append(
						{
							"numtris": ntris,
							"triindex": triindex,
							"skinref": skinref,
							"numnorms": nnorms_m,
						}
					)
				verts = [
					struct.unpack_from("<3f", self.data, vertindex + vi * 12)
					for vi in range(nverts)
				]
				vbone = list(self.data[vertinfoindex : vertinfoindex + nverts])
				norms = [
					struct.unpack_from("<3f", self.data, normindex + ni * 12)
					for ni in range(nnorms)
				]
				nbone = list(self.data[norminfoindex : norminfoindex + nnorms])
				models.append(
					{
						"name": mname,
						"meshes": meshes,
						"verts": verts,
						"vbone": vbone,
						"norms": norms,
						"nbone": nbone,
					}
				)
			out.append({"name": name, "nummodels": nmodels, "base": base, "models": models})
		return out


def rest_bone_mats(bones) -> list:
	mats = []
	for i, b in enumerate(bones):
		q = angle_quaternion_studio(b["value"][3:6])
		local = mat_from_quat_origin(q, b["value"][0:3])
		if b["parent"] < 0:
			mats.append(local)
		else:
			mats.append(mat_concat(mats[b["parent"]], local))
	return mats


def body_value_for_choice(bodyparts, choices: list[int]) -> int:
	"""Encode GoldSrc curstate.body from per-bodypart model indices."""
	body = 0
	for bp, choice in zip(bodyparts, choices):
		body += choice * max(1, bp["base"])
	return body


def iter_body_combos(bodyparts):
	"""Yield (body_value, list of selected model dicts)."""
	if not bodyparts:
		return
	counts = [bp["nummodels"] for bp in bodyparts]
	# Cap explosion — Avalanche props are single-bodypart.
	total = 1
	for c in counts:
		total *= max(1, c)
	if total > 64:
		# Bake body=0 only when combinatorics explode.
		choices = [0] * len(bodyparts)
		models = [bodyparts[i]["models"][0] for i in range(len(bodyparts))]
		yield body_value_for_choice(bodyparts, choices), models
		return

	def rec(i, choices):
		if i >= len(bodyparts):
			models = [bodyparts[j]["models"][choices[j]] for j in range(len(bodyparts))]
			yield body_value_for_choice(bodyparts, choices), models
			return
		for m in range(bodyparts[i]["nummodels"]):
			yield from rec(i + 1, choices + [m])

	yield from rec(0, [])


class EntryBuilder:
	def __init__(self) -> None:
		self.pos: dict[tuple[int, int, int], int] = {}
		self.batches: dict[int, bytearray] = {}  # tex_index -> DL
		self.batch_flags: dict[int, int] = {}
		self.mins = [32767, 32767, 32767]
		self.maxs = [-32768, -32768, -32768]
		self.tris = 0

	def pos_index(self, p) -> int:
		key = (
			int(max(-32767, min(32767, round(p[0])))),
			int(max(-32767, min(32767, round(p[1])))),
			int(max(-32767, min(32767, round(p[2])))),
		)
		idx = self.pos.get(key)
		if idx is None:
			idx = self.pos[key] = len(self.pos)
			for a in range(3):
				self.mins[a] = min(self.mins[a], key[a])
				self.maxs[a] = max(self.maxs[a], key[a])
		return idx

	def emit(self, tex: int, flags: int, prim: int, verts: list) -> None:
		dl = self.batches.setdefault(tex, bytearray())
		self.batch_flags[tex] = flags
		dl += struct.pack(">BH", prim | GX_VTXFMT, len(verts))
		for p, s, t in verts:
			# White — Flipper world studio uses GX_REPLACE (ignores RASC).
			dl += struct.pack(
				">HHhh",
				self.pos_index(p),
				rgb565(255, 255, 255),
				int(max(-32768, min(32767, round(s * (1 << ST_FRAC))))),
				int(max(-32768, min(32767, round(t * (1 << ST_FRAC))))),
			)
		self.tris += max(0, len(verts) - 2)

	def encode(self) -> bytes:
		# Layout matches runtime:
		#   u16 n_pos, u16 n_batches, u32 pos_ofs
		#   then n_batches * { u16 tex, u16 flags, u32 dl_ofs, u32 dl_bytes }
		#   positions; display lists
		texes = sorted(self.batches)
		out = bytearray(8 + 12 * len(texes))
		pad32(out)
		pos_ofs = len(out)
		for key in self.pos:
			out += struct.pack(">hhh", *key)
		pad32(out)
		for i, tex in enumerate(texes):
			dl = bytearray(self.batches[tex])
			pad32(dl)
			struct.pack_into(
				">HHII",
				out,
				8 + 12 * i,
				tex & 0xFFFF,
				self.batch_flags.get(tex, 0) & 0xFFFF,
				len(out),
				len(dl),
			)
			out += dl
		struct.pack_into(">HHI", out, 0, len(self.pos), len(texes), pos_ofs)
		return bytes(out)


def bake_mdl_entries(rel: str, mdl: StudioMdl) -> list[dict]:
	if mdl.flags & STUDIO_HAS_BONEWEIGHTS:
		print(f"skip {rel}: bone weights", file=sys.stderr)
		return []
	bones = mdl.bones()
	if not bones:
		print(f"skip {rel}: no bones", file=sys.stderr)
		return []
	bmats = rest_bone_mats(bones)
	textures = mdl.textures()
	bodyparts = mdl.bodyparts()
	if not bodyparts:
		return []
	name_hash = fnv1a32(rel)
	entries = []
	# Skin family 0 only — map props rarely switch families.
	sref = mdl.skinref(0)
	for body, models in iter_body_combos(bodyparts):
		eb = EntryBuilder()
		for sub in models:
			skinned = [mat_xform(bmats[min(b, len(bmats) - 1)], v) for v, b in zip(sub["verts"], sub["vbone"])]
			for mesh in sub["meshes"]:
				sr = mesh["skinref"]
				tex_i = sref[sr] if sr < len(sref) else sr
				if tex_i < 0 or tex_i >= len(textures):
					tex_i = 0
				tex = textures[tex_i] if textures else {"width": 64, "height": 64, "flags": 0}
				if tex["flags"] & STUDIO_NF_CHROME:
					continue  # chrome needs view-dependent UVs
				su = 1.0 / float(tex["width"])
				sv = 1.0 / float(tex["height"])
				p = mesh["triindex"]
				d = mdl.data
				while True:
					cmd = struct.unpack_from("<h", d, p)[0]
					p += 2
					if cmd == 0:
						break
					fan = cmd < 0
					n = -cmd if fan else cmd
					verts = []
					for _ in range(n):
						vi, _ni, s, t = struct.unpack_from("<hhhh", d, p)
						p += 8
						if vi < 0 or vi >= len(skinned):
							continue
						verts.append((skinned[vi], s * su, t * sv))
					if len(verts) >= 3:
						eb.emit(
							tex_i,
							tex["flags"] & 0xFFFF,
							GX_DRAW_TRIANGLEFAN if fan else GX_DRAW_TRIANGLESTRIP,
							verts,
						)
		if not eb.batches:
			continue
		blob = eb.encode()
		entries.append(
			{
				"name_hash": name_hash,
				"body": body & 0xFFFF,
				"skin": 0,
				"mins": eb.mins,
				"maxs": eb.maxs,
				"blob": blob,
				"tris": eb.tris,
				"rel": rel,
			}
		)
	return entries


def write_pack(entries: list[dict], out_path: Path) -> None:
	n = len(entries)
	# header 32 + dir 32*n  (dir record: hash,body,skin,ofs,bytes,mins[3],maxs[3],pad,pad)
	body = bytearray(32 + 32 * n)
	max_entry = 0
	for i, e in enumerate(entries):
		blob = bytearray(e["blob"])
		pad32(blob)
		ofs = len(body)
		while ofs % 32:
			body += b"\0"
			ofs = len(body)
		body += blob
		max_entry = max(max_entry, len(blob))
		struct.pack_into(
			">IHHII3h3hHH",
			body,
			32 + 32 * i,
			e["name_hash"],
			e["body"],
			e["skin"],
			ofs,
			len(blob),
			e["mins"][0],
			e["mins"][1],
			e["mins"][2],
			e["maxs"][0],
			e["maxs"][1],
			e["maxs"][2],
			0,
			0,
		)
	# magic, version, n_entries, file_size, max_entry, reserved[4] — 32 bytes
	struct.pack_into(
		">4sHHII4I",
		body,
		0,
		b"GCsd",
		1,
		n,
		len(body),
		max_entry,
		0,
		0,
		0,
		0,
	)
	out_path.parent.mkdir(parents=True, exist_ok=True)
	out_path.write_bytes(body)
	tris = sum(e["tris"] for e in entries)
	print(f"studio DL pack: entries={n} tris={tris} max_entry={max_entry}B file={len(body)}B")
	print(f"  wrote {out_path}")


def resolve_mdl(src_root: Path, rel: str) -> Path | None:
	rel = rel.replace("\\", "/").lstrip("/")
	cand = src_root / rel
	if cand.is_file():
		return cand
	want = rel.split("/", 1)[-1].lower()
	models = src_root / "models"
	if models.is_dir():
		for p in models.rglob("*"):
			if p.is_file() and str(p.relative_to(models)).replace("\\", "/").lower() == want:
				return p
	return None


def bsp_model_refs(bsp_path: Path) -> list[str]:
	import re

	raw = bsp_path.read_bytes()
	off, length = struct.unpack_from("<ii", raw, 4)
	ents = raw[off : off + length].decode("latin1", "replace")
	refs = set(re.findall(r'"model"\s+"(models/[^"]+)"', ents, flags=re.I))
	out = []
	for r in refs:
		rel = r.replace("\\", "/").lstrip("/")
		name = rel.rsplit("/", 1)[-1]
		if name.startswith("*"):
			continue
		if not rel.lower().startswith("models/"):
			continue
		out.append(rel)
	return sorted(set(out))


def bake_map(bsp: Path, src_root: Path, out_path: Path) -> int:
	refs = bsp_model_refs(bsp)
	entries: list[dict] = []
	for rel in refs:
		path = resolve_mdl(src_root, rel)
		if path is None:
			print(f"missing {rel}", file=sys.stderr)
			continue
		try:
			mdl = StudioMdl(path)
		except ValueError as e:
			print(e, file=sys.stderr)
			continue
		ents = bake_mdl_entries(rel, mdl)
		print(f"  {rel}: {len(ents)} body/skin entries, bones={mdl.numbones}")
		entries.extend(ents)
	if not entries:
		print("error: no studio DL entries baked", file=sys.stderr)
		return 1
	write_pack(entries, out_path)
	return 0


def main() -> int:
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument(
		"--map",
		type=Path,
		default=ROOT / "content/dod/maps/dod_avalanche.bsp",
	)
	ap.add_argument(
		"--src",
		type=Path,
		default=ROOT / "content/dod",
		help="gamedir root with models/",
	)
	ap.add_argument(
		"--out",
		type=Path,
		default=None,
		help="Output .gsd (default content/dod/media/sd_<map>.gsd)",
	)
	ap.add_argument(
		"--mdl",
		type=Path,
		action="append",
		default=[],
		help="Bake a single MDL (repeatable) instead of BSP refs",
	)
	args = ap.parse_args()
	out = args.out
	if args.mdl:
		entries: list[dict] = []
		for p in args.mdl:
			rel = f"models/mapmodels/{p.name}" if "mapmodels" not in str(p) else str(p)
			# Prefer path relative to src
			try:
				rel = str(p.resolve().relative_to(args.src.resolve())).replace("\\", "/")
			except ValueError:
				rel = f"models/mapmodels/{p.name}"
			mdl = StudioMdl(p)
			entries.extend(bake_mdl_entries(rel, mdl))
		if not out:
			out = ROOT / "content/dod/media" / f"sd_{args.map.stem}.gsd"
		if not entries:
			return 1
		write_pack(entries, out)
		return 0
	if out is None:
		out = ROOT / "content/dod/media" / f"sd_{args.map.stem}.gsd"
	return bake_map(args.map, args.src, out)


if __name__ == "__main__":
	raise SystemExit(main())
