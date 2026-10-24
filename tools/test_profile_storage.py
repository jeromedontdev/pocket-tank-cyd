"""Exercise the firmware's exact empty-partition formatting guard."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef struct { size_t size; } esp_partition_t;
enum { ESP_OK = 0 };
#define ESP_LOGW(...) ((void)0)
static unsigned char image[0x20000];
static bool fail_read;
static int esp_partition_read(const esp_partition_t *p, size_t offset, void *data, size_t size) {
    assert(offset + size <= p->size);
    if (fail_read) return -1;
    memcpy(data, image + offset, size);
    return ESP_OK;
}
"""
TEST = r"""
static void empty_legacy(void) {
    memset(image, 255, sizeof image);
    for (unsigned block = 0; block < 16; block++) {
        unsigned magic = (0x20140529u ^ 256u ^ (16u - block)) & 0xffff;
        image[block * 4096 + 252] = (unsigned char)magic;
        image[block * 4096 + 253] = (unsigned char)(magic >> 8);
        image[block * 4096 + 254] = image[block * 4096 + 255] = 0;
    }
}
int main(void) {
    esp_partition_t p = {sizeof image};
    memset(image, 255, sizeof image);
    assert(blank_partition(&p));
    empty_legacy();
    assert(blank_partition(&p));
    const size_t positions[] = {0, 31, 251, 252, 253, 254, 255, 256, 4095,
        4096, 4348, 65535, 65536, 65788, 131071};
    for (unsigned i = 0; i < sizeof positions / sizeof *positions; i++) {
        size_t offset = positions[i];
        image[offset] ^= 1;
        assert(!blank_partition(&p));
        image[offset] ^= 1;
    }
    /* Erased lookup metadata with any payload is not an empty filesystem. */
    memset(image, 255, sizeof image);
    image[4096] = 0;
    assert(!blank_partition(&p));
    empty_legacy();
    fail_read = true;
    assert(!blank_partition(&p));
    fail_read = false;
    p.size /= 2;
    assert(!blank_partition(&p));
    puts("PASS: erased/exact legacy empty SPIFFS accepted; files, damage, read failures and wrong size never formatted");
}
"""


def check(compiler):
    source = (ROOT / "firmware" / "components" / "tankcore" / "persist_port_esp.c").read_text()
    guard = source[source.index("static bool blank_partition("):source.index("static bool mount_store(")]
    with tempfile.TemporaryDirectory(prefix="cyd-storage-guard-") as work:
        work = Path(work)
        file = work / "test.c"
        file.write_text(HARNESS + guard + TEST)
        executable = work / "test.exe"
        command = [compiler]
        if Path(compiler).stem.lower() == "zig":
            command.append("cc")
        subprocess.run(command + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                                 str(file), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


class StorageGuardSourceTests(unittest.TestCase):
    def test_guard_keeps_nonblank_safety_policy(self):
        source = (ROOT / "firmware" / "components" / "tankcore" / "persist_port_esp.c").read_text()
        self.assertIn(".format_if_mount_failed = blank_partition(partition)", source)
        self.assertIn("return erased || legacy_empty;", source)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    check(parser.parse_args().cc)
