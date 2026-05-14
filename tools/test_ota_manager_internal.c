#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ota_manager_internal.h"

static const char *VALID_METADATA_FIXTURE =
    "{"
    "\"version\":\"0.4.0\"," 
    "\"version_code\":400," 
    "\"firmware_url\":\"https://github.com/DarranShepherd/greenlight/releases/download/v0.4.0/firmware.bin\"," 
    "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\""
    "}";

static const char *MISSING_VERSION_CODE_FIXTURE =
    "{"
    "\"version\":\"v1.2.3\"," 
    "\"firmware_url\":\"https://example.invalid/fw.bin\"," 
    "\"sha256\":\"abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd\""
    "}";

static const char *INVALID_METADATA_FIXTURE =
    "{"
    "\"version\":\"0.4.0\"," 
    "\"firmware_url\":42," 
    "\"sha256\":\"bad\""
    "}";

static void test_metadata_parse_accepts_valid_manifest(void)
{
    ota_release_metadata_t metadata = {0};

    assert(ota_manager_parse_release_metadata(VALID_METADATA_FIXTURE, &metadata) == ESP_OK);
    assert(strcmp(metadata.version, "0.4.0") == 0);
    assert(metadata.version_code == 400);
    assert(strstr(metadata.firmware_url, "firmware.bin") != NULL);
}

static void test_metadata_parse_derives_version_code(void)
{
    ota_release_metadata_t metadata = {0};

    assert(ota_manager_parse_release_metadata(MISSING_VERSION_CODE_FIXTURE, &metadata) == ESP_OK);
    assert(metadata.version_code == 10203);
}

static void test_metadata_parse_rejects_invalid_manifest(void)
{
    ota_release_metadata_t metadata = {0};

    assert(ota_manager_parse_release_metadata(INVALID_METADATA_FIXTURE, &metadata) == ESP_ERR_INVALID_RESPONSE);
}

static void test_version_compare_prefers_newer_release(void)
{
    assert(ota_manager_compare_versions(300, "0.3.0", 400, "0.4.0") < 0);
    assert(ota_manager_compare_versions(400, "0.4.0", 400, "0.4.0") == 0);
    assert(ota_manager_compare_versions(500, "0.5.0", 400, "0.4.0") > 0);
    assert(ota_manager_compare_versions(0, "v1.9.0", 0, "v1.10.0") < 0);
}

int main(void)
{
    test_metadata_parse_accepts_valid_manifest();
    test_metadata_parse_derives_version_code();
    test_metadata_parse_rejects_invalid_manifest();
    test_version_compare_prefers_newer_release();

    puts("ota_manager_internal harness passed");
    return 0;
}