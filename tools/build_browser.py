"""Build the real C simulator for a static site (Emscripten 4.0.18).

    python tools/build_browser.py --out _site --emcc emcc

Run prepare_web_installer.py first for a combined installer/playable site.
No device firmware, private saves, or external compilation services are used.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
EMSCRIPTEN = "4.0.18"
EXPORTS = ["sim_init", "sim_save", "sim_backup", "sim_free", "sim_error",
           "sim_frame", "sim_touch", "sim_action", "sim_width", "sim_height",
           "sim_display_brightness", "sim_page", "sim_setup_page", "sim_mode",
           "sim_mode_chosen", "sim_scene_detail", "sim_brightness", "sim_network_page",
           "sim_feed_at", "sim_food_x", "sim_selected", "sim_tool", "malloc", "free"]


def build(output, compiler):
    version = subprocess.check_output([compiler, "--version"], text=True)
    if EMSCRIPTEN not in version.splitlines()[0]:
        raise RuntimeError("Use pinned Emscripten " + EMSCRIPTEN)
    output.mkdir(parents=True, exist_ok=True)
    work = output / ".browser-build"
    work.mkdir(exist_ok=True)
    try:
        subprocess.run([sys.executable, str(ROOT / "tools" / "pack_cyd_icons.py"),
                        str(ROOT / "common" / "icons.c"), str(work)], check=True)
        # cJSON is also used by the firmware. A standalone checkout is only
        # needed on hosts which have not restored ESP-IDF's managed components.
        cjson = ROOT / "firmware" / "managed_components" / "espressif__cjson" / "cJSON"
        if not (cjson / "cJSON.c").is_file():
            cjson = ROOT / ".browser-tools" / "cJSON"
        if not (cjson / "cJSON.c").is_file():
            raise RuntimeError("Missing cJSON: git clone --branch v1.7.19 --depth 1 "
                               "https://github.com/DaveGamble/cJSON.git .browser-tools/cJSON")
        header = (cjson / "cJSON.h").read_text(encoding="utf-8")
        actual = tuple(int(re.search(r"#define CJSON_VERSION_" + field + r"\s+(\d+)", header)[1])
                       for field in ("MAJOR", "MINOR", "PATCH"))
        if actual != (1, 7, 19):
            raise RuntimeError("Use pinned cJSON v1.7.19")
        sources = [ROOT / "sim" / name for name in ("main.c", "persist_port_sim.c", "browser_port.c")]
        sources += [ROOT / "common" / name for name in (
            "tank.c", "advisor.c", "render.c", "games.c", "progression.c",
            "setup.c", "notice.c", "update.c", "aquarium_store.c",
            "companion.c", "companion_ui.c", "weather.c")]
        subprocess.run([compiler, "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(ROOT / "common"), "-I" + str(cjson),
                        *map(str, sources), str(cjson / "cJSON.c"), str(work / "icons_sd.c"),
                        "-lm", "-sMODULARIZE=1", "-sEXPORT_ES6=1", "-sENVIRONMENT=web",
                        "-sALLOW_MEMORY_GROWTH=1", "-sFORCE_FILESYSTEM=1",
                        "-sEXPORTED_FUNCTIONS=" + json.dumps(["_" + name for name in EXPORTS]),
                        '-sEXPORTED_RUNTIME_METHODS=["FS","UTF8ToString","HEAPU8","HEAPU16"]',
                        "-o", str(work / "tank.mjs")], check=True)
        digest = hashlib.sha256()
        for name in ("tank.mjs", "tank.wasm", "icons.bin"):
            digest.update((work / name).read_bytes())
        asset_name = "game-" + digest.hexdigest()[:16]
        target = output / asset_name
        target.mkdir(exist_ok=True)
        for name in ("tank.mjs", "tank.wasm", "icons.bin"):
            shutil.copyfile(work / name, target / name)
        for name in ("play.mjs", "play.css"):
            data = (ROOT / "web" / name).read_bytes()
            versioned = Path(name).stem + "-" + hashlib.sha256(data).hexdigest()[:16] + Path(name).suffix
            (output / versioned).write_bytes(data)
        html = (ROOT / "web" / "play.html").read_text(encoding="utf-8")
        for name in ("play.mjs", "play.css"):
            data = (ROOT / "web" / name).read_bytes()
            versioned = Path(name).stem + "-" + hashlib.sha256(data).hexdigest()[:16] + Path(name).suffix
            html = html.replace(name, versioned)
        (output / "play.html").write_text(html, encoding="utf-8")
        version = re.search(r'#define PT_RELEASE\s+"([^"]+)"',
                            (ROOT / "common" / "version.h").read_text(encoding="utf-8"))[1]
        commit = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
        (output / "play-build.json").write_text(json.dumps({
            "assets": asset_name, "version": version, "emscripten": EMSCRIPTEN,
            "commit": commit,
        }) + "\n", encoding="utf-8")
        print("Playable browser build:", output / "play.html", "(" + asset_name + ")")
    finally:
        shutil.rmtree(work)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "_site")
    parser.add_argument("--emcc", default="emcc")
    args = parser.parse_args()
    build(args.out.resolve(), args.emcc)
