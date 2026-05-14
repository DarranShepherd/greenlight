#pragma once

#define ESP_RETURN_ON_FALSE(condition, err_code, tag, message) \
    do { \
        (void)(tag); \
        (void)(message); \
        if (!(condition)) { \
            return (err_code); \
        } \
    } while (0)
