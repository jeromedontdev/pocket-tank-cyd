import pathlib
import re
import struct
import tempfile
import unittest
import zlib
from pack_cyd_icons import pack


class IconBankTests(unittest.TestCase):
    def test_planes_offsets_alignment_and_crc(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / "icons.c"
            source.write_text(
                "static const uint16_t a_rgb[1] = {0x1234};\n"
                "static const uint8_t a_a[1] = {255};\n"
                "const icon_t a = {1, 1, a_rgb, a_a};\n"
                "static const uint16_t b_rgb[2] = {0x5678, 0xabcd};\n"
                "static const uint8_t b_a[2] = {0, 128};\n"
                "const icon_t b = {2, 1, b_rgb, b_a};\n"
            )
            pack(source, root / "out")
            data = (root / "out" / "icons.bin").read_bytes()
            self.assertEqual(data, b"\x34\x12\xff\0" + struct.pack("<HHBB", 0x5678, 0xabcd, 0, 128))
            generated = (root / "out" / "icons_sd.c").read_text()
            self.assertIn("const icon_t b = { 2, 1, 0, 0, 4 };", generated)
            header = (root / "out" / "icons_sd_bank.h").read_text()
            self.assertIn(f"0x{zlib.crc32(data):08x}u", header)

    def test_bad_plane_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / "icons.c"
            source.write_text(
                "static const uint16_t a_rgb[1] = {0};\n"
                "static const uint8_t a_a[1] = {0};\n"
                "const icon_t a = {2, 1, a_rgb, a_a};\n"
            )
            with self.assertRaises(ValueError):
                pack(source, root / "out")

    def test_canonical_bank_preserves_every_pixel(self):
        source = pathlib.Path(__file__).resolve().parent.parent / "common" / "icons.c"
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            pack(source, root)
            data = (root / "icons.bin").read_bytes()
            text = source.read_text()
            arrays = {
                name: [int(v.strip(), 0) for v in body.split(",") if v.strip()]
                for name, body in re.findall(r"static const \w+ (\w+)\[\d+\] = \{(.*?)\};", text, re.S)
            }
            entries = re.findall(r"const icon_t (\w+) = \{\s*(\d+),\s*(\d+),\s*(\w+),\s*(\w+)\s*(?:,\s*0\s*)?\};", text)
            metadata = (root / "icons_sd.c").read_text()
            for name, w, h, rgb, alpha in entries:
                offset = int(re.search(rf"const icon_t {name} = \{{ \d+, \d+, 0, 0, (\d+) \}};", metadata)[1])
                n = int(w) * int(h)
                self.assertEqual(offset % 2, 0)
                self.assertEqual(list(struct.unpack_from("<" + "H" * n, data, offset)), arrays[rgb])
                self.assertEqual(list(data[offset + n * 2:offset + n * 3]), arrays[alpha])


if __name__ == "__main__":
    unittest.main()
