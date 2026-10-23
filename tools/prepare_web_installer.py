"""Stage a checksum-verified published factory image for GitHub Pages."""
import hashlib
import json
from pathlib import Path
import shutil
import urllib.request

from package_cyd_release import release_version

REPOSITORY = "jeromedontdev/pocket-tank-cyd"
FACTORY = "pocket-tank-cyd-factory.bin"


def stage_site(source, output, version, image, checksums):
    version = release_version(version)
    expected = dict(line.split("  ", 1)[::-1] for line in checksums.splitlines() if line)
    if hashlib.sha256(image).hexdigest() != expected.get(FACTORY):
        raise ValueError("Published factory image checksum does not match")
    marker = 0x20000 + 0x120
    if image[marker:marker + 32] != b"PTBOARD\0" + b"cyd2432s028".ljust(24, b"\0"):
        raise ValueError("Factory image is not for the supported CYD board/layout")
    shutil.copytree(source, output)
    (output / FACTORY).write_bytes(image)
    manifest = {
        "name": "Pocket Tank CYD",
        "version": version,
        "new_install_prompt_erase": False,
        "builds": [{"chipFamily": "ESP32", "parts": [{"path": FACTORY, "offset": 0}]}],
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def main():
    def fetch(url):
        request = urllib.request.Request(url, headers={"User-Agent": "Pocket-Tank-CYD-installer"})
        with urllib.request.urlopen(request, timeout=120) as response:
            return response.read()

    release = json.loads(fetch(f"https://api.github.com/repos/{REPOSITORY}/releases/latest"))
    if release["draft"] or release["prerelease"]:
        raise ValueError("Web installer requires a published stable release")
    assets = {asset["name"]: asset["browser_download_url"] for asset in release["assets"]}
    root = Path(__file__).resolve().parent.parent
    stage_site(root / "web", root / "_site", release["tag_name"],
               fetch(assets[FACTORY]), fetch(assets["SHA256SUMS.txt"]).decode("utf-8"))


if __name__ == "__main__":
    main()
