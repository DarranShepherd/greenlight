#include "octopus_client.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <esp_check.h>
#include <esp_http_client.h>
#include <esp_log.h>

#include <cJSON.h>

#include "app_settings.h"
#include "octopus_client_internal.h"

static const char *TAG = "octopus_client";
static const char *OCTOPUS_API_PRODUCTS_URL = "https://api.octopus.energy/v1/products/?brand=OCTOPUS_ENERGY&is_business=false&page_size=100";

#ifndef OCTOPUS_CLIENT_CERT_PEM
extern const char octopus_amazon_root_ca_1_pem_start[] asm("_binary_octopus_amazon_root_ca_1_pem_start");
extern const char octopus_amazon_root_ca_1_pem_end[] asm("_binary_octopus_amazon_root_ca_1_pem_end");
#define OCTOPUS_CLIENT_CERT_PEM octopus_amazon_root_ca_1_pem_start
#endif

/*
 * Tariff requests cover two local days, which is usually 96 half-hour rows and can
 * reach 100 around DST changes. Product discovery is capped to 100 items. Keep the
 * buffered response well below typical ESP32 heap pressure and fail closed if the
 * payload grows unexpectedly large or malformed.
 */
#define OCTOPUS_PRODUCTS_MAX_RESPONSE_BYTES (32U * 1024U)
#define OCTOPUS_TARIFFS_MAX_RESPONSE_BYTES (16U * 1024U)
#define OCTOPUS_HTTP_READ_CHUNK_BYTES 1024U
typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    size_t max_response_bytes;
    esp_err_t error;
} http_buffer_t;

static char s_active_product_code[32] = OCTOPUS_CLIENT_DEFAULT_PRODUCT_CODE;
static http_buffer_t s_shared_response_buffer = {0};
static char s_product_stream_read_buffer[OCTOPUS_PRODUCTS_STREAM_CHUNK_BYTES] = {0};
static octopus_product_discovery_parser_t s_product_discovery_parser = {0};

static esp_err_t discover_active_product_code_streaming(char *product_code, size_t product_code_size);
static esp_err_t ensure_http_buffer_capacity(http_buffer_t *buffer, size_t required_capacity);
static void reset_http_buffer(http_buffer_t *buffer, size_t max_response_bytes);

static esp_err_t ensure_http_buffer_capacity(http_buffer_t *buffer, size_t required_capacity)
{
    size_t next_capacity = 0;
    char *next_data = NULL;

    if (buffer == NULL || required_capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (buffer->max_response_bytes > 0 && required_capacity > buffer->max_response_bytes) {
        buffer->error = ESP_ERR_NO_MEM;
        ESP_LOGW(TAG, "Octopus payload exceeded %u bytes", (unsigned int)buffer->max_response_bytes);
        return ESP_ERR_NO_MEM;
    }

    if (required_capacity <= buffer->capacity) {
        return ESP_OK;
    }

    next_capacity = required_capacity;
    if (buffer->max_response_bytes > 0 && next_capacity > buffer->max_response_bytes) {
        next_capacity = buffer->max_response_bytes;
    }

    next_data = realloc(buffer->data, next_capacity);
    if (next_data == NULL) {
        buffer->error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }

    buffer->data = next_data;
    buffer->capacity = next_capacity;
    return ESP_OK;
}

static void reset_http_buffer(http_buffer_t *buffer, size_t max_response_bytes)
{
    if (buffer == NULL) {
        return;
    }

    buffer->length = 0;
    buffer->error = ESP_OK;
    buffer->max_response_bytes = max_response_bytes;
    if (buffer->data != NULL && buffer->capacity > 0) {
        buffer->data[0] = '\0';
    }
}

static void format_utc_timestamp(char *buffer, size_t buffer_size, time_t timestamp)
{
    struct tm utc_tm = {0};

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    gmtime_r(&timestamp, &utc_tm);
    strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%SZ", &utc_tm);
}

static void calculate_query_window(time_t now_local, char *period_from, size_t period_from_size, char *period_to, size_t period_to_size)
{
    struct tm local_tm = {0};
    struct tm query_end_tm = {0};
    time_t today_midnight_local = 0;
    time_t query_end_local = 0;

    localtime_r(&now_local, &local_tm);
    local_tm.tm_hour = 0;
    local_tm.tm_min = 0;
    local_tm.tm_sec = 0;
    today_midnight_local = mktime(&local_tm);

    query_end_tm = local_tm;
    query_end_tm.tm_mday += 2;
    query_end_local = mktime(&query_end_tm);

    format_utc_timestamp(period_from, period_from_size, today_midnight_local);
    format_utc_timestamp(period_to, period_to_size, query_end_local);
}

static esp_err_t perform_get_request(const char *url, http_buffer_t *response)
{
    esp_http_client_config_t http_config = {0};
    esp_http_client_handle_t http_client = NULL;
    int content_length = 0;
    int http_status = 0;
    int bytes_read = 0;
    esp_err_t err = ESP_OK;

    ESP_RETURN_ON_FALSE(url != NULL && response != NULL, ESP_ERR_INVALID_ARG, TAG, "request arguments required");

    reset_http_buffer(response, response->max_response_bytes);

    http_config = (esp_http_client_config_t){
        .url = url,
        .timeout_ms = 15000,
        .cert_pem = OCTOPUS_CLIENT_CERT_PEM,
    };

    http_client = esp_http_client_init(&http_config);
    ESP_RETURN_ON_FALSE(http_client != NULL, ESP_ERR_NO_MEM, TAG, "create HTTP client");

    err = esp_http_client_open(http_client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP open failed for %s: %s", url, esp_err_to_name(err));
        goto cleanup;
    }

    content_length = esp_http_client_fetch_headers(http_client);
    if (content_length < 0) {
        err = ESP_FAIL;
        ESP_LOGW(TAG, "HTTP fetch headers failed for %s", url);
        goto cleanup;
    }

    http_status = esp_http_client_get_status_code(http_client);
    if (http_status != 200) {
        ESP_LOGW(TAG, "HTTP GET %s returned %d", url, http_status);
        err = ESP_FAIL;
        goto cleanup;
    }

    if (content_length > 0) {
        err = ensure_http_buffer_capacity(response, (size_t)content_length + 1U);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "HTTP response for %s requires %u bytes, buffer has %u", url, (unsigned int)((size_t)content_length + 1U), (unsigned int)response->capacity);
            goto cleanup;
        }
    }

    while (true) {
        size_t remaining_capacity = response->capacity - response->length - 1U;

        if (remaining_capacity == 0) {
            err = ensure_http_buffer_capacity(response, response->capacity + OCTOPUS_HTTP_READ_CHUNK_BYTES);
            if (err != ESP_OK) {
                goto cleanup;
            }
            remaining_capacity = response->capacity - response->length - 1U;
        }

        bytes_read = esp_http_client_read(http_client, &response->data[response->length], remaining_capacity);
        if (bytes_read < 0) {
            err = ESP_FAIL;
            ESP_LOGW(TAG, "HTTP read failed for %s", url);
            goto cleanup;
        }

        if (bytes_read == 0) {
            break;
        }

        response->length += (size_t)bytes_read;
        response->data[response->length] = '\0';
    }

cleanup:
    if (http_client != NULL) {
        esp_http_client_close(http_client);
        esp_http_client_cleanup(http_client);
    }

    return err;
}

static esp_err_t discover_active_product_code(char *product_code, size_t product_code_size)
{
    ESP_RETURN_ON_FALSE(product_code != NULL && product_code_size > 0, ESP_ERR_INVALID_ARG, TAG, "product code buffer required");

    return discover_active_product_code_streaming(product_code, product_code_size);
}

static esp_err_t discover_active_product_code_streaming(char *product_code, size_t product_code_size)
{
    esp_http_client_config_t http_config = {
        .url = OCTOPUS_API_PRODUCTS_URL,
        .timeout_ms = 15000,
        .cert_pem = OCTOPUS_CLIENT_CERT_PEM,
    };
    esp_http_client_handle_t http_client = NULL;
    int http_status = 0;
    int read_length = 0;
    bool matched = false;
    bool done = false;
    esp_err_t err = ESP_OK;

    memset(s_product_stream_read_buffer, 0, sizeof(s_product_stream_read_buffer));
    octopus_product_discovery_parser_init(&s_product_discovery_parser);

    http_client = esp_http_client_init(&http_config);
    ESP_RETURN_ON_FALSE(http_client != NULL, ESP_ERR_NO_MEM, TAG, "create product discovery HTTP client");

    err = esp_http_client_open(http_client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP open failed for %s: %s", OCTOPUS_API_PRODUCTS_URL, esp_err_to_name(err));
        goto cleanup;
    }

    err = esp_http_client_fetch_headers(http_client);
    if (err < 0) {
        err = ESP_FAIL;
        ESP_LOGW(TAG, "HTTP fetch headers failed for %s", OCTOPUS_API_PRODUCTS_URL);
        goto cleanup;
    }

    http_status = esp_http_client_get_status_code(http_client);
    if (http_status != 200) {
        ESP_LOGW(TAG, "HTTP GET %s returned %d", OCTOPUS_API_PRODUCTS_URL, http_status);
        err = ESP_FAIL;
        goto cleanup;
    }

    while ((read_length = esp_http_client_read(http_client, s_product_stream_read_buffer, sizeof(s_product_stream_read_buffer))) > 0) {
        err = octopus_product_discovery_parser_feed(
            &s_product_discovery_parser,
            s_product_stream_read_buffer,
            (size_t)read_length,
            product_code,
            product_code_size,
            &matched,
            &done
        );
        if (err != ESP_OK) {
            goto cleanup;
        }

        if (done) {
            err = matched ? ESP_OK : ESP_ERR_NOT_FOUND;
            goto cleanup;
        }
    }

    err = read_length < 0 ? ESP_FAIL : ESP_ERR_NOT_FOUND;

cleanup:
    if (http_client != NULL) {
        esp_http_client_close(http_client);
        esp_http_client_cleanup(http_client);
    }

    return err;
}

esp_err_t octopus_client_fetch_tariffs(
    const char *region_code,
    time_t now_local,
    tariff_slot_t *slots,
    size_t max_slots,
    size_t *slot_count
)
{
    char normalized_region[APP_SETTINGS_REGION_CODE_MAX_LEN + 1] = {0};
    char period_from[24] = {0};
    char period_to[24] = {0};
    char product_code[sizeof(s_active_product_code)] = {0};
    char url[320] = {0};
    esp_err_t err = ESP_OK;

    ESP_RETURN_ON_FALSE(region_code != NULL && region_code[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "region code required");
    ESP_RETURN_ON_FALSE(slots != NULL && slot_count != NULL && max_slots > 0, ESP_ERR_INVALID_ARG, TAG, "slot buffer required");

    strlcpy(normalized_region, region_code, sizeof(normalized_region));
    normalized_region[0] = (char)toupper((unsigned char)normalized_region[0]);
    strlcpy(product_code, s_active_product_code, sizeof(product_code));

    err = discover_active_product_code(product_code, sizeof(product_code));
    if (err == ESP_OK) {
        strlcpy(s_active_product_code, product_code, sizeof(s_active_product_code));
    } else {
        strlcpy(product_code, s_active_product_code, sizeof(product_code));
        ESP_LOGW(TAG, "Falling back to cached Agile product %s after discovery failure: %s", product_code, esp_err_to_name(err));
    }

    calculate_query_window(now_local, period_from, sizeof(period_from), period_to, sizeof(period_to));
    snprintf(
        url,
        sizeof(url),
        "https://api.octopus.energy/v1/products/%s/electricity-tariffs/E-1R-%s-%s/standard-unit-rates/?page_size=150&period_from=%s&period_to=%s",
        product_code,
        product_code,
        normalized_region,
        period_from,
        period_to
    );

    ESP_LOGI(TAG, "Fetching Agile prices for %s region %s", product_code, normalized_region);

    reset_http_buffer(&s_shared_response_buffer, OCTOPUS_TARIFFS_MAX_RESPONSE_BYTES);
    err = perform_get_request(url, &s_shared_response_buffer);
    if (err != ESP_OK) {
        return err;
    }

    return octopus_client_parse_slots_from_response(s_shared_response_buffer.data, slots, max_slots, slot_count);
}