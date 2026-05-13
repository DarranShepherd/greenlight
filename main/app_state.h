#pragma once

#include <stdint.h>

#include "app_settings.h"

typedef enum {
    APP_SCREEN_PRIMARY = 0,
    APP_SCREEN_DETAIL,
    APP_SCREEN_SETTINGS,
    APP_SCREEN_COUNT,
} app_screen_t;

typedef struct {
    app_settings_t settings;
    app_screen_t active_screen;
    uint32_t uptime_seconds;
} app_state_t;

void app_state_init(app_state_t *state, const app_settings_t *settings);
void app_state_set_active_screen(app_state_t *state, app_screen_t screen);
void app_state_set_brightness(app_state_t *state, uint8_t brightness_percent);
void app_state_set_uptime(app_state_t *state, uint32_t uptime_seconds);
const char *app_state_get_screen_name(app_screen_t screen);