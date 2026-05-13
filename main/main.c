#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_check.h>
#include <esp_lcd_touch.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>

#include "app_settings.h"
#include "app_state.h"
#include "lcd.h"
#include "time_manager.h"
#include "touch.h"
#include "ui_router.h"
#include "wifi_manager.h"

static const char *TAG = "greenlight";

void app_main(void)
{
    esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;
    lv_display_t *display = NULL;
    lv_indev_t *touch_input = NULL;
    lvgl_port_touch_cfg_t touch_config = {0};
    app_settings_t settings;
    app_state_t app_state;
    bool was_wifi_connected = false;

    ESP_LOGI(TAG, "Starting Greenlight on CYD");

    ESP_ERROR_CHECK(app_settings_init());
    ESP_ERROR_CHECK(app_settings_load(&settings));
    app_state_init(&app_state, &settings);
    if (!app_state.wifi_has_saved_credentials) {
        app_state_set_active_screen(&app_state, APP_SCREEN_SETTINGS);
    }

    ESP_ERROR_CHECK(lcd_backlight_init());
    ESP_ERROR_CHECK(lcd_init(&panel_io, &panel));

    display = lvgl_display_init(panel_io, panel);
    ESP_RETURN_VOID_ON_FALSE(display != NULL, TAG, "initialize LVGL display");

    touch_set_calibration(&settings.touch_calibration);
    ESP_ERROR_CHECK(touch_init(&touch_handle));
    touch_config.disp = display;
    touch_config.handle = touch_handle;
    touch_input = lvgl_port_add_touch(&touch_config);
    ESP_RETURN_VOID_ON_FALSE(touch_input != NULL, TAG, "register touch input");

    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90);

    ESP_ERROR_CHECK(lcd_set_brightness(app_state.settings.brightness_percent));
    ESP_ERROR_CHECK(ui_router_init(&app_state));
    ESP_ERROR_CHECK(wifi_manager_init(&app_state));
    ESP_ERROR_CHECK(wifi_manager_start());
    ESP_ERROR_CHECK(time_manager_init(&app_state));

    if (app_state.wifi_has_saved_credentials) {
        ESP_LOGI(TAG, "Attempting Wi-Fi reconnect for SSID %s", app_state.settings.wifi_ssid);
        ESP_ERROR_CHECK(wifi_manager_request_connect(app_state.settings.wifi_ssid, app_state.settings.wifi_psk));
    } else {
        ESP_LOGI(TAG, "No saved Wi-Fi credentials, showing onboarding");
        ESP_ERROR_CHECK(wifi_manager_request_scan());
    }

    while (true) {
        bool wifi_connected = wifi_manager_is_connected();

        app_state_set_uptime(&app_state, esp_log_timestamp() / 1000);

        if (wifi_connected && !was_wifi_connected) {
            ESP_ERROR_CHECK(time_manager_request_sync());
        }

        was_wifi_connected = wifi_connected;
        time_manager_update_clock(&app_state);
        ESP_ERROR_CHECK(ui_router_update(&app_state));

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
