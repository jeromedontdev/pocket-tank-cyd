from pathlib import Path
import unittest
from unittest.mock import patch

from board_profiles import BOARDS
from provision_factory import provision_factory


class ProfileFactoryTests(unittest.TestCase):
    def test_only_blank_nvs_differs_app_is_identical(self):
        factory = bytearray(b"\xff" * 0x22000)
        factory[0x20120:0x20140] = BOARDS["unified"].marker
        for hardware, profile_id in (("cyd", 1), ("elegoo", 2)):
            def generate(command, check):
                self.assertTrue(check)
                self.assertIn("esp_idf_nvs_partition_gen", command)
                csv = Path(command[-3]).read_text()
                self.assertIn(f"profile,data,u8,{profile_id}\n", csv)
                Path(command[-2]).write_bytes(bytes([profile_id]) * 0x6000)
            with patch("provision_factory.subprocess.run", side_effect=generate):
                result = provision_factory(factory, hardware)
            self.assertEqual(len(result), len(factory))
            self.assertEqual(result[:0x9000], factory[:0x9000])
            self.assertEqual(result[0xf000:], factory[0xf000:])
            self.assertEqual(result[0x9000:0xf000], bytes([profile_id]) * 0x6000)
        factory[0x20128] ^= 1
        with self.assertRaises(ValueError):
            provision_factory(factory, "cyd")
