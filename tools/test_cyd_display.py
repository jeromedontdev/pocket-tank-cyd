"""Exercise the actual CYD flush loop with delayed DMA and a simulated LCD."""
import argparse
from pathlib import Path
import subprocess
import tempfile


HARNESS = r"""
#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#define CYD_PANEL_W 320
#define CYD_PANEL_H 240
#define CYD_STRIPE_ROWS 16
#define CYD_STRIPE_BYTES (240 * 16 * sizeof(uint16_t))
#define CYD_DIAGNOSTIC_PATTERN 0
#define CYD_LCD_MIRROR_X 0
#define CYD_LCD_MIRROR_Y 1
#define portMAX_DELAY 0
#define ESP_ERROR_CHECK(call) assert((call) == 0)
#define ESP_LOGI(...) ((void)0)
static int TANK_W = 240, TANK_H = 180, tile_y, tile_h;
static int region_x, region_w;
static int render_region_left(void) { return region_w ? region_x : 0; }
static int render_region_right(void) { return region_w ? region_x + region_w : TANK_W; }
static int render_tile_top(void) { return tile_y; }
static int render_tile_bottom(void) { return tile_h ? tile_y + tile_h : TANK_H; }
static int s_panel = 1, s_flush_done = 1;
typedef int TaskHandle_t;
static TaskHandle_t s_worker;
static int s_job_done = 2, job_available = 1;
static struct {
    const uint16_t *pixels;
    int top, bottom, left, right;
} s_job;
static bool job_queued;
static void display_port_flush(const uint16_t *fb);
static void flush_region(const uint16_t *fb, int top, int bottom, int left, int right);
static void display_worker(void *arg);
static jmp_buf worker_yield;
static bool worker_step;
#define pdTRUE 1
static unsigned ulTaskNotifyTake(int clear, int delay) {
    (void)clear; (void)delay;
    if (worker_step) longjmp(worker_yield, 1);
    worker_step = true;
    return 1;
}
static void xTaskNotifyGive(TaskHandle_t worker) { assert(worker && !job_queued); job_queued = true; }
static void xSemaphoreGive(int sem) { assert(sem == s_job_done); job_available = 1; }
static uint16_t stripe_memory[240 * 16], *s_stripe = stripe_memory;
static bool s_inverted;
static uint32_t s_block_crc[6][40];
static bool s_block_valid[6][40];
static int s_layout_width, s_layout_height, s_layout_tile;
static int64_t s_wait_us, s_send_us, s_fill_us;
static unsigned s_blocks_sent, s_blocks_skipped;
static unsigned crc_calls;
static int64_t esp_timer_get_time(void) { static int64_t t; return ++t; }
static uint32_t esp_crc32_le(uint32_t seed, const uint8_t *p, size_t n) {
    crc_calls++;
    uint32_t crc = ~seed;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
static uint16_t lcd[320 * 240];
static const uint16_t *pending;
static uint16_t pending_copy[240 * 16];
static int px0, py0, px1, py1;
static int esp_lcd_panel_draw_bitmap(int panel, int x0, int y0, int x1, int y1, const void *data) {
    (void)panel;
    assert(!pending && x0 >= 0 && x1 <= 240 && y0 >= 0 && y1 <= 320);
    assert(data == s_stripe || data == s_stripe + CYD_STRIPE_BYTES / 2 / sizeof(uint16_t));
    assert((size_t)(x1 - x0) * (y1 - y0) * sizeof(uint16_t) <= CYD_STRIPE_BYTES / 2);
    pending = data; px0 = x0; py0 = y0; px1 = x1; py1 = y1;
    memcpy(pending_copy, data, (size_t)(x1 - x0) * (y1 - y0) * 2);
    return 0;
}
static int xSemaphoreTake(int sem, int delay) {
    (void)sem; (void)delay;
    if (sem == s_job_done) {
        if (job_queued) {
            job_queued = false;
            worker_step = false;
            if (!setjmp(worker_yield)) display_worker(NULL);
        }
        assert(job_available);
        job_available = 0;
        return 1;
    }
    assert(pending);
    /* DMA retains the pointer: writes before completion corrupt the image. */
    assert(!memcmp(pending_copy, pending, (size_t)(px1 - px0) * (py1 - py0) * 2));
    for (int y = py0; y < py1; y++) for (int x = px0; x < px1; x++)
        lcd[y * 240 + x] = pending[(y - py0) * (px1 - px0) + x - px0];
    pending = NULL;
    return 1;
}
"""

TEST = r"""
static void verify_frame(const uint16_t *frame) {
    assert(!pending);
    for (int y = 0; y < 320; y++) for (int x = 0; x < 240; x++) {
        int lx = y * TANK_W / 320, ly = TANK_H - 1 - x * TANK_H / 240;
        if (s_inverted) { lx = TANK_W - 1 - lx; ly = TANK_H - 1 - ly; }
        uint16_t v = frame[ly * TANK_W + lx];
        if (lcd[y * 240 + x] != (uint16_t)((v >> 8) | (v << 8)))
            fprintf(stderr, "Full LCD mismatch %dx%d tile %d inversion %d at %d,%d region %d+%d\n",
                TANK_W, TANK_H, tile_h, s_inverted, x, y, region_x, region_w);
        assert(lcd[y * 240 + x] == (uint16_t)((v >> 8) | (v << 8)));
    }
}
static void check_frame(uint16_t *frame, int height) {
    for (tile_y = 0; tile_y < TANK_H; tile_y += height) {
        tile_h = height == TANK_H ? 0 : TANK_H - tile_y < height ? TANK_H - tile_y : height;
        display_port_flush(frame + tile_y * TANK_W);
    }
    verify_frame(frame);
}
int main(void) {
    (void)display_worker;
    uint16_t *frame = malloc(320 * 240 * 2); assert(frame);
    for (int invert = 0; invert < 2; invert++) {
        TANK_W = 240; TANK_H = 180; s_inverted = invert != 0;
        for (int i = 0; i < TANK_W * TANK_H; i++) frame[i] = (uint16_t)(i * 13u);
        check_frame(frame, 80);
        unsigned sent_before = s_blocks_sent;
        check_frame(frame, 80);
        assert(s_blocks_sent == sent_before);
        region_x = 32; region_w = 64; tile_y = 40; tile_h = 40;
        display_port_flush(frame + tile_y * TANK_W);
        unsigned partial_before = s_blocks_sent;
        display_port_flush(frame + tile_y * TANK_W);
        assert(s_blocks_sent == partial_before);
        region_x = 48; region_w = 32; tile_y = 50; tile_h = 20;
        for (int y = 50; y < 70; y++) for (int x = 48; x < 80; x++) frame[y * TANK_W + x] ^= 0x7342;
        display_port_flush(frame + tile_y * TANK_W);
        verify_frame(frame);
        for (int y = 50; y < 70; y++) for (int x = 48; x < 80; x++) frame[y * TANK_W + x] ^= 0x7342;
        region_x = 32; region_w = 64; tile_y = 40; tile_h = 40;
        partial_before = s_blocks_sent;
        display_port_flush(frame + tile_y * TANK_W);
        assert(s_blocks_sent > partial_before); /* Old CRC must not hide the overlapping write. */
        verify_frame(frame);
        region_x = region_w = 0;
        for (int pass = 0; pass < 8; pass++) {
            region_x = pass * 29; region_w = TANK_W - region_x < 29 ? TANK_W - region_x : 29;
            tile_y = pass % 5 * 40; tile_h = TANK_H - tile_y < 40 ? TANK_H - tile_y : 40;
            for (int y = tile_y; y < tile_y + tile_h; y++)
                for (int x = region_x; x < region_x + region_w; x++) frame[y * TANK_W + x] ^= 0x73ae;
            unsigned crc_before = crc_calls;
            display_port_flush(frame + tile_y * TANK_W);
            assert(crc_calls > crc_before);
            verify_frame(frame);
            unsigned partial_sent = s_blocks_sent;
            display_port_flush(frame + tile_y * TANK_W);
            assert(s_blocks_sent == partial_sent);
            verify_frame(frame);
            region_x = region_w = 0;
            check_frame(frame, 80);
        }
    }
    memset(s_block_valid, 0, sizeof s_block_valid);
    for (int mode = 0; mode < 4; mode++) {
        TANK_W = mode % 2 ? 320 : 240; TANK_H = mode % 2 ? 240 : 180;
        int height = mode % 2 ? 80 : 180;
        for (int i = 0; i < TANK_W * TANK_H; i++) frame[i] = (uint16_t)(i * 13u + i / 43);
        unsigned before = s_blocks_sent;
        check_frame(frame, height);
        if (TANK_H == 240) {
        assert(s_blocks_sent - before == 3 * ((320 + 23) / 24));
        for (int pass = 0; pass < 10; pass++) {
            region_x = pass * 32; region_w = 32;
            tile_y = (pass % 6) * 40; tile_h = 40;
            for (int y = tile_y; y < tile_y + tile_h; y++)
                for (int x = region_x; x < region_x + region_w; x++) frame[y * TANK_W + x] ^= 0x731e;
            display_port_flush(frame + tile_y * TANK_W);
            assert(!pending);
            for (int y = 0; y < 320; y++) for (int x = 0; x < 240; x++) {
                int lx = s_inverted ? TANK_W - 1 - y : y;
                int ly = s_inverted ? x : TANK_H - 1 - x;
                uint16_t v = frame[ly * TANK_W + lx];
                assert(lcd[y * 240 + x] == (uint16_t)((v >> 8) | (v << 8)));
            }
        }
        region_x = region_w = 0;
        check_frame(frame, height);
        }
        unsigned sent = s_blocks_sent, skipped = s_blocks_skipped;
        check_frame(frame, height);
        assert(s_blocks_sent == sent && s_blocks_skipped > skipped);
        frame[73 * TANK_W + 52] ^= 0x8123;
        check_frame(frame, height);
        assert(s_blocks_sent == sent + 1);
        /* Alternating changed/unchanged stripes exercise ping-pong ownership. */
        for (int x = 0; x < TANK_W; x += 16)
            for (int y = 0; y < TANK_H; y += 7) frame[y * TANK_W + x] ^= 0x1234;
        check_frame(frame, height);
        /* One partial pixel must invalidate only its physical canonical stripe. */
        region_x = 52; region_w = 1; tile_y = 73; tile_h = 1;
        frame[73 * TANK_W + 52] ^= 0x4512;
        display_port_flush(frame + tile_y * TANK_W);
        verify_frame(frame);
        unsigned partial_sent = s_blocks_sent;
        display_port_flush(frame + tile_y * TANK_W);
        assert(s_blocks_sent == partial_sent);
        region_x = region_w = 0;
        unsigned skipped_before = s_blocks_skipped;
        check_frame(frame, height);
        assert(s_blocks_sent == partial_sent + 1);
        assert(s_blocks_skipped > skipped_before);
        /* Canonical overlap must also invalidate the old partial fingerprint. */
        frame[73 * TANK_W + 52] ^= 0x7321;
        check_frame(frame, height);
        frame[73 * TANK_W + 52] ^= 0x7321;
        region_x = 52; region_w = 1; tile_y = 73; tile_h = 1;
        partial_sent = s_blocks_sent;
        display_port_flush(frame + tile_y * TANK_W);
        assert(s_blocks_sent > partial_sent);
        verify_frame(frame);
        region_x = region_w = 0;
        check_frame(frame, height);
        s_inverted = !s_inverted; memset(s_block_valid, 0, sizeof s_block_valid);
        check_frame(frame, height);
        tile_y = 0; tile_h = 0; check_frame(frame, TANK_H);
        if (TANK_H == 240) {
            /* Equal-size damage slices in one canonical band must not share CRCs. */
            for (int pass = 0; pass < 4; pass++) {
                tile_y = pass % 2 ? 17 : 31; tile_h = 23;
                for (int y = tile_y; y < tile_y + tile_h; y++)
                    for (int x = 0; x < TANK_W; x++) frame[y * TANK_W + x] = (uint16_t)(0x1234 + pass);
                display_port_flush(frame + tile_y * TANK_W);
                assert(!pending);
                for (int y = 0; y < 320; y++) for (int x = 0; x < 240; x++) {
                    int lx = s_inverted ? TANK_W - 1 - y : y;
                    int ly = s_inverted ? x : TANK_H - 1 - x;
                    uint16_t v = frame[ly * TANK_W + lx];
                    assert(lcd[y * 240 + x] == (uint16_t)((v >> 8) | (v << 8)));
                }
            }
            check_frame(frame, height);
        }
    }
    for (int mode = 0; mode < 8; mode++) {
        TANK_W = mode & 1 ? 320 : 240; TANK_H = mode & 1 ? 240 : 180;
        s_inverted = (mode & 2) != 0;
        int height = mode & 4 ? TANK_H : 80;
        region_x = region_w = 0;
        memset(s_block_valid, 0, sizeof s_block_valid);
        memset(s_partial, 0, sizeof s_partial);
        for (int i = 0; i < TANK_W * TANK_H; i++) frame[i] = (uint16_t)(i * 71u);
        unsigned full_before = s_blocks_sent;
        check_frame(frame, height);
        unsigned full_stripes = s_blocks_sent - full_before;
        frame[73 * TANK_W + 52] ^= 0x4521;
        region_x = 52; region_w = 1; tile_y = 73; tile_h = 1;
        display_port_flush(frame + tile_y * TANK_W);
        verify_frame(frame);
        unsigned sent_before = s_blocks_sent;
        region_x = region_w = 0;
        check_frame(frame, height);
        assert(s_blocks_sent == sent_before + 1);
        printf("PASS: %dx%d %s inversion %d: partial-to-canonical resend 1 of %u stripes\n",
            TANK_W, TANK_H, height == TANK_H ? "full" : "tiled", s_inverted, full_stripes);
        uint32_t seed = 0x5167;
        for (int pass = 0; pass < 80; pass++) {
            seed = seed * 1664525u + 1013904223u;
            region_x = (seed >> 8) % TANK_W;
            region_w = 1 + (seed % 64);
            if (region_x + region_w > TANK_W) region_w = TANK_W - region_x;
            tile_y = (seed >> 16) % TANK_H;
            tile_h = 1 + (seed % 80);
            if (tile_y + tile_h > TANK_H) tile_h = TANK_H - tile_y;
            for (int y = tile_y; y < tile_y + tile_h; y++)
                for (int x = region_x; x < region_x + region_w; x++) frame[y * TANK_W + x] ^= (uint16_t)seed;
            display_port_flush(frame + tile_y * TANK_W);
            verify_frame(frame);
            unsigned sent = s_blocks_sent;
            display_port_flush(frame + tile_y * TANK_W);
            assert(s_blocks_sent == sent);
            if (pass % 4 == 0) {
                region_x = region_w = 0;
                check_frame(frame, height);
            }
        }
        region_x = region_w = 0;
        check_frame(frame, height);
    }
    s_worker = 1;
    for (int mode = 0; mode < 4; mode++) {
        TANK_W = mode & 1 ? 320 : 240; TANK_H = mode & 1 ? 240 : 180;
        s_inverted = (mode & 2) != 0;
        uint16_t buffers[2][320 * 40];
        region_x = region_w = 0;
        memset(s_block_valid, 0, sizeof s_block_valid);
        memset(s_partial, 0, sizeof s_partial);
        for (int i = 0; i < TANK_W * TANK_H; i++) frame[i] = (uint16_t)(i * 43u + 17);
        unsigned slot = 0;
        for (int top = 0; top < TANK_H; top += 40) {
            int rows = TANK_H - top < 40 ? TANK_H - top : 40;
            memcpy(buffers[slot], frame + top * TANK_W, (size_t)rows * TANK_W * 2);
            tile_y = top; tile_h = rows;
            display_port_submit(buffers[slot]);
            assert(job_queued);
            /* Renderer globals change immediately; the worker must use its snapshot. */
            tile_y = tile_h = region_x = region_w = 0;
            slot ^= 1;
        }
        display_port_wait();
        verify_frame(frame);
        unsigned sent_before = s_blocks_sent;
        check_frame(frame, 40);
        assert(s_blocks_sent == sent_before);
    }
    puts("PASS: delayed tile worker snapshots, alternating 40-row source ownership, drain and synchronous compatibility");
    free(frame);
    puts("PASS: actual CYD flush, delayed DMA buffer ownership, exact rotated/scaled pixels, single-block changes, unchanged skips, inversion and Normal/Compact/full/tiled transitions");
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    source = (Path(__file__).resolve().parent.parent / "firmware" / "main" / "display_port_cyd.c").read_text()
    partial = source[source.index("typedef struct {"):source.index("/* Panel-specific")]
    flush = source[source.index("static void flush_region("):source.index("void display_port_sleep(")]
    with tempfile.TemporaryDirectory(prefix="cyd-display-test-") as folder:
        folder = Path(folder)
        test = folder / "test.c"
        test.write_text(HARNESS + partial + flush + TEST)
        executable = folder / "test.exe"
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        subprocess.run(command + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                                  str(test), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
