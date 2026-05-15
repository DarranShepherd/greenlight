#include "board_profile.h"

static const uint8_t s_ipistbit_32_st7789_madctl[] = {0x00};
static const uint8_t s_ipistbit_32_st7789_colmod[] = {0x05};
static const uint8_t s_ipistbit_32_st7789_porch_control[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
static const uint8_t s_ipistbit_32_st7789_gate_control[] = {0x74};
static const uint8_t s_ipistbit_32_st7789_vcom[] = {0x13};
static const uint8_t s_ipistbit_32_st7789_lcm_control[] = {0x2C};
static const uint8_t s_ipistbit_32_st7789_vdv_vrh_enable[] = {0x01};
static const uint8_t s_ipistbit_32_st7789_vrh[] = {0x10};
static const uint8_t s_ipistbit_32_st7789_vdv[] = {0x20};
static const uint8_t s_ipistbit_32_st7789_frame_rate[] = {0x0F};
static const uint8_t s_ipistbit_32_st7789_power_control_1[] = {0xA4, 0xA1};
static const uint8_t s_ipistbit_32_st7789_power_control_2[] = {0xA1};
static const uint8_t s_ipistbit_32_st7789_gamma_positive[] = {0xD0, 0x07, 0x0E, 0x0B, 0x0A, 0x14, 0x38, 0x33, 0x4F, 0x37, 0x16, 0x16, 0x2A, 0x2E};
static const uint8_t s_ipistbit_32_st7789_gamma_negative[] = {0xD0, 0x0B, 0x10, 0x08, 0x08, 0x06, 0x35, 0x54, 0x4D, 0x0A, 0x14, 0x14, 0x2C, 0x2F};
static const uint8_t s_ipistbit_32_st7789_gate_timing[] = {0x11, 0x11, 0x03};

static const greenlight_lcd_init_cmd_t s_ipistbit_32_st7789_init_cmds[] = {
    {.command = 0x36, .data = s_ipistbit_32_st7789_madctl, .data_size = sizeof(s_ipistbit_32_st7789_madctl)},
    {.command = 0x3A, .data = s_ipistbit_32_st7789_colmod, .data_size = sizeof(s_ipistbit_32_st7789_colmod)},
    {.command = 0xB2, .data = s_ipistbit_32_st7789_porch_control, .data_size = sizeof(s_ipistbit_32_st7789_porch_control)},
    {.command = 0xB7, .data = s_ipistbit_32_st7789_gate_control, .data_size = sizeof(s_ipistbit_32_st7789_gate_control)},
    {.command = 0xBB, .data = s_ipistbit_32_st7789_vcom, .data_size = sizeof(s_ipistbit_32_st7789_vcom)},
    {.command = 0xC0, .data = s_ipistbit_32_st7789_lcm_control, .data_size = sizeof(s_ipistbit_32_st7789_lcm_control)},
    {.command = 0xC2, .data = s_ipistbit_32_st7789_vdv_vrh_enable, .data_size = sizeof(s_ipistbit_32_st7789_vdv_vrh_enable)},
    {.command = 0xC3, .data = s_ipistbit_32_st7789_vrh, .data_size = sizeof(s_ipistbit_32_st7789_vrh)},
    {.command = 0xC4, .data = s_ipistbit_32_st7789_vdv, .data_size = sizeof(s_ipistbit_32_st7789_vdv)},
    {.command = 0xC6, .data = s_ipistbit_32_st7789_frame_rate, .data_size = sizeof(s_ipistbit_32_st7789_frame_rate)},
    {.command = 0xD0, .data = s_ipistbit_32_st7789_power_control_1, .data_size = sizeof(s_ipistbit_32_st7789_power_control_1)},
    {.command = 0xD6, .data = s_ipistbit_32_st7789_power_control_2, .data_size = sizeof(s_ipistbit_32_st7789_power_control_2)},
    {.command = 0xE0, .data = s_ipistbit_32_st7789_gamma_positive, .data_size = sizeof(s_ipistbit_32_st7789_gamma_positive)},
    {.command = 0xE1, .data = s_ipistbit_32_st7789_gamma_negative, .data_size = sizeof(s_ipistbit_32_st7789_gamma_negative)},
    {.command = 0xE9, .data = s_ipistbit_32_st7789_gate_timing, .data_size = sizeof(s_ipistbit_32_st7789_gate_timing)},
    {.command = 0x21, .data = NULL, .data_size = 0},
};

static const greenlight_board_profile_t s_cyd_28_2432s028r __attribute__((unused)) = {
    .id = "cyd_28_2432s028r",
    .display_name = "CYD 2.8 2432S028R",
    .display = {
        .h_res = 240,
        .v_res = 320,
        .bits_per_pixel = 16,
        .draw_buffer_lines = 30,
        .double_buffer = true,
        .pixel_clock_hz = 20 * 1000 * 1000,
        .cmd_bits = 8,
        .param_bits = 8,
        .spi_host = SPI2_HOST,
        .spi_clk = GPIO_NUM_14,
        .spi_mosi = GPIO_NUM_13,
        .spi_miso = GPIO_NUM_12,
        .cs = GPIO_NUM_15,
        .dc = GPIO_NUM_2,
        .reset = GPIO_NUM_4,
        .backlight = GPIO_NUM_21,
        .backlight_ledc_channel = LEDC_CHANNEL_1,
        .backlight_ledc_timer = LEDC_TIMER_1,
        .backlight_output_invert = false,
        .mirror_x = true,
        .mirror_y = false,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .controller = GREENLIGHT_LCD_CONTROLLER_ILI9341,
        .init_cmds = NULL,
        .init_cmd_count = 0,
    },
    .touch = {
        .spi_host = SPI3_HOST,
        .spi_clk = GPIO_NUM_25,
        .spi_mosi = GPIO_NUM_32,
        .spi_miso = GPIO_NUM_39,
        .cs = GPIO_NUM_33,
        .irq = GPIO_NUM_NC,
        .reset = GPIO_NUM_NC,
        .clock_hz = 2 * 1000 * 1000,
        .swap_xy = false,
        .mirror_x = true,
        .mirror_y = false,
        .left_edge_x_correction_px = 16,
        .y_offset_px = 0,
    },
};

static const greenlight_board_profile_t s_ipistbit_32_st7789 __attribute__((unused)) = {
    .id = "ipistbit_32_st7789",
    .display_name = "iPistBit 3.2 ST7789",
    .display = {
        .h_res = 240,
        .v_res = 320,
        .bits_per_pixel = 16,
        .draw_buffer_lines = 30,
        .double_buffer = true,
        .pixel_clock_hz = 20 * 1000 * 1000,
        .cmd_bits = 8,
        .param_bits = 8,
        .spi_host = SPI2_HOST,
        .spi_clk = GPIO_NUM_14,
        .spi_mosi = GPIO_NUM_13,
        .spi_miso = GPIO_NUM_12,
        .cs = GPIO_NUM_15,
        .dc = GPIO_NUM_2,
        .reset = GPIO_NUM_NC,
        .backlight = GPIO_NUM_27,
        .backlight_ledc_channel = LEDC_CHANNEL_1,
        .backlight_ledc_timer = LEDC_TIMER_1,
        .backlight_output_invert = false,
        .mirror_x = false,
        .mirror_y = false,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .controller = GREENLIGHT_LCD_CONTROLLER_ST7789,
        .init_cmds = s_ipistbit_32_st7789_init_cmds,
        .init_cmd_count = sizeof(s_ipistbit_32_st7789_init_cmds) / sizeof(s_ipistbit_32_st7789_init_cmds[0]),
    },
    .touch = {
        .spi_host = SPI2_HOST,
        .spi_clk = GPIO_NUM_14,
        .spi_mosi = GPIO_NUM_13,
        .spi_miso = GPIO_NUM_12,
        .cs = GPIO_NUM_33,
        .irq = GPIO_NUM_NC,
        .reset = GPIO_NUM_NC,
        .clock_hz = 2 * 1000 * 1000,
        .swap_xy = false,
        .mirror_x = true,
        .mirror_y = true,
        .left_edge_x_correction_px = 16,
        .y_offset_px = 0,
    },
};

const greenlight_board_profile_t *greenlight_board_profile_get(void)
{
#ifdef CONFIG_GREENLIGHT_BOARD_PROFILE_IPISTBIT_32_ST7789
    return &s_ipistbit_32_st7789;
#else
    return &s_cyd_28_2432s028r;
#endif
}

const char *greenlight_board_id_get(void)
{
    return greenlight_board_profile_get()->id;
}
