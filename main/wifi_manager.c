#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <esp_check.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include "app_settings.h"

#define WIFI_MANAGER_CONNECTED_BIT BIT0
#define WIFI_MANAGER_FAILED_BIT BIT1
#define WIFI_MANAGER_QUEUE_LENGTH 4
#define WIFI_MANAGER_CONNECT_TIMEOUT_MS 45000
#define WIFI_MANAGER_MAX_RETRIES 3

typedef enum {
    WIFI_MANAGER_COMMAND_SCAN = 0,
    WIFI_MANAGER_COMMAND_CONNECT,
} wifi_manager_command_type_t;

typedef struct {
    wifi_manager_command_type_t type;
    char ssid[APP_SETTINGS_WIFI_SSID_MAX_LEN + 1];
    char psk[APP_SETTINGS_WIFI_PSK_MAX_LEN + 1];
} wifi_manager_command_t;

static const char *TAG = "wifi_manager";

static app_state_t *s_state;
static esp_netif_t *s_sta_netif;
static QueueHandle_t s_command_queue;
static EventGroupHandle_t s_event_group;
static wifi_ap_record_t s_scan_records[APP_WIFI_SCAN_MAX_RESULTS];
static uint8_t s_scan_record_count;
static bool s_wifi_started;
static bool s_is_connected;
static bool s_connect_in_progress;
static uint8_t s_retry_count;
static wifi_err_reason_t s_last_disconnect_reason;
static char s_target_ssid[APP_SETTINGS_WIFI_SSID_MAX_LEN + 1];
static char s_target_psk[APP_SETTINGS_WIFI_PSK_MAX_LEN + 1];

static void wifi_manager_task(void *arg);
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

static const char *wifi_reason_to_string(wifi_err_reason_t reason)
{
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND:
            return "network not found";
        case WIFI_REASON_AUTH_FAIL:
            return "authentication failed";
        case WIFI_REASON_ASSOC_FAIL:
            return "association failed";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            return "handshake timeout";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "beacon timeout";
        case WIFI_REASON_CONNECTION_FAIL:
            return "connection failed";
        case WIFI_REASON_TIMEOUT:
            return "link timeout";
        default:
            return "disconnect";
    }
}

static const wifi_ap_record_t *find_scanned_ap(const char *ssid)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return NULL;
    }

    for (uint8_t index = 0; index < s_scan_record_count; index++) {
        if (strncmp((const char *)s_scan_records[index].ssid, ssid, sizeof(s_scan_records[index].ssid)) == 0) {
            return &s_scan_records[index];
        }
    }

    return NULL;
}

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

static void set_status_with_ssid(app_wifi_status_t status, const char *prefix, const char *ssid)
{
    char message[APP_WIFI_STATUS_TEXT_MAX_LEN] = {0};

    if (ssid != NULL && ssid[0] != '\0') {
        snprintf(message, sizeof(message), "%s %s", prefix, ssid);
    } else {
        copy_text(message, sizeof(message), prefix);
    }

    app_state_set_wifi_status(s_state, status, message);
}

static void persist_connected_settings(const char *ssid, const char *psk)
{
    copy_text(s_state->settings.wifi_ssid, sizeof(s_state->settings.wifi_ssid), ssid);
    copy_text(s_state->settings.wifi_psk, sizeof(s_state->settings.wifi_psk), psk);
    app_state_set_wifi_saved_credentials(s_state, ssid != NULL && ssid[0] != '\0');

    esp_err_t err = app_settings_save(&s_state->settings);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist Wi-Fi credentials: %s", esp_err_to_name(err));
    }
}

static void finish_scan_status(uint8_t result_count)
{
    char message[APP_WIFI_STATUS_TEXT_MAX_LEN] = {0};

    if (s_is_connected) {
        snprintf(message, sizeof(message), "Connected to %s", s_state->wifi_connected_ssid);
        app_state_set_wifi_status(s_state, APP_WIFI_STATUS_CONNECTED, message);
        return;
    }

    if (result_count == 0) {
        app_state_set_wifi_status(s_state, APP_WIFI_STATUS_IDLE, "No Wi-Fi networks found");
        return;
    }

    snprintf(message, sizeof(message), "Found %u Wi-Fi network%s", (unsigned int)result_count, result_count == 1 ? "" : "s");
    app_state_set_wifi_status(s_state, APP_WIFI_STATUS_IDLE, message);
}

static esp_err_t execute_scan(void)
{
    wifi_scan_config_t scan_config = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    wifi_ap_record_t records[APP_WIFI_SCAN_MAX_RESULTS] = {0};
    app_wifi_network_t scan_results[APP_WIFI_SCAN_MAX_RESULTS] = {0};
    uint16_t ap_count = APP_WIFI_SCAN_MAX_RESULTS;

    app_state_set_active_screen(s_state, APP_SCREEN_SETTINGS);
    app_state_set_wifi_status(s_state, APP_WIFI_STATUS_SCANNING, "Scanning nearby Wi-Fi networks");

    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan_config, true), TAG, "start Wi-Fi scan");
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_records(&ap_count, records), TAG, "read Wi-Fi scan results");

    memset(s_scan_records, 0, sizeof(s_scan_records));
    memcpy(s_scan_records, records, sizeof(records[0]) * ap_count);
    s_scan_record_count = (uint8_t)ap_count;

    for (uint16_t index = 0; index < ap_count; index++) {
        copy_text(scan_results[index].ssid, sizeof(scan_results[index].ssid), (const char *)records[index].ssid);
        scan_results[index].rssi = records[index].rssi;
        scan_results[index].secure = records[index].authmode != WIFI_AUTH_OPEN;
    }

    app_state_set_wifi_scan_results(s_state, scan_results, (uint8_t)ap_count);
    finish_scan_status((uint8_t)ap_count);
    return ESP_OK;
}

static esp_err_t execute_connect(const char *ssid, const char *psk)
{
    EventBits_t bits = 0;
    wifi_config_t config = {0};
    const wifi_ap_record_t *selected_ap = NULL;

    if (ssid == NULL || ssid[0] == '\0') {
        app_state_set_active_screen(s_state, APP_SCREEN_SETTINGS);
        app_state_set_wifi_status(s_state, APP_WIFI_STATUS_FAILED, "Enter an SSID before connecting");
        return ESP_ERR_INVALID_ARG;
    }

    copy_text((char *)config.sta.ssid, sizeof(config.sta.ssid), ssid);
    copy_text((char *)config.sta.password, sizeof(config.sta.password), psk);
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    selected_ap = find_scanned_ap(ssid);
    if (selected_ap != NULL && selected_ap->primary > 0) {
        config.sta.channel = selected_ap->primary;
    }

    copy_text(s_target_ssid, sizeof(s_target_ssid), ssid);
    copy_text(s_target_psk, sizeof(s_target_psk), psk);

    s_retry_count = 0;
    s_connect_in_progress = true;
    s_is_connected = false;
    s_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
    xEventGroupClearBits(s_event_group, WIFI_MANAGER_CONNECTED_BIT | WIFI_MANAGER_FAILED_BIT);

    app_state_set_active_screen(s_state, APP_SCREEN_SETTINGS);
    app_state_set_wifi_connection(s_state, ssid, "");
    set_status_with_ssid(APP_WIFI_STATUS_CONNECTING, "Connecting to", ssid);
    app_state_set_time_status(s_state, APP_TIME_STATUS_IDLE, false, "Waiting for Wi-Fi before time sync");
    app_state_set_local_time_text(s_state, "Time unavailable");

    (void)esp_wifi_disconnect();
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "apply station config");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "start Wi-Fi connect");

    bits = xEventGroupWaitBits(
        s_event_group,
        WIFI_MANAGER_CONNECTED_BIT | WIFI_MANAGER_FAILED_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(WIFI_MANAGER_CONNECT_TIMEOUT_MS)
    );

    s_connect_in_progress = false;

    if ((bits & WIFI_MANAGER_CONNECTED_BIT) != 0) {
        persist_connected_settings(ssid, psk);
        return ESP_OK;
    }

    if ((bits & WIFI_MANAGER_FAILED_BIT) != 0) {
        return ESP_FAIL;
    }

    app_state_set_active_screen(s_state, APP_SCREEN_SETTINGS);
    app_state_set_wifi_status(s_state, APP_WIFI_STATUS_FAILED, "Wi-Fi connection timed out after 45s");
    return ESP_ERR_TIMEOUT;
}

static void wifi_manager_task(void *arg)
{
    wifi_manager_command_t command = {0};

    (void)arg;

    while (true) {
        if (xQueueReceive(s_command_queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (command.type) {
            case WIFI_MANAGER_COMMAND_SCAN:
                if (execute_scan() != ESP_OK) {
                    app_state_set_wifi_status(s_state, APP_WIFI_STATUS_FAILED, "Wi-Fi scan failed");
                }
                break;

            case WIFI_MANAGER_COMMAND_CONNECT:
                if (execute_connect(command.ssid, command.psk) != ESP_OK && !s_connect_in_progress) {
                    app_state_set_active_screen(s_state, APP_SCREEN_SETTINGS);
                }
                break;

            default:
                break;
        }
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnect_event = (wifi_event_sta_disconnected_t *)event_data;
        char message[APP_WIFI_STATUS_TEXT_MAX_LEN] = {0};

        s_is_connected = false;
        s_last_disconnect_reason = disconnect_event->reason;
        app_state_set_wifi_connection(s_state, "", "");

        if (s_connect_in_progress && s_retry_count < WIFI_MANAGER_MAX_RETRIES) {
            s_retry_count++;
            snprintf(
                message,
                sizeof(message),
                "Retrying %s (%u/%u): %s",
                s_target_ssid,
                (unsigned int)s_retry_count,
                (unsigned int)WIFI_MANAGER_MAX_RETRIES,
                wifi_reason_to_string(disconnect_event->reason)
            );
            app_state_set_wifi_status(s_state, APP_WIFI_STATUS_CONNECTING, message);
            (void)esp_wifi_connect();
            return;
        }

        snprintf(
            message,
            sizeof(message),
            "Wi-Fi failed: %s (%d)",
            wifi_reason_to_string(disconnect_event->reason),
            disconnect_event->reason
        );
        app_state_set_wifi_status(s_state, APP_WIFI_STATUS_FAILED, message);
        app_state_set_active_screen(s_state, APP_SCREEN_SETTINGS);
        xEventGroupSetBits(s_event_group, WIFI_MANAGER_FAILED_BIT);
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ip_event = (ip_event_got_ip_t *)event_data;
        char ip_address[16] = {0};
        char message[APP_WIFI_STATUS_TEXT_MAX_LEN] = {0};

        snprintf(ip_address, sizeof(ip_address), IPSTR, IP2STR(&ip_event->ip_info.ip));

        s_is_connected = true;
        app_state_set_wifi_connection(s_state, s_target_ssid, ip_address);
        snprintf(message, sizeof(message), "Connected to %s", s_target_ssid);
        app_state_set_wifi_status(s_state, APP_WIFI_STATUS_CONNECTED, message);
        app_state_set_time_status(s_state, APP_TIME_STATUS_IDLE, false, "Wi-Fi connected. Starting time sync");
        xEventGroupSetBits(s_event_group, WIFI_MANAGER_CONNECTED_BIT);
    }
}

esp_err_t wifi_manager_init(app_state_t *state)
{
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();

    s_state = state;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        ESP_RETURN_ON_FALSE(s_sta_netif != NULL, ESP_ERR_NO_MEM, TAG, "create default station netif");
    }

    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init_config), TAG, "initialize Wi-Fi stack");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL), TAG, "register Wi-Fi event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL), TAG, "register IP event handler");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set Wi-Fi storage mode");

    s_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_event_group != NULL, ESP_ERR_NO_MEM, TAG, "create Wi-Fi event group");

    s_command_queue = xQueueCreate(WIFI_MANAGER_QUEUE_LENGTH, sizeof(wifi_manager_command_t));
    ESP_RETURN_ON_FALSE(s_command_queue != NULL, ESP_ERR_NO_MEM, TAG, "create Wi-Fi command queue");

    BaseType_t task_created = xTaskCreatePinnedToCore(wifi_manager_task, "wifi_manager", 4096, NULL, 5, NULL, tskNO_AFFINITY);
    ESP_RETURN_ON_FALSE(task_created == pdPASS, ESP_ERR_NO_MEM, TAG, "create Wi-Fi task");

    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    if (s_wifi_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start Wi-Fi driver");
    s_wifi_started = true;
    return ESP_OK;
}

esp_err_t wifi_manager_request_scan(void)
{
    wifi_manager_command_t command = {
        .type = WIFI_MANAGER_COMMAND_SCAN,
    };

    ESP_RETURN_ON_FALSE(s_command_queue != NULL, ESP_ERR_INVALID_STATE, TAG, "Wi-Fi manager not initialized");
    return xQueueSend(s_command_queue, &command, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t wifi_manager_request_connect(const char *ssid, const char *psk)
{
    wifi_manager_command_t command = {
        .type = WIFI_MANAGER_COMMAND_CONNECT,
    };

    ESP_RETURN_ON_FALSE(s_command_queue != NULL, ESP_ERR_INVALID_STATE, TAG, "Wi-Fi manager not initialized");

    copy_text(command.ssid, sizeof(command.ssid), ssid);
    copy_text(command.psk, sizeof(command.psk), psk);

    return xQueueSend(s_command_queue, &command, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool wifi_manager_is_connected(void)
{
    return s_is_connected;
}