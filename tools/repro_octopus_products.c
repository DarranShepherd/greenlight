#define _GNU_SOURCE

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"

static size_t host_test_strlcpy(char *destination, const char *source, size_t destination_size)
{
    size_t source_length = 0;

    if (destination_size == 0) {
        return source != NULL ? strlen(source) : 0;
    }

    if (source == NULL) {
        destination[0] = '\0';
        return 0;
    }

    source_length = strlen(source);
    if (source_length >= destination_size) {
        memcpy(destination, source, destination_size - 1);
        destination[destination_size - 1] = '\0';
    } else {
        memcpy(destination, source, source_length + 1);
    }

    return source_length;
}

#define strlcpy host_test_strlcpy

typedef struct {
    esp_http_client_config_t config;
    long status_code;
} host_http_client_t;

static long s_last_http_status = 0;
static CURLcode s_last_curl_result = CURLE_OK;

static size_t host_http_write_callback(void *contents, size_t size, size_t nmemb, void *user_data)
{
    host_http_client_t *client = (host_http_client_t *)user_data;
    esp_http_client_event_t event = {0};
    size_t chunk_size = size * nmemb;

    if (client == NULL || client->config.event_handler == NULL || contents == NULL || chunk_size == 0) {
        return 0;
    }

    event.event_id = HTTP_EVENT_ON_DATA;
    event.user_data = client->config.user_data;
    event.data = contents;
    event.data_len = (int)chunk_size;

    if (client->config.event_handler(&event) != ESP_OK) {
        return 0;
    }

    return chunk_size;
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    host_http_client_t *client = NULL;

    if (config == NULL || config->url == NULL || config->event_handler == NULL) {
        return NULL;
    }

    client = calloc(1, sizeof(*client));
    if (client == NULL) {
        return NULL;
    }

    client->config = *config;
    return client;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t handle)
{
    host_http_client_t *client = (host_http_client_t *)handle;
    CURL *curl = NULL;

    if (client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    curl = curl_easy_init();
    if (curl == NULL) {
        return ESP_FAIL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, client->config.url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "greenlight-octopus-firmware-repro/1.0");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)client->config.timeout_ms);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, host_http_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, client);

    s_last_curl_result = curl_easy_perform(curl);
    if (s_last_curl_result != CURLE_OK) {
        curl_easy_cleanup(curl);
        return ESP_FAIL;
    }

    client->status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &client->status_code);
    s_last_http_status = client->status_code;

    curl_easy_cleanup(curl);
    return ESP_OK;
}

int esp_http_client_get_status_code(esp_http_client_handle_t handle)
{
    host_http_client_t *client = (host_http_client_t *)handle;

    return client != NULL ? (int)client->status_code : 0;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t handle)
{
    free(handle);
    return ESP_OK;
}

esp_err_t esp_crt_bundle_attach(void *conf)
{
    (void)conf;
    return ESP_OK;
}

#include "../main/octopus_client.c"

#undef strlcpy

static void print_error_context(const char *payload, size_t payload_size, const char *error_ptr)
{
    static const size_t context_radius = 48U;
    size_t error_offset = 0;
    size_t start = 0;
    size_t end = 0;

    if (payload == NULL || error_ptr == NULL || error_ptr < payload || error_ptr > payload + payload_size) {
        puts("Parse error context: <unavailable>");
        return;
    }

    error_offset = (size_t)(error_ptr - payload);
    start = error_offset > context_radius ? error_offset - context_radius : 0;
    end = error_offset + context_radius;
    if (end > payload_size) {
        end = payload_size;
    }

    printf("Parse error offset: %zu\n", error_offset);
    fputs("Parse error context: ", stdout);
    for (size_t index = start; index < end; index++) {
        unsigned char character = (unsigned char)payload[index];
        if (index == error_offset) {
            fputs(" <<<HERE>>> ", stdout);
        }

        if (character == '\n' || character == '\r' || character == '\t') {
            putchar(' ');
        } else if (isprint(character)) {
            putchar((int)character);
        } else {
            putchar('.');
        }
    }
    putchar('\n');
}

static bool body_looks_truncated(const http_buffer_t *response)
{
    size_t index = 0;

    if (response == NULL || response->data == NULL || response->length == 0) {
        return false;
    }

    if (response->length >= OCTOPUS_HTTP_MAX_RESPONSE_BYTES) {
        return true;
    }

    for (index = response->length; index > 0; index--) {
        unsigned char character = (unsigned char)response->data[index - 1];
        if (isspace(character)) {
            continue;
        }
        return character != '}';
    }

    return false;
}

int main(void)
{
    http_buffer_t response = {0};
    char product_code[32] = {0};
    esp_err_t request_err = ESP_OK;
    esp_err_t discovery_err = ESP_OK;
    const char *parse_error = NULL;

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fputs("curl_global_init failed\n", stderr);
        return 1;
    }

    request_err = perform_get_request(OCTOPUS_API_PRODUCTS_URL, &response);

    printf("HTTP status: %ld\n", s_last_http_status);
    printf("Body length: %zu bytes\n", response.length);
    printf("Body looks truncated: %s\n", body_looks_truncated(&response) ? "yes" : "no");
    printf("Buffer capacity after fetch: %zu bytes\n", response.capacity);
    printf("Buffer error: %s\n", esp_err_to_name(response.error));

    if (request_err != ESP_OK) {
        printf("HTTP request result: %s\n", esp_err_to_name(request_err));
        printf("curl result: %s\n", curl_easy_strerror(s_last_curl_result));
        free(response.data);
        curl_global_cleanup();
        return 1;
    }

    discovery_err = discover_active_product_code_from_response(response.data, product_code, sizeof(product_code));
    printf("Discovery helper result: %s\n", esp_err_to_name(discovery_err));
    if (discovery_err == ESP_OK) {
        printf("Discovered product code: %s\n", product_code);
    }

    if (discovery_err == ESP_ERR_INVALID_RESPONSE) {
        parse_error = cJSON_GetErrorPtr();
        puts("cJSON parse: failed");
        print_error_context(response.data, response.length, parse_error);
    } else {
        puts("cJSON parse: succeeded");
    }

    free(response.data);
    curl_global_cleanup();
    return discovery_err == ESP_OK ? 0 : 2;
}