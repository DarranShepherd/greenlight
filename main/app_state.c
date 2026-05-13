#include "app_state.h"

#include <string.h>

void app_state_init(app_state_t *state, const app_settings_t *settings)
{
    memset(state, 0, sizeof(*state));
    state->settings = *settings;
    state->active_screen = APP_SCREEN_PRIMARY;
}

void app_state_set_active_screen(app_state_t *state, app_screen_t screen)
{
    if (screen < APP_SCREEN_COUNT) {
        state->active_screen = screen;
    }
}

void app_state_set_brightness(app_state_t *state, uint8_t brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = APP_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT;
    }

    state->settings.brightness_percent = brightness_percent;
}

void app_state_set_uptime(app_state_t *state, uint32_t uptime_seconds)
{
    state->uptime_seconds = uptime_seconds;
}

const char *app_state_get_screen_name(app_screen_t screen)
{
    switch (screen) {
        case APP_SCREEN_PRIMARY:
            return "Primary";
        case APP_SCREEN_DETAIL:
            return "Detail";
        case APP_SCREEN_SETTINGS:
            return "Settings";
        case APP_SCREEN_COUNT:
        default:
            return "Unknown";
    }
}