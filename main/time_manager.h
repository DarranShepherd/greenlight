#pragma once

#include <esp_err.h>

#include "app_state.h"

esp_err_t time_manager_init(app_state_t *state);
esp_err_t time_manager_request_sync(void);
bool time_manager_is_syncing(void);
void time_manager_update_clock(app_state_t *state);