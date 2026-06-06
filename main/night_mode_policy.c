#include "night_mode_policy.h"

#include <stddef.h>

static bool timestamp_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

bool night_mode_policy_is_overnight_hour(const struct tm *local_time)
{
    int hour = 0;

    if (local_time == NULL) {
        return false;
    }

    hour = local_time->tm_hour;

    if (NIGHT_MODE_POLICY_START_HOUR == NIGHT_MODE_POLICY_END_HOUR) {
        return true;
    }

    if (NIGHT_MODE_POLICY_START_HOUR < NIGHT_MODE_POLICY_END_HOUR) {
        return hour >= NIGHT_MODE_POLICY_START_HOUR && hour < NIGHT_MODE_POLICY_END_HOUR;
    }

    return hour >= NIGHT_MODE_POLICY_START_HOUR || hour < NIGHT_MODE_POLICY_END_HOUR;
}

void night_mode_policy_reset(night_mode_policy_t *policy)
{
    if (policy == NULL) {
        return;
    }

    policy->overnight_active = false;
    policy->touch_wake_active = false;
    policy->touch_wake_until_ms = 0;
}

void night_mode_policy_update(
    night_mode_policy_t *policy,
    bool time_valid,
    const struct tm *local_time,
    bool touch_pressed,
    uint32_t now_ms
)
{
    if (policy == NULL) {
        return;
    }

    if (!time_valid || !night_mode_policy_is_overnight_hour(local_time)) {
        night_mode_policy_reset(policy);
        return;
    }

    policy->overnight_active = true;

    if (touch_pressed) {
        policy->touch_wake_active = true;
        policy->touch_wake_until_ms = now_ms + NIGHT_MODE_POLICY_TOUCH_WAKE_DURATION_MS;
        return;
    }

    if (policy->touch_wake_active && timestamp_reached(now_ms, policy->touch_wake_until_ms)) {
        policy->touch_wake_active = false;
    }
}

uint8_t night_mode_policy_effective_brightness(const night_mode_policy_t *policy, uint8_t configured_brightness)
{
    if (policy == NULL || !policy->overnight_active || policy->touch_wake_active) {
        return configured_brightness;
    }

    return NIGHT_MODE_POLICY_SLEEP_BRIGHTNESS_PERCENT;
}