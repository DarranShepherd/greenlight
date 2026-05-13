#include "octopus_client.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <esp_check.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>

#include "app_settings.h"

static const char *TAG = "octopus_client";
static const char *OCTOPUS_API_PRODUCTS_URL = "https://api.octopus.energy/v1/products/?brand=OCTOPUS_ENERGY&is_business=false&page_size=100";

static char s_active_product_code[32] = OCTOPUS_CLIENT_DEFAULT_PRODUCT_CODE;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} http_buffer_t;

static bool parse_json_number_field(const char *object_text, const char *field_name, double *value);
static bool parse_json_string_field(const char *object_text, const char *field_name, char *value, size_t value_size);

static int http_event_handler(esp_http_client_event_t *event)
{
    http_buffer_t *buffer = (http_buffer_t *)event->user_data;

    if (event->event_id != HTTP_EVENT_ON_DATA || buffer == NULL || event->data == NULL || event->data_len <= 0) {
        return ESP_OK;
    }

    if (buffer->length + (size_t)event->data_len + 1 > buffer->capacity) {
        size_t next_capacity = buffer->capacity == 0 ? 4096 : buffer->capacity;
        char *next_data = NULL;

        while (buffer->length + (size_t)event->data_len + 1 > next_capacity) {
            next_capacity *= 2;
        }

        next_data = realloc(buffer->data, next_capacity);
        if (next_data == NULL) {
            return ESP_FAIL;
        }

        buffer->data = next_data;
        buffer->capacity = next_capacity;
    }

    memcpy(&buffer->data[buffer->length], event->data, event->data_len);
    buffer->length += (size_t)event->data_len;
    buffer->data[buffer->length] = '\0';
    return ESP_OK;
}

static bool parse_fixed_int(const char *text, size_t start, size_t length, int *value)
{
    int parsed = 0;

    if (text == NULL || value == NULL) {
        return false;
    }

    for (size_t index = 0; index < length; index++) {
        char character = text[start + index];
        if (!isdigit((unsigned char)character)) {
            return false;
        }
        parsed = (parsed * 10) + (character - '0');
    }

    *value = parsed;
    return true;
}

static bool parse_iso8601_utc(const char *text, time_t *timestamp)
{
    struct tm utc_tm = {0};
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int offset_hour = 0;
    int offset_minute = 0;
    int offset_seconds = 0;
    time_t parsed_time = 0;
    char suffix = '\0';

    if (text == NULL || timestamp == NULL || strlen(text) < 20) {
        return false;
    }

    if (!parse_fixed_int(text, 0, 4, &year) ||
        !parse_fixed_int(text, 5, 2, &month) ||
        !parse_fixed_int(text, 8, 2, &day) ||
        !parse_fixed_int(text, 11, 2, &hour) ||
        !parse_fixed_int(text, 14, 2, &minute) ||
        !parse_fixed_int(text, 17, 2, &second)) {
        return false;
    }

    utc_tm.tm_year = year - 1900;
    utc_tm.tm_mon = month - 1;
    utc_tm.tm_mday = day;
    utc_tm.tm_hour = hour;
    utc_tm.tm_min = minute;
    utc_tm.tm_sec = second;
    suffix = text[19];

    parsed_time = timegm(&utc_tm);
    if (parsed_time == (time_t)-1) {
        return false;
    }

    if (suffix == 'Z' || suffix == '\0') {
        *timestamp = parsed_time;
        return true;
    }

    if ((suffix == '+' || suffix == '-') && strlen(text) >= 25 && parse_fixed_int(text, 20, 2, &offset_hour) && parse_fixed_int(text, 23, 2, &offset_minute)) {
        offset_seconds = (offset_hour * 3600) + (offset_minute * 60);
        if (suffix == '+') {
            parsed_time -= offset_seconds;
        } else {
            parsed_time += offset_seconds;
        }
        *timestamp = parsed_time;
        return true;
    }

    return false;
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
    int http_status = 0;
    esp_err_t err = ESP_OK;

    ESP_RETURN_ON_FALSE(url != NULL && response != NULL, ESP_ERR_INVALID_ARG, TAG, "request arguments required");

    http_config = (esp_http_client_config_t){
        .url = url,
        .event_handler = http_event_handler,
        .user_data = response,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    http_client = esp_http_client_init(&http_config);
    ESP_RETURN_ON_FALSE(http_client != NULL, ESP_ERR_NO_MEM, TAG, "create HTTP client");

    err = esp_http_client_perform(http_client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP GET failed for %s: %s", url, esp_err_to_name(err));
        goto cleanup;
    }

    http_status = esp_http_client_get_status_code(http_client);
    if (http_status != 200) {
        ESP_LOGW(TAG, "HTTP GET %s returned %d", url, http_status);
        err = ESP_FAIL;
    }

cleanup:
    if (http_client != NULL) {
        esp_http_client_cleanup(http_client);
    }

    return err;
}

static esp_err_t discover_active_product_code(char *product_code, size_t product_code_size)
{
    http_buffer_t response = {0};
    const char *cursor = NULL;
    esp_err_t err = ESP_OK;

    ESP_RETURN_ON_FALSE(product_code != NULL && product_code_size > 0, ESP_ERR_INVALID_ARG, TAG, "product code buffer required");

    err = perform_get_request(OCTOPUS_API_PRODUCTS_URL, &response);
    if (err != ESP_OK) {
        goto cleanup;
    }

    cursor = response.data;
    while (cursor != NULL && (cursor = strstr(cursor, "\"code\"")) != NULL) {
        char code[32] = {0};
        char direction[16] = {0};
        char display_name[32] = {0};

        if (!parse_json_string_field(cursor, "code", code, sizeof(code))) {
            cursor += strlen("\"code\"");
            continue;
        }

        if (strncmp(code, "AGILE-", 6) != 0 || strncmp(code, "AGILE-OUTGOING", 14) == 0) {
            cursor += strlen("\"code\"");
            continue;
        }

        if (!parse_json_string_field(cursor, "direction", direction, sizeof(direction)) ||
            !parse_json_string_field(cursor, "display_name", display_name, sizeof(display_name))) {
            cursor += strlen("\"code\"");
            continue;
        }

        if (strcmp(direction, "IMPORT") == 0 && strcmp(display_name, "Agile Octopus") == 0) {
            strlcpy(product_code, code, product_code_size);
            err = ESP_OK;
            goto cleanup;
        }

        cursor += strlen("\"code\"");
    }

    err = ESP_ERR_NOT_FOUND;

cleanup:
    free(response.data);
    return err;
}

static const char *find_json_value(const char *object_text, const char *field_name)
{
    char pattern[32] = {0};

    snprintf(pattern, sizeof(pattern), "\"%s\"", field_name);
    return strstr(object_text, pattern);
}

static bool parse_json_number_field(const char *object_text, const char *field_name, double *value)
{
    const char *field = find_json_value(object_text, field_name);
    char *parse_end = NULL;

    if (field == NULL || value == NULL) {
        return false;
    }

    field = strchr(field, ':');
    if (field == NULL) {
        return false;
    }

    field++;
    while (*field == ' ' || *field == '\t') {
        field++;
    }

    *value = strtod(field, &parse_end);
    return parse_end != field;
}

static bool parse_json_string_field(const char *object_text, const char *field_name, char *value, size_t value_size)
{
    const char *field = find_json_value(object_text, field_name);
    const char *string_start = NULL;
    const char *string_end = NULL;
    size_t copy_length = 0;

    if (field == NULL || value == NULL || value_size == 0) {
        return false;
    }

    field = strchr(field, ':');
    if (field == NULL) {
        return false;
    }

    string_start = strchr(field, '"');
    if (string_start == NULL) {
        return false;
    }

    string_start++;
    string_end = strchr(string_start, '"');
    if (string_end == NULL) {
        return false;
    }

    copy_length = (size_t)(string_end - string_start);
    if (copy_length >= value_size) {
        copy_length = value_size - 1;
    }

    memcpy(value, string_start, copy_length);
    value[copy_length] = '\0';
    return true;
}

static esp_err_t parse_slots_from_response(const char *json_text, tariff_slot_t *slots, size_t max_slots, size_t *slot_count)
{
    size_t parsed_count = 0;
    const char *cursor = NULL;

    ESP_RETURN_ON_FALSE(json_text != NULL, ESP_ERR_INVALID_RESPONSE, TAG, "empty Octopus response");

    cursor = strstr(json_text, "\"results\"");
    ESP_RETURN_ON_FALSE(cursor != NULL, ESP_ERR_INVALID_RESPONSE, TAG, "Octopus response missing results array");

    while ((cursor = strstr(cursor, "\"value_inc_vat\"")) != NULL && parsed_count < max_slots) {
        double price_including_vat = 0.0;
        char valid_from_text[32] = {0};
        char valid_to_text[32] = {0};
        time_t start_utc = 0;
        time_t end_utc = 0;

        if (!parse_json_number_field(cursor, "value_inc_vat", &price_including_vat) ||
            !parse_json_string_field(cursor, "valid_from", valid_from_text, sizeof(valid_from_text)) ||
            !parse_json_string_field(cursor, "valid_to", valid_to_text, sizeof(valid_to_text))) {
            cursor += strlen("\"value_inc_vat\"");
            continue;
        }

        if (!parse_iso8601_utc(valid_from_text, &start_utc) || !parse_iso8601_utc(valid_to_text, &end_utc)) {
            cursor += strlen("\"value_inc_vat\"");
            continue;
        }

        slots[parsed_count] = (tariff_slot_t){
            .start_utc = start_utc,
            .end_utc = end_utc,
            .start_local = start_utc,
            .end_local = end_utc,
            .price_including_vat = (float)price_including_vat,
        };
        parsed_count++;
        cursor += strlen("\"value_inc_vat\"");
    }

    *slot_count = parsed_count;
    return parsed_count > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
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
    http_buffer_t response = {0};
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

    err = perform_get_request(url, &response);
    if (err != ESP_OK) {
        goto cleanup;
    }

    err = parse_slots_from_response(response.data, slots, max_slots, slot_count);

cleanup:
    free(response.data);
    return err;
}