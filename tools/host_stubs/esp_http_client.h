#pragma once

#include "esp_err.h"

typedef enum {
    HTTP_EVENT_ERROR = 0,
    HTTP_EVENT_ON_CONNECTED,
    HTTP_EVENT_HEADERS_SENT,
    HTTP_EVENT_ON_HEADER,
    HTTP_EVENT_ON_DATA,
    HTTP_EVENT_ON_FINISH,
    HTTP_EVENT_DISCONNECTED,
    HTTP_EVENT_REDIRECT,
} esp_http_client_event_id_t;

typedef struct esp_http_client_event {
    esp_http_client_event_id_t event_id;
    void *user_data;
    void *data;
    int data_len;
} esp_http_client_event_t;

typedef int (*esp_http_client_event_cb_t)(esp_http_client_event_t *event);

typedef struct {
    const char *url;
    esp_http_client_event_cb_t event_handler;
    void *user_data;
    int timeout_ms;
    esp_err_t (*crt_bundle_attach)(void *conf);
} esp_http_client_config_t;

typedef void *esp_http_client_handle_t;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);