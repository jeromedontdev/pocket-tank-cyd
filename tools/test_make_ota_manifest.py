import unittest
from make_ota_manifest import make_manifest


class ManifestTests(unittest.TestCase):
    def image(self):
        data = bytearray(8192)
        data[0] = 0xE9
        marker = b"PTBOARD\0cyd2432s028\0"
        data[0x120:0x120 + len(marker)] = marker
        data[-4096:-4094] = b"\xe7\x02"
        return data

    def test_cyd_fork_only_without_model(self):
        name, manifest = make_manifest(self.image(), "0.3.9", "https://example.com/releases", "Test")
        self.assertEqual(manifest["fork"], "pocket-tank-cyd")
        self.assertEqual(manifest["board"], "cyd2432s028")
        self.assertEqual(manifest["release_num"], 0x000309)
        self.assertEqual(manifest["app"]["size"], 8192)
        self.assertTrue(manifest["app"]["url"].endswith(name))
        self.assertNotIn("model", manifest)

    def test_refuses_other_board_and_unsigned_images(self):
        for offset in (0, 0x128, 4096):
            with self.subTest(offset=offset):
                data = self.image()
                data[offset] ^= 0xFF
                with self.assertRaises(ValueError):
                    make_manifest(data, "0.3.9", "https://example.com", "")

    def test_refuses_oversized_image_and_bad_release_channel(self):
        data = self.image()
        data[4096:4096] = bytes(0x1E0000)
        for image, version, url in (
            (data, "0.3.9", "https://example.com"),
            (self.image(), "256.3.9", "https://example.com"),
            (self.image(), "0.3.9", "http://example.com"),
        ):
            with self.assertRaises(ValueError):
                make_manifest(image, version, url, "")


if __name__ == "__main__":
    unittest.main()
