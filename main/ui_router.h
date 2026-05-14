#pragma once

#include <esp_err.h>
#include <lvgl.h>

#include "app_state.h"

esp_err_t ui_router_init(app_state_t *state);
esp_err_t ui_router_update(const app_state_t *state);
lv_obj_t *ui_router_get_screen_root(app_screen_t screen);