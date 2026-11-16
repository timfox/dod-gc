"""Round-trip tests for tools/split_studio_mdl.py on a synthetic v10 model."""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import split_studio_mdl as ssm  # noqa: E402

NUMBONES = 2


def _anim_block(numframes, seed):
	"""panim table for NUMBONES bones, one blend, plus RLE runs."""
	table = bytearray(NUMBONES * 12)
	values = bytearray()
	for bone in range(NUMBONES):
		for chan in (0, 3):
			off = NUMBONES * 12 + len(values) - bone * 12
			struct.pack_into("<H", table, bone * 12 + chan * 2, off)
			left = numframes
			while left > 0:
				run = min(left, 3)
				values += bytes([run, run])
				for k in range(run):
					values += struct.pack("<h", seed * 100 + bone * 10 + chan + k)
				left -= run
	return bytes(table + values)


def build_model(frames=(5, 9, 4, 7)):
	"""Layout: header, seqdescs, event, anims, bodypart, model, mesh, verts, tris, textures, skin, pixels."""
	buf = bytearray(ssm.HDR_SIZE)
	buf[0:4] = b"IDST"
	struct.pack_into("<i", buf, 4, 10)
	buf[8:14] = b"player"
	fields = {f: 0 for f in ssm.HDR_FIELDS}
	fields["numbones"] = NUMBONES
	fields["numseq"] = len(frames)
	fields["seqindex"] = len(buf)
	seq_off = len(buf)
	buf += bytearray(ssm.SEQ_SIZE * len(frames))
	event_off = len(buf)
	buf += b"EVENTDATA123"
	anims = []
	for i, nf in enumerate(frames):
		block = _anim_block(nf, i + 1)
		while len(buf) % 4:
			buf.append(0)
		so = seq_off + i * ssm.SEQ_SIZE
		buf[so:so + 4] = b"seq%d" % i
		struct.pack_into("<i", buf, so + ssm.SEQ_NUMFRAMES, nf)
		struct.pack_into("<i", buf, so + ssm.SEQ_NUMBLENDS, 1)
		struct.pack_into("<i", buf, so + ssm.SEQ_ANIMINDEX, len(buf))
		if i == 0:
			struct.pack_into("<i", buf, so + ssm.SEQ_EVENTINDEX, event_off)
		anims.append(block)
		buf += block
	while len(buf) % 4:
		buf.append(0)
	fields["numbodyparts"] = 1
	fields["bodypartindex"] = len(buf)
	bp = len(buf)
	buf += bytearray(ssm.BODYPART_SIZE)
	buf[bp:bp + 4] = b"body"
	struct.pack_into("<i", buf, bp + 64, 1)
	struct.pack_into("<i", buf, bp + 72, len(buf))
	mo = len(buf)
	buf += bytearray(ssm.MODEL_SIZE)
	struct.pack_into("<i", buf, mo + 72, 1)
	struct.pack_into("<i", buf, mo + 76, len(buf))
	me = len(buf)
	buf += bytearray(ssm.MESH_SIZE)
	struct.pack_into("<i", buf, mo + 88, len(buf))
	verts = b"VERTVERTVERTVERT"
	buf += verts
	struct.pack_into("<i", buf, me + 4, len(buf))
	tris = b"TRISTRISTRIS"
	buf += tris
	fields["numtextures"] = 1
	fields["textureindex"] = len(buf)
	tx = len(buf)
	buf += bytearray(ssm.TEX_SIZE)
	buf[tx:tx + 8] = b"skin.bmp"
	fields["numskinref"] = 1
	fields["numskinfamilies"] = 1
	fields["skinindex"] = len(buf)
	buf += b"\0\0\0\0"
	fields["texturedataindex"] = len(buf)
	pixels = bytes(range(16)) + bytes(ssm.PALETTE)
	struct.pack_into("<3i", buf, tx + 68, 4, 4, len(buf))
	buf += pixels
	for i, f in enumerate(ssm.HDR_FIELDS):
		struct.pack_into("<i", buf, 140 + 4 * i, fields[f])
	struct.pack_into("<i", buf, 72, len(buf))
	return bytes(buf), anims, verts, tris, pixels


class SplitTest(unittest.TestCase):
	def setUp(self):
		self.data, self.anims, self.verts, self.tris, self.pixels = build_model()

	def test_info_counts_bytes(self):
		r = ssm.info(self.data)
		self.assertEqual(r["anim_bytes"], sum(len(a) for a in self.anims))
		self.assertEqual(r["bodyparts"], [("body", 1)])

	def test_split_round_trip(self):
		files, report = ssm.split(self.data, "player", group_bytes=ssm.SEQHDR_SIZE + 150)
		core = files["player.mdl"]
		hdr = ssm.read_header(core)
		self.assertEqual(hdr["length"], len(core))
		self.assertEqual(hdr["numtextures"], 0)
		self.assertEqual(hdr["numseqgroups"], report["groups"] + 1)
		self.assertGreater(report["groups"], 1)
		self.assertLess(len(core), len(self.data))
		# Every sequence's animation bytes come back unchanged from its group.
		for i, want in enumerate(self.anims):
			so = hdr["seqindex"] + i * ssm.SEQ_SIZE
			group = struct.unpack_from("<i", core, so + ssm.SEQ_SEQGROUP)[0]
			idx = struct.unpack_from("<i", core, so + ssm.SEQ_ANIMINDEX)[0]
			self.assertGreater(group, 0)
			blob = files["player%02d.mdl" % group]
			self.assertEqual(blob[:4], b"IDSQ")
			self.assertEqual(struct.unpack_from("<i", blob, 72)[0], len(blob))
			self.assertLessEqual(len(blob), ssm.SEQHDR_SIZE + 150)
			self.assertEqual(blob[idx:idx + len(want)], want)
			end = ssm.anim_extent(blob, idx, 1, NUMBONES, struct.unpack_from("<i", core, so + ssm.SEQ_NUMFRAMES)[0])
			self.assertEqual(end - idx, len(want))
		# Event data and meshes are still reachable through the moved offsets.
		ev = struct.unpack_from("<i", core, hdr["seqindex"] + ssm.SEQ_EVENTINDEX)[0]
		self.assertEqual(core[ev:ev + 12], b"EVENTDATA123")
		bp = hdr["bodypartindex"]
		self.assertEqual(core[bp:bp + 4], b"body")
		mo = struct.unpack_from("<i", core, bp + 72)[0]
		vert = struct.unpack_from("<i", core, mo + 88)[0]
		self.assertEqual(core[vert:vert + len(self.verts)], self.verts)
		me = struct.unpack_from("<i", core, mo + 76)[0]
		tri = struct.unpack_from("<i", core, me + 4)[0]
		self.assertEqual(core[tri:tri + len(self.tris)], self.tris)
		# Group table names the files the engine will ask for.
		gt = hdr["seqgroupindex"] + ssm.SEQGROUP_SIZE
		self.assertEqual(core[gt + 32:gt + 32 + 19], b"models/player01.mdl")

	def test_texture_file(self):
		files, _ = ssm.split(self.data, "player")
		tfile = files["playerT.mdl"]
		thdr = ssm.read_header(tfile)
		self.assertEqual(thdr["numtextures"], 1)
		self.assertEqual(thdr["numbones"], 0)
		self.assertEqual(thdr["length"], len(tfile))
		t = thdr["textureindex"]
		self.assertEqual(tfile[t:t + 8], b"skin.bmp")
		w, h, idx = struct.unpack_from("<3i", tfile, t + 68)
		self.assertEqual((w, h), (4, 4))
		self.assertEqual(tfile[idx:idx + len(self.pixels)], self.pixels)
		self.assertEqual(idx, thdr["texturedataindex"])

	def test_oversize_sequence_stays_in_core(self):
		files, report = ssm.split(self.data, "player", group_bytes=ssm.SEQHDR_SIZE + 100)
		self.assertIn("seq1", report["kept_in_core"])
		core = files["player.mdl"]
		hdr = ssm.read_header(core)
		so = hdr["seqindex"] + ssm.SEQ_SIZE
		self.assertEqual(struct.unpack_from("<i", core, so + ssm.SEQ_SEQGROUP)[0], 0)
		idx = struct.unpack_from("<i", core, so + ssm.SEQ_ANIMINDEX)[0]
		self.assertEqual(core[idx:idx + len(self.anims[1])], self.anims[1])

	def test_max_groups_keeps_the_rest_in_core(self):
		files, report = ssm.split(self.data, "player", group_bytes=ssm.SEQHDR_SIZE + 100, max_groups=2)
		self.assertEqual(report["groups"], 1)
		self.assertNotIn("player02.mdl", files)

	def test_pack_is_big_endian_and_aligned(self):
		files, report = ssm.split(self.data, "player", pack_path="models/player/anim_%s.gca")
		self.assertEqual(report["groups"], 1)
		pack_name = [n for n in files if n.endswith(".gca")][0]
		pack = files[pack_name]
		self.assertEqual(pack[:4], b"GCAP")
		version, numseq, numbones, data_off, total, _ = struct.unpack_from(">6I", pack, 4)
		self.assertEqual((version, numseq, numbones, total), (1, len(self.anims), NUMBONES, len(pack)))
		self.assertEqual(data_off % 32, 0)
		core = files["player.mdl"]
		hdr = ssm.read_header(core)
		# The core names the pack and carries no animation of its own.
		gt = hdr["seqgroupindex"] + ssm.SEQGROUP_SIZE
		self.assertEqual(core[gt + 32:gt + 96].split(b"\0")[0].decode(), "models/player/" + pack_name)
		self.assertNotIn(self.anims[0], core)
		for i, want in enumerate(self.anims):
			so = hdr["seqindex"] + i * ssm.SEQ_SIZE
			self.assertEqual(struct.unpack_from("<i", core, so + ssm.SEQ_SEQGROUP)[0], 1)
			off, size = struct.unpack_from(">2I", pack, ssm.PACK_HDR_SIZE + i * 8)
			self.assertEqual(off % 32, 0)
			self.assertEqual(size, len(want))
			block = pack[off:off + size]
			# Table offsets and values read back big-endian equal the originals.
			self.assertEqual(struct.unpack_from(">6H", block, 0), struct.unpack_from("<6H", want, 0))
			first = struct.unpack_from("<6H", want, 0)[0]
			self.assertEqual(block[first:first + 2], want[first:first + 2])	# valid/total bytes
			self.assertEqual(struct.unpack_from(">h", block, first + 2), struct.unpack_from("<h", want, first + 2))
			# Swapping twice restores the little-endian block.
			nf = struct.unpack_from("<i", core, so + ssm.SEQ_NUMFRAMES)[0]
			self.assertEqual(len(ssm.swap_anim_block(want, 1, NUMBONES, nf)), len(want))

	def test_identical_animation_shares_a_pack_name(self):
		a, _ = ssm.split(self.data, "axis", pack_path="models/player/anim_%s.gca")
		b, _ = ssm.split(self.data, "allies", pack_path="models/player/anim_%s.gca")
		self.assertEqual([n for n in a if n.endswith(".gca")], [n for n in b if n.endswith(".gca")])

	def test_rejects_non_studio(self):
		with self.assertRaises(ssm.StudioError):
			ssm.split(b"nope" + bytes(300), "x")


def build_body_model():
	"""Texture-less core: one bodypart with two models whose mesh data sits
	before a sequence's event and animation data (so offsets must move)."""
	buf = bytearray(ssm.HDR_SIZE)
	buf[0:4] = b"IDST"
	struct.pack_into("<i", buf, 4, 10)
	fields = {f: 0 for f in ssm.HDR_FIELDS}
	fields["numbones"] = NUMBONES
	fields["numseqgroups"] = 1
	fields["seqgroupindex"] = len(buf)
	buf += bytearray(ssm.SEQGROUP_SIZE)
	fields["numbodyparts"] = 1
	fields["bodypartindex"] = bp = len(buf)
	buf += bytearray(ssm.BODYPART_SIZE)
	buf[bp:bp + 5] = b"heads"
	struct.pack_into("<i", buf, bp + 64, 2)
	struct.pack_into("<i", buf, bp + 72, len(buf))
	mos = [len(buf), len(buf) + ssm.MODEL_SIZE]
	buf += bytearray(2 * ssm.MODEL_SIZE)
	want = []
	for m, mo in enumerate(mos):
		buf[mo:mo + 5] = b"head%d" % m
		nv, nn = 3 + m, 2
		verts = [float(m * 100 + k) for k in range(nv * 3)]
		norms = [float(-k - m) for k in range(nn * 3)]
		tris = [3, 0, 0, 0, 0, 1, 1, 0, 0, 2, 0, 0, 0, -3, 1, 1, 0, 0, 2, 1, 0, 0, nv - 1, 0, 0, 0, 0]
		struct.pack_into("<2i", buf, mo + 72, 1, len(buf))
		me = len(buf)
		buf += bytearray(ssm.MESH_SIZE)
		struct.pack_into("<5i", buf, me, 2, 0, m, nn, 0)
		struct.pack_into("<i", buf, me + 4, len(buf))
		buf += struct.pack("<%dh" % len(tris), *tris)
		struct.pack_into("<3i", buf, mo + 80, nv, len(buf), len(buf) + nv)
		buf += bytes([m] * nv)
		buf += struct.pack("<%df" % len(verts), *verts)
		struct.pack_into("<3i", buf, mo + 92, nn, len(buf), len(buf) + nn)
		buf += bytes([1] * nn)
		buf += struct.pack("<%df" % len(norms), *norms)
		while len(buf) % 4:
			buf.append(0)
		want.append((verts, norms, tris, nv, nn))
	fields["numseq"] = 1
	fields["seqindex"] = so = len(buf)
	buf += bytearray(ssm.SEQ_SIZE)
	buf[so:so + 4] = b"idle"
	event = len(buf)
	buf += b"EVENTDATA123"
	struct.pack_into("<i", buf, so + ssm.SEQ_EVENTINDEX, event)
	struct.pack_into("<i", buf, so + ssm.SEQ_NUMFRAMES, 3)
	struct.pack_into("<i", buf, so + ssm.SEQ_NUMBLENDS, 1)
	anim = _anim_block(3, 7)
	struct.pack_into("<i", buf, so + ssm.SEQ_ANIMINDEX, len(buf))
	buf += anim
	for i, f in enumerate(ssm.HDR_FIELDS):
		struct.pack_into("<i", buf, 140 + 4 * i, fields[f])
	struct.pack_into("<i", buf, 72, len(buf))
	return bytes(buf), want, anim


class StreamMeshesTest(unittest.TestCase):
	def setUp(self):
		self.core, self.want, self.anim = build_body_model()
		self.out, self.packs = ssm.stream_meshes(self.core, "models/player/mesh_%s.gcm")

	def test_core_shrinks_and_names_the_pack(self):
		out = self.out
		hdr = ssm.read_header(out)
		self.assertEqual(hdr["length"], len(out))
		self.assertLess(len(out), len(self.core) + ssm.SEQGROUP_SIZE * 2)
		self.assertEqual(hdr["numseqgroups"], 2)
		(name, pack), = self.packs.items()
		gt = hdr["seqgroupindex"] + ssm.SEQGROUP_SIZE
		self.assertEqual(out[gt:gt + 6], b"meshes")
		self.assertEqual(out[gt + 32:gt + 96].split(b"\0")[0].decode(), "models/player/" + name)
		self.assertTrue(name.startswith("mesh_") and name.endswith(ssm.MESH_PACK_EXT))
		# Sequence data still reachable through moved offsets.
		so = hdr["seqindex"]
		ev = struct.unpack_from("<i", out, so + ssm.SEQ_EVENTINDEX)[0]
		self.assertEqual(out[ev:ev + 12], b"EVENTDATA123")
		ai = struct.unpack_from("<i", out, so + ssm.SEQ_ANIMINDEX)[0]
		self.assertEqual(out[ai:ai + len(self.anim)], self.anim)
		self.assertEqual(out[hdr["bodypartindex"]:hdr["bodypartindex"] + 5], b"heads")

	def test_pack_blobs_read_back_big_endian(self):
		out = self.out
		hdr = ssm.read_header(out)
		(_, pack), = self.packs.items()
		self.assertEqual(pack[:4], ssm.PACK_MAGIC)
		version, numseq, numbones, data_off, total, _ = struct.unpack_from(">6I", pack, 4)
		self.assertEqual((version, numseq, numbones, total), (1, 2, 0, len(pack)))
		mi = struct.unpack_from("<i", out, hdr["bodypartindex"] + 72)[0]
		for m, (verts, norms, tris, nv, nn) in enumerate(self.want):
			mo = mi + m * ssm.MODEL_SIZE
			self.assertEqual(out[mo:mo + 5], b"head%d" % m)
			unused, entry, nummesh, meshindex, numverts, vinfo, vidx, numnorms, ninfo, nidx, b1, b2 = \
				struct.unpack_from("<12i", out, mo + 64)
			self.assertEqual((unused, entry, nummesh, numverts, numnorms, b1, b2),
				(ssm.MESH_STREAMED, m, 1, nv, nn, 0, 0))
			off, size = struct.unpack_from(">2I", pack, ssm.PACK_HDR_SIZE + m * 8)
			self.assertEqual(off % 32, 0)
			self.assertEqual(size % 32, 0)
			blob = pack[off:off + size]
			self.assertEqual(blob[:4], b"GCMB")
			for v in (meshindex, vidx, nidx):
				self.assertEqual(v % 4, 0)
			self.assertEqual(list(struct.unpack_from(">%df" % len(verts), blob, vidx)), verts)
			self.assertEqual(list(struct.unpack_from(">%df" % len(norms), blob, nidx)), norms)
			self.assertEqual(blob[vinfo:vinfo + nv], bytes([m] * nv))
			self.assertEqual(blob[ninfo:ninfo + nn], bytes([1] * nn))
			numtris, triindex, skinref, mnorms, _ = struct.unpack_from(">5i", blob, meshindex)
			self.assertEqual((numtris, skinref, mnorms), (2, m, nn))
			self.assertEqual(list(struct.unpack_from(">%dh" % len(tris), blob, triindex)), tris)
			self.assertEqual(triindex % 4, 0)

	def test_mesh_bytes_leave_the_core(self):
		verts = struct.pack("<%df" % len(self.want[0][0]), *self.want[0][0])
		self.assertIn(verts, self.core)
		self.assertNotIn(verts, self.out)

	def test_same_meshes_share_a_pack_name(self):
		_, again = ssm.stream_meshes(self.core, "models/player/mesh_%s.gcm")
		self.assertEqual(list(again), list(self.packs))

	def test_rejects_textured_model(self):
		data = build_model()[0]
		with self.assertRaises(ssm.StudioError):
			ssm.stream_meshes(data, "models/x.gcm")


if __name__ == "__main__":
	unittest.main()
