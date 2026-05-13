#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_settings.h"

#define APP_WIFI_SCAN_MAX_RESULTS 12
#define APP_WIFI_STATUS_TEXT_MAX_LEN 96
#define APP_TIME_STATUS_TEXT_MAX_LEN 96
#define APP_TIME_LOCAL_TEXT_MAX_LEN 32
#define APP_TARIFF_STATUS_TEXT_MAX_LEN 128
#define APP_TARIFF_SNAPSHOT_TEXT_MAX_LEN 192
#define APP_TARIFF_UPDATED_TEXT_MAX_LEN 48

typedef enum {
    APP_WIFI_STATUS_IDLE = 0,
    APP_WIFI_STATUS_SCANNING,
    APP_WIFI_STATUS_CONNECTING,
    APP_WIFI_STATUS_CONNECTED,
    APP_WIFI_STATUS_FAILED,
} app_wifi_status_t;

typedef enum {
    APP_TIME_STATUS_IDLE = 0,
    APP_TIME_STATUS_SYNCING,
    APP_TIME_STATUS_VALID,
    APP_TIME_STATUS_ERROR,
} app_time_status_t;

typedef enum {
    APP_TARIFF_STATUS_IDLE = 0,
    APP_TARIFF_STATUS_LOADING,
    APP_TARIFF_STATUS_READY,
    APP_TARIFF_STATUS_STALE,
    APP_TARIFF_STATUS_OFFLINE,
} app_tariff_status_t;

typedef struct {
    char ssid[APP_SETTINGS_WIFI_SSID_MAX_LEN + 1];
    int8_t rssi;
    bool secure;
} app_wifi_network_t;

typedef enum {
    APP_SCREEN_PRIMARY = 0,
    APP_SCREEN_DETAIL,
    APP_SCREEN_SETTINGS,
    APP_SCREEN_COUNT,
} app_screen_t;

typedef struct {
    app_settings_t settings;
    app_screen_t active_screen;
    uint32_t uptime_seconds;
    app_wifi_status_t wifi_status;
    bool wifi_has_saved_credentials;
    char wifi_status_text[APP_WIFI_STATUS_TEXT_MAX_LEN];
    char wifi_connected_ssid[APP_SETTINGS_WIFI_SSID_MAX_LEN + 1];
    char wifi_ip_address[16];
    app_wifi_network_t wifi_scan_results[APP_WIFI_SCAN_MAX_RESULTS];
    uint8_t wifi_scan_result_count;
    app_time_status_t time_status;
    bool time_valid;
    char time_status_text[APP_TIME_STATUS_TEXT_MAX_LEN];
    char local_time_text[APP_TIME_LOCAL_TEXT_MAX_LEN];
    app_tariff_status_t tariff_status;
    bool tariff_has_data;
    bool tariff_tomorrow_available;
    char tariff_status_text[APP_TARIFF_STATUS_TEXT_MAX_LEN];
    char tariff_current_text[APP_TARIFF_SNAPSHOT_TEXT_MAX_LEN];
    char tariff_next_text[APP_TARIFF_SNAPSHOT_TEXT_MAX_LEN];
    char tariff_detail_text[APP_TARIFF_SNAPSHOT_TEXT_MAX_LEN];
    char tariff_updated_text[APP_TARIFF_UPDATED_TEXT_MAX_LEN];
} app_state_t;

void app_state_init(app_state_t *state, const app_settings_t *settings);
void app_state_set_active_screen(app_state_t *state, app_screen_t screen);
void app_state_set_brightness(app_state_t *state, uint8_t brightness_percent);
void app_state_set_uptime(app_state_t *state, uint32_t uptime_seconds);
void app_state_set_wifi_saved_credentials(app_state_t *state, bool has_saved_credentials);
void app_state_set_wifi_status(app_state_t *state, app_wifi_status_t status, const char *status_text);
void app_state_set_wifi_connection(app_state_t *state, const char *ssid, const char *ip_address);
void app_state_set_wifi_scan_results(app_state_t *state, const app_wifi_network_t *results, uint8_t result_count);
void app_state_set_time_status(app_state_t *state, app_time_status_t status, bool time_valid, const char *status_text);
void app_state_set_local_time_text(app_state_t *state, const char *local_time_text);
void app_state_set_tariff_status(
    app_state_t *state,
    app_tariff_status_t status,
    bool has_data,
    bool tomorrow_available,
    const char *status_text
);
void app_state_set_tariff_snapshot(
    app_state_t *state,
    const char *current_text,
    const char *next_text,
    const char *detail_text,
    const char *updated_text
);
const char *app_state_get_screen_name(app_screen_t screen);
const char *app_state_get_wifi_status_name(app_wifi_status_t status);
const char *app_state_get_time_status_name(app_time_status_t status);
const char *app_state_get_tariff_status_name(app_tariff_status_t status);