/* display_port_cyd.c - CYD SPI LCD with revision-specific initialization. */
#include "display_port.h"
#include "board_pins.h"
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

static const char *TAG = "cyd_display";
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_flush_done;
static uint16_t *s_stripe;
static uint8_t s_brightness = 0xFF;
static bool s_inverted;
static bool s_backlight_ready;
static uint32_t s_block_crc[3][CYD_PANEL_W / (CYD_STRIPE_ROWS / 2)];
static bool s_block_valid[3][CYD_PANEL_W / (CYD_STRIPE_ROWS / 2)];
static int s_layout_width, s_layout_height, s_layout_tile;
static int64_t s_wait_us, s_send_us;
static unsigned s_blocks_sent, s_blocks_skipped;
/* This CYD revision needs ST7789-style timing/gamma, not the ILI9341 defaults. */
#if !PT_BOARD_ELEGOO
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
#endif
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

#if !PT_BOARD_ELEGOO
    ili9341_vendor_config_t vendor_cfg = {
        .init_cmds = cyd_init_commands,
        .init_cmds_size = sizeof(cyd_init_commands) / sizeof(cyd_init_commands[0]),
    };
#endif
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = CYD_PIN_LCD_RST,
#if PT_BOARD_ELEGOO
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = 16,
#if !PT_BOARD_ELEGOO
        .vendor_config = &vendor_cfg,
#endif
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0x38, NULL, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0x13, NULL, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, false));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, false));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, false, false));
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

void display_port_flush(const uint16_t *fb) {
    if (!s_panel) return;
    int tile_start = render_tile_top(), tile_end = render_tile_bottom();
    int tile_layout = tile_end - tile_start == TANK_H ? TANK_H : 80;
    if (s_layout_width != TANK_W || s_layout_height != TANK_H || s_layout_tile != tile_layout) {
        memset(s_block_valid, 0, sizeof s_block_valid);
        s_layout_width = TANK_W; s_layout_height = TANK_H; s_layout_tile = tile_layout;
    }
    bool reverse_y = (CYD_LCD_MIRROR_Y != 0) != s_inverted;
    int source_start = reverse_y ? TANK_H - tile_end : tile_start;
    int source_end = reverse_y ? TANK_H - tile_start : tile_end;
    int panel_start = (source_start * CYD_PANEL_H + TANK_H - 1) / TANK_H;
    int panel_end = (source_end * CYD_PANEL_H + TANK_H - 1) / TANK_H;
    /* Arbitrary CPU-damage slices do not share the canonical tile CRC geometry. */
    int region_left = render_region_left(), region_right = render_region_right();
    bool canonical = region_left == 0 && region_right == TANK_W &&
        tile_start % 80 == 0 && (tile_end - tile_start == TANK_H ||
        tile_end == (tile_start + 80 < TANK_H ? tile_start + 80 : TANK_H));
    if (!canonical) memset(s_block_valid, 0, sizeof s_block_valid);
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
        for (int row = 0; row < count; row++) {
#if !CYD_DIAGNOSTIC_PATTERN
            int logical_x = (native_y + row) * TANK_W / CYD_PANEL_W;
#if CYD_LCD_MIRROR_X
            logical_x = TANK_W - 1 - logical_x;
#endif
            if (s_inverted) logical_x = TANK_W - 1 - logical_x;
#endif
            uint16_t *target = buffer + row * region_width;
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
        int tile_index = tile_start / 80, block = native_y / (CYD_STRIPE_ROWS / 2);
        uint32_t crc = esp_crc32_le(0, (const uint8_t *)buffer, (size_t)count * region_width * sizeof *buffer);
        if (canonical && s_block_valid[tile_index][block] && s_block_crc[tile_index][block] == crc) {
            s_blocks_skipped++;
            continue;
        }
        int64_t now = esp_timer_get_time();
        if (pending) xSemaphoreTake(s_flush_done, portMAX_DELAY);
        s_wait_us += esp_timer_get_time() - now;
        now = esp_timer_get_time();
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, panel_start, native_y, panel_end, native_y + count, buffer));
        s_send_us += esp_timer_get_time() - now;
        s_block_crc[tile_index][block] = crc; s_block_valid[tile_index][block] = true;
        s_blocks_sent++;
        pending = true;
        stripe ^= 1;
    }
    int64_t now = esp_timer_get_time();
    if (pending) xSemaphoreTake(s_flush_done, portMAX_DELAY);
    s_wait_us += esp_timer_get_time() - now;
    if (!canonical) memset(s_block_valid, 0, sizeof s_block_valid);
}

void display_port_flush_prof(int64_t *wait_us, int64_t *send_us) {
    *wait_us = s_wait_us; *send_us = s_send_us;
    s_wait_us = s_send_us = 0;
    ESP_LOGI(TAG, "LCD blocks: %u updated, %u unchanged", s_blocks_sent, s_blocks_skipped);
    s_blocks_sent = s_blocks_skipped = 0;
}

void display_port_sleep(void) {
    if (s_panel) esp_lcd_panel_disp_on_off(s_panel, false);
    backlight_apply(0);
}

void display_port_wake(void) {
    memset(s_block_valid, 0, sizeof s_block_valid);
    if (s_panel) {
        esp_lcd_panel_disp_on_off(s_panel, true);
    }
    backlight_apply(s_brightness);
}

void display_port_set_inverted(bool inverted) {
    if (s_inverted != inverted) memset(s_block_valid, 0, sizeof s_block_valid);
    s_inverted = inverted;
}

void display_port_set_brightness(uint8_t level) {
    s_brightness = level;
    backlight_apply(level);
}

uint8_t display_port_brightness(void) { return s_brightness; }
