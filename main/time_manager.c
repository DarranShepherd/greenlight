#include "time_manager.h"

#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_check.h>
#include <esp_log.h>
#include <esp_sntp.h>

#define TIME_MANAGER_STACK_SIZE 4096
#define TIME_MANAGER_SYNC_TIMEOUT_SECONDS 30

static const char *TAG = "time_manager";
static const char *TIME_MANAGER_TZ_LABEL = "Europe/London";
static const char *TIME_MANAGER_TZ_POSIX = "GMT0BST,M3.5.0/1,M10.5.0/2";

static app_state_t *s_state;
static TaskHandle_t s_time_task;
static bool s_sync_in_progress;

static bool is_time_valid(void)
{
    time_t now = 0;
    struct tm local_time = {0};

    time(&now);
    localtime_r(&now, &local_time);
    return local_time.tm_year >= (2024 - 1900);
}

static void format_local_time(char *buffer, size_t buffer_size)
{
    time_t now = 0;
    struct tm local_time = {0};

    if (buffer_size == 0) {
        return;
    }

    time(&now);
    localtime_r(&now, &local_time);

    if (!is_time_valid()) {
        buffer[0] = '\0';
        return;
    }

    if (strftime(buffer, buffer_size, "%a %d %b %H:%M", &local_time) == 0) {
        buffer[0] = '\0';
    }
}

static void time_manager_task(void *arg)
{
    (void)arg;

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        s_sync_in_progress = true;

        setenv("TZ", TIME_MANAGER_TZ_POSIX, 1);
        tzset();

        if (esp_sntp_enabled()) {
            esp_sntp_stop();
        }

        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();

        app_state_set_time_status(s_state, APP_TIME_STATUS_SYNCING, false, "Synchronizing time for Europe/London");
        app_state_set_local_time_text(s_state, "Syncing clock");

        bool valid = false;
        for (uint32_t attempt = 0; attempt < TIME_MANAGER_SYNC_TIMEOUT_SECONDS; attempt++) {
            if (is_time_valid()) {
                valid = true;
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        if (valid) {
            char local_time_text[APP_TIME_LOCAL_TEXT_MAX_LEN] = {0};

            format_local_time(local_time_text, sizeof(local_time_text));
            app_state_set_time_status(s_state, APP_TIME_STATUS_VALID, true, "Time synchronized for Europe/London");
            app_state_set_local_time_text(s_state, local_time_text);
            ESP_LOGI(TAG, "Time synchronized in %s", TIME_MANAGER_TZ_LABEL);
        } else {
            app_state_set_time_status(s_state, APP_TIME_STATUS_ERROR, false, "Time sync timed out");
            app_state_set_local_time_text(s_state, "");
            ESP_LOGW(TAG, "Timed out waiting for SNTP time sync");
        }

        s_sync_in_progress = false;
    }
}

esp_err_t time_manager_init(app_state_t *state)
{
    s_state = state;

    BaseType_t task_created = xTaskCreatePinnedToCore(
        time_manager_task,
        "time_manager",
        TIME_MANAGER_STACK_SIZE,
        NULL,
        5,
        &s_time_task,
        tskNO_AFFINITY
    );
    ESP_RETURN_ON_FALSE(task_created == pdPASS, ESP_ERR_NO_MEM, TAG, "create time manager task");

    return ESP_OK;
}

esp_err_t time_manager_request_sync(void)
{
    ESP_RETURN_ON_FALSE(s_time_task != NULL, ESP_ERR_INVALID_STATE, TAG, "time manager not initialized");

    if (s_sync_in_progress) {
        return ESP_OK;
    }

    xTaskNotifyGive(s_time_task);
    return ESP_OK;
}

bool time_manager_is_syncing(void)
{
    return s_sync_in_progress;
}

void time_manager_update_clock(app_state_t *state)
{
    char local_time_text[APP_TIME_LOCAL_TEXT_MAX_LEN] = {0};

    if (state == NULL || !app_state_get_time_valid(state)) {
        return;
    }

    format_local_time(local_time_text, sizeof(local_time_text));
    app_state_set_local_time_text(state, local_time_text);
}