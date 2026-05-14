#pragma once

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL 0x101
#define ESP_ERR_INVALID_RESPONSE 0x102
#define ESP_ERR_NO_MEM 0x103
#define ESP_ERR_INVALID_ARG 0x104
#define ESP_ERR_NOT_FOUND 0x105

static inline const char *esp_err_to_name(esp_err_t err)
{
    switch (err) {
        case ESP_OK:
            return "ESP_OK";
        case ESP_FAIL:
            return "ESP_FAIL";
        case ESP_ERR_INVALID_RESPONSE:
            return "ESP_ERR_INVALID_RESPONSE";
        case ESP_ERR_NO_MEM:
            return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG:
            return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_NOT_FOUND:
            return "ESP_ERR_NOT_FOUND";
        default:
            return "ESP_ERR_UNKNOWN";
    }
}