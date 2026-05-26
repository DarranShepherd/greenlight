#include "touch.h"

#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <esp_check.h>
#include <esp_lcd_touch_gt911.h>
#include <esp_lcd_touch_xpt2046.h>
#include <string.h>

#include "board_profile.h"

static const char *TAG = "touch";
static const int32_t TOUCH_RAW_COORDINATE_MAX = 4095;
static app_touch_calibration_t s_touch_calibration;
static greenlight_touch_profile_t s_touch_profile;
static int32_t s_last_raw_x;
static int32_t s_last_raw_y;
static bool s_has_last_raw_point;

static void touch_apply_orientation(int32_t *x, int32_t *y, int32_t max_x, int32_t max_y)
{
    int32_t adjusted_x = 0;
    int32_t adjusted_y = 0;

    if (x == NULL || y == NULL) {
        return;
    }

    adjusted_x = *x;
    adjusted_y = *y;

    if (s_touch_profile.mirror_x) {
        adjusted_x = max_x - adjusted_x;
    }

    if (s_touch_profile.mirror_y) {
        adjusted_y = max_y - adjusted_y;
    }

    if (s_touch_profile.swap_xy) {
        int32_t tmp = adjusted_x;
        adjusted_x = adjusted_y;
        adjusted_y = tmp;
    }

    *x = adjusted_x;
    *y = adjusted_y;
}

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
        int32_t raw_x = (int32_t)x[index];
        int32_t raw_y = (int32_t)y[index];
        int32_t adjusted_x = 0;
        int32_t adjusted_y = 0;
        bool direct_coordinates = s_touch_profile.controller == GREENLIGHT_TOUCH_CONTROLLER_GT911;

        touch_apply_orientation(
            &raw_x,
            &raw_y,
            direct_coordinates ? (int32_t)(tp->config.x_max - 1) : TOUCH_RAW_COORDINATE_MAX,
            direct_coordinates ? (int32_t)(tp->config.y_max - 1) : TOUCH_RAW_COORDINATE_MAX
        );
        s_last_raw_x = raw_x;
        s_last_raw_y = raw_y;
        s_has_last_raw_point = true;

        if (s_touch_calibration.valid) {
            adjusted_x = (int32_t)(
                ((int64_t)s_touch_calibration.xx * raw_x +
                 (int64_t)s_touch_calibration.xy * raw_y +
                 (int64_t)s_touch_calibration.x_offset) /
                APP_TOUCH_CALIBRATION_SCALE
            );
            adjusted_y = (int32_t)(
                ((int64_t)s_touch_calibration.yx * raw_x +
                 (int64_t)s_touch_calibration.yy * raw_y +
                 (int64_t)s_touch_calibration.y_offset) /
                APP_TOUCH_CALIBRATION_SCALE
            );
        } else if (direct_coordinates) {
            adjusted_x = raw_x;
            adjusted_y = raw_y;
        } else {
            adjusted_x = (raw_x * (tp->config.x_max - 1)) / TOUCH_RAW_COORDINATE_MAX;
            adjusted_y = (raw_y * (tp->config.y_max - 1)) / TOUCH_RAW_COORDINATE_MAX;
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

bool touch_get_latest_raw_point(int32_t *x, int32_t *y)
{
    if (!s_has_last_raw_point || x == NULL || y == NULL) {
        return false;
    }

    *x = s_last_raw_x;
    *y = s_last_raw_y;
    return true;
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

    s_touch_profile = *touch;
    s_has_last_raw_point = false;

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
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
    };

    if (touch->bus_type == GREENLIGHT_TOUCH_BUS_I2C && touch->controller == GREENLIGHT_TOUCH_CONTROLLER_GT911) {
        i2c_master_bus_handle_t i2c_handle = NULL;
        const i2c_master_bus_config_t i2c_config = {
            .i2c_port = touch->i2c_port,
            .sda_io_num = touch->i2c_sda,
            .scl_io_num = touch->i2c_scl,
            .clk_source = I2C_CLK_SRC_DEFAULT,
        };
        esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();

        io_config.dev_addr = touch->i2c_address;
        io_config.scl_speed_hz = touch->clock_hz;

        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_config, &i2c_handle), TAG, "initialize touch I2C bus");
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_handle, &io_config, &touch_io), TAG, "create touch IO handle");
        ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(touch_io, &touch_config, touch_handle), TAG, "create GT911 touch handle");
        return ESP_OK;
    }

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
