#include "touch.h"

#include <driver/spi_master.h>
#include <esp_check.h>
#include <esp_lcd_touch_xpt2046.h>
#include <string.h>

#include "board_profile.h"

static const char *TAG = "touch";
static app_touch_calibration_t s_touch_calibration;

static void touch_process_coordinates(
    esp_lcd_touch_handle_t tp,
    uint16_t *x,
    uint16_t *y,
    uint16_t *strength,
    uint8_t *point_num,
    uint8_t max_point_num
)
{
    (void)strength;

    if (tp == NULL || x == NULL || y == NULL || point_num == NULL) {
        return;
    }

    for (uint8_t index = 0; index < *point_num && index < max_point_num; index++) {
        int32_t adjusted_x = (int32_t)x[index];
        int32_t adjusted_y = (int32_t)y[index];

        if (s_touch_calibration.valid) {
            adjusted_x = (int32_t)(
                ((int64_t)s_touch_calibration.xx * x[index] +
                 (int64_t)s_touch_calibration.xy * y[index] +
                 (int64_t)s_touch_calibration.x_offset) /
                APP_TOUCH_CALIBRATION_SCALE
            );
            adjusted_y = (int32_t)(
                ((int64_t)s_touch_calibration.yx * x[index] +
                 (int64_t)s_touch_calibration.yy * y[index] +
                 (int64_t)s_touch_calibration.y_offset) /
                APP_TOUCH_CALIBRATION_SCALE
            );
        }

        if (adjusted_x < 0) {
            adjusted_x = 0;
        } else if (adjusted_x >= tp->config.x_max) {
            adjusted_x = tp->config.x_max - 1;
        }

        if (adjusted_y < 0) {
            adjusted_y = 0;
        } else if (adjusted_y >= tp->config.y_max) {
            adjusted_y = tp->config.y_max - 1;
        }

        x[index] = (uint16_t)adjusted_x;
        y[index] = (uint16_t)adjusted_y;
    }
}

void touch_set_calibration(const app_touch_calibration_t *calibration)
{
    if (calibration == NULL) {
        memset(&s_touch_calibration, 0, sizeof(s_touch_calibration));
        return;
    }

    s_touch_calibration = *calibration;
}

esp_err_t touch_init(esp_lcd_touch_handle_t *touch_handle)
{
    const greenlight_board_profile_t *board_profile = greenlight_board_profile_get();
    const greenlight_display_profile_t *display = &board_profile->display;
    const greenlight_touch_profile_t *touch = &board_profile->touch;
    esp_lcd_panel_io_handle_t touch_io = NULL;

    const spi_bus_config_t bus_config = {
        .mosi_io_num = touch->spi_mosi,
        .miso_io_num = touch->spi_miso,
        .sclk_io_num = touch->spi_clk,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 32768,
        .flags = SPICOMMON_BUSFLAG_SCLK | SPICOMMON_BUSFLAG_MISO | SPICOMMON_BUSFLAG_MOSI |
                 SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags = ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM,
    };

    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = touch->cs,
        .dc_gpio_num = GPIO_NUM_NC,
        .spi_mode = 0,
        .pclk_hz = touch->clock_hz,
        .trans_queue_depth = 3,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags = {
            .dc_low_on_data = 0,
            .octal_mode = 0,
            .sio_mode = 0,
            .lsb_first = 0,
            .cs_high_active = 0,
        },
    };

    const esp_lcd_touch_config_t touch_config = {
        .x_max = display->h_res,
        .y_max = display->v_res,
        .rst_gpio_num = touch->reset,
        .int_gpio_num = touch->irq,
        .process_coordinates = touch_process_coordinates,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = touch->swap_xy,
            .mirror_x = touch->mirror_x,
            .mirror_y = touch->mirror_y,
        },
    };

    if (touch->spi_host != display->spi_host) {
        ESP_RETURN_ON_ERROR(spi_bus_initialize(touch->spi_host, &bus_config, SPI_DMA_CH_AUTO), TAG, "initialize touch SPI bus");
    }
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)touch->spi_host, &io_config, &touch_io),
        TAG,
        "create touch IO handle"
    );
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_spi_xpt2046(touch_io, &touch_config, touch_handle), TAG, "create touch handle");

    return ESP_OK;
}
