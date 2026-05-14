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

static const char *TAG = "octopus_client";
static const char *OCTOPUS_API_PRODUCTS_URL = "https://api.octopus.energy/v1/products/?brand=OCTOPUS_ENERGY&is_business=false&page_size=100";

extern const char octopus_amazon_root_ca_1_pem_start[] asm("_binary_octopus_amazon_root_ca_1_pem_start");
extern const char octopus_amazon_root_ca_1_pem_end[] asm("_binary_octopus_amazon_root_ca_1_pem_end");

/*
 * Tariff requests cover two local days, which is usually 96 half-hour rows and can
 * reach 100 around DST changes. Product discovery is capped to 100 items. Keep the
 * buffered response well below typical ESP32 heap pressure and fail closed if the
 * payload grows unexpectedly large or malformed.
 */
#define OCTOPUS_PRODUCTS_MAX_RESPONSE_BYTES (32U * 1024U)
#define OCTOPUS_TARIFFS_MAX_RESPONSE_BYTES (16U * 1024U)
#define OCTOPUS_HTTP_READ_CHUNK_BYTES 1024U
#define OCTOPUS_PRODUCTS_STREAM_CHUNK_BYTES 512U
#define OCTOPUS_PRODUCTS_OBJECT_BUFFER_BYTES 4096U
#define OCTOPUS_DIAGNOSTIC_SNIPPET_BYTES 64U
#define OCTOPUS_DIAGNOSTIC_ERROR_WINDOW_BYTES 24U

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
static char s_product_stream_object_buffer[OCTOPUS_PRODUCTS_OBJECT_BUFFER_BYTES] = {0};
static char s_product_results_window[16] = {0};

static cJSON *parse_json_document(const char *json_text);
static esp_err_t get_results_array(cJSON *root, cJSON **results);
static const char *get_json_string_field(const cJSON *object, const char *field_name);
static bool get_json_number_field(const cJSON *object, const char *field_name, double *value);
static esp_err_t discover_active_product_code_from_response(const char *json_text, char *product_code, size_t product_code_size);
static esp_err_t discover_active_product_code_streaming(char *product_code, size_t product_code_size);
static esp_err_t ensure_http_buffer_capacity(http_buffer_t *buffer, size_t required_capacity);
static void reset_http_buffer(http_buffer_t *buffer, size_t max_response_bytes);
static void append_diagnostic_char(char *buffer, size_t buffer_size, size_t *write_index, char character);
static void append_diagnostic_hex_byte(char *buffer, size_t buffer_size, size_t *write_index, unsigned char value);
static void format_diagnostic_snippet(const char *text, size_t text_length, size_t start, size_t span, char *buffer, size_t buffer_size);
static bool response_looks_truncated(const char *text, size_t text_length);
static bool find_unexpected_byte(const char *text, size_t text_length, size_t *offset, unsigned char *value);
static void log_product_discovery_parse_failure(const char *json_text, size_t json_length);

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
    int content_length = 0;
    int http_status = 0;
    int bytes_read = 0;
    esp_err_t err = ESP_OK;

    ESP_RETURN_ON_FALSE(url != NULL && response != NULL, ESP_ERR_INVALID_ARG, TAG, "request arguments required");

    reset_http_buffer(response, response->max_response_bytes);

    http_config = (esp_http_client_config_t){
        .url = url,
        .timeout_ms = 15000,
        .cert_pem = octopus_amazon_root_ca_1_pem_start,
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
        .cert_pem = octopus_amazon_root_ca_1_pem_start,
    };
    esp_http_client_handle_t http_client = NULL;
    size_t results_window_length = 0;
    size_t object_length = 0;
    size_t total_bytes_read = 0;
    int http_status = 0;
    int read_length = 0;
    int brace_depth = 0;
    bool in_results_array = false;
    bool capturing_object = false;
    bool in_string = false;
    bool escape_next = false;
    esp_err_t err = ESP_OK;

    memset(s_product_stream_read_buffer, 0, sizeof(s_product_stream_read_buffer));
    memset(s_product_stream_object_buffer, 0, sizeof(s_product_stream_object_buffer));
    memset(s_product_results_window, 0, sizeof(s_product_results_window));

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
        total_bytes_read += (size_t)read_length;

        for (int index = 0; index < read_length; index++) {
            char character = s_product_stream_read_buffer[index];

            if (!in_results_array) {
                if (results_window_length + 1 < sizeof(s_product_results_window)) {
                    s_product_results_window[results_window_length++] = character;
                } else {
                    memmove(s_product_results_window, s_product_results_window + 1, sizeof(s_product_results_window) - 2);
                    s_product_results_window[sizeof(s_product_results_window) - 2] = character;
                    results_window_length = sizeof(s_product_results_window) - 1;
                }
                s_product_results_window[results_window_length] = '\0';

                if (strstr(s_product_results_window, "\"results\":[") != NULL) {
                    in_results_array = true;
                }

                continue;
            }

            if (!capturing_object) {
                if (character == '{') {
                    capturing_object = true;
                    object_length = 0;
                    brace_depth = 0;
                    in_string = false;
                    escape_next = false;
                } else if (character == ']') {
                    err = ESP_ERR_NOT_FOUND;
                    goto cleanup;
                } else {
                    continue;
                }
            }

            if (object_length + 1 >= sizeof(s_product_stream_object_buffer)) {
                ESP_LOGW(TAG, "Product discovery object exceeded %u bytes after %u response bytes", (unsigned int)sizeof(s_product_stream_object_buffer), (unsigned int)total_bytes_read);
                err = ESP_ERR_NO_MEM;
                goto cleanup;
            }

            s_product_stream_object_buffer[object_length++] = character;

            if (in_string) {
                if (escape_next) {
                    escape_next = false;
                } else if (character == '\\') {
                    escape_next = true;
                } else if (character == '"') {
                    in_string = false;
                }
            } else {
                if (character == '"') {
                    in_string = true;
                } else if (character == '{') {
                    brace_depth++;
                } else if (character == '}') {
                    brace_depth--;
                    if (brace_depth == 0) {
                        cJSON *entry = NULL;
                        const char *code = NULL;
                        const char *direction = NULL;
                        const char *display_name = NULL;

                        s_product_stream_object_buffer[object_length] = '\0';
                        entry = parse_json_document(s_product_stream_object_buffer);
                        if (entry == NULL) {
                            err = ESP_ERR_INVALID_RESPONSE;
                            goto cleanup;
                        }

                        code = get_json_string_field(entry, "code");
                        direction = get_json_string_field(entry, "direction");
                        display_name = get_json_string_field(entry, "display_name");

                        if (code != NULL &&
                            strncmp(code, "AGILE-", 6) == 0 &&
                            strncmp(code, "AGILE-OUTGOING", 14) != 0 &&
                            direction != NULL &&
                            strcmp(direction, "IMPORT") == 0 &&
                            display_name != NULL &&
                            strcmp(display_name, "Agile Octopus") == 0) {
                            strlcpy(product_code, code, product_code_size);
                            cJSON_Delete(entry);
                            ESP_LOGI(TAG, "Products discovery completed after %u bytes", (unsigned int)total_bytes_read);
                            err = ESP_OK;
                            goto cleanup;
                        }

                        cJSON_Delete(entry);
                        capturing_object = false;
                        object_length = 0;
                        s_product_stream_object_buffer[0] = '\0';
                    }
                }
            }
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

static esp_err_t discover_active_product_code_from_response(const char *json_text, char *product_code, size_t product_code_size)
{
    cJSON *root = NULL;
    cJSON *results = NULL;
    cJSON *entry = NULL;
    esp_err_t err = ESP_OK;
    size_t json_length = 0;

    ESP_RETURN_ON_FALSE(product_code != NULL && product_code_size > 0, ESP_ERR_INVALID_ARG, TAG, "product code buffer required");

    if (json_text != NULL) {
        json_length = strlen(json_text);
    }

    root = parse_json_document(json_text);
    if (root == NULL) {
        log_product_discovery_parse_failure(json_text, json_length);
        err = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    err = get_results_array(root, &results);
    if (err != ESP_OK) {
        goto cleanup;
    }

    cJSON_ArrayForEach(entry, results) {
        const char *code = NULL;
        const char *direction = NULL;
        const char *display_name = NULL;

        if (!cJSON_IsObject(entry)) {
            continue;
        }

        code = get_json_string_field(entry, "code");
        if (code == NULL || strncmp(code, "AGILE-", 6) != 0 || strncmp(code, "AGILE-OUTGOING", 14) == 0) {
            continue;
        }

        direction = get_json_string_field(entry, "direction");
        display_name = get_json_string_field(entry, "display_name");
        if (direction == NULL || display_name == NULL) {
            continue;
        }

        if (strcmp(direction, "IMPORT") == 0 && strcmp(display_name, "Agile Octopus") == 0) {
            strlcpy(product_code, code, product_code_size);
            err = ESP_OK;
            goto cleanup;
        }
    }

    err = ESP_ERR_NOT_FOUND;

cleanup:
    cJSON_Delete(root);
    return err;
}

static cJSON *parse_json_document(const char *json_text)
{
    cJSON *root = NULL;
    const char *error_ptr = NULL;

    if (json_text == NULL || json_text[0] == '\0') {
        ESP_LOGW(TAG, "empty Octopus response");
        return NULL;
    }

    root = cJSON_Parse(json_text);
    if (root == NULL) {
        error_ptr = cJSON_GetErrorPtr();
        ESP_LOGW(TAG, "failed to parse Octopus JSON near %.32s", error_ptr != NULL ? error_ptr : "<unknown>");
        (void)error_ptr;
    }

    return root;
}

static void append_diagnostic_char(char *buffer, size_t buffer_size, size_t *write_index, char character)
{
    if (buffer == NULL || write_index == NULL || *write_index + 1 >= buffer_size) {
        return;
    }

    buffer[*write_index] = character;
    (*write_index)++;
    buffer[*write_index] = '\0';
}

static void append_diagnostic_hex_byte(char *buffer, size_t buffer_size, size_t *write_index, unsigned char value)
{
    static const char hex[] = "0123456789ABCDEF";

    append_diagnostic_char(buffer, buffer_size, write_index, '\\');
    append_diagnostic_char(buffer, buffer_size, write_index, 'x');
    append_diagnostic_char(buffer, buffer_size, write_index, hex[(value >> 4) & 0x0F]);
    append_diagnostic_char(buffer, buffer_size, write_index, hex[value & 0x0F]);
}

static void format_diagnostic_snippet(const char *text, size_t text_length, size_t start, size_t span, char *buffer, size_t buffer_size)
{
    size_t end = 0;
    size_t write_index = 0;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    buffer[0] = '\0';
    if (text == NULL || start >= text_length || span == 0) {
        return;
    }

    end = start + span;
    if (end > text_length) {
        end = text_length;
    }

    for (size_t index = start; index < end; index++) {
        unsigned char value = (unsigned char)text[index];

        if (value == '\n') {
            append_diagnostic_char(buffer, buffer_size, &write_index, '\\');
            append_diagnostic_char(buffer, buffer_size, &write_index, 'n');
        } else if (value == '\r') {
            append_diagnostic_char(buffer, buffer_size, &write_index, '\\');
            append_diagnostic_char(buffer, buffer_size, &write_index, 'r');
        } else if (value == '\t') {
            append_diagnostic_char(buffer, buffer_size, &write_index, '\\');
            append_diagnostic_char(buffer, buffer_size, &write_index, 't');
        } else if (value == '\\') {
            append_diagnostic_char(buffer, buffer_size, &write_index, '\\');
            append_diagnostic_char(buffer, buffer_size, &write_index, '\\');
        } else if (isprint(value) != 0) {
            append_diagnostic_char(buffer, buffer_size, &write_index, (char)value);
        } else {
            append_diagnostic_hex_byte(buffer, buffer_size, &write_index, value);
        }
    }
}

static bool response_looks_truncated(const char *text, size_t text_length)
{
    size_t index = text_length;

    if (text == NULL || text_length == 0) {
        return false;
    }

    while (index > 0 && isspace((unsigned char)text[index - 1]) != 0) {
        index--;
    }

    if (index == 0) {
        return false;
    }

    return text[index - 1] != '}' && text[index - 1] != ']';
}

static bool find_unexpected_byte(const char *text, size_t text_length, size_t *offset, unsigned char *value)
{
    if (text == NULL) {
        return false;
    }

    for (size_t index = 0; index < text_length; index++) {
        unsigned char current = (unsigned char)text[index];

        if (current == '\n' || current == '\r' || current == '\t' || (current >= 0x20 && current <= 0x7E)) {
            continue;
        }

        if (offset != NULL) {
            *offset = index;
        }
        if (value != NULL) {
            *value = current;
        }
        return true;
    }

    return false;
}

static void log_product_discovery_parse_failure(const char *json_text, size_t json_length)
{
    char prefix[OCTOPUS_DIAGNOSTIC_SNIPPET_BYTES * 4] = {0};
    char suffix[OCTOPUS_DIAGNOSTIC_SNIPPET_BYTES * 4] = {0};
    char error_window[(OCTOPUS_DIAGNOSTIC_ERROR_WINDOW_BYTES * 2U) * 4] = {0};
    const char *error_ptr = cJSON_GetErrorPtr();
    ptrdiff_t error_offset = -1;
    size_t unexpected_offset = 0;
    unsigned char unexpected_value = 0;
    bool has_unexpected_byte = false;
    bool looks_truncated = false;

    if (json_text == NULL) {
        return;
    }

    if (error_ptr != NULL && error_ptr >= json_text && error_ptr <= json_text + json_length) {
        error_offset = error_ptr - json_text;
    }

    format_diagnostic_snippet(json_text, json_length, 0, OCTOPUS_DIAGNOSTIC_SNIPPET_BYTES, prefix, sizeof(prefix));
    if (json_length > OCTOPUS_DIAGNOSTIC_SNIPPET_BYTES) {
        format_diagnostic_snippet(
            json_text,
            json_length,
            json_length - OCTOPUS_DIAGNOSTIC_SNIPPET_BYTES,
            OCTOPUS_DIAGNOSTIC_SNIPPET_BYTES,
            suffix,
            sizeof(suffix)
        );
    }

    if (error_offset >= 0) {
        size_t window_start = (size_t)error_offset;
        size_t window_length = OCTOPUS_DIAGNOSTIC_ERROR_WINDOW_BYTES * 2U;

        if (window_start > OCTOPUS_DIAGNOSTIC_ERROR_WINDOW_BYTES) {
            window_start -= OCTOPUS_DIAGNOSTIC_ERROR_WINDOW_BYTES;
        } else {
            window_start = 0;
        }

        format_diagnostic_snippet(json_text, json_length, window_start, window_length, error_window, sizeof(error_window));
    }

    looks_truncated = response_looks_truncated(json_text, json_length);
    has_unexpected_byte = find_unexpected_byte(json_text, json_length, &unexpected_offset, &unexpected_value);

    ESP_LOGW(
        TAG,
        "Products discovery parse failed length=%u error_offset=%d truncated=%s unexpected_byte=%s",
        (unsigned int)json_length,
        (int)error_offset,
        looks_truncated ? "yes" : "no",
        has_unexpected_byte ? "yes" : "no"
    );

    if (error_offset >= 0) {
        ESP_LOGW(TAG, "Products discovery parse window around offset %d: %s", (int)error_offset, error_window);
    }

    ESP_LOGW(TAG, "Products discovery parse prefix: %s", prefix[0] != '\0' ? prefix : "<empty>");
    ESP_LOGW(TAG, "Products discovery parse suffix: %s", suffix[0] != '\0' ? suffix : "<empty>");

    if (has_unexpected_byte) {
        ESP_LOGW(
            TAG,
            "Products discovery found unexpected byte 0x%02X at offset %u",
            (unsigned int)unexpected_value,
            (unsigned int)unexpected_offset
        );
    }
}

static esp_err_t get_results_array(cJSON *root, cJSON **results)
{
    cJSON *results_item = NULL;

    ESP_RETURN_ON_FALSE(root != NULL && results != NULL, ESP_ERR_INVALID_ARG, TAG, "results array lookup requires output");

    results_item = cJSON_GetObjectItemCaseSensitive(root, "results");
    if (!cJSON_IsArray(results_item)) {
        ESP_LOGW(TAG, "Octopus response missing results array");
        return ESP_ERR_INVALID_RESPONSE;
    }

    *results = results_item;
    return ESP_OK;
}

static const char *get_json_string_field(const cJSON *object, const char *field_name)
{
    cJSON *item = NULL;

    if (object == NULL || field_name == NULL) {
        return NULL;
    }

    item = cJSON_GetObjectItemCaseSensitive((cJSON *)object, field_name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static bool get_json_number_field(const cJSON *object, const char *field_name, double *value)
{
    cJSON *item = NULL;

    if (object == NULL || field_name == NULL || value == NULL) {
        return false;
    }

    item = cJSON_GetObjectItemCaseSensitive((cJSON *)object, field_name);
    if (!cJSON_IsNumber(item)) {
        return false;
    }

    *value = item->valuedouble;
    return true;
}

static esp_err_t parse_slots_from_response(const char *json_text, tariff_slot_t *slots, size_t max_slots, size_t *slot_count)
{
    size_t parsed_count = 0;
    size_t skipped_count = 0;
    cJSON *root = NULL;
    cJSON *results = NULL;
    cJSON *entry = NULL;
    esp_err_t err = ESP_OK;

    ESP_RETURN_ON_FALSE(json_text != NULL, ESP_ERR_INVALID_RESPONSE, TAG, "empty Octopus response");
    ESP_RETURN_ON_FALSE(slots != NULL && slot_count != NULL && max_slots > 0, ESP_ERR_INVALID_ARG, TAG, "slot buffer required");

    root = parse_json_document(json_text);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    err = get_results_array(root, &results);
    if (err != ESP_OK) {
        goto cleanup;
    }

    cJSON_ArrayForEach(entry, results) {
        double price_including_vat = 0.0;
        const char *valid_from_text = NULL;
        const char *valid_to_text = NULL;
        time_t start_utc = 0;
        time_t end_utc = 0;

        if (!cJSON_IsObject(entry)) {
            skipped_count++;
            continue;
        }

        if (parsed_count >= max_slots) {
            ESP_LOGW(TAG, "Octopus tariff response exceeded slot capacity %u", (unsigned int)max_slots);
            break;
        }

        valid_from_text = get_json_string_field(entry, "valid_from");
        valid_to_text = get_json_string_field(entry, "valid_to");
        if (!get_json_number_field(entry, "value_inc_vat", &price_including_vat) ||
            valid_from_text == NULL ||
            valid_to_text == NULL) {
            skipped_count++;
            continue;
        }

        if (!parse_iso8601_utc(valid_from_text, &start_utc) || !parse_iso8601_utc(valid_to_text, &end_utc)) {
            skipped_count++;
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
    }

    *slot_count = parsed_count;
    if (skipped_count > 0) {
        ESP_LOGW(TAG, "Skipped %u malformed Octopus tariff rows", (unsigned int)skipped_count);
    }

    if (parsed_count > 0) {
        err = ESP_OK;
        goto cleanup;
    }

    err = cJSON_GetArraySize(results) == 0 ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_RESPONSE;

cleanup:
    cJSON_Delete(root);
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

    return parse_slots_from_response(s_shared_response_buffer.data, slots, max_slots, slot_count);
}