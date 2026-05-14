#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_check.h>
#include <esp_lcd_touch.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>

#include "app_settings.h"
#include "app_state.h"
#include "docs_screenshot.h"
#include "lcd.h"
#include "ota_manager.h"
#include "sync_controller.h"
#include "time_manager.h"
#include "touch.h"
#include "ui_router.h"
#include "wifi_manager.h"

static const char *TAG = "greenlight";
static app_settings_t s_settings;
static app_state_t s_app_state;
static app_settings_t s_state_settings_snapshot;
static app_state_t s_state_snapshot;

static void update_startup_stage(app_state_t *state, bool wifi_connected)
{
    if (state == NULL) {
        return;
    }

    app_state_get_snapshot(state, &s_state_snapshot);

    if (s_state_snapshot.startup_stage == APP_STARTUP_STAGE_COMPLETE) {
        return;
    }

    if (!s_state_snapshot.wifi_has_saved_credentials) {
        app_state_set_startup_stage(state, APP_STARTUP_STAGE_ONBOARDING, "Wi-Fi not configured. Open settings to begin onboarding.");
        return;
    }

    if (s_state_snapshot.wifi_status == APP_WIFI_STATUS_FAILED) {
        app_state_set_startup_stage(state, APP_STARTUP_STAGE_ONBOARDING, s_state_snapshot.wifi_status_text);
        return;
    }

    if (!wifi_connected) {
        if (s_state_snapshot.wifi_status == APP_WIFI_STATUS_CONNECTING) {
            app_state_set_startup_stage(state, APP_STARTUP_STAGE_BOOTING, s_state_snapshot.wifi_status_text);
        } else {
            app_state_set_startup_stage(state, APP_STARTUP_STAGE_BOOTING, "Connecting to Wi-Fi");
        }
        return;
    }

    if (!s_state_snapshot.time_valid) {
        app_state_set_startup_stage(state, APP_STARTUP_STAGE_BOOTING, s_state_snapshot.time_status_text);
        return;
    }

    if (!s_state_snapshot.tariff_has_data && s_state_snapshot.tariff_status != APP_TARIFF_STATUS_OFFLINE) {
        app_state_set_startup_stage(state, APP_STARTUP_STAGE_BOOTING, s_state_snapshot.tariff_status_text);
        return;
    }

    app_state_set_startup_stage(state, APP_STARTUP_STAGE_COMPLETE, "Startup complete");
}

void app_main(void)
{
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

    ESP_LOGI(TAG, "Starting Greenlight on CYD");

    ESP_ERROR_CHECK(app_settings_init());
#if CONFIG_GREENLIGHT_DOCS_SCREENSHOT_MODE
    app_settings_set_defaults(&s_settings);
#else
    ESP_ERROR_CHECK(app_settings_load(&s_settings));
#endif
    app_state_init(&s_app_state, &s_settings);
    app_state_get_snapshot(&s_app_state, &s_state_snapshot);
#if !CONFIG_GREENLIGHT_DOCS_SCREENSHOT_MODE
    if (!s_state_snapshot.wifi_has_saved_credentials) {
        app_state_set_active_screen(&s_app_state, APP_SCREEN_SETTINGS);
        app_state_set_startup_stage(&s_app_state, APP_STARTUP_STAGE_ONBOARDING, "Wi-Fi not configured. Open settings to begin onboarding.");
    }
#endif

    ESP_ERROR_CHECK(ota_manager_init(&s_app_state));

    ESP_ERROR_CHECK(lcd_backlight_init());
    ESP_ERROR_CHECK(lcd_init(&panel_io, &panel));

    display = lvgl_display_init(panel_io, panel);
    ESP_RETURN_VOID_ON_FALSE(display != NULL, TAG, "initialize LVGL display");
    ESP_LOGI(TAG, "LVGL display initialized");

    ESP_RETURN_VOID_ON_FALSE(lvgl_port_lock(1000), TAG, "lock LVGL for display rotation");
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "LVGL display rotation applied");

#if !CONFIG_GREENLIGHT_DOCS_SCREENSHOT_MODE

    touch_set_calibration(&s_settings.touch_calibration);
    ESP_ERROR_CHECK(touch_init(&touch_handle));
    touch_config.disp = display;
    touch_config.handle = touch_handle;
    touch_input = lvgl_port_add_touch(&touch_config);
    ESP_RETURN_VOID_ON_FALSE(touch_input != NULL, TAG, "register touch input");
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
    } else {
        ESP_LOGI(TAG, "No saved Wi-Fi credentials, showing onboarding");
        ESP_ERROR_CHECK(wifi_manager_request_scan());
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

        if (!tariff_entry_released) {
            if (s_state_snapshot.startup_stage == APP_STARTUP_STAGE_ONBOARDING) {
                app_state_set_active_screen(&s_app_state, APP_SCREEN_SETTINGS);
                tariff_entry_released = true;
            } else if (s_state_snapshot.tariff_has_data || s_state_snapshot.tariff_status == APP_TARIFF_STATUS_OFFLINE) {
                app_state_set_active_screen(&s_app_state, APP_SCREEN_PRIMARY);
                app_state_set_startup_stage(&s_app_state, APP_STARTUP_STAGE_COMPLETE, "Startup complete");
                tariff_entry_released = true;
            }
        }

        ESP_ERROR_CHECK(ui_router_update(&s_app_state));

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
