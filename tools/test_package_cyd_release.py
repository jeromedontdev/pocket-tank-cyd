import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import zlib

from package_cyd_release import create_bundle, release_version


class ReleaseTests(unittest.TestCase):
    def test_version(self):
        self.assertEqual(release_version("v1.0.0"), "1.0.0")
        for value in ("1.0", "v0.3.8-cyd.1", "../bad", "1.2.3-cyd.123456", "x';exit"):
            with self.assertRaises(ValueError):
                release_version(value)

    def test_bundle_and_checksums(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            build, output = root / "build", root / "dist"
            (build / "cyd_assets").mkdir(parents=True)
            image = bytearray(4096)
            image[0] = 0xe9
            image[0x120:0x128] = b"PTBOARD\0"
            image[0x128:0x140] = b"cyd2432s028".ljust(24, b"\0")
            (build / "pocket_tank.bin").write_bytes(image)
            icons = b"test icon bank"
            name = f"icons-{zlib.crc32(icons):08x}.bin"
            (build / "cyd_assets" / "icons.bin").write_bytes(icons)
            header = build / "cyd_assets" / "icons_sd_bank.h"
            header.write_text(f'#define CYD_ICON_FILE "{name}"\n#define CYD_ICON_BYTES {len(icons)}u\n')
            flash = {"flash_files": {"0x1000": "bootloader.bin", "0x8000": "partitions.bin",
                                    "0xf000": "ota.bin", "0x20000": "pocket_tank.bin"},
                     "extra_esptool_args": {"chip": "esp32"}, "write_flash_args": []}
            (build / "flasher_args.json").write_text(json.dumps(flash))

            def merge(command, check):
                self.assertTrue(check)
                self.assertIn("merge-bin", command)
                Path(command[command.index("-o") + 1]).write_bytes(b"factory")

            with patch("package_cyd_release.subprocess.run", side_effect=merge):
                archive = create_bundle(build, output, "v1.0.0")
            self.assertEqual((output / name).read_bytes(), icons)
            with zipfile.ZipFile(archive) as bundle:
                self.assertIn("update.ptu", bundle.namelist())
                self.assertIn("INSTALL.txt", bundle.namelist())
                self.assertFalse(any("tank.sav" in entry for entry in bundle.namelist()))
            for line in (output / "SHA256SUMS.txt").read_text().splitlines():
                digest, filename = line.split("  ", 1)
                self.assertEqual(digest, hashlib.sha256((output / filename).read_bytes()).hexdigest())
            with self.assertRaises(FileExistsError):
                create_bundle(build, output, "v1.0.0")
            header.write_text("wrong bank")
            with self.assertRaises(ValueError):
                create_bundle(build, root / "bad", "v1.0.0")


if __name__ == "__main__":
    unittest.main()
