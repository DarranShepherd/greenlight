#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "wifi_reconnect_policy.h"

static void test_background_reconnect_uses_expected_backoff_sequence(void)
{
    wifi_reconnect_policy_t policy = {0};

    wifi_reconnect_policy_note_connected(&policy);
    assert(wifi_reconnect_policy_should_schedule(&policy, false, true));

    wifi_reconnect_policy_schedule(&policy, 1000);
    assert(policy.reconnect_scheduled);
    assert(policy.next_reconnect_at_ms == 6000);
    assert(!wifi_reconnect_policy_is_due(&policy, 5999));
    assert(wifi_reconnect_policy_is_due(&policy, 6000));

    wifi_reconnect_policy_note_attempt_started(&policy);
    assert(policy.reconnect_attempt_count == 1);
    assert(wifi_reconnect_policy_current_delay_ms(&policy) == 10000);

    wifi_reconnect_policy_schedule(&policy, 6000);
    assert(policy.next_reconnect_at_ms == 16000);
    wifi_reconnect_policy_note_attempt_started(&policy);
    assert(wifi_reconnect_policy_current_delay_ms(&policy) == 20000);

    wifi_reconnect_policy_schedule(&policy, 16000);
    assert(policy.next_reconnect_at_ms == 36000);
    wifi_reconnect_policy_note_attempt_started(&policy);
    assert(wifi_reconnect_policy_current_delay_ms(&policy) == 30000);

    wifi_reconnect_policy_schedule(&policy, 36000);
    assert(policy.next_reconnect_at_ms == 66000);
    wifi_reconnect_policy_note_attempt_started(&policy);
    assert(wifi_reconnect_policy_current_delay_ms(&policy) == 30000);
}

static void test_background_reconnect_is_disabled_until_first_success_and_during_manual_connect(void)
{
    wifi_reconnect_policy_t policy = {0};

    assert(!wifi_reconnect_policy_should_schedule(&policy, false, true));

    wifi_reconnect_policy_note_connected(&policy);
    assert(!wifi_reconnect_policy_should_schedule(&policy, true, true));

    wifi_reconnect_policy_schedule(&policy, 500);
    wifi_reconnect_policy_note_attempt_started(&policy);
    assert(wifi_reconnect_policy_should_schedule(&policy, false, false));

    wifi_reconnect_policy_reset(&policy);
    assert(!policy.reconnect_scheduled);
    assert(policy.reconnect_attempt_count == 0);
    assert(policy.has_established_connection);
}

int main(void)
{
    test_background_reconnect_uses_expected_backoff_sequence();
    test_background_reconnect_is_disabled_until_first_success_and_during_manual_connect();
    puts("wifi reconnect policy harness passed");
    return 0;
}