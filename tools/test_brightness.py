"""Exercise the real firmware brightness policy with a bounded fake NVS/display."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    work = root / ("brightness-tests-" + str(os.getpid()))
    work.mkdir()
    try:
        (work / "nvs_flash.h").write_text('#include "nvs.h"\n')
        (work / "nvs.h").write_text("""#ifndef TEST_NVS_H
#define TEST_NVS_H
#include <stdint.h>
typedef int esp_err_t;
typedef int nvs_handle_t;
#define ESP_OK 0
#define NVS_READONLY 0
#define NVS_READWRITE 1
const char *esp_err_to_name(esp_err_t);
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_get_u8(nvs_handle_t, const char *, uint8_t *);
esp_err_t nvs_set_u8(nvs_handle_t, const char *, uint8_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
#endif
""")
        (work / "esp_log.h").write_text("""#ifndef TEST_LOG_H
#define TEST_LOG_H
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
#endif
""")
        executable = work / "test-brightness.exe"
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        command += ["-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(work), "-I" + str(root / "firmware" / "main"),
                    "-I" + str(root / "common"),
                    str(root / "tools" / "test_brightness.c"),
                    str(root / "firmware" / "main" / "brightness.c"), "-o", str(executable)]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)
    finally:
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
