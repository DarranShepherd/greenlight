#include "ui_router_internal.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "numeric_fonts.h"
#include "tariff_model.h"

typedef struct {
    lv_color_t tile_bg;
    lv_color_t hero_bg;
    lv_color_t chip_bg;
    lv_color_t chip_text;
    lv_color_t hero_text;
    lv_color_t hero_muted_text;
    lv_color_t footer_bg;
    bool pulse;
} primary_palette_t;

static void format_compact_time(char *buffer, size_t buffer_size, time_t local_time)
{
    struct tm local_tm = {0};

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (local_time <= 0) {
        strlcpy(buffer, "--:--", buffer_size);
        return;
    }

    localtime_r(&local_time, &local_tm);
    if (strftime(buffer, buffer_size, "%H:%M", &local_tm) == 0) {
        strlcpy(buffer, "--:--", buffer_size);
    }
}

static void format_remaining_compact(char *buffer, size_t buffer_size, time_t seconds_remaining)
{
    int hours = 0;
    int minutes = 0;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (seconds_remaining < 0) {
        seconds_remaining = 0;
    }

    hours = (int)(seconds_remaining / 3600);
    minutes = (int)((seconds_remaining % 3600) / 60);

    if (hours > 0) {
        snprintf(buffer, buffer_size, "%dh %dm left", hours, minutes);
    } else {
        snprintf(buffer, buffer_size, "%dm left", minutes);
    }
}

static void format_until_time(char *buffer, size_t buffer_size, time_t local_time)
{
    char time_text[8] = {0};

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    format_compact_time(time_text, sizeof(time_text), local_time);
    snprintf(buffer, buffer_size, "Until %s", time_text);
}

static bool tariff_band_is_extreme(tariff_band_t band)
{
    return band == TARIFF_BAND_SUPER_CHEAP || band == TARIFF_BAND_VERY_EXPENSIVE;
}

static primary_palette_t get_primary_palette(const app_state_t *state)
{
    if (state->tariff_has_data && state->tariff_current_block_valid) {
        switch (state->tariff_current_band) {
            case TARIFF_BAND_SUPER_CHEAP:
                return (primary_palette_t){
                    .tile_bg = lv_color_hex(0x052e2b),
                    .hero_bg = lv_color_hex(0x0f766e),
                    .chip_bg = lv_color_hex(0xccfbf1),
                    .chip_text = lv_color_hex(0x134e4a),
                    .hero_text = lv_color_white(),
                    .hero_muted_text = lv_color_hex(0xe6fffa),
                    .footer_bg = lv_color_hex(0x115e59),
                    .pulse = true,
                };
            case TARIFF_BAND_CHEAP:
                return (primary_palette_t){
                    .tile_bg = lv_color_hex(0x0c2a1f),
                    .hero_bg = lv_color_hex(0x166534),
                    .chip_bg = lv_color_hex(0xdcfce7),
                    .chip_text = lv_color_hex(0x14532d),
                    .hero_text = lv_color_white(),
                    .hero_muted_text = lv_color_hex(0xdcfce7),
                    .footer_bg = lv_color_hex(0x14532d),
                    .pulse = false,
                };
            case TARIFF_BAND_NORMAL:
                return (primary_palette_t){
                    .tile_bg = lv_color_hex(0x2d2214),
                    .hero_bg = lv_color_hex(0xb45309),
                    .chip_bg = lv_color_hex(0xfef3c7),
                    .chip_text = lv_color_hex(0x78350f),
                    .hero_text = lv_color_white(),
                    .hero_muted_text = lv_color_hex(0xfff7ed),
                    .footer_bg = lv_color_hex(0x92400e),
                    .pulse = false,
                };
            case TARIFF_BAND_EXPENSIVE:
                return (primary_palette_t){
                    .tile_bg = lv_color_hex(0x33160f),
                    .hero_bg = lv_color_hex(0xc2410c),
                    .chip_bg = lv_color_hex(0xffedd5),
                    .chip_text = lv_color_hex(0x7c2d12),
                    .hero_text = lv_color_white(),
                    .hero_muted_text = lv_color_hex(0xffedd5),
                    .footer_bg = lv_color_hex(0x9a3412),
                    .pulse = false,
                };
            case TARIFF_BAND_VERY_EXPENSIVE:
                return (primary_palette_t){
                    .tile_bg = lv_color_hex(0x300d11),
                    .hero_bg = lv_color_hex(0x991b1b),
                    .chip_bg = lv_color_hex(0xfee2e2),
                    .chip_text = lv_color_hex(0x7f1d1d),
                    .hero_text = lv_color_white(),
                    .hero_muted_text = lv_color_hex(0xfee2e2),
                    .footer_bg = lv_color_hex(0x7f1d1d),
                    .pulse = true,
                };
            default:
                break;
        }
    }

    if (state->tariff_status == APP_TARIFF_STATUS_OFFLINE) {
        return (primary_palette_t){
            .tile_bg = lv_color_hex(0x1f172a),
            .hero_bg = lv_color_hex(0x3f3f46),
            .chip_bg = lv_color_hex(0xe4e4e7),
            .chip_text = lv_color_hex(0x27272a),
            .hero_text = lv_color_white(),
            .hero_muted_text = lv_color_hex(0xf4f4f5),
            .footer_bg = lv_color_hex(0x27272a),
            .pulse = false,
        };
    }

    return (primary_palette_t){
        .tile_bg = lv_color_hex(0x0f172a),
        .hero_bg = lv_color_hex(0x1e293b),
        .chip_bg = lv_color_hex(0xcbd5e1),
        .chip_text = lv_color_hex(0x0f172a),
        .hero_text = lv_color_white(),
        .hero_muted_text = lv_color_hex(0xe2e8f0),
        .footer_bg = lv_color_hex(0x172033),
        .pulse = false,
    };
}

static primary_palette_t get_primary_palette_for_band(tariff_band_t band)
{
    switch (band) {
        case TARIFF_BAND_SUPER_CHEAP:
            return (primary_palette_t){
                .tile_bg = lv_color_hex(0x052e2b),
                .hero_bg = lv_color_hex(0x0f766e),
                .chip_bg = lv_color_hex(0xccfbf1),
                .chip_text = lv_color_hex(0x134e4a),
                .hero_text = lv_color_white(),
                .hero_muted_text = lv_color_hex(0xe6fffa),
                .footer_bg = lv_color_hex(0x115e59),
                .pulse = true,
            };
        case TARIFF_BAND_CHEAP:
            return (primary_palette_t){
                .tile_bg = lv_color_hex(0x0c2a1f),
                .hero_bg = lv_color_hex(0x166534),
                .chip_bg = lv_color_hex(0xdcfce7),
                .chip_text = lv_color_hex(0x14532d),
                .hero_text = lv_color_white(),
                .hero_muted_text = lv_color_hex(0xdcfce7),
                .footer_bg = lv_color_hex(0x14532d),
                .pulse = false,
            };
        case TARIFF_BAND_NORMAL:
            return (primary_palette_t){
                .tile_bg = lv_color_hex(0x2d2214),
                .hero_bg = lv_color_hex(0xb45309),
                .chip_bg = lv_color_hex(0xfef3c7),
                .chip_text = lv_color_hex(0x78350f),
                .hero_text = lv_color_white(),
                .hero_muted_text = lv_color_hex(0xfff7ed),
                .footer_bg = lv_color_hex(0x92400e),
                .pulse = false,
            };
        case TARIFF_BAND_EXPENSIVE:
            return (primary_palette_t){
                .tile_bg = lv_color_hex(0x33160f),
                .hero_bg = lv_color_hex(0xc2410c),
                .chip_bg = lv_color_hex(0xffedd5),
                .chip_text = lv_color_hex(0x7c2d12),
                .hero_text = lv_color_white(),
                .hero_muted_text = lv_color_hex(0xffedd5),
                .footer_bg = lv_color_hex(0x9a3412),
                .pulse = false,
            };
        case TARIFF_BAND_VERY_EXPENSIVE:
            return (primary_palette_t){
                .tile_bg = lv_color_hex(0x300d11),
                .hero_bg = lv_color_hex(0x991b1b),
                .chip_bg = lv_color_hex(0xfee2e2),
                .chip_text = lv_color_hex(0x7f1d1d),
                .hero_text = lv_color_white(),
                .hero_muted_text = lv_color_hex(0xfee2e2),
                .footer_bg = lv_color_hex(0x7f1d1d),
                .pulse = true,
            };
        default:
            return get_primary_palette(&(app_state_t){0});
    }
}

static void primary_pulse_anim_cb(void *object, int32_t value)
{
    lv_obj_t *dot = (lv_obj_t *)object;

    if (dot == NULL) {
        return;
    }

    lv_obj_set_style_bg_opa(dot, (lv_opa_t)value, 0);
    lv_obj_set_style_outline_opa(dot, (lv_opa_t)(value / 2), 0);
}

static void set_primary_pulse_enabled(ui_router_view_t *view, bool enabled)
{
    lv_anim_t animation;

    if (view->primary_pulse_dot == NULL) {
        return;
    }

    lv_anim_del(view->primary_pulse_dot, primary_pulse_anim_cb);

    if (!enabled) {
        lv_obj_set_style_bg_opa(view->primary_pulse_dot, LV_OPA_80, 0);
        lv_obj_set_style_outline_opa(view->primary_pulse_dot, LV_OPA_30, 0);
        return;
    }

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, view->primary_pulse_dot);
    lv_anim_set_exec_cb(&animation, primary_pulse_anim_cb);
    lv_anim_set_values(&animation, LV_OPA_30, LV_OPA_COVER);
    lv_anim_set_time(&animation, 900);
    lv_anim_set_playback_time(&animation, 900);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&animation);
}

static void style_preview_card(lv_obj_t *card, lv_obj_t *time_label, lv_obj_t *band_label, lv_obj_t *price_label, tariff_band_t band, bool active)
{
    primary_palette_t palette = {0};

    if (card == NULL || time_label == NULL || band_label == NULL || price_label == NULL) {
        return;
    }

    palette = active ? get_primary_palette_for_band(band) : get_primary_palette(&(app_state_t){0});

    lv_obj_set_style_bg_color(card, active ? palette.hero_bg : lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_text_color(time_label, active ? palette.hero_muted_text : lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_text_color(band_label, active ? palette.hero_text : lv_color_white(), 0);
    lv_obj_set_style_text_color(price_label, active ? palette.hero_muted_text : lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(band_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(band_label, &lv_font_montserrat_14, 0);
}

lv_color_t ui_primary_get_band_fill_color(tariff_band_t band)
{
    return get_primary_palette_for_band(band).hero_bg;
}

static void update_primary_preview(ui_router_view_t *view, uint8_t index, const app_tariff_preview_t *preview)
{
    char time_text[24] = {0};
    char numeric_text[24] = {0};

    if (index >= APP_TARIFF_PREVIEW_MAX) {
        return;
    }

    if (preview != NULL && preview->valid) {
        char start_time[8] = {0};
        char end_time[8] = {0};

        format_compact_time(start_time, sizeof(start_time), preview->start_local);
        format_compact_time(end_time, sizeof(end_time), preview->end_local);
        snprintf(time_text, sizeof(time_text), "%s-%s", start_time, end_time);
        snprintf(numeric_text, sizeof(numeric_text), "%.1f", (double)preview->representative_price);
        lv_label_set_text(view->primary_preview_time_labels[index], time_text);
        lv_label_set_text(view->primary_preview_band_labels[index], numeric_text);
        lv_label_set_text(view->primary_preview_price_labels[index], "");
        style_preview_card(
            view->primary_preview_cards[index],
            view->primary_preview_time_labels[index],
            view->primary_preview_band_labels[index],
            view->primary_preview_price_labels[index],
            preview->band,
            true
        );
        return;
    }

    lv_label_set_text(view->primary_preview_time_labels[index], "Later");
    lv_label_set_text(view->primary_preview_band_labels[index], "--");
    lv_label_set_text(view->primary_preview_price_labels[index], "");
    style_preview_card(
        view->primary_preview_cards[index],
        view->primary_preview_time_labels[index],
        view->primary_preview_band_labels[index],
        view->primary_preview_price_labels[index],
        TARIFF_BAND_NORMAL,
        false
    );
}

void ui_primary_update(const app_state_t *state, ui_router_view_t *view)
{
    primary_palette_t palette = get_primary_palette(state);
    char clock_text[12] = {0};

    lv_obj_set_style_bg_color(view->tiles[APP_SCREEN_PRIMARY], palette.tile_bg, 0);
    ui_router_format_clock_label(clock_text, sizeof(clock_text), state->local_time_text);

    if (view->primary_clock_label != NULL) {
        lv_label_set_text(view->primary_clock_label, clock_text);
    }

    if (view->primary_title_label != NULL) {
        lv_label_set_text(view->primary_title_label, "Current Price");
    }

    if (view->primary_wifi_label != NULL) {
        lv_label_set_text(view->primary_wifi_label, state->wifi_status == APP_WIFI_STATUS_CONNECTED ? LV_SYMBOL_WIFI : "");
    }

    if (view->primary_hero_card != NULL) {
        lv_obj_set_style_bg_color(view->primary_hero_card, palette.hero_bg, 0);
        lv_obj_set_style_shadow_color(view->primary_hero_card, lv_color_black(), 0);
        lv_obj_set_style_shadow_width(view->primary_hero_card, 16, 0);
        lv_obj_set_style_shadow_opa(view->primary_hero_card, LV_OPA_20, 0);
    }

    if (view->primary_band_label != NULL) {
        lv_obj_set_style_text_color(view->primary_band_label, palette.hero_text, 0);
        lv_obj_set_style_text_align(view->primary_band_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (view->primary_pulse_dot != NULL) {
        lv_obj_set_style_bg_color(view->primary_pulse_dot, palette.chip_bg, 0);
        lv_obj_set_style_outline_color(view->primary_pulse_dot, palette.chip_bg, 0);
    }

    if (view->primary_pulse_icon_label != NULL) {
        lv_obj_set_style_text_color(view->primary_pulse_icon_label, palette.hero_bg, 0);
    }

    if (view->primary_price_label != NULL) {
        lv_obj_set_style_text_color(view->primary_price_label, palette.hero_text, 0);
        lv_obj_set_style_text_align(view->primary_price_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (view->primary_price_unit_label != NULL) {
        lv_obj_set_style_text_color(view->primary_price_unit_label, palette.hero_muted_text, 0);
        lv_obj_set_style_text_align(view->primary_price_unit_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (view->primary_remaining_label != NULL) {
        lv_obj_set_style_text_color(view->primary_remaining_label, palette.hero_text, 0);
        lv_obj_set_style_text_align(view->primary_remaining_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (view->primary_change_label != NULL) {
        lv_obj_set_style_text_color(view->primary_change_label, palette.hero_muted_text, 0);
        lv_obj_set_style_text_align(view->primary_change_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (view->primary_section_label != NULL) {
        lv_obj_set_style_text_color(view->primary_section_label, lv_color_hex(0xe5e7eb), 0);
    }

    if (state->tariff_has_data && state->tariff_current_block_valid) {
        char price_text[24] = {0};
        char remaining_text[32] = {0};
        char until_text[24] = {0};
        time_t now_local = time(NULL);

        lv_label_set_text(view->primary_band_label, tariff_model_get_band_name(state->tariff_current_band));
        lv_obj_set_style_text_font(view->primary_price_label, &lv_font_montserrat_28_numeric, 0);
        snprintf(price_text, sizeof(price_text), "%.1f", (double)state->tariff_current_price);
        lv_label_set_text(view->primary_price_label, price_text);
        if (view->primary_price_unit_label != NULL) {
            lv_label_set_text(view->primary_price_unit_label, "p/kWh");
        }
        format_remaining_compact(remaining_text, sizeof(remaining_text), state->tariff_current_block_end_local - now_local);
        lv_label_set_text(view->primary_remaining_label, remaining_text);

        format_until_time(until_text, sizeof(until_text), state->tariff_current_block_end_local);
        lv_label_set_text(view->primary_change_label, until_text);

        set_primary_pulse_enabled(view, tariff_band_is_extreme(state->tariff_current_band) && palette.pulse);
    } else {
        lv_label_set_text(view->primary_band_label, app_state_get_tariff_status_name(state->tariff_status));
        lv_obj_set_style_text_font(view->primary_price_label, &lv_font_montserrat_14, 0);
        lv_label_set_text(view->primary_price_label, state->tariff_current_text);
        if (view->primary_price_unit_label != NULL) {
            lv_label_set_text(view->primary_price_unit_label, "");
        }
        lv_label_set_text(view->primary_remaining_label, state->tariff_next_text);
        lv_label_set_text(view->primary_change_label, state->tariff_updated_text);
        set_primary_pulse_enabled(view, false);
    }

    for (uint8_t index = 0; index < APP_TARIFF_PREVIEW_MAX; index++) {
        const app_tariff_preview_t *preview = index < state->tariff_preview_count ? &state->tariff_previews[index] : NULL;
        update_primary_preview(view, index, preview);
    }
}

void ui_primary_create(lv_obj_t *tile, ui_router_view_t *view)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tile, 10, 0);
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 6, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    view->primary_top_bar = lv_obj_create(tile);
    lv_obj_set_width(view->primary_top_bar, lv_pct(100));
    lv_obj_set_height(view->primary_top_bar, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(view->primary_top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->primary_top_bar, 0, 0);
    lv_obj_set_style_pad_all(view->primary_top_bar, 0, 0);
    lv_obj_set_style_pad_column(view->primary_top_bar, 8, 0);
    lv_obj_set_layout(view->primary_top_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(view->primary_top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(view->primary_top_bar, LV_OBJ_FLAG_SCROLLABLE);

    view->primary_clock_label = lv_label_create(view->primary_top_bar);
    lv_obj_set_width(view->primary_clock_label, 52);
    lv_obj_set_style_text_color(view->primary_clock_label, lv_color_white(), 0);

    view->primary_title_label = lv_label_create(view->primary_top_bar);
    lv_obj_set_flex_grow(view->primary_title_label, 1);
    lv_obj_set_style_text_align(view->primary_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(view->primary_title_label, lv_color_white(), 0);

    view->primary_wifi_label = lv_label_create(view->primary_top_bar);
    lv_obj_set_width(view->primary_wifi_label, 52);
    lv_obj_set_style_text_align(view->primary_wifi_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(view->primary_wifi_label, lv_color_white(), 0);

    view->primary_hero_card = lv_obj_create(tile);
    lv_obj_set_width(view->primary_hero_card, lv_pct(100));
    lv_obj_set_height(view->primary_hero_card, 100);
    lv_obj_set_style_radius(view->primary_hero_card, 16, 0);
    lv_obj_set_style_border_width(view->primary_hero_card, 0, 0);
    lv_obj_set_style_pad_all(view->primary_hero_card, 8, 0);
    lv_obj_set_style_pad_column(view->primary_hero_card, 8, 0);
    lv_obj_set_layout(view->primary_hero_card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(view->primary_hero_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(view->primary_hero_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(view->primary_hero_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hero_left_col = lv_obj_create(view->primary_hero_card);
    lv_obj_set_width(hero_left_col, 92);
    lv_obj_set_height(hero_left_col, lv_pct(100));
    lv_obj_set_style_bg_opa(hero_left_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hero_left_col, 0, 0);
    lv_obj_set_style_pad_all(hero_left_col, 0, 0);
    lv_obj_set_style_pad_row(hero_left_col, 2, 0);
    lv_obj_set_layout(hero_left_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hero_left_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero_left_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hero_left_col, LV_OBJ_FLAG_SCROLLABLE);

    view->primary_price_label = lv_label_create(hero_left_col);
    lv_obj_set_width(view->primary_price_label, lv_pct(100));
    lv_label_set_long_mode(view->primary_price_label, LV_LABEL_LONG_WRAP);

    view->primary_price_unit_label = lv_label_create(hero_left_col);
    lv_obj_set_width(view->primary_price_unit_label, lv_pct(100));

    lv_obj_t *hero_center_col = lv_obj_create(view->primary_hero_card);
    lv_obj_set_width(hero_center_col, 72);
    lv_obj_set_height(hero_center_col, lv_pct(100));
    lv_obj_set_style_bg_opa(hero_center_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hero_center_col, 0, 0);
    lv_obj_set_style_pad_all(hero_center_col, 0, 0);
    lv_obj_set_style_pad_row(hero_center_col, 6, 0);
    lv_obj_set_layout(hero_center_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hero_center_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero_center_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hero_center_col, LV_OBJ_FLAG_SCROLLABLE);

    view->primary_band_label = lv_label_create(hero_center_col);
    lv_obj_set_width(view->primary_band_label, lv_pct(100));
    lv_label_set_long_mode(view->primary_band_label, LV_LABEL_LONG_WRAP);

    view->primary_pulse_dot = lv_obj_create(hero_center_col);
    lv_obj_set_size(view->primary_pulse_dot, 48, 48);
    lv_obj_set_style_radius(view->primary_pulse_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(view->primary_pulse_dot, 0, 0);
    lv_obj_set_style_outline_width(view->primary_pulse_dot, 4, 0);
    lv_obj_remove_flag(view->primary_pulse_dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    view->primary_pulse_icon_label = lv_label_create(view->primary_pulse_dot);
    lv_label_set_text(view->primary_pulse_icon_label, LV_SYMBOL_OK);
    lv_obj_center(view->primary_pulse_icon_label);

    lv_obj_t *hero_right_col = lv_obj_create(view->primary_hero_card);
    lv_obj_set_width(hero_right_col, 92);
    lv_obj_set_height(hero_right_col, lv_pct(100));
    lv_obj_set_style_bg_opa(hero_right_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hero_right_col, 0, 0);
    lv_obj_set_style_pad_all(hero_right_col, 0, 0);
    lv_obj_set_style_pad_row(hero_right_col, 0, 0);
    lv_obj_set_layout(hero_right_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hero_right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero_right_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hero_right_col, LV_OBJ_FLAG_SCROLLABLE);

    view->primary_change_label = lv_label_create(hero_right_col);
    lv_obj_set_width(view->primary_change_label, lv_pct(100));
    lv_label_set_long_mode(view->primary_change_label, LV_LABEL_LONG_WRAP);

    view->primary_remaining_label = lv_label_create(hero_right_col);
    lv_obj_set_width(view->primary_remaining_label, lv_pct(100));
    lv_label_set_long_mode(view->primary_remaining_label, LV_LABEL_LONG_WRAP);

    view->primary_section_label = lv_label_create(tile);
    lv_label_set_text(view->primary_section_label, "Next periods");

    lv_obj_t *preview_row = lv_obj_create(tile);
    lv_obj_set_width(preview_row, lv_pct(100));
    lv_obj_set_height(preview_row, 60);
    lv_obj_set_style_bg_opa(preview_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(preview_row, 0, 0);
    lv_obj_set_style_pad_all(preview_row, 0, 0);
    lv_obj_set_style_pad_column(preview_row, 6, 0);
    lv_obj_set_layout(preview_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(preview_row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(preview_row, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t index = 0; index < APP_TARIFF_PREVIEW_MAX; index++) {
        view->primary_preview_cards[index] = lv_obj_create(preview_row);
        lv_obj_set_height(view->primary_preview_cards[index], lv_pct(100));
        lv_obj_set_flex_grow(view->primary_preview_cards[index], 1);
        lv_obj_set_style_radius(view->primary_preview_cards[index], 12, 0);
        lv_obj_set_style_border_width(view->primary_preview_cards[index], 0, 0);
        lv_obj_set_style_pad_all(view->primary_preview_cards[index], 5, 0);
        lv_obj_set_style_pad_row(view->primary_preview_cards[index], 0, 0);
        lv_obj_set_layout(view->primary_preview_cards[index], LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(view->primary_preview_cards[index], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(view->primary_preview_cards[index], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(view->primary_preview_cards[index], LV_OBJ_FLAG_SCROLLABLE);

        view->primary_preview_time_labels[index] = lv_label_create(view->primary_preview_cards[index]);
        lv_obj_set_width(view->primary_preview_time_labels[index], lv_pct(100));
        view->primary_preview_band_labels[index] = lv_label_create(view->primary_preview_cards[index]);
        lv_obj_set_width(view->primary_preview_band_labels[index], lv_pct(100));
        view->primary_preview_price_labels[index] = lv_label_create(view->primary_preview_cards[index]);
        lv_obj_set_width(view->primary_preview_price_labels[index], lv_pct(100));
    }
}