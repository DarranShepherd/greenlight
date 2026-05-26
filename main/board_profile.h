#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <driver/gpio.h>
#include <driver/i2c_types.h>
#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_lcd_types.h>

typedef enum {
    GREENLIGHT_DISPLAY_BUS_SPI = 0,
    GREENLIGHT_DISPLAY_BUS_QSPI,
} greenlight_display_bus_t;

typedef enum {
    GREENLIGHT_DISPLAY_ROTATION_0 = 0,
    GREENLIGHT_DISPLAY_ROTATION_90,
    GREENLIGHT_DISPLAY_ROTATION_180,
    GREENLIGHT_DISPLAY_ROTATION_270,
} greenlight_display_rotation_t;

typedef enum {
    GREENLIGHT_LCD_CONTROLLER_ILI9341 = 0,
    GREENLIGHT_LCD_CONTROLLER_ST7789,
    GREENLIGHT_LCD_CONTROLLER_NV3041A,
} greenlight_lcd_controller_t;

typedef enum {
    GREENLIGHT_TOUCH_BUS_SPI = 0,
    GREENLIGHT_TOUCH_BUS_I2C,
} greenlight_touch_bus_t;

typedef enum {
    GREENLIGHT_TOUCH_CONTROLLER_XPT2046 = 0,
    GREENLIGHT_TOUCH_CONTROLLER_GT911,
} greenlight_touch_controller_t;

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
    greenlight_display_rotation_t rotation;
    greenlight_display_bus_t bus_type;
    uint32_t pixel_clock_hz;
    uint8_t cmd_bits;
    uint8_t param_bits;
    spi_host_device_t spi_host;
    gpio_num_t spi_clk;
    gpio_num_t spi_mosi;
    gpio_num_t spi_miso;
    gpio_num_t spi_data0;
    gpio_num_t spi_data1;
    gpio_num_t spi_data2;
    gpio_num_t spi_data3;
    gpio_num_t cs;
    gpio_num_t dc;
    gpio_num_t reset;
    gpio_num_t backlight;
    ledc_channel_t backlight_ledc_channel;
    ledc_timer_t backlight_ledc_timer;
    bool backlight_output_invert;
    bool mirror_x;
    bool mirror_y;
    bool swap_bytes;
    lcd_rgb_element_order_t rgb_ele_order;
    greenlight_lcd_controller_t controller;
    const greenlight_lcd_init_cmd_t *init_cmds;
    size_t init_cmd_count;
} greenlight_display_profile_t;

typedef struct {
    greenlight_touch_bus_t bus_type;
    greenlight_touch_controller_t controller;
    spi_host_device_t spi_host;
    gpio_num_t spi_clk;
    gpio_num_t spi_mosi;
    gpio_num_t spi_miso;
    i2c_port_num_t i2c_port;
    gpio_num_t i2c_scl;
    gpio_num_t i2c_sda;
    uint16_t i2c_address;
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
    uint16_t primary_hero_card_height;
    uint16_t detail_chart_shell_height;
    uint16_t detail_chart_bar_row_height;
} greenlight_ui_profile_t;

typedef struct {
    const char *id;
    const char *display_name;
    greenlight_display_profile_t display;
    greenlight_touch_profile_t touch;
    greenlight_ui_profile_t ui;
} greenlight_board_profile_t;

const greenlight_board_profile_t *greenlight_board_profile_get(void);
const char *greenlight_board_id_get(void);
