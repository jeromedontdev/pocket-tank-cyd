"""Compile actual boot profile loading/provisioning with controlled NVS failures."""
import argparse
from pathlib import Path
import subprocess
import tempfile

MOCK = r"""
#ifndef MOCK_HARDWARE_H
#define MOCK_HARDWARE_H
#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 1
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define ESP_LOGE(tag, ...) ((void)(tag), errors++)
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define pdMS_TO_TICKS(ms) (ms)
typedef int esp_err_t;
typedef int nvs_handle_t;
static int errors, saved_id, writes, commits, read_error, save_error, set_error, commit_error;
static int nvs_open(const char *ns, int mode, nvs_handle_t *handle) {
    assert(!strcmp(ns, "pt_hardware"));
    if (mode == NVS_READWRITE && save_error) return save_error;
    *handle = 1;
    return ESP_OK;
}
static int nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *value) {
    assert(h == 1 && !strcmp(key, "profile"));
    if (read_error) return read_error;
    *value = (uint8_t)saved_id;
    return ESP_OK;
}
static void nvs_close(nvs_handle_t h) { assert(h == 1); }
static int nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value) {
    assert(h == 1 && !strcmp(key, "profile"));
    writes++;
    if (set_error) return set_error;
    saved_id = value; return ESP_OK;
}
static int nvs_commit(nvs_handle_t h) { assert(h == 1); commits++; return commit_error; }
#if PT_BOARD_UNIFIED
static void vTaskDelay(int ticks) { (void)ticks; abort(); }
#endif
#endif
"""

COMMAND_TEST = r"""
typedef struct { int unused; } tank_t;
static int tank_saves, restarts;
static bool tank_save_ok;
static bool progression_save(tank_t *t) {
    assert(t);
    tank_saves++;
    return tank_save_ok;
}
static void esp_restart(void) { restarts++; }
static void hardware_command(tank_t *t, int argc, char **argv) {
    const char *c = argv[0];
    /* ACTUAL_COMMAND */
}
"""

TEST = r"""
int main(int argc, char **argv) {
    assert(argc == 2);
    int scenario = atoi(argv[1]);
    read_error = scenario == 0 || scenario == 4 ? ESP_ERR_NVS_NOT_FOUND :
                 scenario == 3 ? 7 : ESP_OK;
    saved_id = scenario == 2 ? 99 : scenario == 5 ? 2 : 1;
    bool result = board_hardware_init();
    bool expected = scenario != 3;
#if !PT_BOARD_UNIFIED
    expected = scenario == 0 || scenario == 4 ||
               (scenario == 1 && !PT_BOARD_ELEGOO) ||
               (scenario == 5 && PT_BOARD_ELEGOO);
#endif
    assert(result == expected);
    if (!expected) {
        assert(!board_hardware_current() && errors == 1);
        return 0;
    }
    const board_hardware_t *hardware = board_hardware_current();
    assert(hardware);
#if PT_BOARD_UNIFIED
    unsigned expected_id = scenario == 0 || scenario == 2 || scenario == 4 || scenario == 5 ? 2 : 1;
    assert(hardware->id == expected_id);
    assert(writes == (scenario == 0 || scenario == 2 || scenario == 4 ? 1 : 0));
    assert(commits == writes);
    if (scenario == 4) assert(errors == 2);
#else
    assert(hardware->id == board_hardware_build_default());
    assert(!writes && !commits && !save_error);
#endif
    assert(hardware->has_ldr == (hardware->id == BOARD_HARDWARE_CYD));
    assert(hardware->led_red_pin == (hardware->has_ldr ? 4 : 22));
    assert(hardware->audio_enable_pin == (hardware->has_ldr ? -1 : 4));
    assert(hardware->panel_mirror_columns == !hardware->has_ldr);
    assert(hardware->default_touch_flip_x == (hardware->id == BOARD_HARDWARE_ELEGOO));
    int old_writes = writes, old_commits = commits;
    assert(!board_hardware_save(99));
    assert(writes == old_writes && commits == old_commits);
    save_error = 7;
    assert(!board_hardware_save(hardware->id));
    assert(writes == old_writes && commits == old_commits);
    save_error = 0;
    set_error = 7;
    assert(!board_hardware_save(hardware->id));
    assert(writes == old_writes + 1 && commits == old_commits);
    set_error = 0;
    commit_error = 7;
    assert(!board_hardware_save(hardware->id));
    assert(writes == old_writes + 2 && commits == old_commits + 1);
    commit_error = 0;
    assert(board_hardware_save(hardware->id));
    assert(writes == old_writes + 3 && commits == old_commits + 2);
    assert(board_hardware_current() == hardware);
#if !PT_BOARD_UNIFIED
    assert(!board_hardware_save(hardware->id == 1 ? 2 : 1));
    assert(writes == old_writes + 3 && commits == old_commits + 2);
#endif
    tank_t tank = {0};
    char *query[] = {"hardware"};
    char *invalid[] = {"hardware", "unknown"};
    char *extra[] = {"hardware", "cyd", "extra"};
    char *select[] = {"hardware", hardware->id == BOARD_HARDWARE_CYD ? "cyd" : "elegoo"};
    old_writes = writes;
    hardware_command(NULL, 1, query);
    hardware_command(&tank, 2, invalid);
    hardware_command(&tank, 3, extra);
    hardware_command(NULL, 2, select);
    assert(!tank_saves && !restarts && writes == old_writes);
    hardware_command(&tank, 2, select);
    assert(tank_saves == 1 && !restarts && writes == old_writes);
    tank_save_ok = true;
    save_error = 7;
    hardware_command(&tank, 2, select);
    assert(tank_saves == 2 && !restarts && writes == old_writes);
    save_error = 0;
    hardware_command(&tank, 2, select);
    assert(tank_saves == 3 && restarts == 1 && writes == old_writes + 1);
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="board-hardware-test-") as directory:
        folder = Path(directory)
        for name in ("esp_log.h", "nvs.h", "freertos/FreeRTOS.h", "freertos/task.h"):
            path = folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "mock.h"\n')
        (folder / "mock.h").write_text(MOCK)
        test = folder / "test.c"
        source = root / "firmware" / "main" / "board_hardware_esp.c"
        director = (root / "firmware" / "main" / "director.c").read_text()
        command_branch = director.split('    if (!strcmp(c, "hardware")) {', 1)[1]
        command_branch = '    if (!strcmp(c, "hardware")) {' + command_branch.split('    if (!strcmp(c, "sd"))', 1)[0]
        test.write_text('#include "mock.h"\n#include "' + source.as_posix() + '"\n' +
                        COMMAND_TEST.replace("/* ACTUAL_COMMAND */", command_branch) + TEST)
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        for elegoo, unified in ((0, 0), (1, 0), (0, 1)):
            exe = folder / f"profile-{elegoo}-{unified}.exe"
            subprocess.run(command + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                                      "-DPT_RUNTIME_HARDWARE=1", f"-DPT_BOARD_ELEGOO={elegoo}",
                                      f"-DPT_BOARD_UNIFIED={unified}", "-I" + str(folder),
                                      "-I" + str(root / "common"), str(test), "-o", str(exe)], check=True)
            for scenario in range(6):
                serial_input = "hardware elegoo\n"
                if scenario == 4:
                    serial_input = "hardware unknown\n" + "x" * 80 + "\nhardware elegoo\n"
                subprocess.run([str(exe), str(scenario)], input=serial_input, text=True, check=True)
        print("PASS: profiles, provisioning, invalid commands/IDs, NVS read/write/commit failures, aquarium-save-first recovery")


if __name__ == "__main__":
    main()
