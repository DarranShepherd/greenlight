#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "octopus_client_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <esp_check.h>
#include <esp_log.h>

#include <cJSON.h>

static const char *TAG = "octopus_client";

static void copy_string(char *destination, size_t destination_size, const char *source)
{
    size_t source_length = 0;

    if (destination == NULL || destination_size == 0) {
        return;
    }

    if (source == NULL) {
        destination[0] = '\0';
        return;
    }

    source_length = strlen(source);
    if (source_length >= destination_size) {
        memcpy(destination, source, destination_size - 1);
        destination[destination_size - 1] = '\0';
        return;
    }

    memcpy(destination, source, source_length + 1);
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

void octopus_product_discovery_parser_init(octopus_product_discovery_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }

    memset(parser, 0, sizeof(*parser));
}

esp_err_t octopus_product_discovery_parser_feed(
    octopus_product_discovery_parser_t *parser,
    const char *chunk,
    size_t chunk_length,
    char *product_code,
    size_t product_code_size,
    bool *matched,
    bool *done
)
{
    ESP_RETURN_ON_FALSE(
        parser != NULL &&
            chunk != NULL &&
            product_code != NULL &&
            product_code_size > 0 &&
            matched != NULL &&
            done != NULL,
        ESP_ERR_INVALID_ARG,
        TAG,
        "parser feed arguments required"
    );

    *matched = false;
    *done = false;

    for (size_t index = 0; index < chunk_length; index++) {
        char character = chunk[index];

        parser->total_bytes_read++;

        if (!parser->in_results_array) {
            if (parser->results_window_length + 1 < sizeof(parser->results_window)) {
                parser->results_window[parser->results_window_length++] = character;
            } else {
                memmove(parser->results_window, parser->results_window + 1, sizeof(parser->results_window) - 2);
                parser->results_window[sizeof(parser->results_window) - 2] = character;
                parser->results_window_length = sizeof(parser->results_window) - 1;
            }
            parser->results_window[parser->results_window_length] = '\0';

            if (strstr(parser->results_window, "\"results\":[") != NULL) {
                parser->in_results_array = true;
            }

            continue;
        }

        if (!parser->capturing_object) {
            if (character == '{') {
                parser->capturing_object = true;
                parser->object_length = 0;
                parser->brace_depth = 0;
                parser->in_string = false;
                parser->escape_next = false;
            } else if (character == ']') {
                *done = true;
                return ESP_OK;
            } else {
                continue;
            }
        }

        if (parser->object_length + 1 >= sizeof(parser->object_buffer)) {
            ESP_LOGW(
                TAG,
                "Product discovery object exceeded %u bytes after %u response bytes",
                (unsigned int)sizeof(parser->object_buffer),
                (unsigned int)parser->total_bytes_read
            );
            return ESP_ERR_NO_MEM;
        }

        parser->object_buffer[parser->object_length++] = character;

        if (parser->in_string) {
            if (parser->escape_next) {
                parser->escape_next = false;
            } else if (character == '\\') {
                parser->escape_next = true;
            } else if (character == '"') {
                parser->in_string = false;
            }
        } else {
            if (character == '"') {
                parser->in_string = true;
            } else if (character == '{') {
                parser->brace_depth++;
            } else if (character == '}') {
                parser->brace_depth--;
                if (parser->brace_depth == 0) {
                    cJSON *entry = NULL;
                    const char *code = NULL;
                    const char *direction = NULL;
                    const char *display_name = NULL;

                    parser->object_buffer[parser->object_length] = '\0';
                    entry = parse_json_document(parser->object_buffer);
                    if (entry == NULL) {
                        return ESP_ERR_INVALID_RESPONSE;
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
                        copy_string(product_code, product_code_size, code);
                        cJSON_Delete(entry);
                        ESP_LOGI(TAG, "Products discovery completed after %u bytes", (unsigned int)parser->total_bytes_read);
                        *matched = true;
                        *done = true;
                        return ESP_OK;
                    }

                    cJSON_Delete(entry);
                    parser->capturing_object = false;
                    parser->object_length = 0;
                    parser->object_buffer[0] = '\0';
                }
            }
        }
    }

    return ESP_OK;
}

esp_err_t octopus_client_discover_active_product_code_from_response(const char *json_text, char *product_code, size_t product_code_size)
{
    cJSON *root = NULL;
    cJSON *results = NULL;
    cJSON *entry = NULL;
    esp_err_t err = ESP_OK;

    ESP_RETURN_ON_FALSE(
        json_text != NULL && product_code != NULL && product_code_size > 0,
        ESP_ERR_INVALID_ARG,
        TAG,
        "product discovery response buffer required"
    );

    root = parse_json_document(json_text);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
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
        direction = get_json_string_field(entry, "direction");
        display_name = get_json_string_field(entry, "display_name");

        if (code != NULL &&
            strncmp(code, "AGILE-", 6) == 0 &&
            strncmp(code, "AGILE-OUTGOING", 14) != 0 &&
            direction != NULL &&
            strcmp(direction, "IMPORT") == 0 &&
            display_name != NULL &&
            strcmp(display_name, "Agile Octopus") == 0) {
            copy_string(product_code, product_code_size, code);
            err = ESP_OK;
            goto cleanup;
        }
    }

    err = ESP_ERR_NOT_FOUND;

cleanup:
    cJSON_Delete(root);
    return err;
}

esp_err_t octopus_client_parse_slots_from_response(
    const char *json_text,
    tariff_slot_t *slots,
    size_t max_slots,
    size_t *slot_count
)
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