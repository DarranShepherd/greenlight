#include "app_state.h"

#include <string.h>

static void copy_text(char *destination, size_t destination_size, const char *source)
{
    if (destination_size == 0) {
        return;
    }

    if (source == NULL) {
        destination[0] = '\0';
        return;
    }

    strlcpy(destination, source, destination_size);
}

void app_state_init(app_state_t *state, const app_settings_t *settings)
{
    memset(state, 0, sizeof(*state));
    state->settings = *settings;
    state->active_screen = APP_SCREEN_PRIMARY;
    state->wifi_has_saved_credentials = settings->wifi_ssid[0] != '\0';
    app_state_set_wifi_status(state, APP_WIFI_STATUS_IDLE, state->wifi_has_saved_credentials ? "Saved Wi-Fi ready" : "Enter Wi-Fi credentials to get started");
    app_state_set_time_status(state, APP_TIME_STATUS_IDLE, false, "Waiting for Wi-Fi before time sync");
    app_state_set_local_time_text(state, "Time unavailable");
    app_state_set_tariff_status(state, APP_TARIFF_STATUS_IDLE, false, false, "Waiting for valid local time before tariff fetch");
    app_state_set_tariff_snapshot(
        state,
        "Tariff data not loaded yet",
        "Next grouped periods appear after the first successful Octopus refresh",
        "Day summaries will appear here once tariff slots are available",
        "Not refreshed yet"
    );
    app_state_set_tariff_primary(state, false, 0.0f, TARIFF_BAND_NORMAL, 0, 0, NULL, 0);
}

void app_state_set_active_screen(app_state_t *state, app_screen_t screen)
{
    if (screen < APP_SCREEN_COUNT) {
        state->active_screen = screen;
    }
}

void app_state_set_brightness(app_state_t *state, uint8_t brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = APP_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT;
    }

    state->settings.brightness_percent = brightness_percent;
}

void app_state_set_uptime(app_state_t *state, uint32_t uptime_seconds)
{
    state->uptime_seconds = uptime_seconds;
}

void app_state_set_wifi_saved_credentials(app_state_t *state, bool has_saved_credentials)
{
    state->wifi_has_saved_credentials = has_saved_credentials;
}

void app_state_set_wifi_status(app_state_t *state, app_wifi_status_t status, const char *status_text)
{
    state->wifi_status = status;
    copy_text(state->wifi_status_text, sizeof(state->wifi_status_text), status_text);

    if (status != APP_WIFI_STATUS_CONNECTED) {
        state->wifi_ip_address[0] = '\0';
    }
}

void app_state_set_wifi_connection(app_state_t *state, const char *ssid, const char *ip_address)
{
    copy_text(state->wifi_connected_ssid, sizeof(state->wifi_connected_ssid), ssid);
    copy_text(state->wifi_ip_address, sizeof(state->wifi_ip_address), ip_address);
}

void app_state_set_wifi_scan_results(app_state_t *state, const app_wifi_network_t *results, uint8_t result_count)
{
    if (result_count > APP_WIFI_SCAN_MAX_RESULTS) {
        result_count = APP_WIFI_SCAN_MAX_RESULTS;
    }

    if (result_count > 0 && results != NULL) {
        memcpy(state->wifi_scan_results, results, sizeof(state->wifi_scan_results[0]) * result_count);
    }

    if (result_count < APP_WIFI_SCAN_MAX_RESULTS) {
        memset(&state->wifi_scan_results[result_count], 0, sizeof(state->wifi_scan_results[0]) * (APP_WIFI_SCAN_MAX_RESULTS - result_count));
    }

    state->wifi_scan_result_count = result_count;
}

void app_state_set_time_status(app_state_t *state, app_time_status_t status, bool time_valid, const char *status_text)
{
    state->time_status = status;
    state->time_valid = time_valid;
    copy_text(state->time_status_text, sizeof(state->time_status_text), status_text);
}

void app_state_set_local_time_text(app_state_t *state, const char *local_time_text)
{
    copy_text(state->local_time_text, sizeof(state->local_time_text), local_time_text);
}

void app_state_set_tariff_status(
    app_state_t *state,
    app_tariff_status_t status,
    bool has_data,
    bool tomorrow_available,
    const char *status_text
)
{
    state->tariff_status = status;
    state->tariff_has_data = has_data;
    state->tariff_tomorrow_available = tomorrow_available;
    copy_text(state->tariff_status_text, sizeof(state->tariff_status_text), status_text);
}

void app_state_set_tariff_snapshot(
    app_state_t *state,
    const char *current_text,
    const char *next_text,
    const char *detail_text,
    const char *updated_text
)
{
    copy_text(state->tariff_current_text, sizeof(state->tariff_current_text), current_text);
    copy_text(state->tariff_next_text, sizeof(state->tariff_next_text), next_text);
    copy_text(state->tariff_detail_text, sizeof(state->tariff_detail_text), detail_text);
    copy_text(state->tariff_updated_text, sizeof(state->tariff_updated_text), updated_text);
}

void app_state_set_tariff_primary(
    app_state_t *state,
    bool current_block_valid,
    float current_price,
    tariff_band_t current_band,
    time_t current_block_end_local,
    time_t next_block_start_local,
    const app_tariff_preview_t *previews,
    uint8_t preview_count
)
{
    state->tariff_current_block_valid = current_block_valid;
    state->tariff_current_price = current_price;
    state->tariff_current_band = current_band;
    state->tariff_current_block_end_local = current_block_end_local;
    state->tariff_next_block_start_local = next_block_start_local;

    if (preview_count > APP_TARIFF_PREVIEW_MAX) {
        preview_count = APP_TARIFF_PREVIEW_MAX;
    }

    if (preview_count > 0 && previews != NULL) {
        memcpy(state->tariff_previews, previews, sizeof(state->tariff_previews[0]) * preview_count);
    }

    if (preview_count < APP_TARIFF_PREVIEW_MAX) {
        memset(&state->tariff_previews[preview_count], 0, sizeof(state->tariff_previews[0]) * (APP_TARIFF_PREVIEW_MAX - preview_count));
    }

    state->tariff_preview_count = preview_count;
}

const char *app_state_get_screen_name(app_screen_t screen)
{
    switch (screen) {
        case APP_SCREEN_PRIMARY:
            return "Primary";
        case APP_SCREEN_DETAIL:
            return "Detail";
        case APP_SCREEN_SETTINGS:
            return "Settings";
        case APP_SCREEN_COUNT:
        default:
            return "Unknown";
    }
}

const char *app_state_get_wifi_status_name(app_wifi_status_t status)
{
    switch (status) {
        case APP_WIFI_STATUS_IDLE:
            return "Idle";
        case APP_WIFI_STATUS_SCANNING:
            return "Scanning";
        case APP_WIFI_STATUS_CONNECTING:
            return "Connecting";
        case APP_WIFI_STATUS_CONNECTED:
            return "Connected";
        case APP_WIFI_STATUS_FAILED:
            return "Failed";
        default:
            return "Unknown";
    }
}

const char *app_state_get_time_status_name(app_time_status_t status)
{
    switch (status) {
        case APP_TIME_STATUS_IDLE:
            return "Idle";
        case APP_TIME_STATUS_SYNCING:
            return "Syncing";
        case APP_TIME_STATUS_VALID:
            return "Valid";
        case APP_TIME_STATUS_ERROR:
            return "Error";
        default:
            return "Unknown";
    }
}

const char *app_state_get_tariff_status_name(app_tariff_status_t status)
{
    switch (status) {
        case APP_TARIFF_STATUS_IDLE:
            return "Idle";
        case APP_TARIFF_STATUS_LOADING:
            return "Loading";
        case APP_TARIFF_STATUS_READY:
            return "Ready";
        case APP_TARIFF_STATUS_STALE:
            return "Stale";
        case APP_TARIFF_STATUS_OFFLINE:
            return "Offline";
        default:
            return "Unknown";
    }
}