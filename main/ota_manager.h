#pragma once

#include <esp_err.h>

#include "app_state.h"

esp_err_t ota_manager_init(app_state_t *state);
esp_err_t ota_manager_request_check(void);
esp_err_t ota_manager_request_update(void);