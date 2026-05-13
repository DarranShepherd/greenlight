#pragma once

#include <esp_err.h>
#include <esp_lcd_touch.h>

#include "app_settings.h"

esp_err_t touch_init(esp_lcd_touch_handle_t *touch_handle);
void touch_set_calibration(const app_touch_calibration_t *calibration);
