#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define NIGHT_MODE_POLICY_START_HOUR 0
#define NIGHT_MODE_POLICY_END_HOUR 6
#define NIGHT_MODE_POLICY_SLEEP_BRIGHTNESS_PERCENT 1
#define NIGHT_MODE_POLICY_TOUCH_WAKE_DURATION_MS 15000U

typedef struct {
    bool overnight_active;
    bool touch_wake_active;
    uint32_t touch_wake_until_ms;
} night_mode_policy_t;

bool night_mode_policy_is_overnight_hour(const struct tm *local_time);
void night_mode_policy_reset(night_mode_policy_t *policy);
void night_mode_policy_update(
    night_mode_policy_t *policy,
    bool time_valid,
    const struct tm *local_time,
    bool touch_pressed,
    uint32_t now_ms
);
uint8_t night_mode_policy_effective_brightness(const night_mode_policy_t *policy, uint8_t configured_brightness);