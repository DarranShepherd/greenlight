#include "lcd.h"

#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_check.h>
#include <esp_lcd_ili9341.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>

#include "hardware.h"

static const char *TAG = "lcd";

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
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
    };

    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO), TAG, "initialize LCD SPI bus");
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, panel_io),
        TAG,
        "create LCD IO handle"
    );
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(*panel_io, &panel_config, panel), TAG, "create LCD panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel), TAG, "reset LCD panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel), TAG, "initialize LCD panel");
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
