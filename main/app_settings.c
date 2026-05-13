#include "app_settings.h"

#include <string.h>

#include <esp_check.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

static const char *TAG = "app_settings";
static const char *SETTINGS_NAMESPACE = "settings";
static const char *KEY_WIFI_SSID = "wifi_ssid";
static const char *KEY_WIFI_PSK = "wifi_psk";
static const char *KEY_REGION_CODE = "region";
static const char *KEY_BRIGHTNESS = "brightness";

static uint8_t clamp_brightness(uint8_t brightness_percent)
{
    if (brightness_percent > 100) {
        return APP_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT;
    }

    return brightness_percent;
}

static esp_err_t load_string_setting(nvs_handle_t handle, const char *key, char *value, size_t value_size)
{
    size_t required_size = value_size;
    esp_err_t err = nvs_get_str(handle, key, value, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value[0] = '\0';
        return ESP_OK;
    }

    return err;
}

void app_settings_set_defaults(app_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    memcpy(settings->region_code, "B", 2);
    settings->brightness_percent = APP_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT;
}

esp_err_t app_settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS flash");
        err = nvs_flash_init();
    }

    return err;
}

esp_err_t app_settings_load(app_settings_t *settings)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = ESP_OK;
    uint8_t brightness_percent = APP_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT;

    app_settings_set_defaults(settings);

    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "open settings namespace");

    ESP_GOTO_ON_ERROR(load_string_setting(handle, KEY_WIFI_SSID, settings->wifi_ssid, sizeof(settings->wifi_ssid)), cleanup, TAG, "load SSID");
    ESP_GOTO_ON_ERROR(load_string_setting(handle, KEY_WIFI_PSK, settings->wifi_psk, sizeof(settings->wifi_psk)), cleanup, TAG, "load PSK");
    ESP_GOTO_ON_ERROR(load_string_setting(handle, KEY_REGION_CODE, settings->region_code, sizeof(settings->region_code)), cleanup, TAG, "load region code");

    err = nvs_get_u8(handle, KEY_BRIGHTNESS, &brightness_percent);
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_GOTO_ON_ERROR(err, cleanup, TAG, "load brightness");
        settings->brightness_percent = clamp_brightness(brightness_percent);
    }

cleanup:
    nvs_close(handle);
    return ret;
}

esp_err_t app_settings_save(const app_settings_t *settings)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "open settings namespace");

    ESP_GOTO_ON_ERROR(nvs_set_str(handle, KEY_WIFI_SSID, settings->wifi_ssid), cleanup, TAG, "save SSID");
    ESP_GOTO_ON_ERROR(nvs_set_str(handle, KEY_WIFI_PSK, settings->wifi_psk), cleanup, TAG, "save PSK");
    ESP_GOTO_ON_ERROR(nvs_set_str(handle, KEY_REGION_CODE, settings->region_code), cleanup, TAG, "save region code");
    ESP_GOTO_ON_ERROR(
        nvs_set_u8(handle, KEY_BRIGHTNESS, clamp_brightness(settings->brightness_percent)),
        cleanup,
        TAG,
        "save brightness"
    );
    ESP_GOTO_ON_ERROR(nvs_commit(handle), cleanup, TAG, "commit settings");

cleanup:
    nvs_close(handle);
    return ret;
}

esp_err_t app_settings_save_brightness(uint8_t brightness_percent)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "open settings namespace");

    ESP_GOTO_ON_ERROR(nvs_set_u8(handle, KEY_BRIGHTNESS, clamp_brightness(brightness_percent)), cleanup, TAG, "save brightness");
    ESP_GOTO_ON_ERROR(nvs_commit(handle), cleanup, TAG, "commit brightness");

cleanup:
    nvs_close(handle);
    return ret;
}