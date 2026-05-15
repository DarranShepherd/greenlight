#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_lcd_types.h>

typedef enum {
    GREENLIGHT_LCD_CONTROLLER_ILI9341 = 0,
    GREENLIGHT_LCD_CONTROLLER_ST7789,
} greenlight_lcd_controller_t;

typedef struct {
    uint8_t command;
    const uint8_t *data;
    size_t data_size;
} greenlight_lcd_init_cmd_t;

typedef struct {
    uint16_t h_res;
    uint16_t v_res;
    uint8_t bits_per_pixel;
    uint16_t draw_buffer_lines;
    bool double_buffer;
    uint32_t pixel_clock_hz;
    uint8_t cmd_bits;
    uint8_t param_bits;
    spi_host_device_t spi_host;
    gpio_num_t spi_clk;
    gpio_num_t spi_mosi;
    gpio_num_t spi_miso;
    gpio_num_t cs;
    gpio_num_t dc;
    gpio_num_t reset;
    gpio_num_t backlight;
    ledc_channel_t backlight_ledc_channel;
    ledc_timer_t backlight_ledc_timer;
    bool backlight_output_invert;
    bool mirror_x;
    bool mirror_y;
    lcd_rgb_element_order_t rgb_ele_order;
    greenlight_lcd_controller_t controller;
    const greenlight_lcd_init_cmd_t *init_cmds;
    size_t init_cmd_count;
} greenlight_display_profile_t;

typedef struct {
    spi_host_device_t spi_host;
    gpio_num_t spi_clk;
    gpio_num_t spi_mosi;
    gpio_num_t spi_miso;
    gpio_num_t cs;
    gpio_num_t irq;
    gpio_num_t reset;
    uint32_t clock_hz;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    int16_t left_edge_x_correction_px;
    int16_t y_offset_px;
} greenlight_touch_profile_t;

typedef struct {
    const char *id;
    const char *display_name;
    greenlight_display_profile_t display;
    greenlight_touch_profile_t touch;
} greenlight_board_profile_t;

const greenlight_board_profile_t *greenlight_board_profile_get(void);
const char *greenlight_board_id_get(void);
