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
            entries = re.findall(r"const icon_t (\w+) = \{\s*(\d+),\s*(\d+),\s*(\w+),\s*(\w+)\s*(?:,\s*(\d+)\s*)?\};", text)
            self.assertEqual(len(entries), 47)
            self.assertEqual(zlib.crc32(data), 0xb3de6773)
            metadata = (root / "icons_sd.c").read_text()
            for name, w, h, rgb, alpha, flags in entries:
                offset = int(re.search(rf"const icon_t {name} = \{{ \d+, \d+, 0, 0, (\d+) \}};", metadata)[1])
                n = int(w) * int(h)
                self.assertEqual(offset % 2, 0)
                colors = arrays[rgb]
                if int(flags or 0) & 0x40000000:
                    count = colors[0]
                    indices = colors[1 + count:]
                    per_word = 4 if count <= 16 else 2
                    bits = 16 // per_word
                    colors = [colors[1 + ((indices[i // per_word] >> ((i % per_word) * bits)) & ((1 << bits) - 1))]
                              for i in range(n)]
                self.assertEqual(list(struct.unpack_from("<" + "H" * n, data, offset)), colors)
                plane = arrays[alpha]
                if int(flags or 0) & 0x80000000:
                    plane = [value for i in range(0, len(plane), 2)
                             for value in [plane[i + 1]] * plane[i]]
                self.assertEqual(list(data[offset + n * 2:offset + n * 3]), plane)

    def test_invalid_compressed_alpha_rejected(self):
        for plane in ("0, 255", "2", "1, 255"):
            with self.subTest(plane=plane), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                source = root / "icons.c"
                source.write_text(
                    "static const uint16_t a_rgb[2] = {0, 0};\n"
                    f"static const uint8_t a_a[2] = {{{plane}}};\n"
                    "const icon_t a = {2, 1, a_rgb, a_a, 2147483648};\n")
                with self.assertRaises(ValueError):
                    pack(source, root / "out")

    def test_indexed_rgb_odd_size_and_alpha(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / "icons.c"
            source.write_text(
                "static const uint16_t a_rgb[4] = {1, 0x1234, 0, 0};\n"
                "static const uint8_t a_a[2] = {5, 255};\n"
                "const icon_t a = {5, 1, a_rgb, a_a, 3221225472};\n")
            pack(source, root / "out")
            self.assertEqual((root / "out" / "icons.bin").read_bytes(),
                             struct.pack("<5H", *([0x1234] * 5)) + b"\xff" * 5)

    def test_invalid_indexed_rgb_rejected(self):
        for rgb in ("0", "257", "1, 0x1234", "1, 0x1234, 0x0010"):
            with self.subTest(rgb=rgb), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                source = root / "icons.c"
                source.write_text(
                    f"static const uint16_t a_rgb[4] = {{{rgb}}};\n"
                    "static const uint8_t a_a[2] = {255, 255};\n"
                    "const icon_t a = {2, 1, a_rgb, a_a, 1073741824};\n")
                with self.assertRaises(ValueError):
                    pack(source, root / "out")

    def test_palette_width_boundary(self):
        for count in (16, 17, 256):
            with self.subTest(count=count), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                indices = list(range(count)) + ([count - 1] if count < 256 else [])
                n = len(indices)
                bits = 4 if count == 16 else 8
                per_word = 16 // bits
                words = [sum(index << (j * bits) for j, index in enumerate(indices[i:i + per_word]))
                         for i in range(0, n, per_word)]
                rgb = [count] + [i * 251 for i in range(count)] + words
                source = root / "icons.c"
                source.write_text(
                    f"static const uint16_t a_rgb[{len(rgb)}] = {{{', '.join(map(str, rgb))}}};\n"
                    f"static const uint8_t a_a[{n}] = {{{', '.join(['255'] * n)}}};\n"
                    f"const icon_t a = {{{16 if count == 256 else n}, {16 if count == 256 else 1}, a_rgb, a_a, 1073741824}};\n")
                pack(source, root / "out")
                self.assertEqual((root / "out" / "icons.bin").read_bytes(),
                                 struct.pack("<" + "H" * n, *(i * 251 for i in indices)) + b"\xff" * n)


if __name__ == "__main__":
    unittest.main()
