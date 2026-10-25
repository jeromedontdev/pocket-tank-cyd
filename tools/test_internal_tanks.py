"""Exercise the actual CYD internal tank port with native storage and SD fixtures."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
HARNESS = r'''
#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include "aquarium_store.h"
#include "progression.h"
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static const char *tank_directory;
static const char *sd_directory;
static bool mounted = true, card = true;
static unsigned imports;
static bool mount_store(void) { return mounted; }
static bool sd_port_aquarium_read(const uint32_t id[2], int slot, void *data, size_t size) {
    imports++;
    return card && aquarium_store_read(sd_directory, id, slot, data, size);
}
'''
TEST = r'''
int main(int argc, char **argv) {
    assert(argc == 3);
    tank_directory = argv[1]; sd_directory = argv[2];
    const uint32_t id[2] = {0x12345678, 0x87654321};
    unsigned char in[AQUARIUM_SAVE_BYTES], out[AQUARIUM_SAVE_BYTES];
    memset(in, 17, sizeof in);
    assert(aquarium_store_present(tank_directory, id, 0) == 0);
    assert(aquarium_store_write(sd_directory, id, 0, in, sizeof in));
    assert(aquarium_port_read(id, 0, out, sizeof out) && !memcmp(in, out, sizeof in));
    assert(imports == 1);
    card = false;
    for (int slot = 0; slot < AQUARIUM_CAP; slot++) {
        memset(in, 30 + slot, sizeof in);
        assert(aquarium_port_write(id, slot, in, sizeof in));
    }
    for (int round = 0; round < 10; round++) {
        for (int slot = 0; slot < AQUARIUM_CAP; slot++) {
            memset(in, 40 + round + slot, sizeof in);
            assert(aquarium_port_write(id, slot, in, sizeof in));
            assert(aquarium_port_read(id, slot, out, sizeof out) && !memcmp(in, out, sizeof in));
        }
    }
    assert(imports == 1);
    char path[512];
    /* Power loss during an alternate write must leave the complete archive readable. */
    snprintf(path, sizeof path, "%s/1234567887654321-2.b", tank_directory);
    FILE *partial = fopen(path, "wb"); assert(partial);
    assert(fwrite("partial", 1, 7, partial) == 7); assert(!fclose(partial));
    assert(aquarium_port_read(id, 2, out, sizeof out) && out[0] == 51);
    memset(in, 71, sizeof in);
    assert(aquarium_port_write(id, 2, in, sizeof in));
    assert(aquarium_port_read(id, 2, out, sizeof out) && out[0] == 71);
    for (int copy = 0; copy < 2; copy++) {
        snprintf(path, sizeof path, "%s/1234567887654321-0.%c", tank_directory, copy ? 'b' : 'a');
        FILE *file = fopen(path, "wb"); assert(file);
        assert(fwrite("broken", 1, 6, file) == 6); assert(!fclose(file));
    }
    card = true;
    assert(!aquarium_port_read(id, 0, out, sizeof out));
    assert(imports == 1); /* Never resurrect the stale SD generation. */
    mounted = false;
    assert(!aquarium_port_ready());
    assert(!aquarium_port_write(id, 1, in, sizeof in));
    assert(!aquarium_port_read(id, 1, out, sizeof out));
    mounted = true;
    assert(!aquarium_port_write(id, -1, in, sizeof in));
    assert(!aquarium_port_read(id, 8, out, sizeof out));
    const char *original = tank_directory;
    char missing[512];
    snprintf(missing, sizeof missing, "%s/missing", original);
    tank_directory = missing;
    assert(!aquarium_port_read(id, 0, out, sizeof out)); /* Failed import cannot be reported as success. */
    assert(aquarium_store_present(sd_directory, id, 0) == 1);
    tank_directory = original;
    assert(remove_archives());
    assert(aquarium_store_present(tank_directory, id, 1) == 0);
    assert(aquarium_store_present(sd_directory, id, 0) == 1);
    puts("PASS: eight internal tanks without SD, verified read-through import, repeated replacement, corrupt-local blocks stale SD, mount/bounds failures, wipe cleanup preserves SD");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--idf", type=Path, default=Path(os.environ["IDF_PATH"]) if "IDF_PATH" in os.environ else None)
    args = parser.parse_args()
    source = (ROOT / "firmware" / "components" / "tankcore" / "persist_port_esp.c").read_text()
    port = source[source.index("static bool remove_archives("):source.index("static bool legacy_load(")]
    with tempfile.TemporaryDirectory(prefix="cyd-internal-tanks-") as work:
        work = Path(work)
        local, sd = work / "internal", work / "sd"
        local.mkdir()
        sd.mkdir()
        file = work / "test.c"
        file.write_text(HARNESS + port + TEST)
        compiler = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            compiler.append("cc")
        executable = work / "test.exe"
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I" + str(ROOT / "common"), str(file), str(ROOT / "common" / "aquarium_store.c"),
            "-o", str(executable)], check=True)
        subprocess.run([str(executable), str(local), str(sd)], check=True)
        assert not list(local.iterdir())
        assert len(list(sd.iterdir())) == 1
        if args.idf:
            spec = importlib.util.spec_from_file_location("spiffsgen", args.idf / "components" / "spiffs" / "spiffsgen.py")
            generator = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(generator)
            config = generator.SpiffsBuildConfig(256, 2, 4096, 2, 4, 32, 2, 2,
                                                  True, True, "little", True, True, False)
            fs = generator.SpiffsFS(0x20000, config)
            payload = work / "snapshot"
            payload.write_bytes(next(sd.iterdir()).read_bytes())
            # All eight archives, two active generations and a replacement in flight.
            for slot in range(8):
                fs.create_file(f"/tanks/1234567887654321-{slot}.a", str(payload))
            for name in ("/5052463141435456-0.a", "/5052463141435456-0.b",
                         "/tanks/1234567887654321-0.b"):
                fs.create_file(name, str(payload))
            payload.write_bytes(bytes(28))
            for copy in ("a", "b"):
                fs.create_file("/5052463141435456-1." + copy, str(payload))
            image = fs.to_binary()
            assert len(image) == 0x20000
            occupied = len(fs.blocks) * 4096
            assert occupied <= 0x20000 * 75 // 100, occupied
            print(f"PASS: actual IDF SPIFFS geometry packs eight tanks, active redundancy and in-flight replacement in {occupied}/131072 bytes (<75%)")


if __name__ == "__main__":
    main()
