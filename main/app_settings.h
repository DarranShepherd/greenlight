#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <esp_err.h>

#define APP_SETTINGS_WIFI_SSID_MAX_LEN 32
#define APP_SETTINGS_WIFI_PSK_MAX_LEN 64
#define APP_SETTINGS_REGION_CODE_MAX_LEN 8
#define APP_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT 80

typedef struct {
    char wifi_ssid[APP_SETTINGS_WIFI_SSID_MAX_LEN + 1];
    char wifi_psk[APP_SETTINGS_WIFI_PSK_MAX_LEN + 1];
    char region_code[APP_SETTINGS_REGION_CODE_MAX_LEN + 1];
    uint8_t brightness_percent;
} app_settings_t;

void app_settings_set_defaults(app_settings_t *settings);
esp_err_t app_settings_init(void);
esp_err_t app_settings_load(app_settings_t *settings);
esp_err_t app_settings_save(const app_settings_t *settings);
esp_err_t app_settings_save_brightness(uint8_t brightness_percent);