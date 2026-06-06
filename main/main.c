#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_check.h>
#include <esp_lcd_touch.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>

#include "app_settings.h"
#include "app_state.h"
#include "board_profile.h"
#include "docs_screenshot.h"
#include "lcd.h"
#include "ota_manager.h"
#include "sync_controller.h"
#include "time_manager.h"
#include "touch.h"
#include "ui_router.h"
#include "wifi_manager.h"

#define ONBOARDING_SPLASH_MIN_MS 2000

static const char *TAG = "greenlight";
static app_settings_t s_settings;
static app_state_t s_app_state;
static app_settings_t s_state_settings_snapshot;
static app_state_t s_state_snapshot;
static bool s_onboarding_session_active;
static uint32_t s_onboarding_splash_deadline_ms;

static bool should_hold_onboarding_splash(void)
{
    return s_onboarding_session_active && esp_log_timestamp() < s_onboarding_splash_deadline_ms;
}

static lv_display_rotation_t get_display_rotation(const greenlight_display_profile_t *display)
{
    if (display == NULL) {
        return LV_DISPLAY_ROTATION_0;
    }

    switch (display->rotation) {
        case GREENLIGHT_DISPLAY_ROTATION_90:
            return LV_DISPLAY_ROTATION_90;
        case GREENLIGHT_DISPLAY_ROTATION_180:
            return LV_DISPLAY_ROTATION_180;
        case GREENLIGHT_DISPLAY_ROTATION_270:
            return LV_DISPLAY_ROTATION_270;
        case GREENLIGHT_DISPLAY_ROTATION_0:
        default:
            return LV_DISPLAY_ROTATION_0;
    }
}

#if !CONFIG_GREENLIGHT_DOCS_SCREENSHOT_MODE
static esp_err_t init_touch_with_retries(esp_lcd_touch_handle_t *touch_handle)
{
    static const TickType_t retry_delays[] = {
        pdMS_TO_TICKS(80),
        pdMS_TO_TICKS(160),
        pdMS_TO_TICKS(320),
    };
    esp_err_t err = ESP_FAIL;

    for (size_t attempt = 0; attempt <= sizeof(retry_delays) / sizeof(retry_delays[0]); ++attempt) {
        err = touch_init(touch_handle);
        if (err == ESP_OK) {
            if (attempt > 0) {
                ESP_LOGI(TAG, "Touch init recovered on attempt %u", (unsigned int)(attempt + 1));
            }
            return ESP_OK;
        }

        if (attempt == sizeof(retry_delays) / sizeof(retry_delays[0])) {
            break;
        }

        ESP_LOGW(
            TAG,
            "Touch init attempt %u failed: %s. Retrying...",
            (unsigned int)(attempt + 1),
            esp_err_to_name(err)
        );
        vTaskDelay(retry_delays[attempt]);
    }

    return err;
}
#endif

static void update_startup_stage(app_state_t *state, bool wifi_connected)
{
    app_startup_stage_t progress_stage = APP_STARTUP_STAGE_BOOTING;

    if (state == NULL) {
        return;
    }

    app_state_get_snapshot(state, &s_state_snapshot);

    if (s_state_snapshot.startup_stage == APP_STARTUP_STAGE_COMPLETE) {
        return;
    }

    if (should_hold_onboarding_splash()) {
        app_state_set_startup_stage(state, APP_STARTUP_STAGE_BOOTING, "Starting Greenlight");
        return;
    }

    if (!app_settings_region_is_configured(&s_state_snapshot.settings)) {
        s_onboarding_session_active = true;
        app_state_set_startup_stage(state, APP_STARTUP_STAGE_ONBOARDING, "Choose your Octopus region to begin setup.");
        return;
    }

    if (!s_state_snapshot.wifi_has_saved_credentials) {
        s_onboarding_session_active = true;
        app_state_set_startup_stage(state, APP_STARTUP_STAGE_ONBOARDING, "Region saved. Connect to Wi-Fi to continue setup.");
        return;
    }

    if (s_state_snapshot.wifi_status == APP_WIFI_STATUS_FAILED) {
        s_onboarding_session_active = true;
        app_state_set_startup_stage(state, APP_STARTUP_STAGE_ONBOARDING, s_state_snapshot.wifi_status_text);
        return;
    }

    if (s_onboarding_session_active) {
        progress_stage = APP_STARTUP_STAGE_ONBOARDING;
    }

    if (!wifi_connected) {
        if (s_state_snapshot.wifi_status == APP_WIFI_STATUS_CONNECTING) {
            app_state_set_startup_stage(state, progress_stage, s_state_snapshot.wifi_status_text);
        } else {
            app_state_set_startup_stage(state, progress_stage, "Connecting to Wi-Fi");
        }
        return;
    }

    if (!s_state_snapshot.time_valid) {
        app_state_set_startup_stage(state, progress_stage, s_state_snapshot.time_status_text);
        return;
    }

    if (s_state_snapshot.tariff_status == APP_TARIFF_STATUS_OFFLINE) {
        s_onboarding_session_active = true;
        app_state_set_startup_stage(state, APP_STARTUP_STAGE_ONBOARDING, s_state_snapshot.tariff_status_text);
        return;
    }

    if (!s_state_snapshot.tariff_has_data || !s_state_snapshot.tariff_current_block_valid) {
        app_state_set_startup_stage(state, progress_stage, s_state_snapshot.tariff_status_text);
        return;
    }

    s_onboarding_session_active = false;
    app_state_set_startup_stage(state, APP_STARTUP_STAGE_COMPLETE, "Startup complete");
}

void app_main(void)
{
    const greenlight_board_profile_t *board_profile = greenlight_board_profile_get();
    esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t panel = NULL;
#if !CONFIG_GREENLIGHT_DOCS_SCREENSHOT_MODE
    esp_lcd_touch_handle_t touch_handle = NULL;
    lv_indev_t *touch_input = NULL;
    lvgl_port_touch_cfg_t touch_config = {0};
#endif
    lv_display_t *display = NULL;
    bool was_wifi_connected = false;
    bool tariff_entry_released = false;

    ESP_LOGI(TAG, "Starting Greenlight on %s (%s)", board_profile->display_name, board_profile->id);

    ESP_ERROR_CHECK(app_settings_init());
#if CONFIG_GREENLIGHT_DOCS_SCREENSHOT_MODE
    app_settings_set_defaults(&s_settings);
#else
    ESP_ERROR_CHECK(app_settings_load(&s_settings));
#endif
    app_state_init(&s_app_state, &s_settings);
    app_state_get_snapshot(&s_app_state, &s_state_snapshot);
#if !CONFIG_GREENLIGHT_DOCS_SCREENSHOT_MODE
    s_onboarding_session_active = !app_settings_region_is_configured(&s_state_snapshot.settings) || !s_state_snapshot.wifi_has_saved_credentials;
    s_onboarding_splash_deadline_ms = esp_log_timestamp() + ONBOARDING_SPLASH_MIN_MS;
    if (s_onboarding_session_active) {
        app_state_set_active_screen(&s_app_state, APP_SCREEN_SETTINGS);
        app_state_set_startup_stage(&s_app_state, APP_STARTUP_STAGE_BOOTING, "Starting Greenlight");
    }
#endif

    ESP_ERROR_CHECK(ota_manager_init(&s_app_state));

    ESP_ERROR_CHECK(lcd_backlight_init());
    ESP_ERROR_CHECK(lcd_init(&panel_io, &panel));

    display = lvgl_display_init(panel_io, panel);
    ESP_RETURN_VOID_ON_FALSE(display != NULL, TAG, "initialize LVGL display");
    ESP_LOGI(TAG, "LVGL display initialized");

    ESP_RETURN_VOID_ON_FALSE(lvgl_port_lock(1000), TAG, "lock LVGL for display rotation");
    lv_display_set_rotation(display, get_display_rotation(&board_profile->display));
    lvgl_port_unlock();
    ESP_LOGI(TAG, "LVGL display rotation applied");

#if !CONFIG_GREENLIGHT_DOCS_SCREENSHOT_MODE

    touch_set_calibration(&s_settings.touch_calibration);
    esp_err_t touch_err = init_touch_with_retries(&touch_handle);
    if (touch_err != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed, continuing without touch input: %s", esp_err_to_name(touch_err));
    } else {
        touch_config.disp = display;
        touch_config.handle = touch_handle;
        touch_input = lvgl_port_add_touch(&touch_config);
        ESP_RETURN_VOID_ON_FALSE(touch_input != NULL, TAG, "register touch input");
    }
#endif

    app_state_get_settings(&s_app_state, &s_state_settings_snapshot);
    ESP_ERROR_CHECK(lcd_set_brightness(s_state_settings_snapshot.brightness_percent));

#if CONFIG_GREENLIGHT_DOCS_SCREENSHOT_MODE
    ESP_LOGI(TAG, "Docs mode waiting briefly for LVGL task to settle");
    vTaskDelay(pdMS_TO_TICKS(100));
#endif

    ESP_LOGI(TAG, "Initializing UI router");
    ESP_ERROR_CHECK(ui_router_init(&s_app_state));
    ESP_LOGI(TAG, "UI router initialized");

#if CONFIG_GREENLIGHT_DOCS_SCREENSHOT_MODE
    ESP_LOGI(TAG, "Starting documentation screenshot runner");
    ESP_ERROR_CHECK(docs_screenshot_run(&s_app_state));
    ESP_LOGI(TAG, "Documentation screenshot runner finished");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif

    app_state_set_startup_stage(&s_app_state, APP_STARTUP_STAGE_BOOTING, "Starting Wi-Fi");
    ESP_ERROR_CHECK(wifi_manager_init(&s_app_state));
    ESP_ERROR_CHECK(wifi_manager_start());
    ESP_ERROR_CHECK(time_manager_init(&s_app_state));
    ESP_ERROR_CHECK(sync_controller_init(&s_app_state));

    app_state_get_snapshot(&s_app_state, &s_state_snapshot);
    app_state_get_settings(&s_app_state, &s_state_settings_snapshot);
    if (s_state_snapshot.wifi_has_saved_credentials) {
        ESP_LOGI(TAG, "Attempting Wi-Fi reconnect using saved credentials");
        app_state_set_startup_stage(&s_app_state, APP_STARTUP_STAGE_BOOTING, "Connecting to saved Wi-Fi");
        ESP_ERROR_CHECK(wifi_manager_request_connect(s_state_settings_snapshot.wifi_ssid, s_state_settings_snapshot.wifi_psk));
    } else if (app_settings_region_is_configured(&s_state_settings_snapshot)) {
        ESP_LOGI(TAG, "Region configured but no saved Wi-Fi credentials, showing guided Wi-Fi setup");
        ESP_ERROR_CHECK(wifi_manager_request_scan());
    } else {
        ESP_LOGI(TAG, "No region configured, waiting in guided onboarding");
    }

    while (true) {
        bool wifi_connected = wifi_manager_is_connected();

        app_state_set_uptime(&s_app_state, esp_log_timestamp() / 1000);

        if (wifi_connected && !was_wifi_connected) {
            ESP_ERROR_CHECK(time_manager_request_sync());
        }

        was_wifi_connected = wifi_connected;
        time_manager_update_clock(&s_app_state);
        update_startup_stage(&s_app_state, wifi_connected);
        app_state_get_snapshot(&s_app_state, &s_state_snapshot);

        if (s_state_snapshot.startup_stage == APP_STARTUP_STAGE_ONBOARDING) {
            app_state_set_active_screen(&s_app_state, APP_SCREEN_SETTINGS);
        } else if (!tariff_entry_released && s_state_snapshot.startup_stage == APP_STARTUP_STAGE_COMPLETE) {
            if (s_state_snapshot.tariff_has_data && s_state_snapshot.tariff_current_block_valid) {
                app_state_set_active_screen(&s_app_state, APP_SCREEN_PRIMARY);
                tariff_entry_released = true;
            }
        }

        ESP_ERROR_CHECK(ui_router_update(&s_app_state));

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
