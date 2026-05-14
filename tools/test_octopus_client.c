#define _GNU_SOURCE

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "octopus_client_internal.h"

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

static const char *MALFORMED_PRODUCT_DISCOVERY_FIXTURE =
    "{"
    "\"count\":1,"
    "\"results\":["
    "{\"code\":\"AGILE-25-02-01\",\"direction\":\"IMPORT\",}"
    "]"
    "}";

static const char *NON_MATCHING_PRODUCT_DISCOVERY_FIXTURE =
    "{"
    "\"count\":2,"
    "\"results\":["
    "{\"code\":\"AGILE-25-02-01\",\"direction\":\"IMPORT\"},"
    "{\"code\":\"AGILE-OUTGOING-25-02-01\",\"direction\":\"EXPORT\",\"display_name\":\"Agile Octopus\"}"
    "]"
    "}";

static esp_err_t discover_active_product_code_streaming_fixture(
    const char *response,
    size_t chunk_size,
    char *product_code,
    size_t product_code_size
)
{
    octopus_product_discovery_parser_t parser = {0};
    size_t response_length = response != NULL ? strlen(response) : 0;
    bool matched = false;
    bool done = false;
    esp_err_t err = ESP_OK;

    octopus_product_discovery_parser_init(&parser);

    for (size_t offset = 0; offset < response_length; offset += chunk_size) {
        size_t current_chunk_size = response_length - offset;

        if (current_chunk_size > chunk_size) {
            current_chunk_size = chunk_size;
        }

        err = octopus_product_discovery_parser_feed(
            &parser,
            response + offset,
            current_chunk_size,
            product_code,
            product_code_size,
            &matched,
            &done
        );
        if (err != ESP_OK) {
            return err;
        }

        if (done) {
            return matched ? ESP_OK : ESP_ERR_NOT_FOUND;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

static void test_product_discovery_parser(void)
{
    char product_code[32] = {0};

    assert(discover_active_product_code_streaming_fixture(PRODUCT_DISCOVERY_FIXTURE, 17, product_code, sizeof(product_code)) == ESP_OK);
    assert(strcmp(product_code, "AGILE-25-02-01") == 0);
}

static void test_product_discovery_parser_rejects_malformed_streamed_object(void)
{
    char product_code[32] = {0};

    assert(
        discover_active_product_code_streaming_fixture(
            MALFORMED_PRODUCT_DISCOVERY_FIXTURE,
            11,
            product_code,
            sizeof(product_code)
        ) == ESP_ERR_INVALID_RESPONSE
    );
    assert(product_code[0] == '\0');
}

static void test_product_discovery_parser_returns_not_found_for_non_matching_results(void)
{
    char product_code[32] = {0};

    assert(
        discover_active_product_code_streaming_fixture(
            NON_MATCHING_PRODUCT_DISCOVERY_FIXTURE,
            23,
            product_code,
            sizeof(product_code)
        ) == ESP_ERR_NOT_FOUND
    );
    assert(product_code[0] == '\0');
}

static void test_tariff_parser_skips_malformed_rows(void)
{
    tariff_slot_t slots[8] = {0};
    size_t slot_count = 0;

    assert(octopus_client_parse_slots_from_response(TARIFF_FIXTURE_WITH_PARTIAL_ROWS, slots, 8, &slot_count) == ESP_OK);
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

    assert(octopus_client_parse_slots_from_response(INVALID_TARIFF_FIXTURE, slots, 4, &slot_count) == ESP_ERR_INVALID_RESPONSE);
    assert(slot_count == 0);
}

int main(void)
{
    setenv("TZ", "UTC", 1);
    tzset();

    test_product_discovery_parser();
    test_product_discovery_parser_rejects_malformed_streamed_object();
    test_product_discovery_parser_returns_not_found_for_non_matching_results();
    test_tariff_parser_skips_malformed_rows();
    test_tariff_parser_rejects_all_invalid_rows();

    puts("octopus_client parser harness passed");
    return 0;
}