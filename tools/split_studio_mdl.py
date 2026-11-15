#!/usr/bin/env python3
"""Split a GoldSrc studio model (v10) into the demand-loaded form.

A whole DoD player model (about 3.8 MB) cannot sit in GameCube MEM1. GoldSrc
already has a split layout the engine streams piece by piece:

  name.mdl    core: bones, sequence descriptions, body parts and meshes
  nameT.mdl   textures and skin table (studiomdl's $externaltextures)
  nameNN.mdl  animation data, one sequence group per file ($sequencegroup)

This tool rewrites a monolithic model into that layout without touching a
single vertex, bone or animation value: every mesh and every body group is
kept. Sequence groups are packed up to --group-bytes so each file fits one of
the engine's ARAM-backed animation slots.

  split_studio_mdl.py info  model.mdl
  split_studio_mdl.py split model.mdl outdir [--group-bytes N] [--max-groups N]
"""
import argparse
import os
import struct
import sys

IDST = b"IDST"
IDSQ = b"IDSQ"
VERSION = 10
HDR_SIZE = 244
SEQ_SIZE = 176
SEQGROUP_SIZE = 104
SEQHDR_SIZE = 76
TEX_SIZE = 80
BODYPART_SIZE = 76
MODEL_SIZE = 112
MESH_SIZE = 20
PALETTE = 768

# studiohdr_t int fields from offset 140
HDR_FIELDS = (
	"numbones boneindex numbonecontrollers bonecontrollerindex numhitboxes "
	"hitboxindex numseq seqindex numseqgroups seqgroupindex numtextures "
	"textureindex texturedataindex numskinref numskinfamilies skinindex "
	"numbodyparts bodypartindex numattachments attachmentindex soundtable "
	"soundindex soundgroups soundgroupindex numtransitions transitionindex"
).split()
HDR_INDEX_FIELDS = [f for f in HDR_FIELDS if f.endswith("index")]

# mstudioseqdesc_t offsets
SEQ_NUMEVENTS, SEQ_EVENTINDEX = 48, 52
SEQ_NUMFRAMES = 56
SEQ_NUMPIVOTS, SEQ_PIVOTINDEX = 60, 64
SEQ_AUTOMOVEPOS, SEQ_AUTOMOVEANGLE = 88, 92
SEQ_NUMBLENDS, SEQ_ANIMINDEX = 120, 124
SEQ_SEQGROUP = 156


class StudioError(Exception):
	pass


def _u32(buf, off):
	return struct.unpack_from("<i", buf, off)[0]


def _put(buf, off, val):
	struct.pack_into("<i", buf, off, val)


def read_header(data):
	if len(data) < HDR_SIZE or data[:4] != IDST:
		raise StudioError("not a studio model (IDST)")
	if _u32(data, 4) != VERSION:
		raise StudioError("unsupported studio version %d" % _u32(data, 4))
	hdr = {name: _u32(data, 140 + 4 * i) for i, name in enumerate(HDR_FIELDS)}
	hdr["length"] = _u32(data, 72)
	hdr["name"] = data[8:72].split(b"\0")[0].decode("latin-1")
	return hdr


def anim_extent(data, animindex, numblends, numbones, numframes):
	"""End offset of one sequence's animation block (panim array + RLE)."""
	table = numblends * numbones * 12
	end = animindex + table
	if end > len(data):
		raise StudioError("animation table past end of file")
	for k in range(numblends * numbones):
		base = animindex + k * 12
		for off in struct.unpack_from("<6H", data, base):
			if not off:
				continue
			p = base + off
			frames = 0
			while frames < numframes:
				if p + 2 > len(data):
					raise StudioError("animation values past end of file")
				valid, total = data[p], data[p + 1]
				if total == 0:
					raise StudioError("corrupt animation run at %d" % p)
				p += 2 + 2 * valid
				frames += total
			end = max(end, p)
	return end


def read_sequences(data, hdr):
	seqs = []
	for i in range(hdr["numseq"]):
		off = hdr["seqindex"] + i * SEQ_SIZE
		seq = {
			"off": off,
			"label": data[off:off + 32].split(b"\0")[0].decode("latin-1"),
			"numframes": _u32(data, off + SEQ_NUMFRAMES),
			"numblends": _u32(data, off + SEQ_NUMBLENDS),
			"animindex": _u32(data, off + SEQ_ANIMINDEX),
			"seqgroup": _u32(data, off + SEQ_SEQGROUP),
		}
		if seq["seqgroup"] == 0:
			seq["animend"] = anim_extent( data, seq["animindex"], seq["numblends"],
				hdr["numbones"], seq["numframes"] )
		seqs.append(seq)
	return seqs


def texture_ranges(data, hdr):
	"""Byte ranges (start, end) holding texture structs, skins and pixels."""
	ranges = []
	n = hdr["numtextures"]
	if n <= 0:
		return ranges
	ranges.append((hdr["textureindex"], hdr["textureindex"] + n * TEX_SIZE))
	skin_bytes = 2 * hdr["numskinref"] * hdr["numskinfamilies"]
	if skin_bytes:
		ranges.append((hdr["skinindex"], hdr["skinindex"] + skin_bytes))
	lo, hi = None, 0
	for i in range(n):
		t = hdr["textureindex"] + i * TEX_SIZE
		w, h, idx = struct.unpack_from("<3i", data, t + 68)
		lo = idx if lo is None else min(lo, idx)
		hi = max(hi, idx + w * h + PALETTE)
	ranges.append((lo, hi))
	return ranges


def info(data):
	hdr = read_header(data)
	seqs = read_sequences(data, hdr)
	anim = sum(s["animend"] - s["animindex"] for s in seqs if s["seqgroup"] == 0)
	tex = sum(e - s for s, e in texture_ranges(data, hdr))
	parts = []
	for i in range(hdr["numbodyparts"]):
		off = hdr["bodypartindex"] + i * BODYPART_SIZE
		parts.append((data[off:off + 64].split(b"\0")[0].decode("latin-1"), _u32(data, off + 64)))
	return {
		"hdr": hdr, "seqs": seqs, "anim_bytes": anim, "texture_bytes": tex,
		"other_bytes": len(data) - anim - tex, "bodyparts": parts,
	}


def pack_groups(seqs, group_bytes, max_groups):
	"""Greedy, order-preserving bins of sequence indices. Returns (bins, kept)."""
	room = group_bytes - SEQHDR_SIZE
	bins, kept = [], []
	cur, used = [], 0
	for i, s in enumerate(seqs):
		if s["seqgroup"] != 0:
			continue
		size = (s["animend"] - s["animindex"] + 3) & ~3
		if size > room:
			kept.append(i)	# too big for one slot: stays in the core file
			continue
		if cur and used + size > room:
			bins.append(cur)
			cur, used = [], 0
		cur.append(i)
		used += size
	if cur:
		bins.append(cur)
	if len(bins) > max_groups - 1:
		for extra in bins[max_groups - 1:]:
			kept.extend(extra)
		bins = bins[:max_groups - 1]
	return bins, sorted(kept)


class Remap:
	"""Offset translation after removing byte ranges from a file."""

	def __init__(self, ranges, size):
		merged = []
		for s, e in sorted((( s + 3 ) & ~3, e & ~3 ) for s, e in ranges ):
			e = min(e, size & ~3) if e > size else e
			if e <= s:
				continue
			if merged and s <= merged[-1][1]:
				merged[-1] = (merged[-1][0], max(merged[-1][1], e))
			else:
				merged.append((s, e))
		self.ranges = merged

	def __call__(self, off):
		gone = 0
		for s, e in self.ranges:
			if off >= e:
				gone += e - s
			elif off > s:
				raise StudioError("offset %d points into a removed range" % off)
			else:
				break
		return off - gone

	def apply(self, data):
		out, pos = bytearray(), 0
		for s, e in self.ranges:
			out += data[pos:s]
			pos = e
		out += data[pos:]
		return out


def _fix(buf, off, remap):
	"""Translate one stored offset in place (0 means 'none' and stays 0)."""
	val = _u32(buf, off)
	if val:
		_put(buf, off, remap(val))


def split(data, name, group_bytes=66 * 1024, max_groups=16):
	"""Return {filename: bytes} for the split model."""
	hdr = read_header(data)
	if hdr["numseqgroups"] > 1:
		raise StudioError("model already uses sequence groups")
	seqs = read_sequences(data, hdr)
	bins, kept = pack_groups(seqs, group_bytes, max_groups)

	blocks = sorted((seqs[i]["animindex"], seqs[i]["animend"]) for b in bins for i in b)
	for (s0, e0), (s1, _) in zip(blocks, blocks[1:]):
		if s1 < e0:
			raise StudioError("animation blocks overlap; cannot move them")

	out = {}
	removed = []
	src = bytearray(data)

	# Animation files. animindex becomes relative to the group file.
	for g, members in enumerate(bins, start=1):
		body = bytearray()
		for i in members:
			s = seqs[i]
			while len(body) % 4:
				body.append(0)
			_put(src, s["off"] + SEQ_ANIMINDEX, SEQHDR_SIZE + len(body))
			_put(src, s["off"] + SEQ_SEQGROUP, g)
			s["moved"] = True
			body += data[s["animindex"]:s["animend"]]
			removed.append((s["animindex"], s["animend"]))
		fname = "%s%02d.mdl" % (name, g)
		seqhdr = IDSQ + struct.pack("<i", VERSION) + fname.encode().ljust(64, b"\0")[:64]
		seqhdr += struct.pack("<i", SEQHDR_SIZE + len(body))
		out[fname] = bytes(seqhdr + body)

	# Texture file: a studio header that only describes textures and skins.
	tex_ranges = texture_ranges(data, hdr)
	if tex_ranges:
		thdr = bytearray(data[:HDR_SIZE])
		for f in HDR_FIELDS:
			_put(thdr, 140 + 4 * HDR_FIELDS.index(f), 0)
		n = hdr["numtextures"]
		tex = bytearray(data[hdr["textureindex"]:hdr["textureindex"] + n * TEX_SIZE])
		skins = bytes(data[hdr["skinindex"]:hdr["skinindex"] + 2 * hdr["numskinref"] * hdr["numskinfamilies"]])
		skin_off = HDR_SIZE + len(tex)
		pix_off = (skin_off + len(skins) + 3) & ~3
		pixels = bytearray()
		for i in range(n):
			w, h, idx = struct.unpack_from("<3i", tex, i * TEX_SIZE + 68)
			_put(tex, i * TEX_SIZE + 76, pix_off + len(pixels))
			pixels += data[idx:idx + w * h + PALETTE]
			while len(pixels) % 4:
				pixels.append(0)
		tfile = thdr + tex + skins
		tfile += b"\0" * (pix_off - len(tfile)) + pixels

		def tset(field, val):
			_put(tfile, 140 + 4 * HDR_FIELDS.index(field), val)

		tset("numtextures", n)
		tset("textureindex", HDR_SIZE)
		tset("texturedataindex", pix_off)
		tset("numskinref", hdr["numskinref"])
		tset("numskinfamilies", hdr["numskinfamilies"])
		tset("skinindex", skin_off)
		_put(tfile, 72, len(tfile))
		out[name + "T.mdl"] = bytes(tfile)
		removed += tex_ranges
		for f in ("numtextures", "textureindex", "texturedataindex", "numskinref", "numskinfamilies", "skinindex"):
			_put(src, 140 + 4 * HDR_FIELDS.index(f), 0)

	# Core file: translate every stored offset, then cut the removed ranges.
	remap = Remap(removed, len(data))
	for f in HDR_INDEX_FIELDS:
		_fix(src, 140 + 4 * HDR_FIELDS.index(f), remap)
	for s in seqs:
		for field in (SEQ_EVENTINDEX, SEQ_PIVOTINDEX, SEQ_AUTOMOVEPOS, SEQ_AUTOMOVEANGLE):
			_fix(src, s["off"] + field, remap)
		if s["seqgroup"] == 0 and not s.get("moved"):
			_fix(src, s["off"] + SEQ_ANIMINDEX, remap)
	for b in range(hdr["numbodyparts"]):
		bp = hdr["bodypartindex"] + b * BODYPART_SIZE
		nummodels, modelindex = _u32(data, bp + 64), _u32(data, bp + 72)
		_fix(src, bp + 72, remap)
		for m in range(nummodels):
			mo = modelindex + m * MODEL_SIZE
			nummesh, meshindex = _u32(data, mo + 72), _u32(data, mo + 76)
			for field in (76, 84, 88, 96, 100, 108):	# mesh, vertinfo, vert, norminfo, norm, groups
				_fix(src, mo + field, remap)
			for k in range(nummesh):
				_fix(src, meshindex + k * MESH_SIZE + 4, remap)	# triindex
	core = remap.apply(src)

	# New sequence group table at the end of the core file.
	while len(core) % 4:
		core.append(0)
	table_off = len(core)
	core += b"default".ljust(32, b"\0") + b"\0" * 72
	for g in range(1, len(bins) + 1):
		fname = "models/%s%02d.mdl" % (name, g)
		core += ("group%d" % g).encode().ljust(32, b"\0") + fname.encode().ljust(64, b"\0")[:64] + b"\0" * 8
	_put(core, 140 + 4 * HDR_FIELDS.index("numseqgroups"), len(bins) + 1)
	_put(core, 140 + 4 * HDR_FIELDS.index("seqgroupindex"), table_off)
	_put(core, 72, len(core))
	out[name + ".mdl"] = bytes(core)
	return out, {"groups": len(bins), "kept_in_core": [seqs[i]["label"] for i in kept]}


def main(argv=None):
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	sub = ap.add_subparsers(dest="cmd", required=True)
	p_info = sub.add_parser("info")
	p_info.add_argument("model")
	p_split = sub.add_parser("split")
	p_split.add_argument("model")
	p_split.add_argument("outdir")
	p_split.add_argument("--group-bytes", type=int, default=66 * 1024)
	p_split.add_argument("--max-groups", type=int, default=16)
	args = ap.parse_args(argv)

	with open(args.model, "rb") as f:
		data = f.read()
	name = os.path.splitext(os.path.basename(args.model))[0]
	try:
		if args.cmd == "info":
			r = info(data)
			h = r["hdr"]
			print("%s: %d bytes, bones=%d seq=%d seqgroups=%d textures=%d bodyparts=%d" % (
				name, len(data), h["numbones"], h["numseq"], h["numseqgroups"],
				h["numtextures"], h["numbodyparts"]))
			print("  animation %d B, textures %d B, mesh/other %d B" % (
				r["anim_bytes"], r["texture_bytes"], r["other_bytes"]))
			for label, n in r["bodyparts"]:
				print("  bodypart %-20s models=%d" % (label, n))
			big = max(r["seqs"], key=lambda s: s.get("animend", 0) - s["animindex"], default=None)
			if big and "animend" in big:
				print("  largest sequence %s: %d B" % (big["label"], big["animend"] - big["animindex"]))
			return 0
		files, report = split(data, name, args.group_bytes, args.max_groups)
	except StudioError as e:
		print("error: %s" % e, file=sys.stderr)
		return 1
	os.makedirs(args.outdir, exist_ok=True)
	for fname, blob in sorted(files.items()):
		with open(os.path.join(args.outdir, fname), "wb") as f:
			f.write(blob)
		print("%-24s %8d B" % (fname, len(blob)))
	print("sequence groups: %d" % report["groups"])
	if report["kept_in_core"]:
		print("kept in core (too large or over --max-groups): %s" % ", ".join(report["kept_in_core"]))
	return 0


if __name__ == "__main__":
	sys.exit(main())
