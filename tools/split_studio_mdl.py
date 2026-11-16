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
  split_studio_mdl.py split model.mdl outdir --pack models/player/anim_%s.gca

--pack writes one GameCube-native animation pack instead of sequence group
files: big-endian, one 32-byte aligned block per sequence with a directory, so
the engine can fetch a single sequence from ARAM with one DMA and no byte
swapping. The file name carries a content hash, so models with identical
animation share one pack.

  split_studio_mdl.py split model.mdl outdir --pack ... --mesh-pack models/player/mesh_%s.gcm

--mesh-pack also moves every body-group model's mesh data (triangle commands,
vertices, normals, bone bytes) out of the core into a big-endian pack. A DoD
soldier carries every outfit variant, but draws one model per body part, so
the engine keeps the pack in ARAM and fetches only what is drawn. Each moved
model is marked unused=MESH_STREAMED, unused2=entry, and its offsets are
relative to that entry's blob.
"""
import argparse
import hashlib
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
PACK_MAGIC = b"GCAP"
PACK_VERSION = 1
PACK_HDR_SIZE = 32

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


def swap_anim_block(block, numblends, numbones, numframes):
	"""Little-endian animation block to big-endian (GameCube native)."""
	out = bytearray(block)
	for k in range(numblends * numbones):
		base = k * 12
		offs = struct.unpack_from("<6H", block, base)
		struct.pack_into(">6H", out, base, *offs)
		for off in offs:
			if not off:
				continue
			p = base + off
			frames = 0
			while frames < numframes:
				valid, total = block[p], block[p + 1]
				for v in range(valid):
					q = p + 2 + 2 * v
					out[q], out[q + 1] = block[q + 1], block[q]
				p += 2 + 2 * valid
				frames += total
	return bytes(out)


def build_pack(data, hdr, seqs):
	"""GameCube animation pack: big-endian directory plus 32-byte aligned,
	pre-swapped sequence blocks, so one sequence is one ARAM DMA."""
	numseq = len(seqs)
	data_off = (PACK_HDR_SIZE + numseq * 8 + 31) & ~31
	body = bytearray()
	entries = []
	for s in seqs:
		if s["seqgroup"] != 0:
			entries.append((0, 0))
			continue
		block = swap_anim_block(data[s["animindex"]:s["animend"]], s["numblends"],
			hdr["numbones"], s["numframes"])
		entries.append((data_off + len(body), len(block)))
		body += block
		while len(body) % 32:
			body.append(0)
	head = PACK_MAGIC + struct.pack(">6I", PACK_VERSION, numseq, hdr["numbones"],
		data_off, data_off + len(body), 0) + b"\0\0\0\0"
	table = b"".join(struct.pack(">2I", o, n) for o, n in entries)
	blob = head + table
	blob += b"\0" * (data_off - len(blob)) + body
	return bytes(blob)


def split(data, name, group_bytes=66 * 1024, max_groups=16, pack_path=None):
	"""Return {filename: bytes} for the split model.

	With pack_path, all animation goes to one GameCube pack instead of
	sequence group files; pack_path may contain %s for the content hash."""
	hdr = read_header(data)
	if hdr["numseqgroups"] > 1:
		raise StudioError("model already uses sequence groups")
	seqs = read_sequences(data, hdr)
	if pack_path:
		bins, kept = [[i for i, s in enumerate(seqs) if s["seqgroup"] == 0]], []
	else:
		bins, kept = pack_groups(seqs, group_bytes, max_groups)

	blocks = sorted((seqs[i]["animindex"], seqs[i]["animend"]) for b in bins for i in b)
	for (s0, e0), (s1, _) in zip(blocks, blocks[1:]):
		if s1 < e0:
			raise StudioError("animation blocks overlap; cannot move them")

	out = {}
	removed = []
	src = bytearray(data)

	group_names = []
	if pack_path:
		pack = build_pack(data, hdr, seqs)
		if "%s" in pack_path:
			pack_path = pack_path % hashlib.sha1(pack).hexdigest()[:12]
		if len(pack_path) > 63:
			raise StudioError("pack path longer than 63 characters")
		out[os.path.basename(pack_path)] = pack
		group_names.append(pack_path)
		for i in bins[0]:
			s = seqs[i]
			_put(src, s["off"] + SEQ_ANIMINDEX, 0)
			_put(src, s["off"] + SEQ_SEQGROUP, 1)
			s["moved"] = True
			removed.append((s["animindex"], s["animend"]))
		bins = []

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
	if not group_names:
		group_names = ["models/%s%02d.mdl" % (name, g) for g in range(1, len(bins) + 1)]
	for g, fname in enumerate(group_names, start=1):
		core += ("group%d" % g).encode().ljust(32, b"\0") + fname.encode().ljust(64, b"\0")[:64] + b"\0" * 8
	_put(core, 140 + 4 * HDR_FIELDS.index("numseqgroups"), len(group_names) + 1)
	_put(core, 140 + 4 * HDR_FIELDS.index("seqgroupindex"), table_off)
	_put(core, 72, len(core))
	out[name + ".mdl"] = bytes(core)
	return out, {"groups": len(group_names), "kept_in_core": [seqs[i]["label"] for i in kept]}


# A streamed model's mstudiomodel_t "unused" field holds this value (the
# little-endian bytes "GCMS"); "unused2" holds the model's entry in the pack.
MESH_STREAMED = 0x534D4347
MESH_BLOB_HDR = 32
MESH_PACK_EXT = ".gcm"


def tricmd_end(data, off):
	"""End offset of one mesh's triangle command list (after the 0 short)."""
	p = off
	while True:
		if p + 2 > len(data):
			raise StudioError("triangle commands past end of file")
		n = struct.unpack_from("<h", data, p)[0]
		p += 2
		if n == 0:
			return p
		p += abs(n) * 8


def _be_words(block, fmt_le, fmt_be, size):
	out = bytearray()
	for k in range(0, len(block) - len(block) % size, size):
		out += struct.pack(fmt_be, *struct.unpack_from(fmt_le, block, k))
	return out


def _mesh_blob(data, mo):
	"""One model's mesh data as a big-endian blob plus the ranges it came from.

	Layout: 32-byte header ("GCMB"), meshes, triangle commands, vertices,
	normals, vertex bone bytes, normal bone bytes. Returns (blob, fields,
	ranges); fields are the model header's new blob-relative offsets."""
	nummesh, meshindex = _u32(data, mo + 72), _u32(data, mo + 76)
	numverts, vertinfo, vert = _u32(data, mo + 80), _u32(data, mo + 84), _u32(data, mo + 88)
	numnorms, norminfo, norm = _u32(data, mo + 92), _u32(data, mo + 96), _u32(data, mo + 100)
	ranges = []
	blob = bytearray(b"GCMB" + bytes(MESH_BLOB_HDR - 4))
	fields = {}

	mesh_out = MESH_BLOB_HDR
	blob += bytes(nummesh * MESH_SIZE)
	ranges.append((meshindex, meshindex + nummesh * MESH_SIZE))
	fields["meshindex"] = mesh_out if nummesh else 0
	for k in range(nummesh):
		me = meshindex + k * MESH_SIZE
		numtris, triindex, skinref, mnumnorms, mnormindex = struct.unpack_from("<5i", data, me)
		end = tricmd_end(data, triindex)
		ranges.append((triindex, end))
		tri_out = len(blob)
		blob += _be_words(data[triindex:end], "<h", ">h", 2)
		while len(blob) % 4:
			blob.append(0)
		struct.pack_into(">5i", blob, mesh_out + k * MESH_SIZE, numtris, tri_out, skinref, mnumnorms, 0)

	def take(off, nbytes, word):
		if not nbytes:
			return 0
		ranges.append((off, off + nbytes))
		at = len(blob)
		chunk = data[off:off + nbytes]
		blob.extend(_be_words(chunk, "<f", ">f", 4) if word else chunk)
		while len(blob) % 4:
			blob.append(0)
		return at

	fields["vertindex"] = take(vert, numverts * 12, True)
	fields["normindex"] = take(norm, numnorms * 12, True)
	fields["vertinfoindex"] = take(vertinfo, numverts, False)
	fields["norminfoindex"] = take(norminfo, numnorms, False)
	while len(blob) % 32:
		blob.append(0)
	return bytes(blob), fields, ranges


def stream_meshes(core, pack_path):
	"""Move every body-group model's mesh data out of a core into a pack.

	Only one model per body part is drawn at a time, but a DoD soldier core
	carries every outfit (bodies, heads, helmets, gear). The pack (GCAP
	layout, one 32-byte aligned big-endian blob per model, in body part
	order) goes to ARAM and the engine fetches the drawn models' blobs.
	Returns (new_core, {pack_basename: pack}); pack_path may hold %s for the
	content hash and is recorded as an extra sequence group."""
	hdr = read_header(core)
	if hdr["numtextures"]:
		raise StudioError("stream_meshes expects a core without textures")
	src = bytearray(core)
	entries, removed, models = [], [], []
	for b in range(hdr["numbodyparts"]):
		bp = hdr["bodypartindex"] + b * BODYPART_SIZE
		nummodels, modelindex = _u32(core, bp + 64), _u32(core, bp + 72)
		for m in range(nummodels):
			mo = modelindex + m * MODEL_SIZE
			blob, fields, ranges = _mesh_blob(core, mo)
			models.append((mo, fields, len(entries)))
			entries.append(blob)
			removed += [r for r in ranges if r[1] > r[0]]

	numseq = len(entries)
	data_off = (PACK_HDR_SIZE + numseq * 8 + 31) & ~31
	body, table = bytearray(), []
	for blob in entries:
		table.append((data_off + len(body), len(blob)))
		body += blob
	pack = PACK_MAGIC + struct.pack(">6I", PACK_VERSION, numseq, 0, data_off, data_off + len(body), 0) + b"\0\0\0\0"
	pack += b"".join(struct.pack(">2I", o, n) for o, n in table)
	pack += b"\0" * (data_off - len(pack)) + body
	pack = bytes(pack)
	if "%s" in pack_path:
		pack_path = pack_path % hashlib.sha1(pack).hexdigest()[:12]
	if len(pack_path) > 63:
		raise StudioError("pack path longer than 63 characters")

	# Model headers: mark streamed, blob-relative offsets. Not remapped below.
	for mo, fields, entry in models:
		_put(src, mo + 64, MESH_STREAMED)
		_put(src, mo + 68, entry)
		for name, off in (("meshindex", 76), ("vertinfoindex", 84), ("vertindex", 88),
				("norminfoindex", 96), ("normindex", 100)):
			_put(src, mo + off, fields[name])
		_put(src, mo + 104, 0)
		_put(src, mo + 108, 0)

	remap = Remap(removed, len(core))
	for f in HDR_INDEX_FIELDS:
		_fix(src, 140 + 4 * HDR_FIELDS.index(f), remap)
	for s in read_sequences(core, hdr):
		for field in (SEQ_EVENTINDEX, SEQ_PIVOTINDEX, SEQ_AUTOMOVEPOS, SEQ_AUTOMOVEANGLE):
			_fix(src, s["off"] + field, remap)
		if s["seqgroup"] == 0:
			_fix(src, s["off"] + SEQ_ANIMINDEX, remap)
	for b in range(hdr["numbodyparts"]):
		_fix(src, hdr["bodypartindex"] + b * BODYPART_SIZE + 72, remap)
	out = remap.apply(src)

	# Append the pack as one more sequence group (no sequence points at it).
	old_tab = _u32(out, 140 + 4 * HDR_FIELDS.index("seqgroupindex"))
	ngroups = _u32(out, 140 + 4 * HDR_FIELDS.index("numseqgroups"))
	table_bytes = bytes(out[old_tab:old_tab + ngroups * SEQGROUP_SIZE])
	while len(out) % 4:
		out.append(0)
	new_tab = len(out)
	out += table_bytes
	out += b"meshes".ljust(32, b"\0") + pack_path.encode().ljust(64, b"\0")[:64] + b"\0" * 8
	_put(out, 140 + 4 * HDR_FIELDS.index("numseqgroups"), ngroups + 1)
	_put(out, 140 + 4 * HDR_FIELDS.index("seqgroupindex"), new_tab)
	_put(out, 72, len(out))
	return bytes(out), {os.path.basename(pack_path): pack}


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
	p_split.add_argument("--pack", metavar="PATH",
		help="write all animation to one GameCube pack; PATH is the in-game path, %%s = content hash "
		"(e.g. models/player/anim_%%s.gca)")
	p_split.add_argument("--mesh-pack", metavar="PATH",
		help="stream body-group meshes from one GameCube pack; %%s = content hash "
		"(e.g. models/player/mesh_%%s.gcm)")
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
			sizes = sorted(((s["animend"] - s["animindex"], s["label"]) for s in r["seqs"] if "animend" in s), reverse=True)
			for limit in (16, 32, 66, 128, 256):
				print("  sequences over %3d KiB: %d" % (limit, sum(1 for n, _ in sizes if n > limit * 1024)))
			for n, label in sizes[:12]:
				print("  %8d B  %s" % (n, label))
			return 0
		files, report = split(data, name, getattr(args, "group_bytes", 0), getattr(args, "max_groups", 0), getattr(args, "pack", None))
		if getattr(args, "mesh_pack", None):
			core, packs = stream_meshes(files[name + ".mdl"], args.mesh_pack)
			files[name + ".mdl"] = core
			files.update(packs)
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
