"""Focused real-renderer tests for clock/weather routing and brightness dragging."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil

from run import ROOT, backup, build, load


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    work = ROOT / ("clock-sim-tests-" + str(os.getpid()))
    work.mkdir()
    lib = None
    try:
        for key, name in (("SAVE", "tank.sav"), ("WIFI", "wifi.txt"),
                          ("WEATHER", "weather.bin"), ("BRIGHTNESS", "brightness.bin")):
            os.environ["POCKET_TANK_" + key] = str(work / name)
        lib = load(build(args.cc, work), (work / "icons.bin").read_bytes())

        def touch(x, y, down):
            lib.sim_touch(x * lib.sim_width() / 448, y * lib.sim_height() / 368, down)

        def tap(x, y):
            touch(x, y, 1)
            touch(x, y, 0)

        def frame():
            return ctypes.string_at(lib.sim_frame(0), lib.sim_width() * lib.sim_height() * 2)

        lib.sim_action(2)
        settings = frame()
        original = json.loads(backup(lib))
        tap(330, 100)
        clock = frame()
        tap(100, 260)
        weather = frame()
        assert weather not in (settings, clock)
        tap(140, 234)
        keyboard = frame()
        assert keyboard != weather
        tap(390, 20)
        assert frame() == weather, "Search Back must return to weather settings"
        tap(390, 20)
        assert frame() == clock, "Weather Done must return to CLOCK, not LIFE"
        tap(390, 20)
        assert frame() == settings, "Clock Back must return to general settings"
        assert json.loads(backup(lib)) == original, "Weather browsing must not mutate the aquarium"
        tap(100, 100)

        path = Path(os.environ["POCKET_TANK_BRIGHTNESS"])
        assert lib.sim_brightness() == 100 and not path.exists()
        touch(48, 84, 1)
        assert lib.sim_brightness() == 10 and not path.exists(), "Press must preview without saving"
        touch(224, 84, 1)
        assert lib.sim_brightness() == 55 and not path.exists(), "Dragging must preview continuously"
        touch(401, 40, 1)
        assert lib.sim_brightness() == 100, "Captured slider must tolerate vertical drift"
        touch(5, 84, 1)
        assert lib.sim_brightness() == 10
        touch(48 + 352 * 37 / 90, 84, 1)
        assert lib.sim_brightness() == 47 and not path.exists()
        touch(48 + 352 * 37 / 90, 84, 0)
        assert path.read_bytes() == bytes([47]) and lib.sim_brightness_reload() == 47
        tap(48 + 352 * 73 / 90, 84)
        assert path.read_bytes() == bytes([83]) and lib.sim_brightness_reload() == 83
        touch(224, 84, 1)
        assert lib.sim_brightness() == 55 and path.read_bytes() == bytes([83])
        touch(224, 84, 0)
        assert path.read_bytes() == bytes([55])
        os.environ["POCKET_TANK_BRIGHTNESS"] = str(work / "missing" / "brightness.bin")
        tap(400, 84)
        assert lib.sim_brightness() == 55, "Failed saves must roll back the live preview"
        os.environ["POCKET_TANK_BRIGHTNESS"] = str(path)
        assert lib.sim_brightness_reload() == 55
        for invalid in (b"\x09", b"\x65", b"", b"\x2f\x00"):
            path.write_bytes(invalid)
            assert lib.sim_brightness_reload() == 100, "Corrupt/out-of-range settings use the safe default"
        assert json.loads(backup(lib)) == original, "Device brightness must not mutate aquarium progression"
        print("PASS: CLOCK/weather/search/back routing; draggable 10..100 slider; arbitrary percentages; preview-only motion; restart, corruption and save-failure rollback")
    finally:
        if lib is not None and os.name == "nt":
            ctypes.windll.kernel32.FreeLibrary(ctypes.c_void_p(lib._handle))
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
