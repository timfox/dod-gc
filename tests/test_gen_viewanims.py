"""tools/gen_viewanims.py: the sequence each retail view model plays for each weapon animation role."""
import importlib.util
import os
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gen_viewanims", ROOT / "tools" / "gen_viewanims.py")
G = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(G)

RETAIL = [Path(p) for p in (os.environ.get("DODGC_CONTENT_DOD", ""), "/home/tim/snap/steam/common/.local/share/Steam/steamapps/common/Half-Life/dod") if p]


def retail_models():
    for base in RETAIL:
        if (base / "models" / "v_garand.mdl").is_file():
            return base / "models"
    return None


class ResolveTest(unittest.TestCase):
    TOMMY = ["idle", "reload", "draw", "shoot1", "shoot2", "empty_idle"]

    def test_labels_become_indexes_in_the_models_own_order(self):
        r = G.resolve("v_tommy", G.MODELS["v_tommy"], self.TOMMY)
        self.assertEqual((r["idle"], r["idle_empty"], r["fire"], r["reload"], r["draw"]), (0, 5, [3, 4], 1, 2))
        self.assertEqual((r["fire_empty"], r["holster"], r["pinpull"]), (-1, -1, -1))   # no such role in this model

    def test_a_label_the_model_lacks_is_an_error(self):
        with self.assertRaises(ValueError):
            G.resolve("v_tommy", {"idle": "idle", "reload": "reloading"}, self.TOMMY)

    def test_render_pads_fire_variants_and_is_sorted(self):
        text = G.render({"v_b": G.resolve("v_b", {"idle": "idle", "fire": ["s"]}, ["idle", "s"]),
                         "v_a": G.resolve("v_a", {"idle": "idle", "fire": ["s1", "s2"]}, ["idle", "s1", "s2"])})
        self.assertLess(text.index("v_a.mdl"), text.index("v_b.mdl"))
        self.assertIn('{ "models/v_b.mdl", 0, -1, 1, { 1, -1, -1 }, -1, -1, -1, -1, -1 },', text)


class HeaderTest(unittest.TestCase):
    def test_header_has_a_row_for_every_model(self):
        rows = re.findall(r'\{ "models/(v_\w+)\.mdl"', (ROOT / "src" / "dod_viewanims.h").read_text())
        self.assertEqual(sorted(rows), sorted(G.MODELS))

    def test_every_view_model_the_game_can_use_has_a_row(self):
        used = set()
        for name in ("dod_weapons.c", "dod_game.c"):
            used |= set(re.findall(r'"models/(v_\w+)\.mdl"', (ROOT / "src" / name).read_text()))
        used.discard("v_crowbar")   # the HL1 model, not used in a DoD game
        self.assertFalse(used - set(G.MODELS), f"view models with no animation row: {sorted(used - set(G.MODELS))}")

    @unittest.skipUnless(retail_models(), "retail DoD models not available")
    def test_header_matches_the_retail_models(self):
        resolved = {m: G.resolve(m, spec, G.sequence_labels(retail_models() / f"{m}.mdl")) for m, spec in G.MODELS.items()}
        self.assertEqual((ROOT / "src" / "dod_viewanims.h").read_text(), G.render(resolved))


if __name__ == "__main__":
    unittest.main()
