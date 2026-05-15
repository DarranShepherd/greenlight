#include "lcd.h"

#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_check.h>
#include <esp_lcd_ili9341.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_st7789.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>

#include "board_profile.h"

static const char *TAG = "lcd";

static esp_err_t lcd_send_command(esp_lcd_panel_io_handle_t panel_io, int command, const void *data, size_t data_size)
{
    return esp_lcd_panel_io_tx_param(panel_io, command, data, data_size);
}

static esp_err_t lcd_apply_init_sequence(
    esp_lcd_panel_io_handle_t panel_io,
    const greenlight_lcd_init_cmd_t *init_cmds,
    size_t init_cmd_count
)
{
    for (size_t index = 0; index < init_cmd_count; ++index) {
        const greenlight_lcd_init_cmd_t *init_cmd = &init_cmds[index];
        ESP_RETURN_ON_ERROR(
            lcd_send_command(panel_io, init_cmd->command, init_cmd->data, init_cmd->data_size),
            TAG,
            "apply LCD init command"
        );
    }

    return ESP_OK;
}

esp_err_t lcd_backlight_init(void)
{
    const greenlight_board_profile_t *board_profile = greenlight_board_profile_get();
    const greenlight_display_profile_t *display = &board_profile->display;

    const ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = display->backlight_ledc_timer,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    const ledc_channel_config_t channel_config = {
        .gpio_num = display->backlight,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = display->backlight_ledc_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = display->backlight_ledc_timer,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = display->backlight_output_invert,
    };

    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "configure backlight timer");
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG, "configure backlight channel");

    return ESP_OK;
}

esp_err_t lcd_set_brightness(int percent)
{
    const greenlight_board_profile_t *board_profile = greenlight_board_profile_get();

    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }

    uint32_t duty = (1023 * percent) / 100;

    ESP_RETURN_ON_ERROR(
        ledc_set_duty(LEDC_LOW_SPEED_MODE, board_profile->display.backlight_ledc_channel, duty),
        TAG,
        "set backlight duty"
    );
    ESP_RETURN_ON_ERROR(
        ledc_update_duty(LEDC_LOW_SPEED_MODE, board_profile->display.backlight_ledc_channel),
        TAG,
        "update backlight duty"
    );

    return ESP_OK;
}

esp_err_t lcd_init(esp_lcd_panel_io_handle_t *panel_io, esp_lcd_panel_handle_t *panel)
{
    const greenlight_board_profile_t *board_profile = greenlight_board_profile_get();
    const greenlight_display_profile_t *display = &board_profile->display;
    size_t draw_buffer_size = (size_t)display->h_res * display->draw_buffer_lines;

    const spi_bus_config_t bus_config = {
        .mosi_io_num = display->spi_mosi,
        .miso_io_num = display->spi_miso,
        .sclk_io_num = display->spi_clk,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = draw_buffer_size * sizeof(uint16_t),
    };

    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = display->cs,
        .dc_gpio_num = display->dc,
        .spi_mode = 0,
        .pclk_hz = display->pixel_clock_hz,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = display->cmd_bits,
        .lcd_param_bits = display->param_bits,
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = display->reset,
        .rgb_ele_order = display->rgb_ele_order,
        .bits_per_pixel = display->bits_per_pixel,
    };

    ESP_RETURN_ON_ERROR(spi_bus_initialize(display->spi_host, &bus_config, SPI_DMA_CH_AUTO), TAG, "initialize LCD SPI bus");
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)display->spi_host, &io_config, panel_io),
        TAG,
        "create LCD IO handle"
    );

    if (display->controller == GREENLIGHT_LCD_CONTROLLER_ST7789) {
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(*panel_io, &panel_config, panel), TAG, "create LCD panel");
    } else {
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(*panel_io, &panel_config, panel), TAG, "create LCD panel");
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel), TAG, "reset LCD panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel), TAG, "initialize LCD panel");

    if (display->init_cmd_count > 0) {
        ESP_RETURN_ON_ERROR(
            lcd_apply_init_sequence(*panel_io, display->init_cmds, display->init_cmd_count),
            TAG,
            "apply LCD initialization sequence"
        );
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(*panel, display->mirror_x, display->mirror_y), TAG, "mirror LCD panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*panel, true), TAG, "enable LCD panel");

    return ESP_OK;
}

lv_display_t *lvgl_display_init(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel)
{
    const greenlight_board_profile_t *board_profile = greenlight_board_profile_get();
    const greenlight_display_profile_t *display = &board_profile->display;
    size_t draw_buffer_size = (size_t)display->h_res * display->draw_buffer_lines;

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
        .buffer_size = draw_buffer_size,
        .double_buffer = display->double_buffer,
        .hres = display->h_res,
        .vres = display->v_res,
        .monochrome = false,
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .rotation = {
            .swap_xy = false,
            .mirror_x = display->mirror_x,
            .mirror_y = display->mirror_y,
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
