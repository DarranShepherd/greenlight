#include "ui_router_internal.h"

#include <stdio.h>
#include <time.h>

#define DETAIL_DAY_INDEX_TODAY 0
#define DETAIL_DAY_INDEX_TOMORROW 1
#define DETAIL_DAY_COUNT 2

static void style_detail_stat_label(lv_obj_t *label, const char *caption, float value, lv_color_t color)
{
    char text[24] = {0};
    uint32_t color_hex = 0;

    if (label == NULL) {
        return;
    }

    color_hex = lv_color_to_u32(color) & 0x00FFFFFFU;
    lv_label_set_recolor(label, true);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    snprintf(text, sizeof(text), "%s\n#%06lx %.1f#", caption, (unsigned long)color_hex, (double)value);
    lv_label_set_text(label, text);
}

static void update_detail_time_marker(const app_state_t *state, ui_router_view_t *view)
{
    struct tm local_tm = {0};
    int minutes_of_day = 0;
    lv_coord_t row_width = 0;
    lv_coord_t marker_x = 0;
    lv_obj_t *today_row = view->detail_day_bar_rows[DETAIL_DAY_INDEX_TODAY];
    lv_obj_t *today_marker = view->detail_day_time_markers[DETAIL_DAY_INDEX_TODAY];

    if (today_row == NULL || today_marker == NULL) {
        return;
    }

    lv_obj_add_flag(view->detail_day_time_markers[DETAIL_DAY_INDEX_TOMORROW], LV_OBJ_FLAG_HIDDEN);

    if (!state->time_valid || !state->tariff_today.available) {
        lv_obj_add_flag(today_marker, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    localtime_r(&(time_t){time(NULL)}, &local_tm);
    minutes_of_day = (local_tm.tm_hour * 60) + local_tm.tm_min;
    row_width = lv_obj_get_content_width(today_row);
    if (row_width <= 2) {
        lv_obj_add_flag(today_marker, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    marker_x = (lv_coord_t)(((int32_t)minutes_of_day * (row_width - 2)) / (24 * 60));
    if (marker_x < 0) {
        marker_x = 0;
    }
    if (marker_x > row_width - 2) {
        marker_x = row_width - 2;
    }

    lv_obj_set_x(today_marker, marker_x);
    lv_obj_set_y(today_marker, 0);
    lv_obj_set_height(today_marker, lv_obj_get_height(today_row));
    lv_obj_clear_flag(today_marker, LV_OBJ_FLAG_HIDDEN);
}

static void update_detail_day_panel(ui_router_view_t *view, uint8_t index, const char *title, const app_tariff_day_view_t *day_view, bool two_day_layout)
{
    lv_obj_t *panel = NULL;
    lv_coord_t chart_height = 72;
    float max_price = 0.0f;

    if (index >= DETAIL_DAY_COUNT) {
        return;
    }

    panel = view->detail_day_panels[index];
    if (panel == NULL) {
        return;
    }

    lv_obj_set_width(panel, two_day_layout ? lv_pct(48) : lv_pct(100));
    lv_obj_set_flex_grow(panel, two_day_layout ? 0 : 1);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);

    if (view->detail_day_titles[index] != NULL) {
        lv_label_set_text(view->detail_day_titles[index], title);
    }

    if (day_view == NULL || !day_view->available || day_view->slot_count == 0) {
        if (view->detail_day_titles[index] != NULL) {
            lv_label_set_text_fmt(view->detail_day_titles[index], "%s\nAwaiting prices", title);
        }

        for (uint8_t slot_index = 0; slot_index < APP_TARIFF_DAY_SLOT_MAX; slot_index++) {
            if (view->detail_day_bars[index][slot_index] != NULL) {
                lv_obj_add_flag(view->detail_day_bars[index][slot_index], LV_OBJ_FLAG_HIDDEN);
            }
        }

        style_detail_stat_label(view->detail_day_min_labels[index], "Min", 0.0f, ui_primary_get_band_fill_color(TARIFF_BAND_CHEAP));
        style_detail_stat_label(view->detail_day_avg_labels[index], "Avg", 0.0f, ui_primary_get_band_fill_color(TARIFF_BAND_NORMAL));
        style_detail_stat_label(view->detail_day_max_labels[index], "Max", 0.0f, ui_primary_get_band_fill_color(TARIFF_BAND_EXPENSIVE));
        return;
    }

    max_price = day_view->max_price > 0.0f ? day_view->max_price : 1.0f;
    if (max_price < 5.0f) {
        max_price = 5.0f;
    }

    for (uint8_t slot_index = 0; slot_index < APP_TARIFF_DAY_SLOT_MAX; slot_index++) {
        lv_obj_t *bar = view->detail_day_bars[index][slot_index];

        if (bar == NULL) {
            continue;
        }

        if (slot_index >= day_view->slot_count || !day_view->slots[slot_index].valid) {
            lv_obj_set_height(bar, 1);
            lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
            lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_coord_t height = (lv_coord_t)((day_view->slots[slot_index].price / max_price) * chart_height);
        if (height < 6) {
            height = 6;
        }
        if (height > chart_height) {
            height = chart_height;
        }

        lv_obj_set_height(bar, height);
        lv_obj_set_style_bg_color(bar, ui_primary_get_band_fill_color(day_view->slots[slot_index].band), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
    }

    style_detail_stat_label(view->detail_day_min_labels[index], "Min", day_view->min_price, ui_primary_get_band_fill_color(TARIFF_BAND_CHEAP));
    style_detail_stat_label(view->detail_day_avg_labels[index], "Avg", day_view->avg_price, ui_primary_get_band_fill_color(TARIFF_BAND_NORMAL));
    style_detail_stat_label(view->detail_day_max_labels[index], "Max", day_view->max_price, ui_primary_get_band_fill_color(TARIFF_BAND_EXPENSIVE));
}

void ui_detail_update(const app_state_t *state, ui_router_view_t *view)
{
    char clock_text[12] = {0};

    lv_obj_set_style_bg_color(view->tiles[APP_SCREEN_DETAIL], lv_color_hex(0x0f172a), 0);
    ui_router_format_clock_label(clock_text, sizeof(clock_text), state->local_time_text);

    if (view->detail_clock_label != NULL) {
        lv_label_set_text(view->detail_clock_label, clock_text);
    }

    if (view->detail_title_label != NULL) {
        lv_label_set_text(view->detail_title_label, "Daily Prices");
    }

    if (view->detail_wifi_label != NULL) {
        lv_label_set_text(view->detail_wifi_label, state->wifi_status == APP_WIFI_STATUS_CONNECTED ? LV_SYMBOL_WIFI : "");
    }

    if (view->detail_status_label != NULL) {
        lv_label_set_text(view->detail_status_label, state->tariff_status_text);
    }

    update_detail_day_panel(view, DETAIL_DAY_INDEX_TODAY, "Today", &state->tariff_today, state->tariff_tomorrow.available);
    if (state->tariff_tomorrow.available) {
        update_detail_day_panel(view, DETAIL_DAY_INDEX_TOMORROW, "Tomorrow", &state->tariff_tomorrow, true);
    } else if (view->detail_day_panels[DETAIL_DAY_INDEX_TOMORROW] != NULL) {
        lv_obj_add_flag(view->detail_day_panels[DETAIL_DAY_INDEX_TOMORROW], LV_OBJ_FLAG_HIDDEN);
    }
    update_detail_time_marker(state, view);
}

void ui_detail_create(lv_obj_t *tile, ui_router_view_t *view)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tile, 10, 0);
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 6, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    view->detail_top_bar = lv_obj_create(tile);
    lv_obj_set_width(view->detail_top_bar, lv_pct(100));
    lv_obj_set_height(view->detail_top_bar, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(view->detail_top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->detail_top_bar, 0, 0);
    lv_obj_set_style_pad_all(view->detail_top_bar, 0, 0);
    lv_obj_set_style_pad_column(view->detail_top_bar, 8, 0);
    lv_obj_set_layout(view->detail_top_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(view->detail_top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(view->detail_top_bar, LV_OBJ_FLAG_SCROLLABLE);

    view->detail_clock_label = lv_label_create(view->detail_top_bar);
    lv_obj_set_width(view->detail_clock_label, 52);
    lv_obj_set_style_text_color(view->detail_clock_label, lv_color_white(), 0);

    view->detail_title_label = lv_label_create(view->detail_top_bar);
    lv_obj_set_flex_grow(view->detail_title_label, 1);
    lv_obj_set_style_text_align(view->detail_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(view->detail_title_label, lv_color_white(), 0);

    view->detail_wifi_label = lv_label_create(view->detail_top_bar);
    lv_obj_set_width(view->detail_wifi_label, 52);
    lv_obj_set_style_text_align(view->detail_wifi_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(view->detail_wifi_label, lv_color_white(), 0);

    view->detail_status_label = lv_label_create(tile);
    lv_obj_set_width(view->detail_status_label, lv_pct(100));
    lv_obj_set_style_text_align(view->detail_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(view->detail_status_label, lv_color_hex(0x64748b), 0);
    lv_label_set_long_mode(view->detail_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(view->detail_status_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *day_row = lv_obj_create(tile);
    lv_obj_set_width(day_row, lv_pct(100));
    lv_obj_set_flex_grow(day_row, 1);
    lv_obj_set_style_bg_opa(day_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(day_row, 0, 0);
    lv_obj_set_style_pad_all(day_row, 0, 0);
    lv_obj_set_style_pad_column(day_row, 10, 0);
    lv_obj_set_layout(day_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(day_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(day_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(day_row, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t day_index = 0; day_index < DETAIL_DAY_COUNT; day_index++) {
        lv_obj_t *panel = lv_obj_create(day_row);
        lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(panel, 0, 0);
        lv_obj_set_style_pad_all(panel, 0, 0);
        lv_obj_set_style_pad_row(panel, 6, 0);
        lv_obj_set_height(panel, lv_pct(100));
        lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        view->detail_day_panels[day_index] = panel;

        view->detail_day_titles[day_index] = lv_label_create(panel);
        lv_obj_set_style_text_color(view->detail_day_titles[day_index], lv_color_white(), 0);
        lv_obj_set_style_text_font(view->detail_day_titles[day_index], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(view->detail_day_titles[day_index], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(view->detail_day_titles[day_index], lv_pct(100));

        lv_obj_t *chart_shell = lv_obj_create(panel);
        lv_obj_set_width(chart_shell, lv_pct(100));
        lv_obj_set_height(chart_shell, 94);
        lv_obj_set_style_bg_opa(chart_shell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(chart_shell, 0, 0);
        lv_obj_set_style_pad_top(chart_shell, 4, 0);
        lv_obj_set_style_pad_bottom(chart_shell, 2, 0);
        lv_obj_set_style_pad_left(chart_shell, 0, 0);
        lv_obj_set_style_pad_right(chart_shell, 0, 0);
        lv_obj_set_style_pad_row(chart_shell, 4, 0);
        lv_obj_set_style_pad_column(chart_shell, 0, 0);
        lv_obj_set_layout(chart_shell, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(chart_shell, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(chart_shell, LV_OBJ_FLAG_SCROLLABLE);

        view->detail_day_bar_rows[day_index] = lv_obj_create(chart_shell);
        lv_obj_set_width(view->detail_day_bar_rows[day_index], lv_pct(100));
        lv_obj_set_height(view->detail_day_bar_rows[day_index], 70);
        lv_obj_set_style_bg_opa(view->detail_day_bar_rows[day_index], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(view->detail_day_bar_rows[day_index], 0, 0);
        lv_obj_set_style_pad_top(view->detail_day_bar_rows[day_index], 0, 0);
        lv_obj_set_style_pad_bottom(view->detail_day_bar_rows[day_index], 0, 0);
        lv_obj_set_style_pad_left(view->detail_day_bar_rows[day_index], 0, 0);
        lv_obj_set_style_pad_right(view->detail_day_bar_rows[day_index], 0, 0);
        lv_obj_set_style_pad_column(view->detail_day_bar_rows[day_index], 0, 0);
        lv_obj_set_style_border_side(view->detail_day_bar_rows[day_index], LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(view->detail_day_bar_rows[day_index], lv_color_hex(0x334155), 0);
        lv_obj_set_style_border_width(view->detail_day_bar_rows[day_index], 1, 0);
        lv_obj_set_layout(view->detail_day_bar_rows[day_index], LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(view->detail_day_bar_rows[day_index], LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(view->detail_day_bar_rows[day_index], LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lv_obj_clear_flag(view->detail_day_bar_rows[day_index], LV_OBJ_FLAG_SCROLLABLE);

        for (uint8_t slot_index = 0; slot_index < APP_TARIFF_DAY_SLOT_MAX; slot_index++) {
            view->detail_day_bars[day_index][slot_index] = lv_obj_create(view->detail_day_bar_rows[day_index]);
            lv_obj_set_width(view->detail_day_bars[day_index][slot_index], 4);
            lv_obj_set_height(view->detail_day_bars[day_index][slot_index], 6);
            lv_obj_set_style_radius(view->detail_day_bars[day_index][slot_index], 2, 0);
            lv_obj_set_style_bg_color(view->detail_day_bars[day_index][slot_index], lv_color_hex(0x374151), 0);
            lv_obj_set_style_bg_opa(view->detail_day_bars[day_index][slot_index], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(view->detail_day_bars[day_index][slot_index], 0, 0);
            lv_obj_clear_flag(view->detail_day_bars[day_index][slot_index], LV_OBJ_FLAG_SCROLLABLE);
        }

        view->detail_day_time_markers[day_index] = lv_obj_create(view->detail_day_bar_rows[day_index]);
        lv_obj_set_width(view->detail_day_time_markers[day_index], 2);
        lv_obj_set_height(view->detail_day_time_markers[day_index], lv_pct(100));
        lv_obj_set_style_radius(view->detail_day_time_markers[day_index], 0, 0);
        lv_obj_set_style_bg_color(view->detail_day_time_markers[day_index], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(view->detail_day_time_markers[day_index], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(view->detail_day_time_markers[day_index], 0, 0);
        lv_obj_add_flag(view->detail_day_time_markers[day_index], LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(view->detail_day_time_markers[day_index]);

        lv_obj_t *axis_row = lv_obj_create(chart_shell);
        lv_obj_set_width(axis_row, lv_pct(100));
        lv_obj_set_height(axis_row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(axis_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(axis_row, 0, 0);
        lv_obj_set_style_pad_top(axis_row, 2, 0);
        lv_obj_set_style_pad_bottom(axis_row, 2, 0);
        lv_obj_set_style_pad_left(axis_row, 0, 0);
        lv_obj_set_style_pad_right(axis_row, 0, 0);
        lv_obj_set_layout(axis_row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(axis_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(axis_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(axis_row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *axis_start = lv_label_create(axis_row);
        lv_label_set_text(axis_start, "0");
        lv_obj_set_style_text_color(axis_start, lv_color_hex(0x9ca3af), 0);

        lv_obj_t *axis_mid = lv_label_create(axis_row);
        lv_label_set_text(axis_mid, "12");
        lv_obj_set_style_text_color(axis_mid, lv_color_hex(0x9ca3af), 0);

        lv_obj_t *axis_end = lv_label_create(axis_row);
        lv_label_set_text(axis_end, "24");
        lv_obj_set_style_text_color(axis_end, lv_color_hex(0x9ca3af), 0);

        lv_obj_t *stats_row = lv_obj_create(panel);
        lv_obj_set_width(stats_row, lv_pct(100));
        lv_obj_set_height(stats_row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(stats_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(stats_row, 0, 0);
        lv_obj_set_style_pad_top(stats_row, 8, 0);
        lv_obj_set_style_pad_bottom(stats_row, 0, 0);
        lv_obj_set_style_pad_left(stats_row, 0, 0);
        lv_obj_set_style_pad_right(stats_row, 0, 0);
        lv_obj_set_layout(stats_row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(stats_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(stats_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(stats_row, LV_OBJ_FLAG_SCROLLABLE);

        view->detail_day_min_labels[day_index] = lv_label_create(stats_row);
        lv_obj_set_width(view->detail_day_min_labels[day_index], 48);
        lv_obj_set_style_text_align(view->detail_day_min_labels[day_index], LV_TEXT_ALIGN_LEFT, 0);
        view->detail_day_avg_labels[day_index] = lv_label_create(stats_row);
        lv_obj_set_width(view->detail_day_avg_labels[day_index], 48);
        lv_obj_set_style_text_align(view->detail_day_avg_labels[day_index], LV_TEXT_ALIGN_CENTER, 0);
        view->detail_day_max_labels[day_index] = lv_label_create(stats_row);
        lv_obj_set_width(view->detail_day_max_labels[day_index], 48);
        lv_obj_set_style_text_align(view->detail_day_max_labels[day_index], LV_TEXT_ALIGN_RIGHT, 0);
    }
}