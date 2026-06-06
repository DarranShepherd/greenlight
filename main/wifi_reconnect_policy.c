#include "wifi_reconnect_policy.h"

#include <stddef.h>

static const uint32_t s_reconnect_backoff_ms[] = {
    5000,
    10000,
    20000,
    30000,
};

void wifi_reconnect_policy_reset(wifi_reconnect_policy_t *policy)
{
    if (policy == NULL) {
        return;
    }

    policy->reconnect_scheduled = false;
    policy->reconnect_attempt_count = 0;
    policy->next_reconnect_at_ms = 0;
}

void wifi_reconnect_policy_note_connected(wifi_reconnect_policy_t *policy)
{
    if (policy == NULL) {
        return;
    }

    policy->has_established_connection = true;
    wifi_reconnect_policy_reset(policy);
}

bool wifi_reconnect_policy_should_schedule(const wifi_reconnect_policy_t *policy, bool connect_in_progress, bool was_connected)
{
    if (policy == NULL) {
        return false;
    }

    if (connect_in_progress || !policy->has_established_connection) {
        return false;
    }

    return was_connected || policy->reconnect_attempt_count > 0;
}

uint32_t wifi_reconnect_policy_current_delay_ms(const wifi_reconnect_policy_t *policy)
{
    size_t backoff_count = sizeof(s_reconnect_backoff_ms) / sizeof(s_reconnect_backoff_ms[0]);

    if (backoff_count == 0) {
        return 30000;
    }

    if (policy == NULL || policy->reconnect_attempt_count >= backoff_count) {
        return s_reconnect_backoff_ms[backoff_count - 1];
    }

    return s_reconnect_backoff_ms[policy->reconnect_attempt_count];
}

void wifi_reconnect_policy_schedule(wifi_reconnect_policy_t *policy, uint32_t now_ms)
{
    if (policy == NULL) {
        return;
    }

    policy->reconnect_scheduled = true;
    policy->next_reconnect_at_ms = now_ms + wifi_reconnect_policy_current_delay_ms(policy);
}

bool wifi_reconnect_policy_is_due(const wifi_reconnect_policy_t *policy, uint32_t now_ms)
{
    if (policy == NULL || !policy->reconnect_scheduled) {
        return false;
    }

    return now_ms >= policy->next_reconnect_at_ms;
}

void wifi_reconnect_policy_note_attempt_started(wifi_reconnect_policy_t *policy)
{
    if (policy == NULL) {
        return;
    }

    policy->reconnect_scheduled = false;
    policy->next_reconnect_at_ms = 0;
    policy->reconnect_attempt_count++;
}