#include "histogram_layout.h"

#include <stddef.h>

static float max_float(float left, float right)
{
    return left > right ? left : right;
}

static float min_float(float left, float right)
{
    return left < right ? left : right;
}

static int32_t clamp_i32(int32_t value, int32_t min_value, int32_t max_value)
{
    if (value < min_value) {
        return min_value;
    }

    if (value > max_value) {
        return max_value;
    }

    return value;
}

static int32_t round_to_i32(float value)
{
    return value >= 0.0f ? (int32_t)(value + 0.5f) : (int32_t)(value - 0.5f);
}

static int32_t price_to_y(const histogram_layout_t *layout, float price)
{
    float clamped_price = price;

    if (layout == NULL || layout->range_span <= 0.0f || layout->chart_height <= 0) {
        return 0;
    }

    clamped_price = min_float(max_float(clamped_price, layout->range_min), layout->range_max);
    return clamp_i32(
        round_to_i32(((layout->range_max - clamped_price) * (float)layout->chart_height) / layout->range_span),
        0,
        layout->chart_height
    );
}

bool histogram_layout_prepare(float min_price, float max_price, int32_t chart_height, histogram_layout_t *layout)
{
    float range_min = 0.0f;
    float range_max = 0.0f;
    float range_span = 0.0f;

    if (layout == NULL || chart_height <= 0) {
        return false;
    }

    range_min = min_price < 0.0f ? min_price : 0.0f;
    range_max = max_price > 0.0f ? max_price : 0.0f;
    range_span = range_max - range_min;

    if (range_span < 5.0f) {
        if (range_min < 0.0f && range_max > 0.0f) {
            float padding = (5.0f - range_span) * 0.5f;
            range_min -= padding;
            range_max += padding;
        } else if (range_max <= 0.0f) {
            range_min = range_max - 5.0f;
        } else {
            range_max = range_min + 5.0f;
        }

        range_span = range_max - range_min;
    }

    layout->range_min = range_min;
    layout->range_max = range_max;
    layout->range_span = range_span;
    layout->chart_height = chart_height;
    layout->zero_y = price_to_y(
        &(histogram_layout_t){
            .range_min = range_min,
            .range_max = range_max,
            .range_span = range_span,
            .chart_height = chart_height,
            .zero_y = 0,
        },
        0.0f
    );
    return true;
}

void histogram_layout_resolve_bar(const histogram_layout_t *layout, float price, histogram_bar_layout_t *bar_layout)
{
    int32_t minimum_height = 1;
    int32_t top = 0;
    int32_t bottom = 0;

    if (bar_layout == NULL) {
        return;
    }

    bar_layout->y = 0;
    bar_layout->height = 0;

    if (layout == NULL || layout->range_span <= 0.0f || layout->chart_height <= 0) {
        return;
    }

    minimum_height = price == 0.0f ? 1 : 4;

    if (price >= 0.0f) {
        top = price_to_y(layout, price);
        bottom = clamp_i32(layout->zero_y, 0, layout->chart_height);
        if (bottom < top) {
            bottom = top;
        }

        if ((bottom - top) < minimum_height) {
            top = bottom - minimum_height;
            if (top < 0) {
                top = 0;
            }
        }
    } else {
        top = clamp_i32(layout->zero_y, 0, layout->chart_height);
        bottom = price_to_y(layout, price);
        if (bottom < top) {
            bottom = top;
        }

        if ((bottom - top) < minimum_height) {
            bottom = top + minimum_height;
            if (bottom > layout->chart_height) {
                bottom = layout->chart_height;
            }
        }
    }

    if (bottom <= top) {
        bottom = top + 1;
        if (bottom > layout->chart_height) {
            bottom = layout->chart_height;
            top = bottom > 0 ? bottom - 1 : 0;
        }
    }

    bar_layout->y = top;
    bar_layout->height = bottom - top;
}
