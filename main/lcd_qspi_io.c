#include "lcd_qspi_io.h"

#include <stdlib.h>
#include <string.h>

#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_io_interface.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#define GREENLIGHT_LCD_QSPI_CASET_COMMAND 0x2A
#define GREENLIGHT_LCD_QSPI_RASET_COMMAND 0x2B
#define GREENLIGHT_LCD_QSPI_WORKER_QUEUE_LEN 4
#define GREENLIGHT_LCD_QSPI_WORKER_STACK_SIZE 4096
#define GREENLIGHT_LCD_QSPI_WORKER_PRIORITY 5

typedef struct {
    esp_lcd_panel_io_t base;
    spi_device_handle_t spi_dev;
    gpio_num_t cs_gpio_num;
    size_t max_transfer_bytes;
    QueueHandle_t flush_queue;
    SemaphoreHandle_t flush_done;
    TaskHandle_t flush_task;
    uint8_t pending_caset[4];
    uint8_t pending_raset[4];
    bool pending_caset_valid;
    bool pending_raset_valid;
    esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done;
    void *user_ctx;
} greenlight_lcd_qspi_io_t;

typedef struct {
    greenlight_lcd_qspi_io_t *io;
    const uint8_t *color;
    size_t color_size;
    int lcd_cmd;
    uint8_t caset[4];
    uint8_t raset[4];
    bool caset_valid;
    bool raset_valid;
    bool stop;
} greenlight_lcd_qspi_flush_job_t;

static esp_err_t greenlight_lcd_qspi_send(int lcd_cmd, const void *data, size_t data_size, greenlight_lcd_qspi_io_t *io);

static const char *TAG = "lcd_qspi_io";
static const uint8_t GREENLIGHT_LCD_QSPI_WRITE_COMMAND = 0x02;
static const uint8_t GREENLIGHT_LCD_QSPI_WRITE_MEMORY_COMMAND = 0x32;
static const uint32_t GREENLIGHT_LCD_QSPI_WRITE_MEMORY_ADDRESS = 0x003C00;
static const size_t GREENLIGHT_LCD_QSPI_MAX_TRANSFER_LINES = 32;

static inline void greenlight_lcd_qspi_cs_low(const greenlight_lcd_qspi_io_t *io)
{
    gpio_set_level(io->cs_gpio_num, 0);
}

static inline void greenlight_lcd_qspi_cs_high(const greenlight_lcd_qspi_io_t *io)
{
    gpio_set_level(io->cs_gpio_num, 1);
}

static esp_err_t greenlight_lcd_qspi_send_color_chunks(
    greenlight_lcd_qspi_io_t *io,
    const uint8_t *color_bytes,
    size_t color_size
)
{
    size_t chunk_size = color_size;
    size_t offset = 0;

    ESP_RETURN_ON_FALSE(io != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid qspi io handle");

    if (io->max_transfer_bytes > 0 && chunk_size > io->max_transfer_bytes) {
        chunk_size = io->max_transfer_bytes;
    }

    while (offset < color_size) {
        size_t current_size = color_size - offset;
        spi_transaction_t transaction = {
            .flags = SPI_TRANS_MODE_QIO,
            .cmd = GREENLIGHT_LCD_QSPI_WRITE_MEMORY_COMMAND,
            .addr = GREENLIGHT_LCD_QSPI_WRITE_MEMORY_ADDRESS,
            .length = (current_size < chunk_size ? current_size : chunk_size) * 8,
            .tx_buffer = color_bytes + offset,
        };

        greenlight_lcd_qspi_cs_low(io);
        esp_err_t err = spi_device_polling_transmit(io->spi_dev, &transaction);
        greenlight_lcd_qspi_cs_high(io);
        ESP_RETURN_ON_ERROR(err, TAG, "send LCD color chunk failed");

        offset += transaction.length / 8;
    }

    return ESP_OK;
}

static void greenlight_lcd_qspi_flush_task(void *arg)
{
    greenlight_lcd_qspi_io_t *io = (greenlight_lcd_qspi_io_t *)arg;
    greenlight_lcd_qspi_flush_job_t job = {0};

    while (xQueueReceive(io->flush_queue, &job, portMAX_DELAY) == pdTRUE) {
        if (job.stop) {
            break;
        }

        esp_err_t err = ESP_OK;

        if (job.caset_valid) {
            err = greenlight_lcd_qspi_send(GREENLIGHT_LCD_QSPI_CASET_COMMAND, job.caset, sizeof(job.caset), io);
        }
        if (err == ESP_OK && job.raset_valid) {
            err = greenlight_lcd_qspi_send(GREENLIGHT_LCD_QSPI_RASET_COMMAND, job.raset, sizeof(job.raset), io);
        }
        if (err == ESP_OK) {
            err = greenlight_lcd_qspi_send(job.lcd_cmd, NULL, 0, io);
        }
        if (err == ESP_OK) {
            err = greenlight_lcd_qspi_send_color_chunks(io, job.color, job.color_size);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "async qspi flush failed: %s", esp_err_to_name(err));
        }

        if (io->on_color_trans_done != NULL) {
            io->on_color_trans_done((esp_lcd_panel_io_handle_t)io, NULL, io->user_ctx);
        }
    }

    if (io->flush_done != NULL) {
        xSemaphoreGive(io->flush_done);
    }

    vTaskDelete(NULL);
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

    if (io != NULL && io->flush_queue != NULL && param != NULL && param_size == 4) {
        if (lcd_cmd == GREENLIGHT_LCD_QSPI_CASET_COMMAND) {
            memcpy(io->pending_caset, param, sizeof(io->pending_caset));
            io->pending_caset_valid = true;
            return ESP_OK;
        }

        if (lcd_cmd == GREENLIGHT_LCD_QSPI_RASET_COMMAND) {
            memcpy(io->pending_raset, param, sizeof(io->pending_raset));
            io->pending_raset_valid = true;
            return ESP_OK;
        }
    }

    return greenlight_lcd_qspi_send(lcd_cmd, param, param_size, io);
}

static esp_err_t greenlight_lcd_qspi_tx_color(esp_lcd_panel_io_t *panel_io, int lcd_cmd, const void *color, size_t color_size)
{
    greenlight_lcd_qspi_io_t *io = (greenlight_lcd_qspi_io_t *)panel_io;
    greenlight_lcd_qspi_flush_job_t job = {
        .io = io,
        .color = (const uint8_t *)color,
        .color_size = color_size,
        .lcd_cmd = lcd_cmd,
        .caset_valid = io != NULL ? io->pending_caset_valid : false,
        .raset_valid = io != NULL ? io->pending_raset_valid : false,
    };

    ESP_RETURN_ON_FALSE(io != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid qspi io handle");
    ESP_RETURN_ON_FALSE(lcd_cmd >= 0, ESP_ERR_INVALID_ARG, TAG, "invalid LCD command");

    if (job.caset_valid) {
        memcpy(job.caset, io->pending_caset, sizeof(job.caset));
        io->pending_caset_valid = false;
    }
    if (job.raset_valid) {
        memcpy(job.raset, io->pending_raset, sizeof(job.raset));
        io->pending_raset_valid = false;
    }

    if (io->flush_queue != NULL) {
        BaseType_t queued = xQueueSend(io->flush_queue, &job, portMAX_DELAY);
        ESP_RETURN_ON_FALSE(queued == pdTRUE, ESP_ERR_TIMEOUT, TAG, "queue qspi flush job");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(greenlight_lcd_qspi_send(lcd_cmd, NULL, 0, io), TAG, "send LCD write-memory command");

    esp_err_t err = greenlight_lcd_qspi_send_color_chunks(io, job.color, job.color_size);
    ESP_RETURN_ON_ERROR(err, TAG, "send LCD color failed");

    if (err == ESP_OK && io->on_color_trans_done != NULL) {
        io->on_color_trans_done((esp_lcd_panel_io_handle_t)panel_io, NULL, io->user_ctx);
    }

    return err;
}

static esp_err_t greenlight_lcd_qspi_del(esp_lcd_panel_io_t *panel_io)
{
    greenlight_lcd_qspi_io_t *io = (greenlight_lcd_qspi_io_t *)panel_io;
    greenlight_lcd_qspi_flush_job_t stop_job = {
        .stop = true,
    };

    ESP_RETURN_ON_FALSE(io != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid qspi io handle");

    if (io->flush_queue != NULL) {
        (void)xQueueSend(io->flush_queue, &stop_job, pdMS_TO_TICKS(100));
    }
    if (io->flush_done != NULL) {
        (void)xSemaphoreTake(io->flush_done, pdMS_TO_TICKS(500));
        vSemaphoreDelete(io->flush_done);
    }
    if (io->flush_queue != NULL) {
        vQueueDelete(io->flush_queue);
    }

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
    io->max_transfer_bytes = (size_t)display->h_res * GREENLIGHT_LCD_QSPI_MAX_TRANSFER_LINES * sizeof(uint16_t);
    io->flush_queue = xQueueCreate(GREENLIGHT_LCD_QSPI_WORKER_QUEUE_LEN, sizeof(greenlight_lcd_qspi_flush_job_t));
    io->flush_done = xSemaphoreCreateBinary();

    ESP_RETURN_ON_FALSE(io->flush_queue != NULL, ESP_ERR_NO_MEM, TAG, "create qspi flush queue");
    ESP_RETURN_ON_FALSE(io->flush_done != NULL, ESP_ERR_NO_MEM, TAG, "create qspi flush semaphore");

    ESP_RETURN_ON_ERROR(gpio_config(&cs_gpio_config), TAG, "configure qspi cs gpio");
    greenlight_lcd_qspi_cs_high(io);

    esp_err_t err = spi_bus_add_device(display->spi_host, &device_config, &io->spi_dev);
    if (err != ESP_OK) {
        vQueueDelete(io->flush_queue);
        vSemaphoreDelete(io->flush_done);
        free(io);
        return err;
    }

    BaseType_t task_created = xTaskCreatePinnedToCore(
        greenlight_lcd_qspi_flush_task,
        "lcd_qspi_flush",
        GREENLIGHT_LCD_QSPI_WORKER_STACK_SIZE,
        io,
        GREENLIGHT_LCD_QSPI_WORKER_PRIORITY,
        &io->flush_task,
        0
    );
    if (task_created != pdPASS) {
        spi_bus_remove_device(io->spi_dev);
        vQueueDelete(io->flush_queue);
        vSemaphoreDelete(io->flush_done);
        free(io);
        return ESP_ERR_NO_MEM;
    }

    *ret_io = (esp_lcd_panel_io_handle_t)io;
    return ESP_OK;
}