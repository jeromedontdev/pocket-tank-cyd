"""Create a CYD-only GitHub release bundle from an existing ESP-IDF build."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile
import zlib

from make_sd_update import package


def release_version(value):
    value = value.removeprefix("v")
    if not re.fullmatch(r"\d+\.\d+\.\d+-cyd\.[1-9]\d*", value) or len(value) > 15:
        raise ValueError("Use vMAJOR.MINOR.PATCH-cyd.N, at most 15 characters without v")
    return value


def create_bundle(build, output, version):
    version = release_version(version)
    build, output = Path(build), Path(output)
    image = (build / "pocket_tank.bin").read_bytes()
    update = package(image, version)
    icons = (build / "cyd_assets" / "icons.bin").read_bytes()
    metadata = (build / "cyd_assets" / "icons_sd_bank.h").read_text()
    icon_name = f"icons-{zlib.crc32(icons):08x}.bin"
    if (f'"{icon_name}"' not in metadata or
            f"#define CYD_ICON_BYTES {len(icons)}u" not in metadata):
        raise ValueError("SD icon bank does not match its generated metadata")
    args = json.loads((build / "flasher_args.json").read_text())
    expected = {0x1000, 0x8000, 0xf000, 0x20000}
    if ({int(offset, 0) for offset in args["flash_files"]} != expected or
            args["extra_esptool_args"]["chip"] != "esp32"):
        raise ValueError("Unexpected CYD initial-install flash layout")
    if output.exists():
        raise FileExistsError(f"Release output already exists: {output}")
    output.mkdir(parents=True)
    factory = output / "pocket-tank-cyd-factory.bin"
    command = [sys.executable, "-m", "esptool", "--chip", "esp32", "merge-bin",
               "-o", str(factory.resolve()), *args["write_flash_args"]]
    for offset, name in args["flash_files"].items():
        command.extend((offset, str((build / name).resolve())))
    subprocess.run(command, check=True)
    # These assets are shared by the standalone downloads and the all-in-one ZIP.
    (output / "pocket-tank-cyd-app.bin").write_bytes(image)
    (output / "update.ptu").write_bytes(update)
    (output / icon_name).write_bytes(icons)
    source = Path(__file__).resolve().parent.parent
    shutil.copyfile(source / "LICENSE", output / "LICENSE")
    instructions = f"""Pocket Tank CYD {version} (alpha)
ESP32-2432S028 only. Not for other ESP32 displays.

EXISTING CYD INSTALLATION (preserve your tanks)
1. Use Settings > Updates > BACKUP TO SD before updating.
2. Power off before moving the SD card.
3. Copy {icon_name} to the SD card root.
4. Copy update.ptu to pocket-tank/update.ptu on the SD card.
5. Reinsert the card, power on, and use Settings > Updates to install.
The SD updater verifies board, size and SHA-256. Packages are not signed:
only install files from a source you trust.

USB APP-ONLY UPGRADE
pocket-tank-cyd-app.bin is NOT a factory image.
Use the source checkout's tools\\build_cyd.ps1 -Flash, which detects the
active app slot, or query `ota` over serial and write the app at the reported
active slot (ota_0: 0x20000; ota_1: 0x200000).
Do not assume the active slot or overwrite the partition table or NVS.

NEW BOARD / DELIBERATE FACTORY RESET ONLY
WARNING: the factory image overwrites NVS, saved tanks, Wi-Fi credentials,
touch calibration and OTA slot selection. Never use it for a normal upgrade.
With esptool v5 installed:
python -m esptool --chip esp32 --port COM4 --baud 460800 write-flash 0x0 pocket-tank-cyd-factory.bin
Copy {icon_name} to the SD card root, then complete first-time setup.
Back up the entire SD card separately; NVS reset loses collection identity.

Do not downgrade: the extended CYD save format is not supported upstream.
Source, build instructions, full feature guide and screenshots:
https://github.com/jeromedontdev/pocket-tank-cyd
"""
    (output / "INSTALL.txt").write_text(instructions, encoding="utf-8")
    notes = f"""## Pocket Tank CYD {version}

Alpha release for **ESP32-2432S028 CYD only**.

- Corrected display scaling/colors, touch calibration and readable menus.
- SD-backed artwork, verified JSON backups and offline firmware updates.
- Fish customization, Aquarium Journal and a free 20-slot Fish Bank.
- Up to eight purchasable, named aquariums with a global sand-dollar wallet.
- Expanded decorations, three surface floats, upkeep and fish-care upgrades.
- Price-sorted shop, consistent buttons and ownership-colored icons.
- Upstream daytime fish-rest recovery fix.

### Downloads
- **pocket-tank-cyd-{version}.zip**: complete bundle with installation instructions.
- **update.ptu**: existing installations; copy to `pocket-tank/update.ptu` on SD.
- **{icon_name}**: required artwork; copy to the SD card root.
- **pocket-tank-cyd-app.bin**: advanced, active-slot USB upgrades only.
- **pocket-tank-cyd-factory.bin**: new boards only, flashed at `0x0`.
- **SHA256SUMS.txt**: download checksums.

**Back up your tank before upgrading. Factory flashing overwrites NVS and
loses tanks, credentials, calibration and collection identity.** Do not use
the factory image for a normal update. SD packages are verified but unsigned.
Read INSTALL.txt before flashing. Do not downgrade to upstream firmware.

[Illustrated guide](https://github.com/jeromedontdev/pocket-tank-cyd/blob/v{version}/docs/CYD-FEATURES.md)
"""
    (output / "RELEASE-NOTES.md").write_text(notes, encoding="utf-8")
    archive = output / f"pocket-tank-cyd-{version}.zip"
    members = sorted(path for path in output.iterdir() if path.is_file())
    with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED) as bundle:
        for path in members:
            bundle.write(path, path.name)
    checksums = "".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n"
                        for path in sorted(output.iterdir()) if path.is_file())
    (output / "SHA256SUMS.txt").write_text(checksums, encoding="ascii")
    return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    print(create_bundle(args.build, args.out, args.version))


if __name__ == "__main__":
    main()
