#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <esp_err.h>

#include "tariff_model.h"

#define OCTOPUS_PRODUCTS_STREAM_CHUNK_BYTES 512U
#define OCTOPUS_PRODUCTS_OBJECT_BUFFER_BYTES 4096U

typedef struct {
    size_t results_window_length;
    size_t object_length;
    size_t total_bytes_read;
    int brace_depth;
    bool in_results_array;
    bool capturing_object;
    bool in_string;
    bool escape_next;
    char object_buffer[OCTOPUS_PRODUCTS_OBJECT_BUFFER_BYTES];
    char results_window[16];
} octopus_product_discovery_parser_t;

void octopus_product_discovery_parser_init(octopus_product_discovery_parser_t *parser);
esp_err_t octopus_product_discovery_parser_feed(
    octopus_product_discovery_parser_t *parser,
    const char *chunk,
    size_t chunk_length,
    char *product_code,
    size_t product_code_size,
    bool *matched,
    bool *done
);
esp_err_t octopus_client_discover_active_product_code_from_response(
    const char *json_text,
    char *product_code,
    size_t product_code_size
);
esp_err_t octopus_client_parse_slots_from_response(
    const char *json_text,
    tariff_slot_t *slots,
    size_t max_slots,
    size_t *slot_count
);