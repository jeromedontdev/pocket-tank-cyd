import struct
import unittest

from board_profiles import BOARDS
from flash_unified import detect_slot, validate_backup, validate_image, SLOT_SIZE


class UnifiedMigrationTests(unittest.TestCase):
    def backup(self, hardware="cyd", marker=None):
        data = bytearray(b"\xff" * 0x400000)
        layout = [(1, 2, 0x9000, 0x6000, "nvs"), (1, 0, 0xf000, 0x2000, "otadata"),
                  (1, 1, 0x11000, 0x1000, "phy_init"), (0, 16, 0x20000, SLOT_SIZE, "ota_0"),
                  (0, 17, 0x200000, SLOT_SIZE, "ota_1"), (1, 130, 0x3e0000, 0x20000, "storage")]
        for i, (kind, subtype, address, size, label) in enumerate(layout):
            struct.pack_into("<HBBII16sI", data, 0x8000 + i * 32, 0x50aa, kind, subtype,
                             address, size, label.encode(), 0)
        data[0x20120:0x20140] = marker or BOARDS[hardware].marker
        return data

    def test_explicit_same_hardware_legacy_and_unified_only(self):
        for hardware in ("cyd", "elegoo"):
            validate_backup(self.backup(hardware), "ota_0", hardware)
            validate_backup(self.backup(marker=BOARDS["unified"].marker), "ota_0", hardware)
            with self.assertRaises(ValueError):
                validate_backup(self.backup(hardware), "ota_0", "elegoo" if hardware == "cyd" else "cyd")
        with self.assertRaises(ValueError):
            validate_backup(self.backup(marker=b"x" * 32), "ota_0", "cyd")

    def test_layout_ota_and_size_checks(self):
        for offset in (0x8000, 0x8002, 0x8004, 0x8008, 0x800c, 0x801c, 0x80c0, 0xf000):
            data = self.backup()
            data[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                validate_backup(data, "ota_0", "cyd")
        with self.assertRaises(ValueError):
            validate_backup(self.backup()[:-1], "ota_0", "cyd")
        data = self.backup()
        struct.pack_into("<I", data, 0xf000, 1)
        struct.pack_into("<I", data, 0xf018, 2)
        struct.pack_into("<I", data, 0xf01c, 0x4743989a)
        validate_backup(data, "ota_0", "cyd")
        for state in (0, 1):
            struct.pack_into("<I", data, 0xf018, state)
            with self.assertRaises(ValueError):
                validate_backup(data, "ota_0", "cyd")

    def test_slot_and_app_guards(self):
        self.assertEqual(detect_slot("running from ota_1, next boot ota_1"), "ota_1")
        for text in ("", "running from ota_1, next boot ota_0",
                     "running from ota_0, next boot ota_0\nrunning from ota_1, next boot ota_1"):
            with self.assertRaises(ValueError):
                detect_slot(text)
        image = bytearray(0x200)
        image[0] = 0xe9
        image[0x120:0x140] = BOARDS["unified"].marker
        validate_image(image)
        for offset in (0, 12, 13, 0x128):
            corrupt = bytearray(image)
            corrupt[offset] ^= 1
            with self.assertRaises(ValueError):
                validate_image(corrupt)
