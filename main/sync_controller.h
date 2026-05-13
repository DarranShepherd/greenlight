#pragma once

#include <esp_err.h>

#include "app_state.h"

esp_err_t sync_controller_init(app_state_t *state);
void sync_controller_request_refresh(void);