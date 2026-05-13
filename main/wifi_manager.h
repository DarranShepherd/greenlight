#pragma once

#include <stdbool.h>

#include <esp_err.h>

#include "app_state.h"

esp_err_t wifi_manager_init(app_state_t *state);
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_request_scan(void);
esp_err_t wifi_manager_request_connect(const char *ssid, const char *psk);
bool wifi_manager_is_connected(void);