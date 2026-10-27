"""Capture new static ornaments and wearable geometry through the native renderer."""
import argparse
from html import escape
from pathlib import Path
import subprocess
import sys


DECORATIONS = [
    "Lighthouse", "Sailboat", "Rope Coil", "Wheel", "Cargo Crate",
    "Dive Bell", "Stone Bridge", "Pagoda", "Stone Lantern", "Bonsai",
    "Torii", "Crescent", "Split Geode", "Ammonite", "Ceramic Mushrooms",
    "Sandcastle", "Arrow Sign", "Seabed Mailbox", "Parchment Map", "Submarine",
]


def image(src, alt, width=None):
    size = f' width="{width}"' if width else ""
    return f'<img src="{escape(src)}" alt="{escape(alt)}"{size}>'


def write_index(output):
    entries = []
    for offset, name in enumerate(DECORATIONS):
        item = 43 + offset
        previews = "".join(
            f'<a href="decoration-{item:02d}-{width}.png">'
            f'{image(f"decoration-{item:02d}-{width}.png", f"Decoration {item}: {name} at {width}px tank resolution", 240)}'
            f'<span>{width}px tank</span></a>'
            for width in (240, 320)
        )
        entries.append(
            f'<article><h2>ID {item}: {escape(name)}</h2><div class="previews">{previews}</div></article>'
        )
    comparisons = []
    for size, label in (("readable", "Readable"), ("tank-scale", "Actual tank scale")):
        for width in (240, 320):
            filename = f"wearables-{size}-{width}.png"
            comparisons.append(
                f'<a href="{filename}"><h3>{label} · {width}px tank</h3>'
                f'{image(filename, f"{label} wearable lineup, {width}px renderer", 480)}</a>'
            )
    html = f"""<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>New Decoration and Wearable Art Review</title>
<style>
body{{margin:2rem auto;max-width:1180px;padding:0 1rem;background:#10232b;color:#e8f1e7;font:16px/1.45 system-ui,sans-serif}}
h1,h2,h3{{color:#f1d89b}}a{{color:#a7e3dd}}.contact img{{max-width:100%;height:auto;image-rendering:pixelated}}
.grid,.wearables{{display:grid;grid-template-columns:repeat(auto-fit,minmax(340px,1fr));gap:1rem}}
article,.wearables>a,.contact{{padding:1rem;background:#1a343e;border-radius:10px}}
.previews{{display:flex;flex-wrap:wrap;gap:.75rem}}.previews a{{display:grid;gap:.25rem;text-align:center}}
.previews img{{max-width:min(100%,240px);height:auto;image-rendering:pixelated}}
.wearables img{{display:block;max-width:100%;height:auto;image-rendering:pixelated}}
</style>
<h1>New Decoration and Wearable Art Review</h1>
<p>All images are RGB565 captures from the native tank renderer. Individual ornament
frames use their 40×40 in-tank artwork bounds; the contact sheet labels every new shop ID.</p>
<section class="contact"><h2>All 20 new decorations</h2>
{image("new-decoration-contact.png", "Labeled contact sheet of decorations 43 through 62", 960)}
<p><a href="new-decoration-contact.png">Open contact sheet</a></p></section>
<h2>Wearable geometry comparison</h2><div class="wearables">{''.join(comparisons)}</div>
<h2>Individual decoration captures</h2><div class="grid">{''.join(entries)}</div>
</html>"""
    (output / "index.html").write_text(html, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True, help="GCC/Clang-compatible compiler or Zig executable")
    parser.add_argument("--out", type=Path, required=True, help="Directory for PNG captures and the gallery index")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    args.out.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        sys.executable, str(root / "tools" / "test_page_art.py"),
        "--cc", args.cc, "--content-gallery", str(args.out.resolve()),
    ], check=True, cwd=root)
    write_index(args.out)


if __name__ == "__main__":
    main()
