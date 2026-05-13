#pragma once

#include <esp_err.h>

#include "app_state.h"

esp_err_t ui_router_init(app_state_t *state);
esp_err_t ui_router_update(const app_state_t *state);