import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from prepare_web_installer import FACTORY, stage_site


class WebInstallerTests(unittest.TestCase):
    def test_verified_factory_manifest(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "web"
            source.mkdir()
            (source / "index.html").write_text("installer")
            image = bytearray(0x20200)
            image[0x20120:0x20140] = b"PTBOARD\0" + b"cyd2432s028".ljust(24, b"\0")
            checksum = f"{hashlib.sha256(image).hexdigest()}  {FACTORY}\n"
            stage_site(source, root / "site", "v1.0.0", image, checksum)
            manifest = json.loads((root / "site" / "manifest.json").read_text())
            self.assertEqual(manifest["version"], "1.0.0")
            self.assertEqual(manifest["builds"], [
                {"chipFamily": "ESP32", "parts": [{"path": FACTORY, "offset": 0}]}])
            self.assertEqual((root / "site" / FACTORY).read_bytes(), image)
            self.assertEqual((root / "site" / "index.html").read_text(), "installer")
            with self.assertRaises(ValueError):
                stage_site(source, root / "bad", "1.0.0", image + b"x", checksum)
            image[0x20128] = 0
            checksum = f"{hashlib.sha256(image).hexdigest()}  {FACTORY}\n"
            with self.assertRaises(ValueError):
                stage_site(source, root / "wrong-board", "1.0.0", image, checksum)


if __name__ == "__main__":
    unittest.main()
