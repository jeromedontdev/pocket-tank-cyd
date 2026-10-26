"""Build and run the isolated placement-copy regression harness."""
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parent.parent
    build = root / ".setup-placement-test-build"
    build.mkdir(exist_ok=False)
    common = root / "common"
    cjson = root / "firmware" / "managed_components" / "espressif__cjson" / "cJSON"
    try:
        subprocess.run([sys.executable, str(root / "tools" / "pack_cyd_icons.py"),
                        str(common / "icons.c"), str(build)], check=True)
        native_cc = shutil.which("cc")
        emcc = root / ".browser-tools" / "emsdk" / "upstream" / "emscripten" / "emcc.bat"
        if native_cc:
            executable = build / "test-setup-placement.exe"
            command = [native_cc]
        else:
            executable = build / "test-setup-placement.js"
            node = root / ".browser-tools" / "emsdk" / "node" / "22.16.0_64bit" / "bin" / "node.exe"
            command = [os.environ.get("COMSPEC", "cmd.exe"), "/c", str(emcc)]
        command += ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(common), "-I" + str(cjson)]
        command += [str(root / "tools" / name) for name in
                    ("test_setup_placement.c", "test_clock_weather.c", "test_decor_scenery.c")]
        command += [str(common / name) for name in (
            "tank.c", "progression.c", "advisor.c", "render.c", "games.c", "setup.c",
            "notice.c", "update.c", "aquarium_store.c", "companion.c", "companion_ui.c", "weather.c")]
        command += [str(build / "icons_sd.c"), str(cjson / "cJSON.c"),
                    str(root / "sim" / "net_port_sim.c"), str(root / "sim" / "weather_port_sim.c")]
        if native_cc and sys.platform != "win32":
            command += ["-pthread", "-lm"]
        elif not native_cc:
            command += ["-lm", "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sFORCE_FILESYSTEM=1"]
        command += ["-o", str(executable)]
        subprocess.run(command, check=True)
        if native_cc:
            slots = str(build)
            wifi, weather = str(build / "wifi.txt"), str(build / "weather.bin")
        else:
            slots, wifi, weather = "/fixtures", "/wifi.txt", "/weather.bin"
        environment = dict(os.environ, POCKET_TANK_FAKE_UPDATE="", POCKET_TANK_TEST_SLOTS=slots,
                           POCKET_TANK_WIFI=wifi, POCKET_TANK_WEATHER=weather)
        run_command = [str(executable)] if native_cc else [str(node), str(executable)]
        subprocess.run(run_command, check=True, env=environment)
    finally:
        shutil.rmtree(build)


if __name__ == "__main__":
    main()
