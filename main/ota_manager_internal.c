#include "ota_manager_internal.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <esp_check.h>

#include <cJSON.h>

static bool parse_semver_triplet(const char *version_text, uint32_t parts[3])
{
    const char *cursor = version_text;
    char *end = NULL;

    if (version_text == NULL || parts == NULL) {
        return false;
    }

    while (*cursor == 'v' || *cursor == 'V' || isspace((unsigned char)*cursor)) {
        cursor++;
    }

    for (size_t index = 0; index < 3; index++) {
        unsigned long value = 0;

        if (!isdigit((unsigned char)*cursor)) {
            return false;
        }

        value = strtoul(cursor, &end, 10);
        if (end == cursor) {
            return false;
        }

        parts[index] = (uint32_t)value;
        cursor = end;

        if (index == 2) {
            break;
        }

        if (*cursor != '.') {
            parts[index + 1] = 0;
            if (index == 0) {
                parts[2] = 0;
            }
            return true;
        }

        cursor++;
    }

    return true;
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

static bool get_json_number_field(const cJSON *object, const char *field_name, uint32_t *value)
{
    cJSON *item = NULL;

    if (object == NULL || field_name == NULL || value == NULL) {
        return false;
    }

    item = cJSON_GetObjectItemCaseSensitive((cJSON *)object, field_name);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0) {
        return false;
    }

    *value = (uint32_t)item->valuedouble;
    return true;
}

static bool is_lower_hex_string(const char *text, size_t expected_length)
{
    size_t length = 0;

    if (text == NULL) {
        return false;
    }

    length = strlen(text);
    if (length != expected_length) {
        return false;
    }

    for (size_t index = 0; index < length; index++) {
        if (!isxdigit((unsigned char)text[index])) {
            return false;
        }
    }

    return true;
}

uint32_t ota_manager_version_code_from_string(const char *version_text)
{
    uint32_t parts[3] = {0};

    if (!parse_semver_triplet(version_text, parts)) {
        return 0;
    }

    if (parts[1] > 99 || parts[2] > 99) {
        return 0;
    }

    return (parts[0] * 10000U) + (parts[1] * 100U) + parts[2];
}

int ota_manager_compare_versions(
    uint32_t current_version_code,
    const char *current_version,
    uint32_t candidate_version_code,
    const char *candidate_version
)
{
    uint32_t current_parts[3] = {0};
    uint32_t candidate_parts[3] = {0};
    bool have_current_parts = parse_semver_triplet(current_version, current_parts);
    bool have_candidate_parts = parse_semver_triplet(candidate_version, candidate_parts);

    if (current_version_code == 0) {
        current_version_code = ota_manager_version_code_from_string(current_version);
    }

    if (candidate_version_code == 0) {
        candidate_version_code = ota_manager_version_code_from_string(candidate_version);
    }

    if (current_version_code != 0 || candidate_version_code != 0) {
        if (current_version_code < candidate_version_code) {
            return -1;
        }
        if (current_version_code > candidate_version_code) {
            return 1;
        }
    }

    if (have_current_parts && have_candidate_parts) {
        for (size_t index = 0; index < 3; index++) {
            if (current_parts[index] < candidate_parts[index]) {
                return -1;
            }
            if (current_parts[index] > candidate_parts[index]) {
                return 1;
            }
        }
        return 0;
    }

    if (current_version == NULL && candidate_version == NULL) {
        return 0;
    }
    if (current_version == NULL) {
        return -1;
    }
    if (candidate_version == NULL) {
        return 1;
    }

    return strcmp(current_version, candidate_version);
}

esp_err_t ota_manager_parse_release_metadata(const char *json_text, ota_release_metadata_t *metadata)
{
    cJSON *root = NULL;
    const char *version = NULL;
    const char *firmware_url = NULL;
    const char *sha256 = NULL;

    ESP_RETURN_ON_FALSE(json_text != NULL && metadata != NULL, ESP_ERR_INVALID_ARG, "ota_manager_internal", "metadata arguments required");

    memset(metadata, 0, sizeof(*metadata));

    root = cJSON_Parse(json_text);
    ESP_RETURN_ON_FALSE(root != NULL, ESP_ERR_INVALID_RESPONSE, "ota_manager_internal", "invalid OTA metadata JSON");

    version = get_json_string_field(root, "version");
    firmware_url = get_json_string_field(root, "firmware_url");
    sha256 = get_json_string_field(root, "sha256");

    if (version == NULL || firmware_url == NULL || sha256 == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (!get_json_number_field(root, "version_code", &metadata->version_code)) {
        metadata->version_code = ota_manager_version_code_from_string(version);
    }

    if (metadata->version_code == 0 || !is_lower_hex_string(sha256, OTA_RELEASE_SHA256_TEXT_LEN - 1U)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    strlcpy(metadata->version, version, sizeof(metadata->version));
    strlcpy(metadata->firmware_url, firmware_url, sizeof(metadata->firmware_url));
    strlcpy(metadata->sha256, sha256, sizeof(metadata->sha256));
    cJSON_Delete(root);
    return ESP_OK;
}