#define _GNU_SOURCE

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    (void)config;
    return NULL;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t client)
{
    (void)client;
    return ESP_FAIL;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client)
{
    (void)client;
    return 0;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client)
{
    (void)client;
    return ESP_OK;
}

esp_err_t esp_crt_bundle_attach(void *conf)
{
    (void)conf;
    return ESP_OK;
}

#include "../main/octopus_client.c"

#undef strlcpy

static const char *PRODUCT_DISCOVERY_FIXTURE =
    "{"
    "\"count\":4,"
    "\"results\":["
    "{\"code\":\"GO-25-01-01\",\"direction\":\"IMPORT\",\"display_name\":\"Go\"},"
    "{\"code\":\"AGILE-OUTGOING-25-02-01\",\"direction\":\"EXPORT\",\"display_name\":\"Agile Outgoing\"},"
    "{\"code\":\"AGILE-25-02-01\",\"direction\":\"IMPORT\"},"
    "{\"code\":\"AGILE-25-02-01\",\"direction\":\"IMPORT\",\"display_name\":\"Agile Octopus\",\"unexpected\":{\"brand\":\"Octopus\"}}"
    "]"
    "}";

static const char *TARIFF_FIXTURE_WITH_PARTIAL_ROWS =
    "{"
    "\"count\":5,"
    "\"results\":["
    "{\"valid_from\":\"2024-05-14T00:00:00Z\",\"valid_to\":\"2024-05-14T00:30:00Z\",\"value_inc_vat\":12.34},"
    "{\"valid_from\":\"2024-05-14T00:30:00Z\",\"valid_to\":\"2024-05-14T01:00:00Z\"},"
    "{\"valid_from\":\"bad-date\",\"valid_to\":\"2024-05-14T01:30:00Z\",\"value_inc_vat\":18.76},"
    "{\"valid_from\":\"2024-05-14T23:30:00Z\",\"valid_to\":\"2024-05-15T00:00:00Z\",\"value_inc_vat\":9.99},"
    "{\"valid_from\":\"2024-05-15T00:00:00Z\",\"valid_to\":\"2024-05-15T00:30:00Z\",\"value_inc_vat\":8.88}"
    "]"
    "}";

static const char *INVALID_TARIFF_FIXTURE =
    "{"
    "\"count\":1,"
    "\"results\":["
    "{\"valid_from\":42,\"valid_to\":false,\"value_inc_vat\":\"bad\"}"
    "]"
    "}";

static void test_product_discovery_parser(void)
{
    char product_code[32] = {0};

    assert(discover_active_product_code_from_response(PRODUCT_DISCOVERY_FIXTURE, product_code, sizeof(product_code)) == ESP_OK);
    assert(strcmp(product_code, "AGILE-25-02-01") == 0);
}

static void test_tariff_parser_skips_malformed_rows(void)
{
    tariff_slot_t slots[8] = {0};
    size_t slot_count = 0;

    assert(parse_slots_from_response(TARIFF_FIXTURE_WITH_PARTIAL_ROWS, slots, 8, &slot_count) == ESP_OK);
    assert(slot_count == 3);
    assert(slots[0].price_including_vat == 12.34f);
    assert(slots[1].price_including_vat == 9.99f);
    assert(slots[2].price_including_vat == 8.88f);
    assert(slots[1].start_utc < slots[2].start_utc);
}

static void test_tariff_parser_rejects_all_invalid_rows(void)
{
    tariff_slot_t slots[4] = {0};
    size_t slot_count = 99;

    assert(parse_slots_from_response(INVALID_TARIFF_FIXTURE, slots, 4, &slot_count) == ESP_ERR_INVALID_RESPONSE);
    assert(slot_count == 0);
}

int main(void)
{
    setenv("TZ", "UTC", 1);
    tzset();

    test_product_discovery_parser();
    test_tariff_parser_skips_malformed_rows();
    test_tariff_parser_rejects_all_invalid_rows();

    puts("octopus_client parser harness passed");
    return 0;
}