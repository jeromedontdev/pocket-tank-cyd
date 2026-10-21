"""Package a signed CYD-only image for an explicitly configured fork channel."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
BOARD = "cyd2432s028"
FORK = "pocket-tank-cyd"


def make_manifest(data, release, base_url, note):
    marker = b"PTBOARD\0" + BOARD.encode() + b"\0"
    if data[:1] != b"\xe9" or data[0x120:0x120 + len(marker)] != marker:
        raise ValueError("Image does not carry the CYD board marker")
    if len(data) < 4096 or data[-4096:-4094] != b"\xe7\x02":
        raise ValueError("OTA requires an ESP-IDF signed image; USB flashing remains available")
    if len(data) > 0x1E0000:
        raise ValueError("Image exceeds the existing CYD app partition")
    if not base_url.startswith("https://"):
        raise ValueError("The fork release channel must use HTTPS")
    if not re.fullmatch(r"\d+\.\d+\.\d+", release):
        raise ValueError("Release must be MAJOR.MINOR.PATCH")
    parts = [int(part) for part in release.split(".")]
    if any(part > 255 for part in parts):
        raise ValueError("Release components must fit in one byte")
    name = f"pocket_tank-v{release}-{BOARD}.bin"
    manifest = {
        "fork": FORK, "board": BOARD, "release": release,
        "release_num": (parts[0] << 16) | (parts[1] << 8) | parts[2],
        "note": note[:70], "needs_cable": False,
        "app": {"url": f"{base_url.rstrip('/')}/{name}", "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest()},
    }
    return name, manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "firmware" / "build_cyd")
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--note", default="A new version of the CYD tank")
    args = parser.parse_args()
    release = re.search(r'#define PT_RELEASE\s+"([^"]+)"',
                        (ROOT / "common" / "version.h").read_text()).group(1)
    data = (args.build_dir / "pocket_tank.bin").read_bytes()
    try:
        name, manifest = make_manifest(data, release, args.base_url, args.note)
    except ValueError as error:
        parser.error(str(error))
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / name).write_bytes(data)
    (args.out / f"latest-{BOARD}.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"CYD fork OTA: {name}, {len(data)} bytes")


if __name__ == "__main__":
    main()
