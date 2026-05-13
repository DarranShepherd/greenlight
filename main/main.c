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
#include "touch.h"
#include "ui_router.h"

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

    ESP_LOGI(TAG, "Starting Greenlight on CYD");

    ESP_ERROR_CHECK(app_settings_init());
    ESP_ERROR_CHECK(app_settings_load(&settings));
    app_state_init(&app_state, &settings);

    ESP_ERROR_CHECK(lcd_backlight_init());
    ESP_ERROR_CHECK(lcd_init(&panel_io, &panel));

    display = lvgl_display_init(panel_io, panel);
    ESP_RETURN_VOID_ON_FALSE(display != NULL, TAG, "initialize LVGL display");

    ESP_ERROR_CHECK(touch_init(&touch_handle));
    touch_config.disp = display;
    touch_config.handle = touch_handle;
    touch_input = lvgl_port_add_touch(&touch_config);
    ESP_RETURN_VOID_ON_FALSE(touch_input != NULL, TAG, "register touch input");

    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90);

    ESP_ERROR_CHECK(lcd_set_brightness(app_state.settings.brightness_percent));
    ESP_ERROR_CHECK(ui_router_init(&app_state));

    while (true) {
        app_state_set_uptime(&app_state, esp_log_timestamp() / 1000);
        ESP_ERROR_CHECK(ui_router_update(&app_state));

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
