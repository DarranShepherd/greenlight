#include "tariff_model.h"

#include <stdlib.h>
#include <string.h>

static int compare_slots_by_start_utc(const void *left, const void *right)
{
    const tariff_slot_t *left_slot = (const tariff_slot_t *)left;
    const tariff_slot_t *right_slot = (const tariff_slot_t *)right;

    if (left_slot->start_utc < right_slot->start_utc) {
        return -1;
    }

    if (left_slot->start_utc > right_slot->start_utc) {
        return 1;
    }

    return 0;
}

tariff_band_t tariff_model_classify_price(float price_including_vat)
{
    if (price_including_vat <= 5.0f) {
        return TARIFF_BAND_SUPER_CHEAP;
    }

    if (price_including_vat <= 15.0f) {
        return TARIFF_BAND_CHEAP;
    }

    if (price_including_vat <= 25.0f) {
        return TARIFF_BAND_NORMAL;
    }

    if (price_including_vat <= 40.0f) {
        return TARIFF_BAND_EXPENSIVE;
    }

    return TARIFF_BAND_VERY_EXPENSIVE;
}

const char *tariff_model_get_band_name(tariff_band_t band)
{
    switch (band) {
        case TARIFF_BAND_SUPER_CHEAP:
            return "Super Cheap";
        case TARIFF_BAND_CHEAP:
            return "Cheap";
        case TARIFF_BAND_NORMAL:
            return "Normal";
        case TARIFF_BAND_EXPENSIVE:
            return "Expensive";
        case TARIFF_BAND_VERY_EXPENSIVE:
            return "Very Expensive";
        default:
            return "Unknown";
    }
}

int tariff_model_get_local_day_key(time_t local_time)
{
    struct tm local_tm = {0};

    localtime_r(&local_time, &local_tm);
    return ((local_tm.tm_year + 1900) * 1000) + local_tm.tm_yday;
}

static void update_day_summary(tariff_day_summary_t *summary, float price)
{
    if (summary->slot_count == 0) {
        summary->min_price = price;
        summary->max_price = price;
        summary->avg_price = price;
        summary->slot_count = 1;
        return;
    }

    if (price < summary->min_price) {
        summary->min_price = price;
    }

    if (price > summary->max_price) {
        summary->max_price = price;
    }

    summary->avg_price = ((summary->avg_price * summary->slot_count) + price) / (summary->slot_count + 1);
    summary->slot_count++;
}

static void finalize_block(tariff_block_t *block, float price_total)
{
    if (block == NULL || block->slot_count == 0) {
        return;
    }

    block->representative_price = price_total / block->slot_count;
}

bool tariff_model_build(
    const tariff_slot_t *source_slots,
    size_t source_slot_count,
    time_t now_local,
    runtime_tariff_state_t *runtime_state
)
{
    tariff_slot_t sorted_slots[TARIFF_MODEL_MAX_SLOTS] = {0};
    int today_key = tariff_model_get_local_day_key(now_local);
    int tomorrow_key = tariff_model_get_local_day_key(now_local + (24 * 60 * 60));
    tariff_block_t *active_block = NULL;
    float active_block_price_total = 0.0f;

    if (source_slots == NULL || runtime_state == NULL || source_slot_count == 0 || source_slot_count > TARIFF_MODEL_MAX_SLOTS) {
        return false;
    }

    memset(runtime_state, 0, sizeof(*runtime_state));
    runtime_state->current_slot_index = -1;
    runtime_state->current_block_index = -1;
    runtime_state->today_summary.date_key = today_key;
    runtime_state->tomorrow_summary.date_key = tomorrow_key;

    memcpy(sorted_slots, source_slots, sizeof(sorted_slots[0]) * source_slot_count);
    qsort(sorted_slots, source_slot_count, sizeof(sorted_slots[0]), compare_slots_by_start_utc);

    for (size_t index = 0; index < source_slot_count; index++) {
        tariff_slot_t slot = sorted_slots[index];
        int slot_day_key = tariff_model_get_local_day_key(slot.start_local);

        slot.band = tariff_model_classify_price(slot.price_including_vat);

        if (slot_day_key != today_key && slot_day_key != tomorrow_key) {
            continue;
        }

        if (runtime_state->slot_count >= TARIFF_MODEL_MAX_SLOTS) {
            break;
        }

        runtime_state->slots[runtime_state->slot_count] = slot;

        if (slot_day_key == today_key) {
            runtime_state->has_today = true;
            update_day_summary(&runtime_state->today_summary, slot.price_including_vat);
        } else if (slot_day_key == tomorrow_key) {
            runtime_state->has_tomorrow = true;
            update_day_summary(&runtime_state->tomorrow_summary, slot.price_including_vat);
        }

        if (runtime_state->block_count == 0) {
            active_block = &runtime_state->blocks[runtime_state->block_count++];
            *active_block = (tariff_block_t){
                .start_local = slot.start_local,
                .end_local = slot.end_local,
                .band = slot.band,
                .min_price = slot.price_including_vat,
                .max_price = slot.price_including_vat,
                .slot_count = 1,
            };
            active_block_price_total = slot.price_including_vat;
        } else if (active_block != NULL && active_block->band == slot.band && active_block->end_local == slot.start_local) {
            active_block->end_local = slot.end_local;
            if (slot.price_including_vat < active_block->min_price) {
                active_block->min_price = slot.price_including_vat;
            }
            if (slot.price_including_vat > active_block->max_price) {
                active_block->max_price = slot.price_including_vat;
            }
            active_block->slot_count++;
            active_block_price_total += slot.price_including_vat;
        } else {
            finalize_block(active_block, active_block_price_total);
            if (runtime_state->block_count >= TARIFF_MODEL_MAX_BLOCKS) {
                break;
            }

            active_block = &runtime_state->blocks[runtime_state->block_count++];
            *active_block = (tariff_block_t){
                .start_local = slot.start_local,
                .end_local = slot.end_local,
                .band = slot.band,
                .min_price = slot.price_including_vat,
                .max_price = slot.price_including_vat,
                .slot_count = 1,
            };
            active_block_price_total = slot.price_including_vat;
        }

        if (now_local >= slot.start_local && now_local < slot.end_local) {
            runtime_state->current_slot_index = (int)runtime_state->slot_count;
            runtime_state->current_block_index = (int)(runtime_state->block_count - 1);
        }

        runtime_state->slot_count++;
    }

    finalize_block(active_block, active_block_price_total);
    return runtime_state->has_today;
}