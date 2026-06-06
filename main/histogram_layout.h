#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float range_min;
    float range_max;
    float range_span;
    int32_t chart_height;
    int32_t zero_y;
} histogram_layout_t;

typedef struct {
    int32_t y;
    int32_t height;
} histogram_bar_layout_t;

bool histogram_layout_prepare(float min_price, float max_price, int32_t chart_height, histogram_layout_t *layout);
void histogram_layout_resolve_bar(const histogram_layout_t *layout, float price, histogram_bar_layout_t *bar_layout);
