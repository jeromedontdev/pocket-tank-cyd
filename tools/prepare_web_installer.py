"""Stage checksum-verified factory and save-preserving USB images for Pages."""
import argparse
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
    "v240-market-hub.png", "v240-creature-market.png", "v240-contract.png",
    "v240-auction.png", "v240-casino.png", "v240-tide-wheel.png", "pocket-tank-2.4.0-demo.mp4",
    "fish-interaction-feedback.png", "fish-life-feedback.png",
    "harbor-garden-decorations.png", "jellyfish-wardrobe.png",
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
    "shop.png", "game-reef-defender.png",
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


def stage_site(source, output, version, image, checksums, app, elegoo=None, unified=None):
    version = release_version(version)
    images = [(BOARDS["cyd"], image, checksums, app)]
    if elegoo is not None:
        images.append((BOARDS["elegoo"], *elegoo))
    for profile, factory, sums, application in images:
        validate_images(profile, factory, sums, application)
    if unified is not None:
        factory, sums, application, profile_factories = unified
        validate_images(BOARDS["unified"], factory, sums, application)
        expected = dict(line.split("  ", 1)[::-1] for line in sums.splitlines() if line)
        for hardware in ("cyd", "elegoo"):
            name = f"pocket-tank-unified-{hardware}-factory.bin"
            data = profile_factories[hardware]
            if (hashlib.sha256(data).hexdigest() != expected.get(name) or
                    data[:0x9000] != factory[:0x9000] or
                    data[0xf000:] != factory[0xf000:]):
                raise ValueError("Profile factory differs outside NVS or fails checksum")
        images.append((BOARDS["unified"], factory, sums, application))
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
    if unified is not None:
        for hardware in ("cyd", "elegoo"):
            profile = BOARDS[hardware]
            factory_name = f"pocket-tank-unified-{hardware}-factory.bin"
            (output / factory_name).write_bytes(profile_factories[hardware])
            manifest = {
                "name": f"Pocket Tank CYD - {profile.name}", "version": version,
                "hardware": profile.identity, "board": BOARDS["unified"].identity,
                "new_install_prompt_erase": False,
                "builds": [{"chipFamily": "ESP32", "parts": [{"path": factory_name, "offset": 0}]}],
            }
            (output / f"{profile.asset_prefix}manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
            update = json.loads((output / "unified-update.json").read_text())
            update["hardware"] = profile.identity
            (output / f"{profile.asset_prefix}update.json").write_text(json.dumps(update, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundles", type=Path, help="Local release bundle root (cyd/elegoo/unified)")
    parser.add_argument("--version", help="Required with --bundles")
    parser.add_argument("--out", type=Path, default=Path("_site"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    if args.bundles:
        if not args.version:
            parser.error("--bundles requires --version")
        def read(board, name):
            return (args.bundles / board / name).read_bytes()
        stage_site(root / "web", args.out, args.version,
                   read("cyd", FACTORY), read("cyd", "SHA256SUMS.txt").decode(), read("cyd", APP),
                   elegoo=(read("elegoo", "pocket-tank-elegoo-factory.bin"),
                           read("elegoo", "elegoo-SHA256SUMS.txt").decode(),
                           read("elegoo", "pocket-tank-elegoo-app.bin")),
                   unified=(read("unified", "pocket-tank-unified-factory.bin"),
                            read("unified", "unified-SHA256SUMS.txt").decode(),
                            read("unified", "pocket-tank-unified-app.bin"),
                            {hardware: read("unified", f"pocket-tank-unified-{hardware}-factory.bin")
                             for hardware in ("cyd", "elegoo")}))
        return
    def fetch(url):
        request = urllib.request.Request(url, headers={"User-Agent": "Pocket-Tank-CYD-installer"})
        with urllib.request.urlopen(request, timeout=120) as response:
            return response.read()

    release = json.loads(fetch(f"https://api.github.com/repos/{REPOSITORY}/releases/latest"))
    if release["draft"] or release["prerelease"]:
        raise ValueError("Web installer requires a published stable release")
    assets = {asset["name"]: asset["browser_download_url"] for asset in release["assets"]}
    elegoo = BOARDS["elegoo"]
    unified = None
    if "unified-SHA256SUMS.txt" in assets:
        unified = (fetch(assets["pocket-tank-unified-factory.bin"]),
                   fetch(assets["unified-SHA256SUMS.txt"]).decode("utf-8"),
                   fetch(assets["pocket-tank-unified-app.bin"]),
                   {hardware: fetch(assets[f"pocket-tank-unified-{hardware}-factory.bin"])
                    for hardware in ("cyd", "elegoo")})
    stage_site(root / "web", args.out, release["tag_name"],
               fetch(assets[FACTORY]), fetch(assets["SHA256SUMS.txt"]).decode("utf-8"),
               fetch(assets[APP]),
               elegoo=(fetch(assets[f"{elegoo.image_prefix}-factory.bin"]),
                       fetch(assets["elegoo-SHA256SUMS.txt"]).decode("utf-8"),
                       fetch(assets[f"{elegoo.image_prefix}-app.bin"])), unified=unified)


if __name__ == "__main__":
    main()
