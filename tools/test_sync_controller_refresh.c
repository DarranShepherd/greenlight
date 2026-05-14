#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/task.h"

#include "app_state.h"
#include "tariff_model.h"

size_t strlcpy(char *destination, const char *source, size_t destination_size)
{
    size_t source_length = source != NULL ? strlen(source) : 0;

    if (destination_size == 0) {
        return source_length;
    }

    if (source == NULL) {
        destination[0] = '\0';
        return 0;
    }

    if (source_length >= destination_size) {
        memcpy(destination, source, destination_size - 1);
        destination[destination_size - 1] = '\0';
    } else {
        memcpy(destination, source, source_length + 1);
    }

    return source_length;
}

static bool s_stub_wifi_connected = true;
static esp_err_t s_stub_fetch_result = ESP_OK;
static tariff_slot_t s_stub_slots[TARIFF_MODEL_MAX_SLOTS];
static size_t s_stub_slot_count = 0;
static char s_last_fetch_region[APP_SETTINGS_REGION_CODE_MAX_LEN + 1];

bool wifi_manager_is_connected(void)
{
    return s_stub_wifi_connected;
}

esp_err_t octopus_client_fetch_tariffs(
    const char *region_code,
    time_t now_local,
    tariff_slot_t *slots,
    size_t max_slots,
    size_t *slot_count
)
{
    (void)now_local;

    strlcpy(s_last_fetch_region, region_code, sizeof(s_last_fetch_region));
    if (s_stub_fetch_result != ESP_OK) {
        return s_stub_fetch_result;
    }

    assert(slots != NULL);
    assert(slot_count != NULL);
    assert(s_stub_slot_count <= max_slots);
    memcpy(slots, s_stub_slots, sizeof(s_stub_slots[0]) * s_stub_slot_count);
    *slot_count = s_stub_slot_count;
    return ESP_OK;
}

BaseType_t xTaskCreatePinnedToCore(
    void (*task_fn)(void *),
    const char *name,
    unsigned int stack_size,
    void *arg,
    unsigned int priority,
    TaskHandle_t *task_handle,
    int core_id
)
{
    (void)task_fn;
    (void)name;
    (void)stack_size;
    (void)arg;
    (void)priority;
    (void)task_handle;
    (void)core_id;
    return pdPASS;
}

void vTaskDelay(unsigned int ticks)
{
    (void)ticks;
}

#include "../main/app_state.c"
#include "../main/tariff_model.c"
#include "../main/sync_controller.c"

static tariff_slot_t make_slot(time_t start_local, float price_including_vat)
{
    return (tariff_slot_t){
        .start_utc = start_local,
        .end_utc = start_local + (30 * 60),
        .start_local = start_local,
        .end_local = start_local + (30 * 60),
        .price_including_vat = price_including_vat,
    };
}

static void load_stub_slots(time_t now_local)
{
    time_t slot_start = now_local - (30 * 60);

    s_stub_slot_count = 4;
    s_stub_slots[0] = make_slot(slot_start, 12.0f);
    s_stub_slots[1] = make_slot(slot_start + (30 * 60), 18.0f);
    s_stub_slots[2] = make_slot(slot_start + (60 * 60), 26.0f);
    s_stub_slots[3] = make_slot(slot_start + (90 * 60), 31.0f);
}

int main(void)
{
    app_settings_t settings = {0};
    app_state_t state = {0};
    runtime_tariff_state_t preserved_runtime = {0};
    time_t now_local = 1715688000;

    setenv("TZ", "UTC", 1);
    tzset();

    strlcpy(settings.region_code, "B", sizeof(settings.region_code));
    settings.brightness_percent = APP_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT;
    app_state_init(&state, &settings);
    app_state_set_time_status(&state, APP_TIME_STATUS_VALID, true, "Time synchronized");

    s_state = &state;
    s_stub_wifi_connected = true;
    s_stub_fetch_result = ESP_OK;
    load_stub_slots(now_local);

    assert(refresh_tariffs(now_local) == ESP_OK);
    assert(strcmp(s_active_region_code, "B") == 0);
    assert(strcmp(s_last_fetch_region, "B") == 0);
    assert(s_runtime_state.slot_count == s_stub_slot_count);

    preserved_runtime = s_runtime_state;

    strlcpy(state.settings.region_code, "C", sizeof(state.settings.region_code));
    sync_controller_request_refresh();

    assert(s_refresh_requested);
    assert(s_has_successful_load);
    assert(s_runtime_state.slot_count == preserved_runtime.slot_count);

    s_stub_fetch_result = ESP_FAIL;
    assert(refresh_tariffs(now_local) == ESP_FAIL);

    s_tariff_status = APP_TARIFF_STATUS_STALE;
    publish_runtime_snapshot(now_local);

    assert(strcmp(s_last_fetch_region, "C") == 0);
    assert(strcmp(s_active_region_code, "B") == 0);
    assert(s_runtime_state.slot_count == preserved_runtime.slot_count);
    assert(memcmp(&s_runtime_state, &preserved_runtime, sizeof(s_runtime_state)) == 0);
    assert(state.tariff_has_data);
    assert(state.tariff_status == APP_TARIFF_STATUS_STALE);
    assert(strstr(state.tariff_status_text, "failed for region C") != NULL);
    assert(strstr(state.tariff_status_text, "region B") != NULL);

    puts("sync_controller refresh preservation harness passed");
    return 0;
}