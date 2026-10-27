/* display_port_cyd.c - CYD SPI LCD with revision-specific initialization. */
#include "display_port.h"
#include "board_pins.h"
#include "board_hardware.h"
#include "tank.h"
#include "render.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_crc.h"
#include "esp_timer.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "cyd_display";
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_flush_done;
static uint16_t *s_stripe;
static uint8_t s_brightness = 0xFF;
static bool s_inverted;
static bool s_backlight_ready;
static uint32_t s_block_crc[6][CYD_PANEL_W / (CYD_STRIPE_ROWS / 2)];
static bool s_block_valid[6][CYD_PANEL_W / (CYD_STRIPE_ROWS / 2)];
static int s_layout_width, s_layout_height, s_layout_tile;
static int64_t s_wait_us, s_send_us, s_fill_us;
static unsigned s_blocks_sent, s_blocks_skipped;
static TaskHandle_t s_worker;
static SemaphoreHandle_t s_job_done;
static struct {
    const uint16_t *pixels;
    int top, bottom, left, right;
} s_job;
static void display_worker(void *arg);
typedef struct {
    uint32_t crc;
    uint16_t x0, y0, x1, y1;
    bool valid;
} partial_block_t;
static partial_block_t s_partial[48];
static unsigned s_partial_next;
static bool s_layout_inverted;
static void partial_invalidate(int x0, int y0, int x1, int y1) {
    for (unsigned i = 0; i < sizeof s_partial / sizeof s_partial[0]; i++) {
        partial_block_t *p = &s_partial[i];
        if (p->valid && x0 < p->x1 && x1 > p->x0 && y0 < p->y1 && y1 > p->y0)
            p->valid = false;
    }
}
static void canonical_invalidate(int x0, int y0, int x1, int y1) {
    int height = s_layout_tile;
    bool reverse_y = (CYD_LCD_MIRROR_Y != 0) != s_inverted;
    for (int top = 0; top < TANK_H; top += height) {
        int bottom = top + height < TANK_H ? top + height : TANK_H;
        int start = reverse_y ? TANK_H - bottom : top;
        int end = reverse_y ? TANK_H - top : bottom;
        int left = (start * CYD_PANEL_H + TANK_H - 1) / TANK_H;
        int right = (end * CYD_PANEL_H + TANK_H - 1) / TANK_H;
        if (x0 >= right || x1 <= left) continue;
        int rows = CYD_PANEL_H * CYD_STRIPE_ROWS / 2 / (right - left);
        for (int y = y0 / rows * rows; y < y1; y += rows)
            s_block_valid[top / 40][y / (CYD_STRIPE_ROWS / 2)] = false;
    }
}
/* Panel-specific power/gamma settings; the original CYD sequence stays unchanged. */
static const ili9341_lcd_init_cmd_t elegoo_init_commands[] = {
    /* Undo the driver's early sleep-out before programming panel power. */
    {0x01, NULL, 0, 150},
    {0xCF, (uint8_t[]){0x00, 0xC9, 0x30}, 3, 0},
    {0xED, (uint8_t[]){0x64, 0x03, 0x12, 0x81}, 4, 0},
    {0xE8, (uint8_t[]){0x85, 0x10, 0x7A}, 3, 0},
    {0xCB, (uint8_t[]){0x39, 0x2C, 0x00, 0x34, 0x02}, 5, 0},
    {0xF7, (uint8_t[]){0x20}, 1, 0},
    {0xEA, (uint8_t[]){0x00, 0x00}, 2, 0},
    {0xC0, (uint8_t[]){0x1B}, 1, 0},
    {0xC1, (uint8_t[]){0x00}, 1, 0},
    {0xC5, (uint8_t[]){0x44, 0x30}, 2, 0},
    {0xC7, (uint8_t[]){0xB6}, 1, 0},
    {0x36, (uint8_t[]){0x08}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0xB1, (uint8_t[]){0x00, 0x1A}, 2, 0},
    {0xB6, (uint8_t[]){0x0A, 0x82}, 2, 0},
    {0xF2, (uint8_t[]){0x00}, 1, 0},
    {0x26, (uint8_t[]){0x01}, 1, 0},
    {0xE0, (uint8_t[]){0x0F, 0x2A, 0x28, 0x08, 0x0E, 0x08, 0x54, 0xA9, 0x43, 0x0A, 0x0F, 0x00, 0x00, 0x00, 0x00}, 15, 0},
    {0xE1, (uint8_t[]){0x00, 0x15, 0x17, 0x07, 0x11, 0x06, 0x2B, 0x56, 0x3C, 0x05, 0x10, 0x0F, 0x3F, 0x3F, 0x0F}, 15, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0x3F}, 4, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x00, 0xEF}, 4, 0},
    {0x11, NULL, 0, 120},
};
static const ili9341_lcd_init_cmd_t cyd_init_commands[] = {
    {0xB2, (uint8_t[]){0x0C, 0x0C, 0x00, 0x33, 0x33}, 5, 0},
    {0xB7, (uint8_t[]){0x35}, 1, 0},
    {0xBB, (uint8_t[]){0x19}, 1, 0},
    {0xC0, (uint8_t[]){0x2C}, 1, 0},
    {0xC2, (uint8_t[]){0x01}, 1, 0},
    {0xC3, (uint8_t[]){0x12}, 1, 0},
    {0xC4, (uint8_t[]){0x20}, 1, 0},
    {0xC6, (uint8_t[]){0x0F}, 1, 0},
    {0xD0, (uint8_t[]){0xA4, 0xA1}, 2, 0},
    {0xE0, (uint8_t[]){0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23}, 14, 0},
    {0xE1, (uint8_t[]){0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23}, 14, 0},
};
#define CYD_STRIPE_BYTES (CYD_PANEL_H * CYD_STRIPE_ROWS * sizeof(uint16_t))
#define CYD_DIAGNOSTIC_PATTERN 0
#if CYD_DIAGNOSTIC_PATTERN
static const uint16_t cyd_test_colors[] = {
    0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000, 0xFFE0, 0x07FF, 0xF81F,
};
#endif

static void backlight_apply(uint8_t level) {
    if (!s_backlight_ready) return;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static bool on_color_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *context) {
    (void)io;
    (void)event;
    (void)context;
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
}

bool display_port_init(void) {
    ESP_ERROR_CHECK(gpio_set_direction(CYD_PIN_LCD_BL, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(CYD_PIN_LCD_BL, 0));
    spi_bus_config_t bus = ILI9341_PANEL_BUS_SPI_CONFIG(CYD_PIN_LCD_SCLK, CYD_PIN_LCD_MOSI, CYD_STRIPE_BYTES);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    s_stripe = heap_caps_malloc(CYD_STRIPE_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_stripe) {
        ESP_LOGE(TAG, "no DMA memory for the LCD stripe");
        return false;
    }

    esp_lcd_panel_io_handle_t io;
    s_flush_done = xSemaphoreCreateBinary();
    if (!s_flush_done) return false;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = CYD_PIN_LCD_DC,
        .cs_gpio_num = CYD_PIN_LCD_CS,
        .pclk_hz = 40000000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io));

    ili9341_vendor_config_t vendor_cfg = {
        .init_cmds = cyd_init_commands,
        .init_cmds_size = sizeof(cyd_init_commands) / sizeof(cyd_init_commands[0]),
    };
    const board_hardware_t *hardware = board_hardware_current();
    if (hardware->id == BOARD_HARDWARE_ELEGOO) {
        vendor_cfg.init_cmds = elegoo_init_commands;
        vendor_cfg.init_cmds_size = sizeof(elegoo_init_commands) / sizeof(elegoo_init_commands[0]);
    }
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = CYD_PIN_LCD_RST,
        .rgb_ele_order = hardware->id == BOARD_HARDWARE_ELEGOO ?
                         LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    if (hardware->id == BOARD_HARDWARE_CYD) {
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0x38, NULL, 0));
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0x13, NULL, 0));
    }
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, false));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, false));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, hardware->panel_mirror_columns, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t channel = {
        .gpio_num = CYD_PIN_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
    s_backlight_ready = true;
    display_port_set_brightness(s_brightness);
    ESP_LOGI(TAG, "CYD panel ready: %dx%d SPI LCD, tank frame %dx%d", CYD_PANEL_W, CYD_PANEL_H, TANK_W, TANK_H);
    return true;
}

bool display_port_pipeline_init(void) {
    if (s_worker) return true;
    s_job_done = xSemaphoreCreateBinary();
    if (!s_job_done) {
        ESP_LOGE(TAG, "no completion semaphore for LCD pipeline");
        return false;
    }
    xSemaphoreGive(s_job_done);
    if (xTaskCreatePinnedToCore(display_worker, "lcd", 3072, NULL, 4, &s_worker, 1) != pdPASS) {
        ESP_LOGE(TAG, "no internal RAM for LCD pipeline task");
        vSemaphoreDelete(s_job_done);
        s_job_done = NULL;
        return false;
    }
    return true;
}

static void flush_region(const uint16_t *fb, int tile_start, int tile_end, int region_left, int region_right) {
    if (!s_panel) return;
    int64_t started = esp_timer_get_time(), waited = s_wait_us, sent = s_send_us;
    int canonical_height = tile_end - tile_start <= 40 ? 40 : 80;
    if (tile_end == TANK_H && tile_start % 80 == 0 && s_layout_tile == 80)
        canonical_height = 80;
    bool canonical = region_left == 0 && region_right == TANK_W &&
        (tile_end - tile_start == TANK_H ||
        (tile_start % canonical_height == 0 &&
         tile_end == (tile_start + canonical_height < TANK_H ? tile_start + canonical_height : TANK_H)));
    int tile_layout = canonical ? (tile_end - tile_start == TANK_H ? TANK_H : canonical_height) :
        (s_layout_tile ? s_layout_tile : 80);
    if (s_layout_width != TANK_W || s_layout_height != TANK_H ||
        s_layout_tile != tile_layout || s_layout_inverted != s_inverted) {
        memset(s_block_valid, 0, sizeof s_block_valid);
        memset(s_partial, 0, sizeof s_partial);
        s_layout_width = TANK_W; s_layout_height = TANK_H; s_layout_tile = tile_layout;
        s_layout_inverted = s_inverted;
    }
    bool reverse_y = (CYD_LCD_MIRROR_Y != 0) != s_inverted;
    int source_start = reverse_y ? TANK_H - tile_end : tile_start;
    int source_end = reverse_y ? TANK_H - tile_start : tile_end;
    int panel_start = (source_start * CYD_PANEL_H + TANK_H - 1) / TANK_H;
    int panel_end = (source_end * CYD_PANEL_H + TANK_H - 1) / TANK_H;
    /* Partial writes invalidate only overlapping entries in the retained canonical layout. */
#if CYD_DIAGNOSTIC_PATTERN
    (void)fb;
#else
    /* Rotation/scaling is constant along each native column, not per pixel. */
    int offsets[CYD_PANEL_H];
    for (int x = panel_start; x < panel_end; x++) {
        int y = x * TANK_H / CYD_PANEL_H;
#if CYD_LCD_MIRROR_Y
        y = TANK_H - 1 - y;
#endif
        if (s_inverted) y = TANK_H - 1 - y;
        offsets[x] = (y - tile_start) * TANK_W;
    }
#endif
    int region_width = panel_end - panel_start;
    /* Each ping-pong half fits 24 rows for an 80-column band, not just 16. */
    int rows = CYD_STRIPE_BYTES / 2 / sizeof(uint16_t) / region_width;
    bool reverse_x = (CYD_LCD_MIRROR_X != 0) != s_inverted;
    int source_left = reverse_x ? TANK_W - region_right : region_left;
    int source_right = reverse_x ? TANK_W - region_left : region_right;
    int native_start = (source_left * CYD_PANEL_W + TANK_W - 1) / TANK_W;
    int native_end = (source_right * CYD_PANEL_W + TANK_W - 1) / TANK_W;
    bool pending = false;
    unsigned stripe = 0;
    for (int native_y = native_start; native_y < native_end; native_y += rows) {
        int count = native_end - native_y < rows ? native_end - native_y : rows;
        uint16_t *buffer = s_stripe + stripe * (CYD_STRIPE_BYTES / 2 / sizeof(uint16_t));
#if !CYD_DIAGNOSTIC_PATTERN
        int previous_x = -1;
#endif
        for (int row = 0; row < count; row++) {
            uint16_t *target = buffer + row * region_width;
#if !CYD_DIAGNOSTIC_PATTERN
            int logical_x = (native_y + row) * TANK_W / CYD_PANEL_W;
#if CYD_LCD_MIRROR_X
            logical_x = TANK_W - 1 - logical_x;
#endif
            if (s_inverted) logical_x = TANK_W - 1 - logical_x;
            if (row && logical_x == previous_x) {
                memcpy(target, target - region_width, (size_t)region_width * sizeof *target);
                continue;
            }
            previous_x = logical_x;
#endif
            for (int native_x = panel_start; native_x < panel_end; native_x++) {
#if CYD_DIAGNOSTIC_PATTERN
                uint16_t pixel = cyd_test_colors[native_x * 8 / CYD_PANEL_H];
                int level = (native_y + row) * 31 / (CYD_PANEL_W - 1);
                pixel = (uint16_t)((((pixel >> 11) * level / 31) << 11) |
                                   (((((pixel >> 5) & 63) * level / 31) << 5)) |
                                   ((pixel & 31) * level / 31));
#else
                uint16_t pixel = fb[offsets[native_x] + logical_x];
#endif
                target[native_x - panel_start] = (uint16_t)((pixel >> 8) | (pixel << 8));
            }
        }
        int tile_index = tile_start / 40, block = native_y / (CYD_STRIPE_ROWS / 2);
        uint32_t crc = esp_crc32_le(0, (const uint8_t *)buffer, (size_t)count * region_width * sizeof *buffer);
        if (canonical && s_block_valid[tile_index][block] && s_block_crc[tile_index][block] == crc) {
            s_blocks_skipped++;
            continue;
        }
        bool unchanged = false;
        if (!canonical) for (unsigned i = 0; i < sizeof s_partial / sizeof s_partial[0]; i++) {
            const partial_block_t *p = &s_partial[i];
            if (p->valid && p->x0 == panel_start && p->x1 == panel_end &&
                p->y0 == native_y && p->y1 == native_y + count && p->crc == crc) {
                unchanged = true; break;
            }
        }
        if (unchanged) { s_blocks_skipped++; continue; }
        int64_t now = esp_timer_get_time();
        if (pending) xSemaphoreTake(s_flush_done, portMAX_DELAY);
        s_wait_us += esp_timer_get_time() - now;
        now = esp_timer_get_time();
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, panel_start, native_y, panel_end, native_y + count, buffer));
        partial_invalidate(panel_start, native_y, panel_end, native_y + count);
        if (!canonical) canonical_invalidate(panel_start, native_y, panel_end, native_y + count);
        if (!canonical) {
            partial_block_t *p = &s_partial[s_partial_next++ % (sizeof s_partial / sizeof s_partial[0])];
            *p = (partial_block_t){crc, panel_start, native_y, panel_end, native_y + count, true};
        }
        s_send_us += esp_timer_get_time() - now;
        if (canonical) {
            s_block_crc[tile_index][block] = crc; s_block_valid[tile_index][block] = true;
        }
        s_blocks_sent++;
        pending = true;
        stripe ^= 1;
    }
    int64_t now = esp_timer_get_time();
    if (pending) xSemaphoreTake(s_flush_done, portMAX_DELAY);
    s_wait_us += esp_timer_get_time() - now;
    s_fill_us += esp_timer_get_time() - started - (s_wait_us - waited) - (s_send_us - sent);
}

static void display_worker(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        flush_region(s_job.pixels, s_job.top, s_job.bottom, s_job.left, s_job.right);
        xSemaphoreGive(s_job_done);
    }
}

void display_port_wait(void) {
    if (!s_worker) return;
    xSemaphoreTake(s_job_done, portMAX_DELAY);
    xSemaphoreGive(s_job_done);
}

void display_port_submit(const uint16_t *fb) {
    if (!s_worker) {
        display_port_flush(fb);
        return;
    }
    xSemaphoreTake(s_job_done, portMAX_DELAY);
    s_job.pixels = fb;
    s_job.top = render_tile_top(); s_job.bottom = render_tile_bottom();
    s_job.left = render_region_left(); s_job.right = render_region_right();
    xTaskNotifyGive(s_worker);
}

void display_port_flush(const uint16_t *fb) {
    display_port_wait();
    flush_region(fb, render_tile_top(), render_tile_bottom(), render_region_left(), render_region_right());
}

void display_port_flush_prof(int64_t *wait_us, int64_t *send_us, int64_t *fill_us) {
    display_port_wait();
    *wait_us = s_wait_us; *send_us = s_send_us;
    *fill_us = s_fill_us;
    s_wait_us = s_send_us = s_fill_us = 0;
    ESP_LOGD(TAG, "LCD blocks: %u updated, %u unchanged", s_blocks_sent, s_blocks_skipped);
    if (s_worker) ESP_LOGD(TAG, "LCD worker minimum free stack: %u bytes",
                          (unsigned)uxTaskGetStackHighWaterMark(s_worker));
    s_blocks_sent = s_blocks_skipped = 0;
}

void display_port_sleep(void) {
    display_port_wait();
    if (s_panel) esp_lcd_panel_disp_on_off(s_panel, false);
    backlight_apply(0);
}

void display_port_wake(void) {
    display_port_wait();
    memset(s_block_valid, 0, sizeof s_block_valid);
    if (s_panel) {
        esp_lcd_panel_disp_on_off(s_panel, true);
    }
    backlight_apply(s_brightness);
}

void display_port_set_inverted(bool inverted) {
    display_port_wait();
    if (s_inverted != inverted) memset(s_block_valid, 0, sizeof s_block_valid);
    s_inverted = inverted;
}

void display_port_set_brightness(uint8_t level) {
    s_brightness = level;
    backlight_apply(level);
}

uint8_t display_port_brightness(void) { return s_brightness; }
