#include "app_settings.h"

#include <ctype.h>
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
static const char *KEY_TOUCH_CAL_VALID = "touch_cal_ok";
static const char *KEY_TOUCH_XX = "touch_xx";
static const char *KEY_TOUCH_XY = "touch_xy";
static const char *KEY_TOUCH_X_OFFSET = "touch_xoff";
static const char *KEY_TOUCH_YX = "touch_yx";
static const char *KEY_TOUCH_YY = "touch_yy";
static const char *KEY_TOUCH_Y_OFFSET = "touch_yoff";
/*
 * These defaults were captured from the IPistBit 3.2-inch board on /dev/ttyUSB0
 * and are only intended as a first-boot fallback for that hardware profile.
 * When board profiles land, the original 2.8-inch CYD should use its own
 * board-specific default calibration instead of reusing these coefficients.
 */
static const app_touch_calibration_t DEFAULT_TOUCH_CALIBRATION = {
    .valid = true,
    .xx = 1075,
    .xy = 15,
    .x_offset = -15253,
    .yx = 557,
    .yy = 1161,
    .y_offset = -44624,
};

static uint8_t clamp_brightness(uint8_t brightness_percent)
{
    if (brightness_percent < APP_SETTINGS_MIN_BRIGHTNESS_PERCENT) {
        return APP_SETTINGS_MIN_BRIGHTNESS_PERCENT;
    }

    if (brightness_percent > 100) {
        return APP_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT;
    }

    return brightness_percent;
}

static void normalize_region_code(char *region_code, size_t region_code_size)
{
    if (region_code == NULL || region_code_size == 0) {
        return;
    }

    if (region_code[0] == '\0') {
        strlcpy(region_code, "B", region_code_size);
        return;
    }

    region_code[0] = (char)toupper((unsigned char)region_code[0]);
    region_code[1] = '\0';
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

static esp_err_t load_i32_setting(nvs_handle_t handle, const char *key, int32_t *value)
{
    esp_err_t err = nvs_get_i32(handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }

    return err;
}

static esp_err_t store_touch_calibration(nvs_handle_t handle, const app_touch_calibration_t *calibration)
{
    uint8_t valid = calibration != NULL && calibration->valid ? 1 : 0;

    ESP_RETURN_ON_ERROR(nvs_set_u8(handle, KEY_TOUCH_CAL_VALID, valid), TAG, "save touch calibration valid flag");
    ESP_RETURN_ON_ERROR(nvs_set_i32(handle, KEY_TOUCH_XX, calibration != NULL ? calibration->xx : 0), TAG, "save touch xx");
    ESP_RETURN_ON_ERROR(nvs_set_i32(handle, KEY_TOUCH_XY, calibration != NULL ? calibration->xy : 0), TAG, "save touch xy");
    ESP_RETURN_ON_ERROR(nvs_set_i32(handle, KEY_TOUCH_X_OFFSET, calibration != NULL ? calibration->x_offset : 0), TAG, "save touch x offset");
    ESP_RETURN_ON_ERROR(nvs_set_i32(handle, KEY_TOUCH_YX, calibration != NULL ? calibration->yx : 0), TAG, "save touch yx");
    ESP_RETURN_ON_ERROR(nvs_set_i32(handle, KEY_TOUCH_YY, calibration != NULL ? calibration->yy : 0), TAG, "save touch yy");
    ESP_RETURN_ON_ERROR(nvs_set_i32(handle, KEY_TOUCH_Y_OFFSET, calibration != NULL ? calibration->y_offset : 0), TAG, "save touch y offset");
    return ESP_OK;
}

void app_settings_set_defaults(app_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    memcpy(settings->region_code, "B", 2);
    settings->brightness_percent = APP_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT;
    settings->touch_calibration = DEFAULT_TOUCH_CALIBRATION;
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
    normalize_region_code(settings->region_code, sizeof(settings->region_code));

    err = nvs_get_u8(handle, KEY_BRIGHTNESS, &brightness_percent);
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_GOTO_ON_ERROR(err, cleanup, TAG, "load brightness");
        settings->brightness_percent = clamp_brightness(brightness_percent);
    }

    uint8_t touch_calibration_valid = 0;
    err = nvs_get_u8(handle, KEY_TOUCH_CAL_VALID, &touch_calibration_valid);
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_GOTO_ON_ERROR(err, cleanup, TAG, "load touch calibration valid flag");
        settings->touch_calibration.valid = touch_calibration_valid != 0;
        ESP_GOTO_ON_ERROR(load_i32_setting(handle, KEY_TOUCH_XX, &settings->touch_calibration.xx), cleanup, TAG, "load touch xx");
        ESP_GOTO_ON_ERROR(load_i32_setting(handle, KEY_TOUCH_XY, &settings->touch_calibration.xy), cleanup, TAG, "load touch xy");
        ESP_GOTO_ON_ERROR(load_i32_setting(handle, KEY_TOUCH_X_OFFSET, &settings->touch_calibration.x_offset), cleanup, TAG, "load touch x offset");
        ESP_GOTO_ON_ERROR(load_i32_setting(handle, KEY_TOUCH_YX, &settings->touch_calibration.yx), cleanup, TAG, "load touch yx");
        ESP_GOTO_ON_ERROR(load_i32_setting(handle, KEY_TOUCH_YY, &settings->touch_calibration.yy), cleanup, TAG, "load touch yy");
        ESP_GOTO_ON_ERROR(load_i32_setting(handle, KEY_TOUCH_Y_OFFSET, &settings->touch_calibration.y_offset), cleanup, TAG, "load touch y offset");
    }

cleanup:
    nvs_close(handle);
    return ret;
}

esp_err_t app_settings_save(const app_settings_t *settings)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    app_settings_t normalized_settings = {0};
    ESP_RETURN_ON_ERROR(ret, TAG, "open settings namespace");

    normalized_settings = *settings;
    normalize_region_code(normalized_settings.region_code, sizeof(normalized_settings.region_code));

    ESP_GOTO_ON_ERROR(nvs_set_str(handle, KEY_WIFI_SSID, normalized_settings.wifi_ssid), cleanup, TAG, "save SSID");
    ESP_GOTO_ON_ERROR(nvs_set_str(handle, KEY_WIFI_PSK, normalized_settings.wifi_psk), cleanup, TAG, "save PSK");
    ESP_GOTO_ON_ERROR(nvs_set_str(handle, KEY_REGION_CODE, normalized_settings.region_code), cleanup, TAG, "save region code");
    ESP_GOTO_ON_ERROR(
        nvs_set_u8(handle, KEY_BRIGHTNESS, clamp_brightness(normalized_settings.brightness_percent)),
        cleanup,
        TAG,
        "save brightness"
    );
    ESP_GOTO_ON_ERROR(store_touch_calibration(handle, &normalized_settings.touch_calibration), cleanup, TAG, "save touch calibration");
    ESP_GOTO_ON_ERROR(nvs_commit(handle), cleanup, TAG, "commit settings");

cleanup:
    nvs_close(handle);
    return ret;
}

esp_err_t app_settings_save_touch_calibration(const app_touch_calibration_t *calibration)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "open settings namespace");

    ESP_GOTO_ON_ERROR(store_touch_calibration(handle, calibration), cleanup, TAG, "save touch calibration");
    ESP_GOTO_ON_ERROR(nvs_commit(handle), cleanup, TAG, "commit touch calibration");

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