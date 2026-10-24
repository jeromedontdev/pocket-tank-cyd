import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

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
            build.mkdir()
            image = bytearray(4096)
            image[0] = 0xe9
            image[0x120:0x128] = b"PTBOARD\0"
            image[0x128:0x140] = b"cyd2432s028".ljust(24, b"\0")
            (build / "pocket_tank.bin").write_bytes(image)
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
            self.assertFalse(any(path.name.startswith("icons-") for path in output.iterdir()))
            with zipfile.ZipFile(archive) as bundle:
                self.assertIn("update.ptu", bundle.namelist())
                self.assertIn("INSTALL.txt", bundle.namelist())
                self.assertFalse(any("tank.sav" in entry for entry in bundle.namelist()))
                self.assertFalse(any(entry.startswith("icons-") for entry in bundle.namelist()))
                self.assertIn("no SD icons are required", bundle.read("INSTALL.txt").decode())
                notes = bundle.read("RELEASE-NOTES.md").decode()
                self.assertTrue(notes.startswith('<p align="center">'))
                self.assertIn("https://raw.githubusercontent.com/jeromedontdev/pocket-tank-cyd/main/web/logo.svg", notes)
                self.assertIn('alt="Pocket Tank CYD aquarium logo"', notes)
                self.assertIn("The Lost Current", notes)
                self.assertIn("Twelve games", notes)
                self.assertIn("schema-14 JSON", notes)
                self.assertIn("distinct aquarium game", notes)
                self.assertIn("JSON schema 16", notes)
                self.assertIn("global 20-slot Fish Bank", notes)
            for line in (output / "SHA256SUMS.txt").read_text().splitlines():
                digest, filename = line.split("  ", 1)
                self.assertEqual(digest, hashlib.sha256((output / filename).read_bytes()).hexdigest())
            with self.assertRaises(FileExistsError):
                create_bundle(build, output, "v1.0.0")
            flash["extra_esptool_args"]["chip"] = "esp32s3"
            (build / "flasher_args.json").write_text(json.dumps(flash))
            with self.assertRaises(ValueError):
                create_bundle(build, root / "bad", "v1.0.0")


if __name__ == "__main__":
    unittest.main()
