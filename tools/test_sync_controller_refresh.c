#define _POSIX_C_SOURCE 200809L
#define GREENLIGHT_HOST_TEST 1

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
#include "sync_controller.h"
#include "sync_controller_internal.h"
#include "tariff_model.h"

size_t strlcpy(char *destination, const char *source, size_t destination_size)
{
    size_t source_length = strlen(source);

    if (destination_size == 0) {
        return source_length;
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

static time_t make_local_timestamp(int year, int month, int day, int hour, int minute, int second)
{
    struct tm local_tm = {
        .tm_year = year - 1900,
        .tm_mon = month - 1,
        .tm_mday = day,
        .tm_hour = hour,
        .tm_min = minute,
        .tm_sec = second,
        .tm_isdst = -1,
    };

    return mktime(&local_tm);
}

static void reset_harness(app_state_t *state, app_settings_t *settings, const char *region_code)
{
    memset(s_stub_slots, 0, sizeof(s_stub_slots));
    memset(s_last_fetch_region, 0, sizeof(s_last_fetch_region));

    s_stub_wifi_connected = true;
    s_stub_fetch_result = ESP_OK;
    s_stub_slot_count = 0;

    sync_controller_test_reset(state, settings, region_code);
}

static void load_linear_slots(time_t first_slot_start, size_t slot_count, float first_price, float step)
{
    assert(slot_count <= TARIFF_MODEL_MAX_SLOTS);

    s_stub_slot_count = slot_count;
    for (size_t index = 0; index < slot_count; index++) {
        s_stub_slots[index] = make_slot(first_slot_start + ((time_t)index * 30 * 60), first_price + ((float)index * step));
    }
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

static void test_region_switch_failure_preserves_last_good_dataset(void)
{
    app_settings_t settings = {0};
    app_state_t state = {0};
    runtime_tariff_state_t preserved_runtime = {0};
    time_t now_local = 1715688000;

    setenv("TZ", "UTC", 1);
    tzset();

    reset_harness(&state, &settings, "B");
    load_stub_slots(now_local);

    assert(sync_controller_test_refresh_tariffs(now_local) == ESP_OK);
    assert(strcmp(sync_controller_test_get_active_region_code(), "B") == 0);
    assert(strcmp(s_last_fetch_region, "B") == 0);
    assert(sync_controller_test_get_runtime_state()->slot_count == s_stub_slot_count);

    preserved_runtime = *sync_controller_test_get_runtime_state();

    strlcpy(state.settings.region_code, "C", sizeof(state.settings.region_code));
    sync_controller_request_refresh();

    assert(sync_controller_test_get_refresh_requested());
    assert(sync_controller_test_get_has_successful_load());
    assert(sync_controller_test_get_runtime_state()->slot_count == preserved_runtime.slot_count);

    s_stub_fetch_result = ESP_FAIL;
    assert(sync_controller_test_refresh_tariffs(now_local) == ESP_FAIL);

    sync_controller_test_set_tariff_status(APP_TARIFF_STATUS_STALE);
    sync_controller_test_publish_runtime_snapshot(now_local);

    assert(strcmp(s_last_fetch_region, "C") == 0);
    assert(strcmp(sync_controller_test_get_active_region_code(), "B") == 0);
    assert(sync_controller_test_get_runtime_state()->slot_count == preserved_runtime.slot_count);
    assert(memcmp(sync_controller_test_get_runtime_state(), &preserved_runtime, sizeof(preserved_runtime)) == 0);
    assert(state.tariff_has_data);
    assert(state.tariff_status == APP_TARIFF_STATUS_STALE);
    assert(strstr(state.tariff_status_text, "failed for region C") != NULL);
    assert(strstr(state.tariff_status_text, "region B") != NULL);
}

static void test_region_switch_retry_commits_new_dataset_after_stale_failure(void)
{
    app_settings_t settings = {0};
    app_state_t state = {0};
    time_t now_local = 1715688000;

    setenv("TZ", "UTC", 1);
    tzset();

    reset_harness(&state, &settings, "B");
    load_stub_slots(now_local);
    assert(sync_controller_test_refresh_tariffs(now_local) == ESP_OK);

    strlcpy(state.settings.region_code, "C", sizeof(state.settings.region_code));
    sync_controller_request_refresh();
    s_stub_fetch_result = ESP_FAIL;
    assert(sync_controller_test_refresh_tariffs(now_local) == ESP_FAIL);
    sync_controller_test_set_tariff_status(APP_TARIFF_STATUS_STALE);
    sync_controller_test_publish_runtime_snapshot(now_local);

    s_stub_fetch_result = ESP_OK;
    load_linear_slots(now_local, 6, 4.0f, 0.5f);
    assert(sync_controller_test_refresh_tariffs(now_local + 30) == ESP_OK);

    assert(strcmp(s_last_fetch_region, "C") == 0);
    assert(strcmp(sync_controller_test_get_active_region_code(), "C") == 0);
    assert(!sync_controller_test_get_refresh_requested());
    assert(sync_controller_test_get_runtime_state()->slot_count == 6);
    assert(state.tariff_status == APP_TARIFF_STATUS_READY);
    assert(state.tariff_has_data);
    assert(strstr(state.tariff_status_text, "region C") != NULL);
    assert(state.tariff_current_price == 4.0f);
}

static void test_partial_tomorrow_publication_populates_day_views_without_full_day_assumption(void)
{
    app_settings_t settings = {0};
    app_state_t state = {0};
    time_t now_local = 0;
    time_t today_midnight = 0;

    setenv("TZ", "UTC", 1);
    tzset();

    now_local = make_local_timestamp(2024, 5, 14, 18, 0, 0);
    today_midnight = make_local_timestamp(2024, 5, 14, 0, 0, 0);

    reset_harness(&state, &settings, "B");

    s_stub_slot_count = 54;
    for (size_t index = 0; index < 48; index++) {
        s_stub_slots[index] = make_slot(today_midnight + ((time_t)index * 30 * 60), 10.0f + ((float)index * 0.1f));
    }
    for (size_t index = 0; index < 6; index++) {
        s_stub_slots[48 + index] = make_slot(today_midnight + (24 * 60 * 60) + ((time_t)index * 30 * 60), 7.0f + ((float)index * 0.2f));
    }

    assert(sync_controller_test_refresh_tariffs(now_local) == ESP_OK);

    assert(state.tariff_status == APP_TARIFF_STATUS_READY);
    assert(state.tariff_tomorrow_available);
    assert(state.tariff_today.available);
    assert(state.tariff_tomorrow.available);
    assert(state.tariff_today.slot_count == 48);
    assert(state.tariff_tomorrow.slot_count == 6);
    assert(state.tariff_tomorrow.slots[0].valid);
    assert(state.tariff_tomorrow.slots[5].valid);
    assert(strstr(state.tariff_detail_text, "Tomorrow 6 slots") != NULL);
}

static void test_runtime_snapshot_rolls_forward_when_time_crosses_block_boundary(void)
{
    app_settings_t settings = {0};
    app_state_t state = {0};
    time_t now_local = 0;

    setenv("TZ", "UTC", 1);
    tzset();

    now_local = make_local_timestamp(2024, 5, 14, 13, 50, 0);

    reset_harness(&state, &settings, "B");
    load_linear_slots(make_local_timestamp(2024, 5, 14, 13, 30, 0), 4, 10.0f, 10.0f);

    assert(sync_controller_test_refresh_tariffs(now_local) == ESP_OK);
    assert(state.tariff_current_block_valid);
    assert(state.tariff_current_price == 10.0f);
    assert(state.tariff_current_block_end_local == make_local_timestamp(2024, 5, 14, 14, 0, 0));

    sync_controller_test_publish_runtime_snapshot(make_local_timestamp(2024, 5, 14, 14, 7, 0));

    assert(state.tariff_current_block_valid);
    assert(state.tariff_current_price == 20.0f);
    assert(state.tariff_current_block_end_local == make_local_timestamp(2024, 5, 14, 14, 30, 0));
    assert(strstr(state.tariff_current_text, "until 14:30") != NULL);
}

static void test_tariff_model_handles_london_spring_forward_day(void)
{
    tariff_slot_t slots[TARIFF_MODEL_MAX_SLOTS] = {0};
    runtime_tariff_state_t runtime_state = {0};
    time_t now_local = 0;
    time_t day_start = 0;
    time_t next_day_start = 0;
    size_t slot_count = 0;

    setenv("TZ", "Europe/London", 1);
    tzset();

    now_local = make_local_timestamp(2024, 3, 31, 12, 0, 0);
    day_start = make_local_timestamp(2024, 3, 31, 0, 0, 0);
    next_day_start = make_local_timestamp(2024, 4, 1, 0, 0, 0);
    slot_count = (size_t)((next_day_start - day_start) / (30 * 60));

    assert(slot_count == 46);
    for (size_t index = 0; index < slot_count; index++) {
        slots[index] = make_slot(day_start + ((time_t)index * 30 * 60), 12.0f);
    }

    assert(tariff_model_build(slots, slot_count, now_local, &runtime_state));
    assert(runtime_state.has_today);
    assert(!runtime_state.has_tomorrow);
    assert(runtime_state.today_summary.slot_count == 46);
    assert(runtime_state.slot_count == 46);
    assert(runtime_state.current_slot_index >= 0);
    assert(runtime_state.block_count == 1);
}

int main(void)
{
    test_region_switch_failure_preserves_last_good_dataset();
    test_region_switch_retry_commits_new_dataset_after_stale_failure();
    test_partial_tomorrow_publication_populates_day_views_without_full_day_assumption();
    test_runtime_snapshot_rolls_forward_when_time_crosses_block_boundary();
    test_tariff_model_handles_london_spring_forward_day();

    puts("sync_controller refresh preservation harness passed");
    return 0;
}