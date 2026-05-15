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

static bool is_non_empty_string(const char *text)
{
    return text != NULL && text[0] != '\0';
}

static const cJSON *find_variant_metadata(const cJSON *variants, const char *board_id)
{
    if (!cJSON_IsObject((cJSON *)variants) || !is_non_empty_string(board_id)) {
        return NULL;
    }

    return cJSON_GetObjectItemCaseSensitive((cJSON *)variants, board_id);
}

static bool validate_variant_metadata_map(const cJSON *variants)
{
    const cJSON *variant = NULL;

    if (variants == NULL) {
        return false;
    }

    if (!cJSON_IsObject(variants)) {
        return false;
    }

    cJSON_ArrayForEach(variant, (cJSON *)variants) {
        const char *firmware_url = NULL;
        const char *sha256 = NULL;

        if (!cJSON_IsObject(variant) || variant->string == NULL || variant->string[0] == '\0') {
            return false;
        }

        firmware_url = get_json_string_field(variant, "firmware_url");
        sha256 = get_json_string_field(variant, "sha256");
        if (firmware_url == NULL || sha256 == NULL) {
            return false;
        }

        if (!is_lower_hex_string(sha256, OTA_RELEASE_SHA256_TEXT_LEN - 1U)) {
            return false;
        }
    }

    return true;
}

bool ota_manager_release_metadata_is_selected_for_board(const ota_release_metadata_t *metadata, const char *board_id)
{
    if (metadata == NULL || !is_non_empty_string(board_id)) {
        return false;
    }

    if (strcmp(metadata->board_id, board_id) != 0) {
        return false;
    }

    if (!is_non_empty_string(metadata->version) || metadata->version_code == 0) {
        return false;
    }

    if (!is_non_empty_string(metadata->firmware_url)) {
        return false;
    }

    return is_lower_hex_string(metadata->sha256, OTA_RELEASE_SHA256_TEXT_LEN - 1U);
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

esp_err_t ota_manager_parse_release_metadata(const char *json_text, const char *board_id, ota_release_metadata_t *metadata)
{
    cJSON *root = NULL;
    cJSON *variants = NULL;
    const cJSON *selected_variant = NULL;
    const char *version = NULL;
    const char *firmware_url = NULL;
    const char *sha256 = NULL;

    ESP_RETURN_ON_FALSE(
        json_text != NULL && metadata != NULL && is_non_empty_string(board_id),
        ESP_ERR_INVALID_ARG,
        "ota_manager_internal",
        "metadata arguments required"
    );

    memset(metadata, 0, sizeof(*metadata));

    root = cJSON_Parse(json_text);
    ESP_RETURN_ON_FALSE(root != NULL, ESP_ERR_INVALID_RESPONSE, "ota_manager_internal", "invalid OTA metadata JSON");

    version = get_json_string_field(root, "version");
    variants = cJSON_GetObjectItemCaseSensitive(root, "variants");

    if (version == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (!validate_variant_metadata_map(variants)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    selected_variant = find_variant_metadata(variants, board_id);
    if (!cJSON_IsObject((cJSON *)selected_variant)) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    firmware_url = get_json_string_field(selected_variant, "firmware_url");
    sha256 = get_json_string_field(selected_variant, "sha256");

    if (firmware_url == NULL || sha256 == NULL) {
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
    strlcpy(metadata->board_id, board_id, sizeof(metadata->board_id));
    strlcpy(metadata->firmware_url, firmware_url, sizeof(metadata->firmware_url));
    strlcpy(metadata->sha256, sha256, sizeof(metadata->sha256));
    cJSON_Delete(root);
    return ESP_OK;
}