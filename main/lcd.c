#include "lcd.h"

#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_check.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_st7789.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>

#include "hardware.h"

static const char *TAG = "lcd";

static esp_err_t lcd_send_command(esp_lcd_panel_io_handle_t panel_io, int command, const void *data, size_t data_size)
{
    return esp_lcd_panel_io_tx_param(panel_io, command, data, data_size);
}

static esp_err_t lcd_apply_vendor_st7789_init(esp_lcd_panel_io_handle_t panel_io)
{
    static const uint8_t porch_control[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    static const uint8_t power_control_1[] = {0xA4, 0xA1};
    static const uint8_t gamma_positive[] = {0xD0, 0x07, 0x0E, 0x0B, 0x0A, 0x14, 0x38, 0x33, 0x4F, 0x37, 0x16, 0x16, 0x2A, 0x2E};
    static const uint8_t gamma_negative[] = {0xD0, 0x0B, 0x10, 0x08, 0x08, 0x06, 0x35, 0x54, 0x4D, 0x0A, 0x14, 0x14, 0x2C, 0x2F};
    static const uint8_t gate_control[] = {0x11, 0x11, 0x03};

    ESP_RETURN_ON_ERROR(lcd_send_command(panel_io, 0x36, (uint8_t[]) {0x00}, 1), TAG, "set MADCTL");
    ESP_RETURN_ON_ERROR(lcd_send_command(panel_io, 0x3A, (uint8_t[]) {0x05}, 1), TAG, "set COLMOD");
    ESP_RETURN_ON_ERROR(lcd_send_command(panel_io, 0xB2, porch_control, sizeof(porch_control)), TAG, "set porch control");
    ESP_RETURN_ON_ERROR(lcd_send_command(panel_io, 0xB7, (uint8_t[]) {0x74}, 1), TAG, "set gate control");
    ESP_RETURN_ON_ERROR(lcd_send_command(panel_io, 0xBB, (uint8_t[]) {0x13}, 1), TAG, "set VCOM");
    ESP_RETURN_ON_ERROR(lcd_send_command(panel_io, 0xC0, (uint8_t[]) {0x2C}, 1), TAG, "set LCM control");
    ESP_RETURN_ON_ERROR(lcd_send_command(panel_io, 0xC2, (uint8_t[]) {0x01}, 1), TAG, "set VDV/VRH enable");
    ESP_RETURN_ON_ERROR(lcd_send_command(panel_io, 0xC3, (uint8_t[]) {0x10}, 1), TAG, "set VRH");
    ESP_RETURN_ON_ERROR(lcd_send_command(panel_io, 0xC4, (uint8_t[]) {0x20}, 1), TAG, "set VDV");
    ESP_RETURN_ON_ERROR(lcd_send_command(panel_io, 0xC6, (uint8_t[]) {0x0F}, 1), TAG, "set frame rate");
    ESP_RETURN_ON_ERROR(lcd_send_command(panel_io, 0xD0, power_control_1, sizeof(power_control_1)), TAG, "set power control");
    ESP_RETURN_ON_ERROR(lcd_send_command(panel_io, 0xD6, (uint8_t[]) {0xA1}, 1), TAG, "set power control 2");
    ESP_RETURN_ON_ERROR(lcd_send_command(panel_io, 0xE0, gamma_positive, sizeof(gamma_positive)), TAG, "set positive gamma");
    ESP_RETURN_ON_ERROR(lcd_send_command(panel_io, 0xE1, gamma_negative, sizeof(gamma_negative)), TAG, "set negative gamma");
    ESP_RETURN_ON_ERROR(lcd_send_command(panel_io, 0xE9, gate_control, sizeof(gate_control)), TAG, "set gate timing");
    ESP_RETURN_ON_ERROR(lcd_send_command(panel_io, 0x21, NULL, 0), TAG, "enable display inversion");

    return ESP_OK;
}

esp_err_t lcd_backlight_init(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LCD_BACKLIGHT_LEDC_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    const ledc_channel_config_t channel_config = {
        .gpio_num = LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LCD_BACKLIGHT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LCD_BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = false,
    };

    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "configure backlight timer");
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG, "configure backlight channel");

    return ESP_OK;
}

esp_err_t lcd_set_brightness(int percent)
{
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }

    uint32_t duty = (1023 * percent) / 100;

    ESP_RETURN_ON_ERROR(
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_BACKLIGHT_LEDC_CHANNEL, duty),
        TAG,
        "set backlight duty"
    );
    ESP_RETURN_ON_ERROR(
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_BACKLIGHT_LEDC_CHANNEL),
        TAG,
        "update backlight duty"
    );

    return ESP_OK;
}

esp_err_t lcd_init(esp_lcd_panel_io_handle_t *panel_io, esp_lcd_panel_handle_t *panel)
{
    const spi_bus_config_t bus_config = {
        .mosi_io_num = LCD_SPI_MOSI,
        .miso_io_num = LCD_SPI_MISO,
        .sclk_io_num = LCD_SPI_CLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_DRAW_BUFFER_SIZE * sizeof(uint16_t),
    };

    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_CS,
        .dc_gpio_num = LCD_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RESET,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
    };

    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO), TAG, "initialize LCD SPI bus");
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, panel_io),
        TAG,
        "create LCD IO handle"
    );
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(*panel_io, &panel_config, panel), TAG, "create LCD panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel), TAG, "reset LCD panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel), TAG, "initialize LCD panel");
    ESP_RETURN_ON_ERROR(lcd_apply_vendor_st7789_init(*panel_io), TAG, "apply vendor ST7789 initialization");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(*panel, LCD_MIRROR_X, LCD_MIRROR_Y), TAG, "mirror LCD panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*panel, true), TAG, "enable LCD panel");

    return ESP_OK;
}

lv_display_t *lvgl_display_init(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel)
{
    const lvgl_port_cfg_t lvgl_config = {
        .task_priority = 4,
        .task_stack = 8192,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };

    const lvgl_port_display_cfg_t display_config = {
        .io_handle = panel_io,
        .panel_handle = panel,
        .buffer_size = LCD_DRAW_BUFFER_SIZE,
        .double_buffer = LCD_DOUBLE_BUFFER,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .rotation = {
            .swap_xy = false,
            .mirror_x = LCD_MIRROR_X,
            .mirror_y = LCD_MIRROR_Y,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .swap_bytes = true,
        },
    };

    esp_err_t err = lvgl_port_init(&lvgl_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LVGL port: %s", esp_err_to_name(err));
        return NULL;
    }

    return lvgl_port_add_disp(&display_config);
}
