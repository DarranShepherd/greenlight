#include <assert.h>

#include "histogram_layout.h"

static void test_positive_only_layout(void)
{
    histogram_layout_t layout = {0};
    histogram_bar_layout_t bar = {0};

    assert(histogram_layout_prepare(10.0f, 20.0f, 70, &layout));
    assert(layout.range_min == 0.0f);
    assert(layout.range_max == 20.0f);
    assert(layout.zero_y == 70);

    histogram_layout_resolve_bar(&layout, 10.0f, &bar);
    assert(bar.y >= 0);
    assert(bar.height > 0);
    assert(bar.y + bar.height == layout.zero_y);
}

static void test_negative_only_layout(void)
{
    histogram_layout_t layout = {0};
    histogram_bar_layout_t bar = {0};

    assert(histogram_layout_prepare(-20.0f, -10.0f, 70, &layout));
    assert(layout.range_max == 0.0f);
    assert(layout.range_min == -20.0f);
    assert(layout.zero_y == 0);

    histogram_layout_resolve_bar(&layout, -10.0f, &bar);
    assert(bar.y == layout.zero_y);
    assert(bar.height > 0);
}

static void test_mixed_layout(void)
{
    histogram_layout_t layout = {0};
    histogram_bar_layout_t positive_bar = {0};
    histogram_bar_layout_t negative_bar = {0};

    assert(histogram_layout_prepare(-5.0f, 20.0f, 70, &layout));
    assert(layout.range_min == -5.0f);
    assert(layout.range_max == 20.0f);
    assert(layout.zero_y > 0);
    assert(layout.zero_y < layout.chart_height);

    histogram_layout_resolve_bar(&layout, 20.0f, &positive_bar);
    histogram_layout_resolve_bar(&layout, -5.0f, &negative_bar);

    assert(positive_bar.y == 0);
    assert(positive_bar.y + positive_bar.height == layout.zero_y);
    assert(negative_bar.y == layout.zero_y);
    assert(negative_bar.y + negative_bar.height == layout.chart_height);
}

static void test_small_mixed_range_is_padded(void)
{
    histogram_layout_t layout = {0};

    assert(histogram_layout_prepare(-1.0f, 2.0f, 70, &layout));
    assert(layout.range_span >= 5.0f);
    assert(layout.zero_y > 0);
    assert(layout.zero_y < layout.chart_height);
}

int main(void)
{
    test_positive_only_layout();
    test_negative_only_layout();
    test_mixed_layout();
    test_small_mixed_range_is_padded();
    return 0;
}
