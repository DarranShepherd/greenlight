#pragma once

#ifdef GREENLIGHT_HOST_TEST

#include <stdbool.h>
#include <time.h>

#include <esp_err.h>

#include "app_settings.h"
#include "app_state.h"
#include "tariff_model.h"

void sync_controller_test_reset(app_state_t *state, app_settings_t *settings, const char *region_code);
esp_err_t sync_controller_test_refresh_tariffs(time_t now_local);
void sync_controller_test_publish_runtime_snapshot(time_t now_local);
void sync_controller_test_set_tariff_status(app_tariff_status_t status);
const runtime_tariff_state_t *sync_controller_test_get_runtime_state(void);
bool sync_controller_test_get_refresh_requested(void);
bool sync_controller_test_get_has_successful_load(void);
const char *sync_controller_test_get_active_region_code(void);

#endif