#pragma once

#include <stddef.h>
#include <time.h>

#include <esp_err.h>

#include "tariff_model.h"

#define OCTOPUS_CLIENT_DEFAULT_PRODUCT_CODE "AGILE-24-10-01"

esp_err_t octopus_client_fetch_tariffs(
    const char *region_code,
    time_t now_local,
    tariff_slot_t *slots,
    size_t max_slots,
    size_t *slot_count
);