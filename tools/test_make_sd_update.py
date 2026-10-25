import hashlib
import unittest

from make_sd_update import HEADER, package
from board_profiles import BOARDS


class SDUpdateTests(unittest.TestCase):
    def image(self):
        data = bytearray(4096)
        data[0] = 0xe9
        data[0x120:0x128] = b"PTBOARD\0"
        data[0x128:0x140] = b"cyd2432s028".ljust(24, b"\0")
        return bytes(data)

    def test_package_layout_and_integrity(self):
        image = self.image()
        result = package(image, "1.0.0")
        magic, board, size, sha, release = HEADER.unpack(result[:HEADER.size])
        self.assertEqual(HEADER.size, 84)
        self.assertEqual(magic, b"PTCYDSD1")
        self.assertEqual(board.rstrip(b"\0"), b"cyd2432s028")
        self.assertEqual(size, len(image))
        self.assertEqual(sha, hashlib.sha256(image).digest())
        self.assertEqual(release.rstrip(b"\0"), b"1.0.0")
        self.assertEqual(result[HEADER.size:], image)
        damaged = bytearray(image)
        damaged[-1] ^= 1
        self.assertNotEqual(sha, hashlib.sha256(damaged).digest())

    def test_reject_wrong_board_and_missing_marker(self):
        for start in (0, 0x120, 0x128):
            image = bytearray(self.image())
            image[start] ^= 1
            with self.assertRaises(ValueError):
                package(image, "0.3.8")

    def test_reject_wrong_size(self):
        for image in (b"", self.image()[:128], self.image() + bytes(0x1e0000)):
            with self.assertRaises(ValueError):
                package(image, "0.3.8")

    def test_elegoo_identity_and_cross_board_refusal(self):
        image = bytearray(self.image())
        image[0x120:0x140] = BOARDS["elegoo"].marker
        result = package(image, "2.3.6", b"elegoo28")
        self.assertEqual(HEADER.unpack(result[:HEADER.size])[1].rstrip(b"\0"), b"elegoo28")
        for data, board in ((image, b"cyd2432s028"), (self.image(), b"elegoo28"),
                            (image, b"unknown")):
            with self.subTest(board=board), self.assertRaises(ValueError):
                package(data, "2.3.6", board)

    def test_reject_invalid_labels(self):
        for label in ("", "x" * 16, "bad\nlabel", "\x7f"):
            with self.assertRaises(ValueError):
                package(self.image(), label)


if __name__ == "__main__":
    unittest.main()
