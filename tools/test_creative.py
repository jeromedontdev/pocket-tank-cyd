"""Run fixed first-run Creative mode, editing, rollback and migration regressions."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = root / "firmware" / "build_creative_native"
    build.mkdir(exist_ok=True)
    common = root / "common"
    cjson = root / "firmware" / "managed_components" / "espressif__cjson" / "cJSON"
    sources = [common / name for name in ("tank.c", "progression.c", "companion.c")]
    try:
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        command += ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(common), "-I" + str(cjson)]
        command += [str(root / "tools" / "test_creative.c")]
        command += [str(source) for source in sources]
        command += [str(cjson / "cJSON.c"),
                    "-o", str(build / "test-creative.exe")]
        if sys.platform != "win32":
            command += ["-pthread", "-lm"]
        subprocess.run(command, check=True)
        subprocess.run([str(build / "test-creative.exe")], check=True, cwd=root)
    finally:
        shutil.rmtree(build)


if __name__ == "__main__":
    main()
