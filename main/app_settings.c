#include "app_settings.h"

#include <ctype.h>
#include <string.h>

#include <esp_check.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "board_profile.h"

static const char *TAG = "app_settings";
static const char *SETTINGS_NAMESPACE = "settings";
static const char *KEY_WIFI_SSID = "wifi_ssid";
static const char *KEY_WIFI_PSK = "wifi_psk";
static const char *KEY_REGION_CODE = "region";
static const char *KEY_BRIGHTNESS = "brightness";
static const char *KEY_FUTURE_PERIODS_MODE = "future_mode";
static const char *KEY_TOUCH_CAL_BOARD = "touch_board";
static const char *KEY_TOUCH_CAL_VERSION = "touch_ver";
static const char *KEY_TOUCH_CAL_VALID = "touch_cal_ok";
static const char *KEY_TOUCH_XX = "touch_xx";
static const char *KEY_TOUCH_XY = "touch_xy";
static const char *KEY_TOUCH_X_OFFSET = "touch_xoff";
static const char *KEY_TOUCH_YX = "touch_yx";
static const char *KEY_TOUCH_YY = "touch_yy";
static const char *KEY_TOUCH_Y_OFFSET = "touch_yoff";
static const uint8_t TOUCH_CALIBRATION_STORAGE_VERSION = 2;

static const app_touch_calibration_t s_esp32_2432s028_ili9341_touch_seed = {
    .valid = true,
    .xx = 67,
    .xy = 0,
    .x_offset = -17078,
    .yx = 0,
    .yy = 89,
    .y_offset = -13574,
};

static const app_touch_calibration_t s_esp32_32e_st7789_touch_seed = {
    .valid = true,
    .xx = 65,
    .xy = 0,
    .x_offset = -15053,
    .yx = 0,
    .yy = 89,
    .y_offset = -14482,
};

static const app_touch_calibration_t s_esp32_2432s028_st7789_touch_seed = {
    .valid = true,
    .xx = 66,
    .xy = 0,
    .x_offset = -14946,
    .yx = 0,
    .yy = 87,
    .y_offset = -15139,
};

static const app_touch_calibration_t s_uncalibrated_touch_seed = {
    .valid = false,
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

static app_future_periods_mode_t clamp_future_periods_mode(uint8_t mode)
{
    return mode == APP_FUTURE_PERIODS_MODE_EXACT ? APP_FUTURE_PERIODS_MODE_EXACT : APP_FUTURE_PERIODS_MODE_SIMPLIFIED;
}

static void normalize_region_code(char *region_code, size_t region_code_size)
{
    if (region_code == NULL || region_code_size == 0) {
        return;
    }

    if (region_code[0] == '\0') {
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

static const app_touch_calibration_t *get_touch_calibration_seed(void)
{
    const greenlight_board_profile_t *board_profile = greenlight_board_profile_get();
    const char *board_id = greenlight_board_id_get();

    if (board_profile->touch.controller == GREENLIGHT_TOUCH_CONTROLLER_GT911) {
        return &s_uncalibrated_touch_seed;
    }

    if (strcmp(board_id, "esp32_32e_st7789") == 0) {
        return &s_esp32_32e_st7789_touch_seed;
    }

    if (strcmp(board_id, "esp32_2432s028_st7789") == 0) {
        return &s_esp32_2432s028_st7789_touch_seed;
    }

    return &s_esp32_2432s028_ili9341_touch_seed;
}

static esp_err_t load_touch_calibration_values(nvs_handle_t handle, app_touch_calibration_t *calibration)
{
    uint8_t touch_calibration_valid = 0;

    ESP_RETURN_ON_ERROR(nvs_get_u8(handle, KEY_TOUCH_CAL_VALID, &touch_calibration_valid), TAG, "load touch calibration valid flag");

    calibration->valid = touch_calibration_valid != 0;
    ESP_RETURN_ON_ERROR(load_i32_setting(handle, KEY_TOUCH_XX, &calibration->xx), TAG, "load touch xx");
    ESP_RETURN_ON_ERROR(load_i32_setting(handle, KEY_TOUCH_XY, &calibration->xy), TAG, "load touch xy");
    ESP_RETURN_ON_ERROR(load_i32_setting(handle, KEY_TOUCH_X_OFFSET, &calibration->x_offset), TAG, "load touch x offset");
    ESP_RETURN_ON_ERROR(load_i32_setting(handle, KEY_TOUCH_YX, &calibration->yx), TAG, "load touch yx");
    ESP_RETURN_ON_ERROR(load_i32_setting(handle, KEY_TOUCH_YY, &calibration->yy), TAG, "load touch yy");
    ESP_RETURN_ON_ERROR(load_i32_setting(handle, KEY_TOUCH_Y_OFFSET, &calibration->y_offset), TAG, "load touch y offset");
    return ESP_OK;
}

static esp_err_t load_touch_calibration(nvs_handle_t handle, app_touch_calibration_t *calibration)
{
    char stored_board_id[32] = {0};
    size_t stored_board_id_size = sizeof(stored_board_id);
    const char *current_board_id = greenlight_board_id_get();
    uint8_t stored_version = 0;
    esp_err_t err = nvs_get_str(handle, KEY_TOUCH_CAL_BOARD, stored_board_id, &stored_board_id_size);

    if (err == ESP_OK) {
        if (strcmp(stored_board_id, current_board_id) != 0) {
            ESP_LOGI(TAG, "Ignoring touch calibration for board %s while running on %s", stored_board_id, current_board_id);
            return ESP_OK;
        }

        err = nvs_get_u8(handle, KEY_TOUCH_CAL_VERSION, &stored_version);
        if (err == ESP_ERR_NVS_NOT_FOUND || stored_version != TOUCH_CALIBRATION_STORAGE_VERSION) {
            ESP_LOGI(TAG, "Ignoring touch calibration in legacy format version %u", (unsigned int)stored_version);
            return ESP_OK;
        }

        if (err != ESP_OK) {
            return err;
        }

        return load_touch_calibration_values(handle, calibration);
    }

    if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    return ESP_OK;
}

static esp_err_t store_touch_calibration(nvs_handle_t handle, const app_touch_calibration_t *calibration)
{
    uint8_t valid = calibration != NULL && calibration->valid ? 1 : 0;
    const char *board_id = greenlight_board_id_get();

    ESP_RETURN_ON_ERROR(nvs_set_str(handle, KEY_TOUCH_CAL_BOARD, board_id), TAG, "save touch calibration board");
    ESP_RETURN_ON_ERROR(nvs_set_u8(handle, KEY_TOUCH_CAL_VERSION, TOUCH_CALIBRATION_STORAGE_VERSION), TAG, "save touch calibration version");
    ESP_RETURN_ON_ERROR(nvs_set_u8(handle, KEY_TOUCH_CAL_VALID, valid), TAG, "save touch calibration valid flag");
    ESP_RETURN_ON_ERROR(nvs_set_i32(handle, KEY_TOUCH_XX, calibration != NULL ? calibration->xx : 0), TAG, "save touch xx");
    ESP_RETURN_ON_ERROR(nvs_set_i32(handle, KEY_TOUCH_XY, calibration != NULL ? calibration->xy : 0), TAG, "save touch xy");
    ESP_RETURN_ON_ERROR(nvs_set_i32(handle, KEY_TOUCH_X_OFFSET, calibration != NULL ? calibration->x_offset : 0), TAG, "save touch x offset");
    ESP_RETURN_ON_ERROR(nvs_set_i32(handle, KEY_TOUCH_YX, calibration != NULL ? calibration->yx : 0), TAG, "save touch yx");
    ESP_RETURN_ON_ERROR(nvs_set_i32(handle, KEY_TOUCH_YY, calibration != NULL ? calibration->yy : 0), TAG, "save touch yy");
    ESP_RETURN_ON_ERROR(nvs_set_i32(handle, KEY_TOUCH_Y_OFFSET, calibration != NULL ? calibration->y_offset : 0), TAG, "save touch y offset");
    return ESP_OK;
}

bool app_settings_region_is_configured(const app_settings_t *settings)
{
    return settings != NULL && settings->region_code[0] != '\0';
}

void app_settings_set_defaults(app_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->brightness_percent = APP_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT;
    settings->future_periods_mode = APP_FUTURE_PERIODS_MODE_SIMPLIFIED;
    settings->touch_calibration = *get_touch_calibration_seed();
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
    uint8_t future_periods_mode = APP_FUTURE_PERIODS_MODE_SIMPLIFIED;

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

    err = nvs_get_u8(handle, KEY_FUTURE_PERIODS_MODE, &future_periods_mode);
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_GOTO_ON_ERROR(err, cleanup, TAG, "load future periods mode");
        settings->future_periods_mode = clamp_future_periods_mode(future_periods_mode);
    }

    ESP_GOTO_ON_ERROR(load_touch_calibration(handle, &settings->touch_calibration), cleanup, TAG, "load touch calibration");

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
    ESP_GOTO_ON_ERROR(
        nvs_set_u8(handle, KEY_FUTURE_PERIODS_MODE, (uint8_t)clamp_future_periods_mode(normalized_settings.future_periods_mode)),
        cleanup,
        TAG,
        "save future periods mode"
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