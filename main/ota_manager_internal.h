#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <esp_err.h>

#define OTA_RELEASE_VERSION_TEXT_MAX_LEN 32
#define OTA_RELEASE_BOARD_ID_MAX_LEN 32
#define OTA_RELEASE_URL_MAX_LEN 320
#define OTA_RELEASE_SHA256_TEXT_LEN 65

typedef struct {
    char version[OTA_RELEASE_VERSION_TEXT_MAX_LEN];
    uint32_t version_code;
    char board_id[OTA_RELEASE_BOARD_ID_MAX_LEN];
    char firmware_url[OTA_RELEASE_URL_MAX_LEN];
    char sha256[OTA_RELEASE_SHA256_TEXT_LEN];
} ota_release_metadata_t;

/*
 * OTA metadata contract for issue #9:
 * - Manifests must provide a variants object.
 * - Runtime selection requires variants.<board_id>.firmware_url and
 *   variants.<board_id>.sha256 for the local compiled board.
 * - Selection fails closed if variants is missing or the local board entry is
 *   missing or invalid.
 */
esp_err_t ota_manager_parse_release_metadata(const char *json_text, const char *board_id, ota_release_metadata_t *metadata);
bool ota_manager_release_metadata_is_selected_for_board(const ota_release_metadata_t *metadata, const char *board_id);
uint32_t ota_manager_version_code_from_string(const char *version_text);
int ota_manager_compare_versions(
    uint32_t current_version_code,
    const char *current_version,
    uint32_t candidate_version_code,
    const char *candidate_version
);