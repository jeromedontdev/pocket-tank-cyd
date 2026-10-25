"""Stage checksum-verified factory and save-preserving USB images for Pages."""
import hashlib
import json
from pathlib import Path
import shutil
import urllib.request

from package_cyd_release import release_version
from board_profiles import BOARDS

REPOSITORY = "jeromedontdev/pocket-tank-cyd"
FACTORY = "pocket-tank-cyd-factory.bin"
APP = "pocket-tank-cyd-app.bin"
BOARD_MARKER = b"PTBOARD\0" + b"cyd2432s028".ljust(24, b"\0")
SLOT_SIZE = 0x1e0000
FEATURE_IMAGES = (
    "tank-normal.png", "family-tree.png", "rpg-story.png",
    "game-tide-tactics.png", "game-reef-architect.png", "games-page-two.png",
    "clock-jumbo-digital.png", "clock-jumbo-flip.png", "clock-jumbo-analog.png",
    "clock-settings.png", "weather-settings.png", "journal-goals.png",
    "settings-device.png", "sd-updates.png",
    "home-collections.png", "games-page-three.png",
    "tank-crafted-ornaments.png", "tank-crafted-seabed.png",
    "tank-crafted-reef.png", "tank-crafted-plants.png",
    "fish-bodies.png", "fish-accessories.png", "aquarium-details.png", "tank-crafted-floats.png",
    "typography-normal.png", "typography-enlarged.png",
)


def validate_images(profile, image, checksums, app):
    factory, application = f"{profile.image_prefix}-factory.bin", f"{profile.image_prefix}-app.bin"
    expected = dict(line.split("  ", 1)[::-1] for line in checksums.splitlines() if line)
    for name, data in ((factory, image), (application, app)):
        if hashlib.sha256(data).hexdigest() != expected.get(name):
            raise ValueError(f"Published {name} checksum does not match")
    marker = 0x20000 + 0x120
    if image[marker:marker + 32] != profile.marker:
        raise ValueError("Factory image is not for the supported CYD board/layout")
    if (not 0x140 <= len(app) <= SLOT_SIZE or app[0] != 0xe9 or
            app[12:14] != b"\0\0" or app[0x120:0x140] != profile.marker):
        raise ValueError("App image is not a supported CYD ESP32 application or exceeds its slot")
    if image[0x20000:0x20000 + len(app)] != app:
        raise ValueError("Factory and app images do not contain the same application")


def stage_site(source, output, version, image, checksums, app, elegoo=None):
    version = release_version(version)
    images = [(BOARDS["cyd"], image, checksums, app)]
    if elegoo is not None:
        images.append((BOARDS["elegoo"], *elegoo))
    for profile, factory, sums, application in images:
        validate_images(profile, factory, sums, application)
    shutil.copytree(source, output)
    media = output / "media"
    media.mkdir()
    for name in FEATURE_IMAGES:
        shutil.copyfile(source.parent / "docs" / "media" / "cyd-features" / name,
                        media / name)
    for profile, factory, _, application in images:
        factory_name, app_name = f"{profile.image_prefix}-factory.bin", f"{profile.image_prefix}-app.bin"
        (output / factory_name).write_bytes(factory)
        (output / app_name).write_bytes(application)
        manifest = {
            "name": f"Pocket Tank CYD - {profile.name}",
            "version": version,
            "new_install_prompt_erase": False,
            "builds": [{"chipFamily": "ESP32", "parts": [{"path": factory_name, "offset": 0}]}],
        }
        (output / f"{profile.asset_prefix}manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        update = {
            "format": 1, "board": profile.identity, "version": version,
            "app": app_name, "size": len(application),
            "sha256": hashlib.sha256(application).hexdigest(),
            "md5": hashlib.md5(application).hexdigest(),
        }
        (output / f"{profile.asset_prefix}update.json").write_text(json.dumps(update, indent=2) + "\n", encoding="utf-8")


def main():
    def fetch(url):
        request = urllib.request.Request(url, headers={"User-Agent": "Pocket-Tank-CYD-installer"})
        with urllib.request.urlopen(request, timeout=120) as response:
            return response.read()

    release = json.loads(fetch(f"https://api.github.com/repos/{REPOSITORY}/releases/latest"))
    if release["draft"] or release["prerelease"]:
        raise ValueError("Web installer requires a published stable release")
    assets = {asset["name"]: asset["browser_download_url"] for asset in release["assets"]}
    elegoo = BOARDS["elegoo"]
    root = Path(__file__).resolve().parent.parent
    stage_site(root / "web", root / "_site", release["tag_name"],
               fetch(assets[FACTORY]), fetch(assets["SHA256SUMS.txt"]).decode("utf-8"),
               fetch(assets[APP]),
               elegoo=(fetch(assets[f"{elegoo.image_prefix}-factory.bin"]),
                       fetch(assets["elegoo-SHA256SUMS.txt"]).decode("utf-8"),
                       fetch(assets[f"{elegoo.image_prefix}-app.bin"])))


if __name__ == "__main__":
    main()
