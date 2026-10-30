import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import unittest
import uuid

from prepare_web_installer import APP, BOARD_MARKER, FACTORY, FEATURE_IMAGES, SLOT_SIZE, stage_site
from board_profiles import BOARDS


class WebInstallerTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(__file__).resolve().parent / f".test-web-{uuid.uuid4().hex}"
        self.root.mkdir()
        self.addCleanup(shutil.rmtree, self.root)
        self.source = self.root / "web"
        self.source.mkdir()
        (self.source / "index.html").write_text("features")
        (self.source / "flash.html").write_text("installer")
        media = self.root / "docs" / "media" / "cyd-features"
        media.mkdir(parents=True)
        for name in FEATURE_IMAGES:
            (media / name).write_bytes(b"screenshot:" + name.encode())
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
        self.assertEqual((output / "index.html").read_text(), "features")
        self.assertEqual((output / "flash.html").read_text(), "installer")
        for name in FEATURE_IMAGES:
            self.assertEqual((output / "media" / name).read_bytes(),
                             b"screenshot:" + name.encode())
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
            self.assertFalse((self.root / "site").exists())

    def test_feature_home_and_flasher_navigation(self):
        source = Path(__file__).resolve().parent.parent / "web"
        home = (source / "index.html").read_text(encoding="utf-8")
        flasher = (source / "flash.html").read_text(encoding="utf-8")
        viewer = (source / "viewer.html").read_text(encoding="utf-8")
        self.assertIn('href="./flash.html"', home)
        self.assertIn('href="./viewer.html"', home)
        self.assertIn('href="./"', flasher)
        self.assertIn('href="./flash.html"', viewer)
        self.assertIn('href="#latest-features">What\'s new</a>', home)
        self.assertIn("New in 2.4.0", home)
        self.assertIn("Firmware Updates", home)
        self.assertIn("Check GitHub via Wi-Fi", home)
        for current in ("doubloons", "Creature Market", "Casino", "Contracts", "Auction", "Help"):
            self.assertIn(current, home)
        for stale in ("125 sand dollars", "50 XP", "bonus dollars", "New in 2.3", "postcard", "accessories"):
            self.assertNotIn(stale, home)
        self.assertIn("Firmware Updates", flasher)
        self.assertIn("Check GitHub via Wi-Fi", flasher)
        self.assertIn('id="latest-features"', home)
        self.assertNotIn("coming-next", home)
        self.assertNotIn("NEXT RELEASE", home.upper())
        for theme in ("Digital", "Analog", "Flip", "Binary", "Orbit"):
            self.assertIn(theme, home)
        self.assertIn("distinct aquarium game", home)
        for text in (home, flasher):
            self.assertNotRegex(text, r"ESP32-2432S028|ESP32-32E|ILI9341V")
        self.assertIn('value="cyd2432s028">CYD</option>', flasher)
        self.assertIn('value="elegoo28">ELEGOO CYD</option>', flasher)
        self.assertIn('value="" selected disabled', flasher)
        self.assertIn("Reliable automatic board detection is not available", home)
        self.assertIn("updater can verify its saved profile", flasher)
        self.assertIn("same firmware application", home)
        self.assertNotIn("Each has its own app", home)
        self.assertNotIn("usb-update.mjs", home)
        self.assertNotIn("esp-web-install-button", home)
        self.assertIn('src="./usb-update.mjs"', flasher)
        self.assertIn('id="update-section"', flasher)
        self.assertIn('id="factory-section"', flasher)
        self.assertEqual(set(re.findall(r'src="./media/([^"]+)"', home)),
                         set(FEATURE_IMAGES))
        for text in (home, flasher, viewer):
            for target in re.findall(r'(?:src|href)="(\./[^"#]*)"', text):
                if target.startswith("./media/") or target == "./manifest.json":
                    continue
                self.assertTrue((source / target).exists(), target)
    def test_wrong_factory_board(self):
        self.image[0x20128] = 0
        with self.assertRaisesRegex(ValueError, "Factory"):
            self.stage()

    def test_two_board_staging_and_identity_checks(self):
        profile = BOARDS["elegoo"]
        app = bytearray(self.app)
        app[0x120:0x140] = profile.marker
        factory = bytearray(0x20000) + app
        sums = "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in (
            ("pocket-tank-elegoo-factory.bin", factory), ("pocket-tank-elegoo-app.bin", app)))
        for wrong in ((factory, sums, self.app), (self.image, sums, app),
                      (factory, sums.replace("elegoo-app", "cyd-app"), app)):
            with self.assertRaises(ValueError):
                stage_site(self.source, self.root / "site", "2.3.6",
                           self.image, self.checksums(), self.app, elegoo=wrong)
            self.assertFalse((self.root / "site").exists())
        stage_site(self.source, self.root / "site", "2.3.6",
                   self.image, self.checksums(), self.app, elegoo=(factory, sums, app))
        output = self.root / "site"
        manifest = json.loads((output / "elegoo-manifest.json").read_text())
        update = json.loads((output / "elegoo-update.json").read_text())
        self.assertEqual(manifest["version"], "2.3.6")
        self.assertEqual(manifest["builds"][0]["parts"][0]["path"], "pocket-tank-elegoo-factory.bin")
        self.assertEqual(update["board"], "elegoo28")
        self.assertEqual(update["app"], "pocket-tank-elegoo-app.bin")
        self.assertEqual(update["sha256"], hashlib.sha256(app).hexdigest())
        self.assertEqual((output / APP).read_bytes(), self.app)
        self.assertEqual((output / "pocket-tank-elegoo-app.bin").read_bytes(), app)

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

    def test_same_unified_app_with_profile_specific_factories(self):
        shared = bytearray(self.app)
        shared[0x120:0x140] = BOARDS["unified"].marker
        factory = bytearray(0x20000) + shared
        factories = {}
        for hardware in ("cyd", "elegoo"):
            factories[hardware] = bytearray(factory)
            factories[hardware][0x9000] = 1 if hardware == "cyd" else 2
        assets = {"pocket-tank-unified-app.bin": shared,
                  "pocket-tank-unified-factory.bin": factory,
                  **{f"pocket-tank-unified-{key}-factory.bin": value for key, value in factories.items()}}
        sums = "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in assets.items())
        stage_site(self.source, self.root / "site", "2.3.6", self.image, self.checksums(), self.app,
                   unified=(factory, sums, shared, factories))
        output = self.root / "site"
        a = json.loads((output / "update.json").read_text())
        b = json.loads((output / "elegoo-update.json").read_text())
        self.assertEqual(a["board"], "cydesp32profiles")
        self.assertEqual(a["app"], b["app"])
        self.assertEqual(a["sha256"], b["sha256"])
        self.assertEqual(a["hardware"], "cyd2432s028")
        self.assertEqual(b["hardware"], "elegoo28")
        self.assertEqual((output / a["app"]).read_bytes(), shared)
        for hardware, prefix in (("cyd", ""), ("elegoo", "elegoo-")):
            manifest = json.loads((output / f"{prefix}manifest.json").read_text())
            self.assertEqual(manifest["builds"][0]["parts"][0]["path"],
                             f"pocket-tank-unified-{hardware}-factory.bin")

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
