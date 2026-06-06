#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool has_established_connection;
    bool reconnect_scheduled;
    uint32_t reconnect_attempt_count;
    uint32_t next_reconnect_at_ms;
} wifi_reconnect_policy_t;

void wifi_reconnect_policy_reset(wifi_reconnect_policy_t *policy);
void wifi_reconnect_policy_note_connected(wifi_reconnect_policy_t *policy);
bool wifi_reconnect_policy_should_schedule(const wifi_reconnect_policy_t *policy, bool connect_in_progress, bool was_connected);
uint32_t wifi_reconnect_policy_current_delay_ms(const wifi_reconnect_policy_t *policy);
void wifi_reconnect_policy_schedule(wifi_reconnect_policy_t *policy, uint32_t now_ms);
bool wifi_reconnect_policy_is_due(const wifi_reconnect_policy_t *policy, uint32_t now_ms);
void wifi_reconnect_policy_note_attempt_started(wifi_reconnect_policy_t *policy);