#ifndef BOARD_PINS_H
#define BOARD_PINS_H
#define CYD_PANEL_W        320
#define CYD_PANEL_H        240
#define CYD_STRIPE_ROWS    16
#define CYD_PIN_LCD_SCLK   14
#define CYD_PIN_LCD_MOSI   13
#define CYD_PIN_LCD_CS     15
#define CYD_PIN_LCD_DC     2
#define CYD_PIN_LCD_RST    (-1)
#define CYD_PIN_LCD_BL     21
#define CYD_PIN_TP_SCLK    25
#define CYD_PIN_TP_MOSI    32
#define CYD_PIN_TP_MISO    39
#define CYD_PIN_TP_CS      33
#define CYD_PIN_TP_IRQ     36
#define CYD_TOUCH_X_MIN    240
#define CYD_TOUCH_X_MAX    3850
#define CYD_TOUCH_Y_MIN    240
#define CYD_TOUCH_Y_MAX    3850
#define CYD_TOUCH_SWAP_XY  0
#define CYD_TOUCH_MIRROR_X 1
#define CYD_TOUCH_MIRROR_Y 0
#define CYD_LCD_MIRROR_X   0
#define CYD_LCD_MIRROR_Y   1
/* the back RGB LED (active low) and the room-light sensor (LDR divider on an ADC1 pin) */
#define CYD_PIN_LED_R      4
#define CYD_PIN_LED_G      16
#define CYD_PIN_LED_B      17
#define CYD_PIN_LDR        34
#define CYD_LDR_ADC_CHANNEL ADC_CHANNEL_6
#define CYD_LDR_MIN_BASE     200     /* avoid treating normal room light as dark after a very bright baseline */
#endif
