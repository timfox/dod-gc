"""Every HUD function that draws pixels must be registered in dod_hud_geom.h and covered by the geometry test.

Background: an ammo readout drawn by a second path (the engine's lean route) overlapped the retail one and the overlap
test could not see it because the draw was not in the model. A new drawing function in hud_dod.cpp now fails here until it
is added to DOD_HUD_DRAW_FUNCTIONS and to the "covers:" line in tests/dod_hud_geom_test.c.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRAW = re.compile(r"pfnFillRGBABlend|DrawHudString|DrawHudNumber|SPR_Draw|pfnSPR_DrawGeneric|FillRGBA\(")


class HudDrawRegistryTests(unittest.TestCase):
    def test_drawing_functions_are_registered(self):
        src = (ROOT / "src/client/hud_dod.cpp").read_text()
        header = (ROOT / "src/dod_hud_geom.h").read_text()
        geom_test = (ROOT / "tests/dod_hud_geom_test.c").read_text()
        registered = set(re.findall(r'"(CHudDod::\w+)"', header.split("DOD_HUD_DRAW_FUNCTIONS", 1)[1].split("typedef", 1)[0]))
        covered = set(re.findall(r"CHudDod::\w+", geom_test.split("covers:", 1)[1].split("*/", 1)[0]))
        # function bodies: from "type CHudDod::Name(" to the next top-level definition
        parts = re.split(r"\n(?=(?:static )?\w[\w \*]*\bCHudDod::\w+\(|(?:static )?\w[\w \*]*\bDodHud_\w+\()", src)
        drawing = set()
        for part in parts:
            m = re.match(r"(?:static )?\w[\w \*]*\b(CHudDod::\w+)\(", part)
            if m and DRAW.search(part):
                drawing.add(m.group(1))
        self.assertTrue(drawing, "no drawing functions found: the parser is out of date")
        # Draw (the dispatcher) only calls DrawRetail; everything that draws pixels itself must be modelled
        self.assertEqual(sorted(drawing - registered), [], "drawing function not in DOD_HUD_DRAW_FUNCTIONS")
        self.assertEqual(sorted(drawing - covered), [], "drawing function not covered by dod_hud_geom_test.c")

    def test_retail_flag_is_set_only_when_it_draws(self):
        src = (ROOT / "src/client/hud_dod.cpp").read_text()
        self.assertIn('Cvar_SetValue( "gc_hud_retail", 0.0f )', src)
        self.assertIn('Cvar_SetValue( "gc_hud_retail", 1.0f )', src)


if __name__ == "__main__":
    unittest.main()
