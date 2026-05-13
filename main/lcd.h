#pragma once

#include <esp_err.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>

esp_err_t lcd_backlight_init(void);
esp_err_t lcd_set_brightness(int percent);
esp_err_t lcd_init(esp_lcd_panel_io_handle_t *panel_io, esp_lcd_panel_handle_t *panel);
lv_display_t *lvgl_display_init(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel);
