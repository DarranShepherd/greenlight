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
#include "sync_controller.h"
#include "time_manager.h"
#include "touch.h"
#include "ui_router.h"
#include "wifi_manager.h"

static const char *TAG = "greenlight";
static app_settings_t s_settings;
static app_state_t s_app_state;

void app_main(void)
{
    esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;
    lv_display_t *display = NULL;
    lv_indev_t *touch_input = NULL;
    lvgl_port_touch_cfg_t touch_config = {0};
    bool was_wifi_connected = false;
    bool tariff_entry_released = false;

    ESP_LOGI(TAG, "Starting Greenlight on CYD");

    ESP_ERROR_CHECK(app_settings_init());
    ESP_ERROR_CHECK(app_settings_load(&s_settings));
    app_state_init(&s_app_state, &s_settings);
    if (!s_app_state.wifi_has_saved_credentials) {
        app_state_set_active_screen(&s_app_state, APP_SCREEN_SETTINGS);
    }

    ESP_ERROR_CHECK(lcd_backlight_init());
    ESP_ERROR_CHECK(lcd_init(&panel_io, &panel));

    display = lvgl_display_init(panel_io, panel);
    ESP_RETURN_VOID_ON_FALSE(display != NULL, TAG, "initialize LVGL display");

    touch_set_calibration(&s_settings.touch_calibration);
    ESP_ERROR_CHECK(touch_init(&touch_handle));
    touch_config.disp = display;
    touch_config.handle = touch_handle;
    touch_input = lvgl_port_add_touch(&touch_config);
    ESP_RETURN_VOID_ON_FALSE(touch_input != NULL, TAG, "register touch input");

    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90);

    ESP_ERROR_CHECK(lcd_set_brightness(s_app_state.settings.brightness_percent));
    ESP_ERROR_CHECK(ui_router_init(&s_app_state));
    ESP_ERROR_CHECK(wifi_manager_init(&s_app_state));
    ESP_ERROR_CHECK(wifi_manager_start());
    ESP_ERROR_CHECK(time_manager_init(&s_app_state));
    ESP_ERROR_CHECK(sync_controller_init(&s_app_state));

    if (s_app_state.wifi_has_saved_credentials) {
        ESP_LOGI(TAG, "Attempting Wi-Fi reconnect for SSID %s", s_app_state.settings.wifi_ssid);
        ESP_ERROR_CHECK(wifi_manager_request_connect(s_app_state.settings.wifi_ssid, s_app_state.settings.wifi_psk));
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

        if (!tariff_entry_released) {
            if (s_app_state.tariff_has_data || s_app_state.tariff_status == APP_TARIFF_STATUS_OFFLINE) {
                app_state_set_active_screen(&s_app_state, APP_SCREEN_PRIMARY);
                tariff_entry_released = true;
            }
        }

        ESP_ERROR_CHECK(ui_router_update(&s_app_state));

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
