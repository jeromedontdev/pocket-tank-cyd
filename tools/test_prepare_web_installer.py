import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import unittest
import uuid

from prepare_web_installer import APP, BOARD_MARKER, FACTORY, SLOT_SIZE, stage_site


class WebInstallerTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(__file__).resolve().parent / f".test-web-{uuid.uuid4().hex}"
        self.root.mkdir()
        self.addCleanup(shutil.rmtree, self.root)
        self.source = self.root / "web"
        self.source.mkdir()
        (self.source / "index.html").write_text("installer")
        self.app = bytearray(0x200)
        self.app[0] = 0xe9
        self.app[0x120:0x140] = BOARD_MARKER
        self.image = bytearray(0x20000) + self.app

    def checksums(self, app=None, image=None):
        return "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n"
                       for name, data in ((FACTORY, self.image if image is None else image),
                                          (APP, self.app if app is None else app)))

    def stage(self, app=None, image=None, checksums=None, version="v1.0.0"):
        stage_site(self.source, self.root / "site", version,
                   self.image if image is None else image,
                   self.checksums() if checksums is None else checksums,
                   self.app if app is None else app)

    def test_verified_factory_manifest(self):
        self.stage()
        output = self.root / "site"
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertEqual(manifest["version"], "1.0.0")
        self.assertEqual(manifest["builds"], [
            {"chipFamily": "ESP32", "parts": [{"path": FACTORY, "offset": 0}]}])
        self.assertEqual((output / FACTORY).read_bytes(), self.image)
        self.assertEqual((output / APP).read_bytes(), self.app)
        self.assertEqual((output / "index.html").read_text(), "installer")
        update = json.loads((output / "update.json").read_text())
        self.assertEqual(update, {
            "format": 1, "board": "cyd2432s028", "version": "1.0.0", "app": APP,
            "size": len(self.app), "sha256": hashlib.sha256(self.app).hexdigest(),
            "md5": hashlib.md5(self.app).hexdigest(),
        })
        self.assertNotIn("offset", update)

    def test_corrupt_or_missing_checksums_refused_before_output(self):
        for name in (FACTORY, APP):
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.stage(checksums=self.checksums().replace(name, "missing.bin"))
            self.assertFalse((self.root / "site").exists())
        for arguments in ({"image": self.image + b"x"}, {"app": self.app + b"x"}):
            with self.subTest(arguments=tuple(arguments)), self.assertRaises(ValueError):
                self.stage(**arguments)

    def test_wrong_factory_board(self):
        self.image[0x20128] = 0
        with self.assertRaisesRegex(ValueError, "Factory"):
            self.stage()

    def test_invalid_app_images(self):
        variants = [self.app[:0x100], self.app + bytes(SLOT_SIZE),
                    bytearray(self.app), bytearray(self.app), bytearray(self.app)]
        variants[2][0] = 0
        variants[3][12] = 9
        variants[4][0x128] = 0
        for app in variants:
            with self.subTest(length=len(app)), self.assertRaises(ValueError):
                self.stage(app=app, checksums=self.checksums(app=app))
            self.assertFalse((self.root / "site").exists())

    def test_factory_app_must_match(self):
        self.app[0x150] = 42
        with self.assertRaisesRegex(ValueError, "same application"):
            self.stage()

    def test_nonrelease_version_refused(self):
        with self.assertRaises(ValueError):
            self.stage(version="1.0.0-dev")

    @unittest.skipUnless(shutil.which("node"), "Node not on PATH; run test_usb_updater.mjs in a browser or Node")
    def test_javascript_slot_safety(self):
        script = Path(__file__).with_name("test_usb_updater.mjs")
        subprocess.run(["node", "--input-type=module", "-e",
                        f"import({json.dumps(script.as_uri())}).then(m => m.runTests())"],
                       check=True, cwd=script.parent)


if __name__ == "__main__":
    unittest.main()
