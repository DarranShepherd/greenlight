#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_check.h>
#include <esp_lcd_touch.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>

#include "lcd.h"
#include "touch.h"

static const char *TAG = "greenlight";

static lv_obj_t *counter_label;
static lv_obj_t *uptime_label;
static lv_indev_t *touch_input;
static uint32_t tap_count;

static void update_counter_label(void)
{
    lv_label_set_text_fmt(counter_label, "Touches: %lu", (unsigned long)tap_count);
}

static void button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    tap_count++;
    update_counter_label();
}

static esp_err_t create_ui(void)
{
    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Greenlight");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "ESP-IDF 6 + LVGL on CYD");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x7dd3fc), LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 50);

    lv_obj_t *button = lv_button_create(screen);
    lv_obj_set_size(button, 150, 56);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, 18);
    lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *button_label = lv_label_create(button);
    lv_label_set_text(button_label, "Tap me");
    lv_obj_center(button_label);

    counter_label = lv_label_create(screen);
    lv_obj_set_style_text_color(counter_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(counter_label, LV_ALIGN_CENTER, 0, 70);
    update_counter_label();

    uptime_label = lv_label_create(screen);
    lv_obj_set_style_text_color(uptime_label, lv_color_hex(0xa3a3a3), LV_PART_MAIN);
    lv_obj_align(uptime_label, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_label_set_text(uptime_label, "Uptime: 0s");

    lvgl_port_unlock();
    return ESP_OK;
}

void app_main(void)
{
    esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;
    lv_display_t *display = NULL;
    lvgl_port_touch_cfg_t touch_config = {0};

    ESP_LOGI(TAG, "Starting Greenlight on CYD");

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

    ESP_ERROR_CHECK(lcd_set_brightness(80));
    ESP_ERROR_CHECK(create_ui());

    while (true) {
        if (lvgl_port_lock(0)) {
            if (uptime_label != NULL) {
                lv_label_set_text_fmt(uptime_label, "Uptime: %lus", (unsigned long)(esp_log_timestamp() / 1000));
            }
            lvgl_port_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
