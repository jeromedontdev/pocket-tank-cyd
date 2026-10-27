"""Verify frame-time percentile bounds, slow-frame thresholds and resets."""
import argparse
from pathlib import Path
import subprocess
import tempfile


HARNESS = r"""
#include <assert.h>
#include <stdio.h>
#include "frame_pacing.h"
int main(void) {
    frame_pacing_t p = {0};
    assert(!frame_pacing_percentile(&p, 99));
    for (unsigned i = 1; i <= 200; i++) frame_pacing_add(&p, i * 1000);
    assert(p.frames == 200 && p.total_us == 20100000 && p.max_us == 200000);
    assert(p.over_50 == 150 && p.over_80 == 120 && p.over_100 == 100);
    assert(frame_pacing_percentile(&p, 50) >= 100000 && frame_pacing_percentile(&p, 50) <= 102000);
    assert(frame_pacing_percentile(&p, 95) >= 190000 && frame_pacing_percentile(&p, 95) <= 192000);
    assert(frame_pacing_percentile(&p, 99) >= 198000 && frame_pacing_percentile(&p, 99) <= 200000);
    memset(&p, 0, sizeof p);
    frame_pacing_add(&p, 4000000);
    assert(frame_pacing_percentile(&p, 99) == 4000000);
    memset(&p, 0, sizeof p);
    frame_pacing_add(&p, 50000); frame_pacing_add(&p, 80000); frame_pacing_add(&p, 100000);
    assert(!p.over_100 && p.over_80 == 1 && p.over_50 == 2);
    puts("PASS: frame pacing percentiles, conservative 2-ms bounds, exact maxima, stalls and threshold boundaries");
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="cyd-pacing-test-") as folder:
        folder = Path(folder)
        source = folder / "test.c"
        source.write_text(HARNESS)
        executable = folder / "test.exe"
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        subprocess.run(command + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                                 "-I" + str(root / "firmware" / "main"), str(source),
                                 "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
