#include "ota_manager.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_app_desc.h>
#include <esp_check.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

#include "octopus_client.h"
#include "ota_manager_internal.h"
#include "wifi_manager.h"

#define OTA_MANAGER_STACK_SIZE 12288
#define OTA_MANAGER_MAX_HTTP_REQUEST_SIZE 4096
#define OTA_MANAGER_HTTP_TX_BUFFER_BYTES 2048
#define OTA_MANAGER_METADATA_URL_MAX_LEN 192
#define OTA_MANAGER_METADATA_BUFFER_BYTES 2048

typedef enum {
    OTA_MANAGER_COMMAND_CHECK = 0,
    OTA_MANAGER_COMMAND_UPDATE,
} ota_manager_command_type_t;

typedef struct {
    ota_manager_command_type_t type;
} ota_manager_command_t;

static const char *TAG = "ota_manager";
static app_state_t *s_state;
static ota_release_metadata_t s_last_metadata;
static bool s_have_metadata;
static bool s_operation_in_progress;

typedef struct {
    char *buffer;
    size_t buffer_size;
    size_t length;
    esp_err_t error;
} ota_http_response_t;

#ifndef OTA_MANAGER_CERT_PEM
extern const char github_ota_root_bundle_pem_start[] asm("_binary_github_ota_root_bundle_pem_start");
extern const char github_ota_root_bundle_pem_end[] asm("_binary_github_ota_root_bundle_pem_end");
#define OTA_MANAGER_CERT_PEM github_ota_root_bundle_pem_start
#endif

static void ota_manager_task(void *arg);

static esp_err_t ota_http_event_handler(esp_http_client_event_t *event)
{
    ota_http_response_t *response = event != NULL ? (ota_http_response_t *)event->user_data : NULL;

    if (event == NULL || response == NULL) {
        return ESP_OK;
    }

    if (event->event_id == HTTP_EVENT_ON_DATA && event->data != NULL && event->data_len > 0) {
        size_t remaining_capacity = response->buffer_size - response->length;

        if (response->buffer == NULL || response->buffer_size == 0 || remaining_capacity <= (size_t)event->data_len) {
            response->error = ESP_ERR_NO_MEM;
            return ESP_FAIL;
        }

        memcpy(&response->buffer[response->length], event->data, (size_t)event->data_len);
        response->length += (size_t)event->data_len;
        response->buffer[response->length] = '\0';
    }

    return ESP_OK;
}

static void set_firmware_status(
    app_firmware_update_status_t status,
    bool update_available,
    const char *available_version,
    uint32_t available_version_code,
    uint8_t progress_percent,
    const char *status_text
)
{
    app_state_set_firmware_status(
        s_state,
        status,
        update_available,
        available_version,
        available_version_code,
        progress_percent,
        status_text
    );
}

static void initialize_current_version(void)
{
    const esp_app_desc_t *app_description = esp_app_get_description();
    const char *version = app_description != NULL ? app_description->version : "dev";

    app_state_set_firmware_info(s_state, version, ota_manager_version_code_from_string(version));
}

static esp_err_t perform_get_request(const char *url, char *response_buffer, size_t response_buffer_size)
{
    ota_http_response_t response = {
        .buffer = response_buffer,
        .buffer_size = response_buffer_size,
        .length = 0,
        .error = ESP_OK,
    };
    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 15000,
        .cert_pem = OTA_MANAGER_CERT_PEM,
        .buffer_size_tx = OTA_MANAGER_HTTP_TX_BUFFER_BYTES,
        .event_handler = ota_http_event_handler,
        .user_data = &response,
    };
    esp_http_client_handle_t http_client = NULL;
    int http_status = 0;
    int esp_tls_error_code = 0;
    int esp_tls_flags = 0;
    esp_err_t err = ESP_OK;

    ESP_RETURN_ON_FALSE(url != NULL && response_buffer != NULL && response_buffer_size > 1, ESP_ERR_INVALID_ARG, TAG, "HTTP request buffer required");

    response_buffer[0] = '\0';
    ESP_LOGI(
        TAG,
        "OTA GET start heap_free=%u largest=%u stack_hw=%u",
        (unsigned int)esp_get_free_heap_size(),
        (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned int)uxTaskGetStackHighWaterMark(NULL)
    );
    http_client = esp_http_client_init(&http_config);
    ESP_RETURN_ON_FALSE(http_client != NULL, ESP_ERR_NO_MEM, TAG, "create OTA HTTP client");

    err = esp_http_client_perform(http_client);
    if (err != ESP_OK) {
        (void)esp_http_client_get_and_clear_last_tls_error(http_client, &esp_tls_error_code, &esp_tls_flags);
        ESP_LOGW(TAG, "OTA GET %s failed: %s tls=0x%x flags=0x%x", url, esp_err_to_name(err), esp_tls_error_code, esp_tls_flags);
        goto cleanup;
    }

    http_status = esp_http_client_get_status_code(http_client);
    if (http_status != 200) {
        ESP_LOGW(TAG, "OTA metadata GET %s returned %d", url, http_status);
        err = ESP_FAIL;
        goto cleanup;
    }

    if (response.error != ESP_OK) {
        err = response.error;
        goto cleanup;
    }

    ESP_LOGI(
        TAG,
        "OTA GET done heap_free=%u largest=%u stack_hw=%u",
        (unsigned int)esp_get_free_heap_size(),
        (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned int)uxTaskGetStackHighWaterMark(NULL)
    );

cleanup:
    if (http_client != NULL) {
        esp_http_client_close(http_client);
        esp_http_client_cleanup(http_client);
    }

    return err;
}

static void build_metadata_url(char *buffer, size_t buffer_size)
{
    snprintf(
        buffer,
        buffer_size,
        "https://github.com/%s/%s/releases/latest/download/metadata.json",
        CONFIG_GREENLIGHT_GITHUB_OWNER,
        CONFIG_GREENLIGHT_GITHUB_REPO
    );
}

static esp_err_t perform_update_check(void)
{
    char current_version[APP_FIRMWARE_VERSION_TEXT_MAX_LEN] = {0};
    uint32_t current_version_code = 0;
    char metadata_url[OTA_MANAGER_METADATA_URL_MAX_LEN] = {0};
    char metadata_json[OTA_MANAGER_METADATA_BUFFER_BYTES] = {0};
    ota_release_metadata_t metadata = {0};
    esp_err_t err = ESP_OK;
    int version_compare = 0;

    app_state_get_firmware_info(s_state, current_version, sizeof(current_version), &current_version_code);

    if (!wifi_manager_is_connected()) {
        set_firmware_status(APP_FIRMWARE_UPDATE_STATUS_IDLE, false, "", 0, 0, "Connect to Wi-Fi to check for firmware updates");
        return ESP_ERR_INVALID_STATE;
    }

    build_metadata_url(metadata_url, sizeof(metadata_url));
    octopus_client_release_memory();
    set_firmware_status(APP_FIRMWARE_UPDATE_STATUS_CHECKING, false, "", 0, 0, "Checking GitHub Releases for firmware updates");

    err = perform_get_request(metadata_url, metadata_json, sizeof(metadata_json));
    if (err != ESP_OK) {
        set_firmware_status(APP_FIRMWARE_UPDATE_STATUS_ERROR, false, "", 0, 0, "Firmware update check failed");
        return err;
    }

    err = ota_manager_parse_release_metadata(metadata_json, &metadata);
    if (err != ESP_OK) {
        set_firmware_status(APP_FIRMWARE_UPDATE_STATUS_ERROR, false, "", 0, 0, "Firmware metadata was invalid");
        return err;
    }

    version_compare = ota_manager_compare_versions(
        current_version_code,
        current_version,
        metadata.version_code,
        metadata.version
    );

    s_last_metadata = metadata;
    s_have_metadata = true;

    if (version_compare < 0) {
        char status_text[APP_FIRMWARE_STATUS_TEXT_MAX_LEN] = {0};

        snprintf(status_text, sizeof(status_text), "New firmware %s is available", metadata.version);
        set_firmware_status(
            APP_FIRMWARE_UPDATE_STATUS_AVAILABLE,
            true,
            metadata.version,
            metadata.version_code,
            0,
            status_text
        );
        return ESP_OK;
    }

    set_firmware_status(APP_FIRMWARE_UPDATE_STATUS_UP_TO_DATE, false, metadata.version, metadata.version_code, 0, "Firmware is up to date");
    return ESP_OK;
}

static void digest_to_hex(const uint8_t digest[32], char *buffer, size_t buffer_size)
{
    static const char hex_digits[] = "0123456789abcdef";

    if (digest == NULL || buffer == NULL || buffer_size < OTA_RELEASE_SHA256_TEXT_LEN) {
        return;
    }

    for (size_t index = 0; index < 32; index++) {
        buffer[index * 2] = hex_digits[(digest[index] >> 4) & 0x0F];
        buffer[(index * 2) + 1] = hex_digits[digest[index] & 0x0F];
    }

    buffer[64] = '\0';
}

static esp_err_t verify_partition_sha256(const esp_partition_t *partition, const char *expected_sha256)
{
    uint8_t digest[32] = {0};
    char digest_hex[OTA_RELEASE_SHA256_TEXT_LEN] = {0};

    ESP_RETURN_ON_FALSE(partition != NULL && expected_sha256 != NULL, ESP_ERR_INVALID_ARG, TAG, "OTA partition verification arguments required");
    ESP_RETURN_ON_ERROR(esp_partition_get_sha256(partition, digest), TAG, "read OTA partition digest");

    digest_to_hex(digest, digest_hex, sizeof(digest_hex));
    if (strcmp(digest_hex, expected_sha256) != 0) {
        ESP_LOGE(TAG, "OTA metadata sha256 %s did not match app image digest %s", expected_sha256, digest_hex);
        return ESP_ERR_INVALID_CRC;
    }

    return ESP_OK;
}

static esp_err_t perform_update_install(void)
{
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    esp_http_client_config_t http_config = {0};
    esp_https_ota_config_t ota_config = {0};
    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t err = ESP_OK;
    int image_size = -1;
    int image_len_read = 0;
    int last_logged_progress = -1;
    int last_status_progress = -1;
    bool complete_data_received = false;
    char status_text[APP_FIRMWARE_STATUS_TEXT_MAX_LEN] = {0};

    if (!wifi_manager_is_connected()) {
        set_firmware_status(APP_FIRMWARE_UPDATE_STATUS_ERROR, false, s_last_metadata.version, s_last_metadata.version_code, 0, "Connect to Wi-Fi before updating firmware");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_have_metadata) {
        set_firmware_status(APP_FIRMWARE_UPDATE_STATUS_ERROR, false, "", 0, 0, "Check for updates before starting OTA");
        return ESP_ERR_INVALID_STATE;
    }

    snprintf(status_text, sizeof(status_text), "Downloading firmware %s", s_last_metadata.version);
    set_firmware_status(
        APP_FIRMWARE_UPDATE_STATUS_DOWNLOADING,
        true,
        s_last_metadata.version,
        s_last_metadata.version_code,
        0,
        status_text
    );

    http_config.url = s_last_metadata.firmware_url;
    http_config.timeout_ms = 20000;
    http_config.cert_pem = OTA_MANAGER_CERT_PEM;
    http_config.buffer_size_tx = OTA_MANAGER_HTTP_TX_BUFFER_BYTES;

    octopus_client_release_memory();
    ota_config.http_config = &http_config;
#if CONFIG_ESP_HTTPS_OTA_ENABLE_PARTIAL_DOWNLOAD
    ota_config.partial_http_download = true;
    ota_config.max_http_request_size = OTA_MANAGER_MAX_HTTP_REQUEST_SIZE;
#endif
    err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        set_firmware_status(
            APP_FIRMWARE_UPDATE_STATUS_ERROR,
            true,
            s_last_metadata.version,
            s_last_metadata.version_code,
            0,
            "Firmware download or apply failed"
        );
        return err;
    }

    image_size = esp_https_ota_get_image_size(ota_handle);
    ESP_LOGI(TAG, "OTA download started target=%s image_size=%d", s_last_metadata.version, image_size);

    while ((err = esp_https_ota_perform(ota_handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int progress_percent = 0;

        image_len_read = esp_https_ota_get_image_len_read(ota_handle);
        if (image_size > 0 && image_len_read >= 0) {
            progress_percent = (image_len_read * 100) / image_size;
            if (progress_percent > 99) {
                progress_percent = 99;
            }
        }

        if (progress_percent != last_status_progress) {
            snprintf(
                status_text,
                sizeof(status_text),
                "Downloading firmware %s",
                s_last_metadata.version
            );

            set_firmware_status(
                APP_FIRMWARE_UPDATE_STATUS_DOWNLOADING,
                true,
                s_last_metadata.version,
                s_last_metadata.version_code,
                (uint8_t)progress_percent,
                status_text
            );
            last_status_progress = progress_percent;
        }

        if (progress_percent >= 0 && (last_logged_progress < 0 || progress_percent >= last_logged_progress + 10)) {
            ESP_LOGI(
                TAG,
                "OTA download progress read=%d total=%d percent=%d",
                image_len_read,
                image_size,
                progress_percent
            );
            last_logged_progress = progress_percent;
        }
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA download failed after %d bytes: %s", esp_https_ota_get_image_len_read(ota_handle), esp_err_to_name(err));
        (void)esp_https_ota_abort(ota_handle);
        set_firmware_status(
            APP_FIRMWARE_UPDATE_STATUS_ERROR,
            true,
            s_last_metadata.version,
            s_last_metadata.version_code,
            0,
            "Firmware download or apply failed"
        );
        return err;
    }

    complete_data_received = esp_https_ota_is_complete_data_received(ota_handle);
    if (!complete_data_received) {
        ESP_LOGW(TAG, "OTA download ended before complete image was received");
        (void)esp_https_ota_abort(ota_handle);
        set_firmware_status(
            APP_FIRMWARE_UPDATE_STATUS_ERROR,
            true,
            s_last_metadata.version,
            s_last_metadata.version_code,
            0,
            "Firmware download was incomplete"
        );
        return ESP_FAIL;
    }

    err = esp_https_ota_finish(ota_handle);
    ota_handle = NULL;
    if (err != ESP_OK) {
        set_firmware_status(
            APP_FIRMWARE_UPDATE_STATUS_ERROR,
            true,
            s_last_metadata.version,
            s_last_metadata.version_code,
            0,
            "Firmware download or apply failed"
        );
        return err;
    }

    ESP_LOGI(TAG, "OTA image written successfully (%d bytes)", image_len_read);

    set_firmware_status(
        APP_FIRMWARE_UPDATE_STATUS_APPLYING,
        true,
        s_last_metadata.version,
        s_last_metadata.version_code,
        100,
        "Validating downloaded firmware"
    );

    err = verify_partition_sha256(update_partition, s_last_metadata.sha256);
    if (err != ESP_OK) {
        if (running_partition != NULL) {
            (void)esp_ota_set_boot_partition(running_partition);
        }
        set_firmware_status(
            APP_FIRMWARE_UPDATE_STATUS_ERROR,
            true,
            s_last_metadata.version,
            s_last_metadata.version_code,
            0,
            "Firmware checksum mismatch"
        );
        return err;
    }

    set_firmware_status(
        APP_FIRMWARE_UPDATE_STATUS_REBOOTING,
        true,
        s_last_metadata.version,
        s_last_metadata.version_code,
        100,
        "Firmware installed. Rebooting now"
    );

    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
    return ESP_OK;
}

static void ota_manager_task(void *arg)
{
    ota_manager_command_type_t command = (ota_manager_command_type_t)(intptr_t)arg;

    switch (command) {
        case OTA_MANAGER_COMMAND_CHECK:
            (void)perform_update_check();
            break;
        case OTA_MANAGER_COMMAND_UPDATE:
            (void)perform_update_install();
            break;
        default:
            break;
    }

    s_operation_in_progress = false;
    vTaskDelete(NULL);
}

esp_err_t ota_manager_init(app_state_t *state)
{
    ESP_RETURN_ON_FALSE(state != NULL, ESP_ERR_INVALID_ARG, TAG, "OTA state required");

    s_state = state;
    initialize_current_version();
    return ESP_OK;
}

esp_err_t ota_manager_request_check(void)
{
    BaseType_t task_created = pdFALSE;

    ESP_RETURN_ON_FALSE(s_state != NULL, ESP_ERR_INVALID_STATE, TAG, "OTA manager not initialized");

    if (s_operation_in_progress) {
        return ESP_OK;
    }

    s_operation_in_progress = true;
    task_created = xTaskCreatePinnedToCore(
        ota_manager_task,
        "ota_check",
        OTA_MANAGER_STACK_SIZE,
        (void *)(intptr_t)OTA_MANAGER_COMMAND_CHECK,
        5,
        NULL,
        tskNO_AFFINITY
    );
    if (task_created != pdPASS) {
        s_operation_in_progress = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t ota_manager_request_update(void)
{
    BaseType_t task_created = pdFALSE;

    ESP_RETURN_ON_FALSE(s_state != NULL, ESP_ERR_INVALID_STATE, TAG, "OTA manager not initialized");

    if (s_operation_in_progress) {
        return ESP_OK;
    }

    s_operation_in_progress = true;
    task_created = xTaskCreatePinnedToCore(
        ota_manager_task,
        "ota_apply",
        OTA_MANAGER_STACK_SIZE,
        (void *)(intptr_t)OTA_MANAGER_COMMAND_UPDATE,
        5,
        NULL,
        tskNO_AFFINITY
    );
    if (task_created != pdPASS) {
        s_operation_in_progress = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}