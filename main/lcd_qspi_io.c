#include "lcd_qspi_io.h"

#include <stdlib.h>
#include <string.h>

#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_io_interface.h>

typedef struct {
    esp_lcd_panel_io_t base;
    spi_device_handle_t spi_dev;
    gpio_num_t cs_gpio_num;
    esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done;
    void *user_ctx;
} greenlight_lcd_qspi_io_t;

static const char *TAG = "lcd_qspi_io";
static const uint8_t GREENLIGHT_LCD_QSPI_WRITE_COMMAND = 0x02;
static const uint8_t GREENLIGHT_LCD_QSPI_WRITE_MEMORY_COMMAND = 0x32;
static const uint32_t GREENLIGHT_LCD_QSPI_WRITE_MEMORY_ADDRESS = 0x003C00;

static inline void greenlight_lcd_qspi_cs_low(const greenlight_lcd_qspi_io_t *io)
{
    gpio_set_level(io->cs_gpio_num, 0);
}

static inline void greenlight_lcd_qspi_cs_high(const greenlight_lcd_qspi_io_t *io)
{
    gpio_set_level(io->cs_gpio_num, 1);
}

static esp_err_t greenlight_lcd_qspi_send(int lcd_cmd, const void *data, size_t data_size, greenlight_lcd_qspi_io_t *io)
{
    spi_transaction_t transaction = {
        .flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR,
        .cmd = GREENLIGHT_LCD_QSPI_WRITE_COMMAND,
        .addr = ((uint32_t)(lcd_cmd & 0xFF)) << 8,
        .length = data_size * 8,
        .tx_buffer = data,
    };

    ESP_RETURN_ON_FALSE(io != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid qspi io handle");
    ESP_RETURN_ON_FALSE(lcd_cmd >= 0, ESP_ERR_INVALID_ARG, TAG, "invalid LCD command");

    greenlight_lcd_qspi_cs_low(io);
    esp_err_t err = spi_device_polling_transmit(io->spi_dev, &transaction);
    greenlight_lcd_qspi_cs_high(io);
    return err;
}

static esp_err_t greenlight_lcd_qspi_rx_param(esp_lcd_panel_io_t *panel_io, int lcd_cmd, void *param, size_t param_size)
{
    (void)panel_io;
    (void)lcd_cmd;
    (void)param;
    (void)param_size;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t greenlight_lcd_qspi_tx_param(esp_lcd_panel_io_t *panel_io, int lcd_cmd, const void *param, size_t param_size)
{
    greenlight_lcd_qspi_io_t *io = (greenlight_lcd_qspi_io_t *)panel_io;
    return greenlight_lcd_qspi_send(lcd_cmd, param, param_size, io);
}

static esp_err_t greenlight_lcd_qspi_tx_color(esp_lcd_panel_io_t *panel_io, int lcd_cmd, const void *color, size_t color_size)
{
    greenlight_lcd_qspi_io_t *io = (greenlight_lcd_qspi_io_t *)panel_io;
    spi_transaction_t transaction = {
        .flags = SPI_TRANS_MODE_QIO,
        .cmd = GREENLIGHT_LCD_QSPI_WRITE_MEMORY_COMMAND,
        .addr = GREENLIGHT_LCD_QSPI_WRITE_MEMORY_ADDRESS,
        .length = color_size * 8,
        .tx_buffer = color,
    };

    ESP_RETURN_ON_FALSE(io != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid qspi io handle");
    ESP_RETURN_ON_FALSE(lcd_cmd >= 0, ESP_ERR_INVALID_ARG, TAG, "invalid LCD command");

    ESP_RETURN_ON_ERROR(greenlight_lcd_qspi_send(lcd_cmd, NULL, 0, io), TAG, "send LCD write-memory command");

    greenlight_lcd_qspi_cs_low(io);
    esp_err_t err = spi_device_polling_transmit(io->spi_dev, &transaction);
    greenlight_lcd_qspi_cs_high(io);

    if (err == ESP_OK && io->on_color_trans_done != NULL) {
        io->on_color_trans_done((esp_lcd_panel_io_handle_t)panel_io, NULL, io->user_ctx);
    }

    return err;
}

static esp_err_t greenlight_lcd_qspi_del(esp_lcd_panel_io_t *panel_io)
{
    greenlight_lcd_qspi_io_t *io = (greenlight_lcd_qspi_io_t *)panel_io;

    ESP_RETURN_ON_FALSE(io != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid qspi io handle");

    ESP_RETURN_ON_ERROR(spi_bus_remove_device(io->spi_dev), TAG, "remove qspi panel device");
    free(io);
    return ESP_OK;
}

static esp_err_t greenlight_lcd_qspi_register_event_callbacks(
    esp_lcd_panel_io_t *panel_io,
    const esp_lcd_panel_io_callbacks_t *cbs,
    void *user_ctx
)
{
    greenlight_lcd_qspi_io_t *io = (greenlight_lcd_qspi_io_t *)panel_io;

    ESP_RETURN_ON_FALSE(io != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid qspi io handle");

    io->on_color_trans_done = cbs != NULL ? cbs->on_color_trans_done : NULL;
    io->user_ctx = user_ctx;
    return ESP_OK;
}

esp_err_t greenlight_lcd_new_panel_io_qspi(
    const greenlight_display_profile_t *display,
    esp_lcd_panel_io_handle_t *ret_io
)
{
    greenlight_lcd_qspi_io_t *io = NULL;
    const gpio_config_t cs_gpio_config = {
        .pin_bit_mask = 1ULL << display->cs,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const spi_device_interface_config_t device_config = {
        .command_bits = 8,
        .address_bits = 24,
        .mode = 0,
        .clock_speed_hz = (int)display->pixel_clock_hz,
        .spics_io_num = -1,
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };

    ESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_INVALID_ARG, TAG, "missing display profile");
    ESP_RETURN_ON_FALSE(ret_io != NULL, ESP_ERR_INVALID_ARG, TAG, "missing output handle");

    io = calloc(1, sizeof(*io));
    ESP_RETURN_ON_FALSE(io != NULL, ESP_ERR_NO_MEM, TAG, "allocate qspi io handle");

    io->base.rx_param = greenlight_lcd_qspi_rx_param;
    io->base.tx_param = greenlight_lcd_qspi_tx_param;
    io->base.tx_color = greenlight_lcd_qspi_tx_color;
    io->base.del = greenlight_lcd_qspi_del;
    io->base.register_event_callbacks = greenlight_lcd_qspi_register_event_callbacks;
    io->cs_gpio_num = display->cs;

    ESP_RETURN_ON_ERROR(gpio_config(&cs_gpio_config), TAG, "configure qspi cs gpio");
    greenlight_lcd_qspi_cs_high(io);

    esp_err_t err = spi_bus_add_device(display->spi_host, &device_config, &io->spi_dev);
    if (err != ESP_OK) {
        free(io);
        return err;
    }

    *ret_io = (esp_lcd_panel_io_handle_t)io;
    return ESP_OK;
}