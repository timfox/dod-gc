"""tools/gen_viewsounds.py: the sounds retail view models play from their animation events."""
import importlib.util
import os
import re
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gen_viewsounds", ROOT / "tools" / "gen_viewsounds.py")
G = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(G)

RETAIL = [Path(p) for p in (os.environ.get("DODGC_CONTENT_DOD", ""), "/home/tim/snap/steam/common/.local/share/Steam/steamapps/common/Half-Life/dod") if p]


def retail_models():
    for base in RETAIL:
        if (base / "models" / "v_garand.mdl").is_file():
            return base / "models"
    return None


def fake_model(events):
    """A minimal studio file with one sequence carrying `events` [(frame, event, option)] at 20 fps."""
    seq_at, ev_at = 400, 600
    data = bytearray(1200)
    data[0:4] = b"IDST"
    struct.pack_into("<ii", data, 164, 1, seq_at)
    data[seq_at:seq_at + 4] = b"idle"
    struct.pack_into("<fiiiii", data, seq_at + 32, 20.0, 0, 0, 0, len(events), ev_at)
    for i, (frame, event, option) in enumerate(events):
        struct.pack_into("<3i", data, ev_at + i * 76, frame, event, 0)
        data[ev_at + i * 76 + 12:ev_at + i * 76 + 12 + len(option)] = option.encode()
    return bytes(data)


class EventParseTest(unittest.TestCase):
    def test_only_sound_events_are_kept_and_sorted_by_frame(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "v_x.mdl"
            path.write_bytes(fake_model([(40, 5004, "weapons/b.wav"), (1, 5001, "31"), (10, 5004, "weapons/a.wav")]))
            (fps, events), = G.sequence_events(path)
        self.assertEqual(fps, 20.0)
        self.assertEqual(events, [(10, "weapons/a.wav"), (40, "weapons/b.wav")])

    def test_render_uses_milliseconds(self):
        text = G.render([("v_x", "DOD_ANIM_RELOAD", [(500, "weapons/a.wav"), (2000, "weapons/b.wav")])])
        self.assertIn('{ "models/v_x.mdl", DOD_ANIM_RELOAD, 2, { { 500, "weapons/a.wav" }, { 2000, "weapons/b.wav" } } },', text)


class HeaderTest(unittest.TestCase):
    def test_every_sound_is_a_retail_weapon_sound(self):
        names = set(re.findall(r'"(weapons/[^"]+\.wav)"', (ROOT / "src" / "dod_viewsounds.h").read_text()))
        self.assertIn("weapons/tommy_reload_clipout.wav", names)
        self.assertNotIn("weapons/gewehr_reload_clipin.wav", names)   # not an event of any retail view model

    @unittest.skipUnless(retail_models(), "retail DoD models not available")
    def test_header_matches_the_retail_models(self):
        self.assertEqual((ROOT / "src" / "dod_viewsounds.h").read_text(), G.render(G.collect(retail_models())))


if __name__ == "__main__":
    unittest.main()
