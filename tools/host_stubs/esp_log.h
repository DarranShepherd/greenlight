#pragma once

static inline void esp_log_stub(const char *tag, const char *fmt, ...)
{
	(void)tag;
	(void)fmt;
}

#define ESP_LOGI(tag, fmt, ...) do { esp_log_stub((tag), (fmt), ##__VA_ARGS__); } while (0)
#define ESP_LOGW(tag, fmt, ...) do { esp_log_stub((tag), (fmt), ##__VA_ARGS__); } while (0)