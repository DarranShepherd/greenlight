#include "sync_controller.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_check.h>
#include <esp_log.h>

#include "app_settings.h"
#include "octopus_client.h"
#include "tariff_model.h"
#include "wifi_manager.h"

#define SYNC_CONTROLLER_STACK_SIZE 8192
#define SYNC_CONTROLLER_POLL_INTERVAL_MS 1000
#define SYNC_CONTROLLER_REFRESH_RETRY_SECONDS 30
#define SYNC_CONTROLLER_TOMORROW_RETRY_SECONDS (5 * 60)

static const char *TAG = "sync_controller";

static app_state_t *s_state;
static runtime_tariff_state_t s_runtime_state;
static runtime_tariff_state_t s_next_runtime_state;
static tariff_slot_t s_fetched_slots[TARIFF_MODEL_MAX_SLOTS];
static bool s_has_successful_load;
static time_t s_last_attempt_time;
static time_t s_last_success_time;
static int s_loaded_today_key;
static app_tariff_status_t s_tariff_status = APP_TARIFF_STATUS_IDLE;
static bool s_refresh_requested;
static char s_active_region_code[APP_SETTINGS_REGION_CODE_MAX_LEN + 1];

static void get_requested_region_code(char *buffer, size_t buffer_size)
{
    app_settings_t settings = {0};

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (s_state == NULL) {
        strlcpy(buffer, "B", buffer_size);
        return;
    }

    app_state_get_settings(s_state, &settings);
    if (settings.region_code[0] == '\0') {
        strlcpy(buffer, "B", buffer_size);
        return;
    }

    strlcpy(buffer, settings.region_code, buffer_size);
}

static const char *get_active_region_code(void)
{
    if (s_active_region_code[0] == '\0') {
        return "B";
    }

    return s_active_region_code;
}

static void format_time_compact(char *buffer, size_t buffer_size, time_t local_time)
{
    struct tm local_tm = {0};

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    localtime_r(&local_time, &local_tm);
    if (strftime(buffer, buffer_size, "%H:%M", &local_tm) == 0) {
        strlcpy(buffer, "--:--", buffer_size);
    }
}

static void format_refresh_time(char *buffer, size_t buffer_size, time_t local_time)
{
    struct tm local_tm = {0};

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (local_time <= 0) {
        strlcpy(buffer, "Not refreshed yet", buffer_size);
        return;
    }

    localtime_r(&local_time, &local_tm);
    if (strftime(buffer, buffer_size, "Last refresh %a %H:%M", &local_tm) == 0) {
        strlcpy(buffer, "Last refresh unavailable", buffer_size);
    }
}

static void format_log_timestamp(char *buffer, size_t buffer_size, time_t local_time)
{
    struct tm local_tm = {0};

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (local_time <= 0) {
        strlcpy(buffer, "n/a", buffer_size);
        return;
    }

    localtime_r(&local_time, &local_tm);
    if (strftime(buffer, buffer_size, "%Y-%m-%d %H:%M", &local_tm) == 0) {
        strlcpy(buffer, "n/a", buffer_size);
    }
}

static void format_duration(char *buffer, size_t buffer_size, time_t seconds_remaining)
{
    int hours = 0;
    int minutes = 0;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (seconds_remaining < 0) {
        seconds_remaining = 0;
    }

    hours = (int)(seconds_remaining / 3600);
    minutes = (int)((seconds_remaining % 3600) / 60);

    if (hours > 0) {
        snprintf(buffer, buffer_size, "%dh %dm", hours, minutes);
    } else {
        snprintf(buffer, buffer_size, "%dm", minutes);
    }
}

static const tariff_slot_t *get_current_slot(void)
{
    if (s_runtime_state.current_slot_index < 0 || s_runtime_state.current_slot_index >= (int)s_runtime_state.slot_count) {
        return NULL;
    }

    return &s_runtime_state.slots[s_runtime_state.current_slot_index];
}

static const tariff_block_t *get_current_block(void)
{
    if (s_runtime_state.current_block_index < 0 || s_runtime_state.current_block_index >= (int)s_runtime_state.block_count) {
        return NULL;
    }

    return &s_runtime_state.blocks[s_runtime_state.current_block_index];
}

static void populate_day_view(int date_key, const tariff_day_summary_t *summary, app_tariff_day_view_t *day_view)
{
    if (summary == NULL || day_view == NULL) {
        return;
    }

    memset(day_view, 0, sizeof(*day_view));

    if (summary->slot_count == 0) {
        return;
    }

    day_view->available = true;
    day_view->slot_count = summary->slot_count > APP_TARIFF_DAY_SLOT_MAX ? APP_TARIFF_DAY_SLOT_MAX : summary->slot_count;
    day_view->min_price = summary->min_price;
    day_view->avg_price = summary->avg_price;
    day_view->max_price = summary->max_price;

    for (size_t index = 0; index < s_runtime_state.slot_count; index++) {
        const tariff_slot_t *slot = &s_runtime_state.slots[index];
        uint8_t slot_index = 0;

        if (tariff_model_get_local_day_key(slot->start_local) != date_key) {
            continue;
        }

        slot_index = 0;
        while (slot_index < day_view->slot_count && day_view->slots[slot_index].valid) {
            slot_index++;
        }

        if (slot_index >= day_view->slot_count) {
            break;
        }

        day_view->slots[slot_index] = (app_tariff_day_slot_t){
            .valid = true,
            .price = slot->price_including_vat,
            .band = slot->band,
        };
    }
}

static void publish_runtime_snapshot(time_t now_local)
{
    char requested_region_code[APP_SETTINGS_REGION_CODE_MAX_LEN + 1] = {0};
    char status_text[APP_TARIFF_STATUS_TEXT_MAX_LEN] = {0};
    char current_text[APP_TARIFF_SNAPSHOT_TEXT_MAX_LEN] = {0};
    char next_text[APP_TARIFF_SNAPSHOT_TEXT_MAX_LEN] = {0};
    char detail_text[APP_TARIFF_SNAPSHOT_TEXT_MAX_LEN] = {0};
    char updated_text[APP_TARIFF_UPDATED_TEXT_MAX_LEN] = {0};
    app_tariff_preview_t previews[APP_TARIFF_PREVIEW_MAX] = {0};
    app_tariff_day_view_t today_view = {0};
    app_tariff_day_view_t tomorrow_view = {0};
    const tariff_slot_t *current_slot = get_current_slot();
    const tariff_block_t *current_block = get_current_block();
    struct tm local_tm = {0};
    size_t next_offset = 0;
    time_t next_block_start_local = 0;
    uint8_t preview_count = 0;

    localtime_r(&now_local, &local_tm);
    get_requested_region_code(requested_region_code, sizeof(requested_region_code));

    if (s_tariff_status == APP_TARIFF_STATUS_STALE) {
        const char *active_region = get_active_region_code();

        if (strcmp(requested_region_code, active_region) == 0) {
            snprintf(status_text, sizeof(status_text), "Refresh failed. Showing last good Agile dataset for region %s", active_region);
        } else {
            snprintf(
                status_text,
                sizeof(status_text),
                "Refresh failed for region %s. Showing last good Agile dataset for region %s",
                requested_region_code,
                active_region
            );
        }
    } else if (s_runtime_state.has_tomorrow) {
        snprintf(status_text, sizeof(status_text), "Today and tomorrow loaded for region %s", get_active_region_code());
    } else if (local_tm.tm_hour >= 16) {
        snprintf(status_text, sizeof(status_text), "Today's prices loaded. Polling every 5 min for tomorrow in region %s", get_active_region_code());
    } else {
        snprintf(status_text, sizeof(status_text), "Today's prices loaded. Waiting for tomorrow after 16:00 in region %s", get_active_region_code());
    }

    if (current_slot != NULL && current_block != NULL) {
        char end_time[8] = {0};
        char remaining_text[24] = {0};

        format_time_compact(end_time, sizeof(end_time), current_block->end_local);
        format_duration(remaining_text, sizeof(remaining_text), current_block->end_local - now_local);
        snprintf(
            current_text,
            sizeof(current_text),
            "Now %.2fp/kWh, %s until %s (%s left)",
            current_slot->price_including_vat,
            tariff_model_get_band_name(current_block->band),
            end_time,
            remaining_text
        );
    } else {
        strlcpy(current_text, "No active tariff slot matches the current local time", sizeof(current_text));
    }

    if (s_runtime_state.current_block_index >= 0) {
        for (int index = s_runtime_state.current_block_index + 1; index < (int)s_runtime_state.block_count && index <= s_runtime_state.current_block_index + 3; index++) {
            const tariff_block_t *block = &s_runtime_state.blocks[index];
            char start_time[8] = {0};
            char end_time[8] = {0};
            int written = 0;

            if (preview_count == 0) {
                next_block_start_local = block->start_local;
            }

            if (preview_count < APP_TARIFF_PREVIEW_MAX) {
                previews[preview_count++] = (app_tariff_preview_t){
                    .valid = true,
                    .start_local = block->start_local,
                    .end_local = block->end_local,
                    .representative_price = block->representative_price,
                    .band = block->band,
                };
            }

            format_time_compact(start_time, sizeof(start_time), block->start_local);
            format_time_compact(end_time, sizeof(end_time), block->end_local);
            written = snprintf(
                &next_text[next_offset],
                sizeof(next_text) - next_offset,
                "%s%s-%s %s",
                next_offset == 0 ? "Next: " : " | ",
                start_time,
                end_time,
                tariff_model_get_band_name(block->band)
            );
            if (written < 0 || (size_t)written >= sizeof(next_text) - next_offset) {
                break;
            }
            next_offset += (size_t)written;
        }
    }

    if (next_text[0] == '\0') {
        strlcpy(next_text, "Next: No further grouped blocks loaded yet", sizeof(next_text));
    }

    if (s_runtime_state.has_tomorrow) {
        snprintf(
            detail_text,
            sizeof(detail_text),
            "Today %u slots min %.2f avg %.2f max %.2f | Tomorrow %u slots min %.2f avg %.2f max %.2f",
            (unsigned int)s_runtime_state.today_summary.slot_count,
            s_runtime_state.today_summary.min_price,
            s_runtime_state.today_summary.avg_price,
            s_runtime_state.today_summary.max_price,
            (unsigned int)s_runtime_state.tomorrow_summary.slot_count,
            s_runtime_state.tomorrow_summary.min_price,
            s_runtime_state.tomorrow_summary.avg_price,
            s_runtime_state.tomorrow_summary.max_price
        );
    } else {
        snprintf(
            detail_text,
            sizeof(detail_text),
            "Today %u slots min %.2f avg %.2f max %.2f | Tomorrow pending publication",
            (unsigned int)s_runtime_state.today_summary.slot_count,
            s_runtime_state.today_summary.min_price,
            s_runtime_state.today_summary.avg_price,
            s_runtime_state.today_summary.max_price
        );
    }

    format_refresh_time(updated_text, sizeof(updated_text), s_last_success_time);
    populate_day_view(s_runtime_state.today_summary.date_key, &s_runtime_state.today_summary, &today_view);
    populate_day_view(s_runtime_state.tomorrow_summary.date_key, &s_runtime_state.tomorrow_summary, &tomorrow_view);
    app_state_set_tariff_status(s_state, s_tariff_status, true, s_runtime_state.has_tomorrow, status_text);
    app_state_set_tariff_snapshot(s_state, current_text, next_text, detail_text, updated_text);
    app_state_set_tariff_primary(
        s_state,
        current_slot != NULL && current_block != NULL,
        current_slot != NULL ? current_slot->price_including_vat : 0.0f,
        current_block != NULL ? current_block->band : TARIFF_BAND_NORMAL,
        current_block != NULL ? current_block->end_local : 0,
        next_block_start_local,
        previews,
        preview_count
    );
    app_state_set_tariff_detail(s_state, &today_view, &tomorrow_view);
}

static void publish_waiting_status(const char *status_text)
{
    app_state_set_tariff_status(s_state, APP_TARIFF_STATUS_IDLE, false, false, status_text);
    app_state_set_tariff_snapshot(
        s_state,
        "Tariff data not loaded yet",
        "Next grouped periods appear after the first successful Octopus refresh",
        "Day summaries will appear here once tariff slots are available",
        "Not refreshed yet"
    );
    app_state_set_tariff_primary(s_state, false, 0.0f, TARIFF_BAND_NORMAL, 0, 0, NULL, 0);
    app_state_set_tariff_detail(s_state, NULL, NULL);
    s_tariff_status = APP_TARIFF_STATUS_IDLE;
}

static void publish_offline_status(const char *status_text)
{
    app_state_set_tariff_status(s_state, APP_TARIFF_STATUS_OFFLINE, false, false, status_text);
    app_state_set_tariff_snapshot(
        s_state,
        "Offline: unable to load Agile prices during startup",
        "Reconnect Wi-Fi or retry later to populate upcoming grouped blocks",
        "No tariff dataset is held on flash. The UI is intentionally showing offline state instead of stale data.",
        "Startup fetch failed"
    );
    app_state_set_tariff_primary(s_state, false, 0.0f, TARIFF_BAND_NORMAL, 0, 0, NULL, 0);
    app_state_set_tariff_detail(s_state, NULL, NULL);
    s_tariff_status = APP_TARIFF_STATUS_OFFLINE;
}

static esp_err_t refresh_tariffs(time_t now_local)
{
    size_t fetched_slot_count = 0;
    esp_err_t err = ESP_OK;
    char requested_region[APP_SETTINGS_REGION_CODE_MAX_LEN + 1] = {0};

    get_requested_region_code(requested_region, sizeof(requested_region));

    s_last_attempt_time = now_local;
    app_state_set_tariff_status(s_state, APP_TARIFF_STATUS_LOADING, s_has_successful_load, s_runtime_state.has_tomorrow, "Fetching Octopus Agile prices");

    memset(s_fetched_slots, 0, sizeof(s_fetched_slots));
    memset(&s_next_runtime_state, 0, sizeof(s_next_runtime_state));

    err = octopus_client_fetch_tariffs(requested_region, now_local, s_fetched_slots, TARIFF_MODEL_MAX_SLOTS, &fetched_slot_count);
    if (err != ESP_OK) {
        return err;
    }

    if (!tariff_model_build(s_fetched_slots, fetched_slot_count, now_local, &s_next_runtime_state)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    time_t latest_end_local = 0;
    time_t latest_tomorrow_end_local = 0;
    char latest_end_text[24] = {0};
    char latest_tomorrow_end_text[24] = {0};
    int tomorrow_key = tariff_model_get_local_day_key(now_local + (24 * 60 * 60));

    for (size_t index = 0; index < fetched_slot_count; index++) {
        if (s_fetched_slots[index].end_local > latest_end_local) {
            latest_end_local = s_fetched_slots[index].end_local;
        }
    }

    for (size_t index = 0; index < s_next_runtime_state.slot_count; index++) {
        const tariff_slot_t *slot = &s_next_runtime_state.slots[index];

        if (tariff_model_get_local_day_key(slot->start_local) == tomorrow_key && slot->end_local > latest_tomorrow_end_local) {
            latest_tomorrow_end_local = slot->end_local;
        }
    }

    format_log_timestamp(latest_end_text, sizeof(latest_end_text), latest_end_local);
    format_log_timestamp(latest_tomorrow_end_text, sizeof(latest_tomorrow_end_text), latest_tomorrow_end_local);

    s_runtime_state = s_next_runtime_state;
    strlcpy(s_active_region_code, requested_region, sizeof(s_active_region_code));
    s_has_successful_load = true;
    s_last_success_time = now_local;
    s_loaded_today_key = tariff_model_get_local_day_key(now_local);
    s_tariff_status = APP_TARIFF_STATUS_READY;
    s_refresh_requested = false;
    publish_runtime_snapshot(now_local);
    ESP_LOGI(
        TAG,
        "Tariff refresh ready: %u slots, %u blocks, today=%u slots, tomorrow=%s, latest_end=%s, tomorrow_end=%s",
        (unsigned int)s_runtime_state.slot_count,
        (unsigned int)s_runtime_state.block_count,
        (unsigned int)s_runtime_state.today_summary.slot_count,
        s_runtime_state.has_tomorrow ? "yes" : "no",
        latest_end_text,
        latest_tomorrow_end_local > 0 ? latest_tomorrow_end_text : "n/a"
    );
    return ESP_OK;
}

static bool should_fetch_tomorrow(time_t now_local)
{
    struct tm local_tm = {0};

    if (!s_has_successful_load || s_runtime_state.has_tomorrow) {
        return false;
    }

    localtime_r(&now_local, &local_tm);
    if (local_tm.tm_hour < 16) {
        return false;
    }

    return (s_last_attempt_time == 0) || ((now_local - s_last_attempt_time) >= SYNC_CONTROLLER_TOMORROW_RETRY_SECONDS);
}

static bool should_retry_requested_refresh(time_t now_local)
{
    if (!s_refresh_requested) {
        return false;
    }

    return (s_last_attempt_time == 0) || ((now_local - s_last_attempt_time) >= SYNC_CONTROLLER_REFRESH_RETRY_SECONDS);
}

static void sync_controller_task(void *arg)
{
    (void)arg;

    while (true) {
        time_t now_local = time(NULL);

        if (s_state == NULL) {
            vTaskDelay(pdMS_TO_TICKS(SYNC_CONTROLLER_POLL_INTERVAL_MS));
            continue;
        }

        if (!wifi_manager_is_connected()) {
            if (!s_has_successful_load) {
                publish_waiting_status("Waiting for Wi-Fi before tariff fetch");
            }
            vTaskDelay(pdMS_TO_TICKS(SYNC_CONTROLLER_POLL_INTERVAL_MS));
            continue;
        }

        if (!app_state_get_time_valid(s_state)) {
            if (!s_has_successful_load) {
                publish_waiting_status("Waiting for valid local time before tariff fetch");
            }
            vTaskDelay(pdMS_TO_TICKS(SYNC_CONTROLLER_POLL_INTERVAL_MS));
            continue;
        }

        if (!s_has_successful_load || s_loaded_today_key != tariff_model_get_local_day_key(now_local) || should_fetch_tomorrow(now_local) || should_retry_requested_refresh(now_local)) {
            esp_err_t refresh_err = refresh_tariffs(now_local);
            if (refresh_err != ESP_OK) {
                ESP_LOGW(TAG, "Tariff refresh failed: %s", esp_err_to_name(refresh_err));
                if (!s_has_successful_load) {
                    publish_offline_status("Startup tariff fetch failed. Showing offline status instead of stale data");
                } else {
                    s_tariff_status = APP_TARIFF_STATUS_STALE;
                    publish_runtime_snapshot(now_local);
                }
            }
        } else if (s_has_successful_load) {
            publish_runtime_snapshot(now_local);
        }

        vTaskDelay(pdMS_TO_TICKS(SYNC_CONTROLLER_POLL_INTERVAL_MS));
    }
}

esp_err_t sync_controller_init(app_state_t *state)
{
    BaseType_t task_created = pdFALSE;

    s_state = state;
    task_created = xTaskCreatePinnedToCore(
        sync_controller_task,
        "sync_controller",
        SYNC_CONTROLLER_STACK_SIZE,
        NULL,
        5,
        NULL,
        tskNO_AFFINITY
    );
    ESP_RETURN_ON_FALSE(task_created == pdPASS, ESP_ERR_NO_MEM, TAG, "create sync controller task");

    return ESP_OK;
}

void sync_controller_request_refresh(void)
{
    s_last_attempt_time = 0;
    s_refresh_requested = true;

    if (s_state == NULL) {
        return;
    }

    if (!wifi_manager_is_connected()) {
        if (!s_has_successful_load) {
            publish_waiting_status("Region changed. Waiting for Wi-Fi before tariff fetch");
        }
        return;
    }

    if (!app_state_get_time_valid(s_state)) {
        if (!s_has_successful_load) {
            publish_waiting_status("Region changed. Waiting for valid local time before tariff fetch");
        }
        return;
    }

    app_state_set_tariff_status(
        s_state,
        APP_TARIFF_STATUS_LOADING,
        s_has_successful_load,
        s_runtime_state.has_tomorrow,
        s_has_successful_load
            ? "Region changed. Refreshing Octopus Agile prices while keeping the last good dataset active"
            : "Region changed. Refreshing Octopus Agile prices"
    );
}

#ifdef GREENLIGHT_HOST_TEST
#include "sync_controller_internal.h"

void sync_controller_test_reset(app_state_t *state, app_settings_t *settings, const char *region_code)
{
    memset(&s_runtime_state, 0, sizeof(s_runtime_state));
    memset(&s_next_runtime_state, 0, sizeof(s_next_runtime_state));
    memset(s_fetched_slots, 0, sizeof(s_fetched_slots));
    memset(s_active_region_code, 0, sizeof(s_active_region_code));

    s_state = NULL;
    s_has_successful_load = false;
    s_last_attempt_time = 0;
    s_last_success_time = 0;
    s_loaded_today_key = 0;
    s_tariff_status = APP_TARIFF_STATUS_IDLE;
    s_refresh_requested = false;

    memset(settings, 0, sizeof(*settings));
    strlcpy(settings->region_code, region_code, sizeof(settings->region_code));
    settings->brightness_percent = APP_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT;

    app_state_init(state, settings);
    app_state_set_time_status(state, APP_TIME_STATUS_VALID, true, "Time synchronized");
    s_state = state;
}

esp_err_t sync_controller_test_refresh_tariffs(time_t now_local)
{
    return refresh_tariffs(now_local);
}

void sync_controller_test_publish_runtime_snapshot(time_t now_local)
{
    publish_runtime_snapshot(now_local);
}

void sync_controller_test_set_tariff_status(app_tariff_status_t status)
{
    s_tariff_status = status;
}

const runtime_tariff_state_t *sync_controller_test_get_runtime_state(void)
{
    return &s_runtime_state;
}

bool sync_controller_test_get_refresh_requested(void)
{
    return s_refresh_requested;
}

bool sync_controller_test_get_has_successful_load(void)
{
    return s_has_successful_load;
}

const char *sync_controller_test_get_active_region_code(void)
{
    return get_active_region_code();
}
#endif