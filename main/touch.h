#pragma once

#include <stdbool.h>

#include <esp_err.h>
#include <esp_lcd_touch.h>

#include "app_settings.h"

esp_err_t touch_init(esp_lcd_touch_handle_t *touch_handle);
bool touch_get_latest_raw_point(int32_t *x, int32_t *y);
bool touch_consume_activity(void);
void touch_set_calibration(const app_touch_calibration_t *calibration);
