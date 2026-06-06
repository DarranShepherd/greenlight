#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "night_mode_policy.h"

static struct tm make_local_time(int hour, int minute)
{
    return (struct tm) {
        .tm_year = 126,
        .tm_mon = 5,
        .tm_mday = 6,
        .tm_hour = hour,
        .tm_min = minute,
    };
}

static void test_overnight_window_uses_midnight_to_six_am(void)
{
    assert(night_mode_policy_is_overnight_hour(&(struct tm){ .tm_hour = 0 }));
    assert(night_mode_policy_is_overnight_hour(&(struct tm){ .tm_hour = 5 }));
    assert(!night_mode_policy_is_overnight_hour(&(struct tm){ .tm_hour = 6 }));
    assert(!night_mode_policy_is_overnight_hour(&(struct tm){ .tm_hour = 23 }));
}

static void test_overnight_period_dims_until_touch_and_then_times_out(void)
{
    night_mode_policy_t policy = {0};
    struct tm local_time = make_local_time(1, 30);

    night_mode_policy_update(&policy, true, &local_time, false, 1000);
    assert(policy.overnight_active);
    assert(!policy.touch_wake_active);
    assert(night_mode_policy_effective_brightness(&policy, 80) == NIGHT_MODE_POLICY_SLEEP_BRIGHTNESS_PERCENT);

    night_mode_policy_update(&policy, true, &local_time, true, 1500);
    assert(policy.touch_wake_active);
    assert(policy.touch_wake_until_ms == 1500 + NIGHT_MODE_POLICY_TOUCH_WAKE_DURATION_MS);
    assert(night_mode_policy_effective_brightness(&policy, 80) == 80);

    night_mode_policy_update(&policy, true, &local_time, false, policy.touch_wake_until_ms - 1);
    assert(policy.touch_wake_active);
    assert(night_mode_policy_effective_brightness(&policy, 80) == 80);

    night_mode_policy_update(&policy, true, &local_time, false, policy.touch_wake_until_ms);
    assert(!policy.touch_wake_active);
    assert(night_mode_policy_effective_brightness(&policy, 80) == NIGHT_MODE_POLICY_SLEEP_BRIGHTNESS_PERCENT);
}

static void test_leaving_overnight_period_restores_normal_brightness(void)
{
    night_mode_policy_t policy = {0};
    struct tm night_time = make_local_time(2, 0);
    struct tm day_time = make_local_time(6, 0);

    night_mode_policy_update(&policy, true, &night_time, false, 2000);
    assert(policy.overnight_active);

    night_mode_policy_update(&policy, true, &day_time, false, 3000);
    assert(!policy.overnight_active);
    assert(!policy.touch_wake_active);
    assert(night_mode_policy_effective_brightness(&policy, 65) == 65);
}

int main(void)
{
    test_overnight_window_uses_midnight_to_six_am();
    test_overnight_period_dims_until_touch_and_then_times_out();
    test_leaving_overnight_period_restores_normal_brightness();
    puts("night mode policy harness passed");
    return 0;
}