#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define TARIFF_MODEL_MAX_SLOTS 128
#define TARIFF_MODEL_MAX_BLOCKS TARIFF_MODEL_MAX_SLOTS

typedef enum {
    TARIFF_BAND_SUPER_CHEAP = 0,
    TARIFF_BAND_CHEAP,
    TARIFF_BAND_NORMAL,
    TARIFF_BAND_EXPENSIVE,
    TARIFF_BAND_VERY_EXPENSIVE,
} tariff_band_t;

typedef struct {
    time_t start_utc;
    time_t end_utc;
    time_t start_local;
    time_t end_local;
    float price_including_vat;
    tariff_band_t band;
} tariff_slot_t;

typedef struct {
    time_t start_local;
    time_t end_local;
    tariff_band_t band;
    float min_price;
    float max_price;
    float representative_price;
    uint8_t slot_count;
} tariff_block_t;

typedef struct {
    int date_key;
    uint8_t slot_count;
    float min_price;
    float avg_price;
    float max_price;
} tariff_day_summary_t;

typedef struct {
    tariff_slot_t slots[TARIFF_MODEL_MAX_SLOTS];
    size_t slot_count;
    tariff_block_t blocks[TARIFF_MODEL_MAX_BLOCKS];
    size_t block_count;
    tariff_day_summary_t today_summary;
    tariff_day_summary_t tomorrow_summary;
    bool has_today;
    bool has_tomorrow;
    int current_slot_index;
    int current_block_index;
} runtime_tariff_state_t;

tariff_band_t tariff_model_classify_price(float price_including_vat);
const char *tariff_model_get_band_name(tariff_band_t band);
int tariff_model_get_local_day_key(time_t local_time);
bool tariff_model_build(
    const tariff_slot_t *source_slots,
    size_t source_slot_count,
    time_t now_local,
    runtime_tariff_state_t *runtime_state
);