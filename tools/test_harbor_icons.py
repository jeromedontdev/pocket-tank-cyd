"""Verify original source artwork is reproducible and has real negative space."""
import unittest

from PIL import Image

from draw_harbor_icons import ROOT, RETIRED, draw


class HarborIconTests(unittest.TestCase):
    def test_retired_originals_are_not_shipped(self):
        for name in RETIRED:
            self.assertFalse((ROOT / "assets" / "icons" / f"{name}.png").exists())

    def test_source_geometry_materials_and_reproducibility(self):
        for path in sorted((ROOT / "assets" / "icons").glob("*.png")):
            if path.stem in RETIRED:
                continue
            with self.subTest(icon=path.name), Image.open(path) as source:
                actual = source.convert("RGBA")
                self.assertEqual(actual.tobytes(), draw(path.stem, actual.width).tobytes())
                pixels = list(actual.getdata())
                self.assertEqual({a for _, _, _, a in pixels}, {0, 255})
                opaque = [p for p in pixels if p[3]]
                self.assertGreater(len(opaque), actual.width * actual.height // 16)
                colors = {((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                          for r, g, b, _ in opaque}
                self.assertGreaterEqual(len(colors), 4)
                self.assertLessEqual(len(colors), 16)
                for x, y in ((0, 0), (actual.width-1, 0),
                             (0, actual.height-1), (actual.width-1, actual.height-1)):
                    self.assertEqual(actual.getpixel((x, y))[3], 0)

    def test_gate_and_shackle_holes_are_transparent(self):
        self.assertEqual(draw("shop_castle", 32).getpixel((16, 26))[3], 0)
        self.assertEqual(draw("tool_lock_closed", 24).getpixel((12, 6))[3], 0)
        self.assertEqual(draw("tool_lock_open", 24).getpixel((12, 6))[3], 0)


if __name__ == "__main__":
    unittest.main()
