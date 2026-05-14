#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <esp_err.h>

#define APP_SETTINGS_WIFI_SSID_MAX_LEN 32
#define APP_SETTINGS_WIFI_PSK_MAX_LEN 64
#define APP_SETTINGS_REGION_CODE_MAX_LEN 8
#define APP_SETTINGS_MIN_BRIGHTNESS_PERCENT 10
#define APP_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT 80
#define APP_TOUCH_CALIBRATION_SCALE 1000

typedef struct {
    bool valid;
    int32_t xx;
    int32_t xy;
    int32_t x_offset;
    int32_t yx;
    int32_t yy;
    int32_t y_offset;
} app_touch_calibration_t;

/*
 * Wi-Fi credentials are persisted in the device's NVS settings namespace so the
 * station can reconnect after reboot. On ESP32 this is practical but not a hard
 * secret store, so the UI intentionally does not repopulate the saved PSK even
 * though the runtime still loads it for reconnect.
 */
typedef struct {
    char wifi_ssid[APP_SETTINGS_WIFI_SSID_MAX_LEN + 1];
    char wifi_psk[APP_SETTINGS_WIFI_PSK_MAX_LEN + 1];
    char region_code[APP_SETTINGS_REGION_CODE_MAX_LEN + 1];
    uint8_t brightness_percent;
    app_touch_calibration_t touch_calibration;
} app_settings_t;

void app_settings_set_defaults(app_settings_t *settings);
esp_err_t app_settings_init(void);
esp_err_t app_settings_load(app_settings_t *settings);
esp_err_t app_settings_save(const app_settings_t *settings);
esp_err_t app_settings_save_brightness(uint8_t brightness_percent);
esp_err_t app_settings_save_touch_calibration(const app_touch_calibration_t *calibration);