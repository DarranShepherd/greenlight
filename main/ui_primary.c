#include "ui_router_internal.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "board_profile.h"
#include "numeric_fonts.h"
#include "tariff_model.h"

#define PRIMARY_PULSE_FRAME_MS 50U

static const lv_opa_t s_primary_pulse_lookup[] = {
    30, 31, 32, 36, 40, 45, 51, 59, 67, 76, 86, 97, 108, 119, 131,
    142, 154, 166, 177, 188, 199, 209, 218, 226, 234, 240, 245, 249, 253, 254,
    255, 254, 253, 249, 245, 240, 234, 226, 218, 209, 199, 188, 177, 166, 154,
    142, 131, 119, 108, 97, 86, 76, 67, 59, 51, 45, 40, 36, 32, 31,
};

#define PRIMARY_PULSE_PHASE_COUNT ((uint8_t)(sizeof(s_primary_pulse_lookup) / sizeof(s_primary_pulse_lookup[0])))

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

static primary_palette_t get_primary_palette(const app_state_t *state);
static void apply_primary_pulse_phase(ui_router_view_t *view);
static void primary_pulse_timer_cb(lv_timer_t *timer);

static lv_coord_t get_primary_hero_card_height(void)
{
    const greenlight_board_profile_t *board_profile = greenlight_board_profile_get();

    if (board_profile != NULL) {
        return board_profile->ui.primary_hero_card_height;
    }

    return 100;
}

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

static const char *get_primary_band_indicator_symbol(tariff_band_t band)
{
    switch (band) {
        case TARIFF_BAND_SUPER_CHEAP:
        case TARIFF_BAND_CHEAP:
            return LV_SYMBOL_OK;
        case TARIFF_BAND_NORMAL:
            return "-";
        case TARIFF_BAND_EXPENSIVE:
        case TARIFF_BAND_VERY_EXPENSIVE:
            return LV_SYMBOL_CLOSE;
        default:
            return "?";
    }
}

static primary_palette_t get_primary_palette_for_band(tariff_band_t band)
{
    switch (band) {
        case TARIFF_BAND_SUPER_CHEAP:
            return (primary_palette_t){
                .tile_bg = lv_color_hex(0x050816),
                .hero_bg = lv_color_hex(0x008c0c),
                .chip_bg = lv_color_hex(0x62f05a),
                .chip_text = lv_color_hex(0xeeff00),
                .hero_text = lv_color_hex(0xeeff00),
                .hero_muted_text = lv_color_hex(0xeeff00),
                .footer_bg = lv_color_hex(0x007508),
                .pulse = true,
            };
        case TARIFF_BAND_CHEAP:
            return (primary_palette_t){
                .tile_bg = lv_color_hex(0x050816),
                .hero_bg = lv_color_hex(0x8ee45a),
                .chip_bg = lv_color_hex(0xdfffba),
                .chip_text = lv_color_hex(0x2c5900),
                .hero_text = lv_color_hex(0x1f3e00),
                .hero_muted_text = lv_color_hex(0x2c5900),
                .footer_bg = lv_color_hex(0x66b53a),
                .pulse = false,
            };
        case TARIFF_BAND_NORMAL:
            return (primary_palette_t){
                .tile_bg = lv_color_hex(0x050816),
                .hero_bg = lv_color_hex(0xeeff00),
                .chip_bg = lv_color_hex(0xf7ff70),
                .chip_text = lv_color_hex(0x596200),
                .hero_text = lv_color_hex(0x424900),
                .hero_muted_text = lv_color_hex(0x596200),
                .footer_bg = lv_color_hex(0xb6c900),
                .pulse = false,
            };
        case TARIFF_BAND_EXPENSIVE:
            return (primary_palette_t){
                .tile_bg = lv_color_hex(0x050816),
                .hero_bg = lv_color_hex(0xff8a00),
                .chip_bg = lv_color_hex(0xffd299),
                .chip_text = lv_color_hex(0x5d2500),
                .hero_text = lv_color_hex(0x431800),
                .hero_muted_text = lv_color_hex(0x5d2500),
                .footer_bg = lv_color_hex(0xc85f00),
                .pulse = false,
            };
        case TARIFF_BAND_VERY_EXPENSIVE:
            return (primary_palette_t){
                .tile_bg = lv_color_hex(0x050816),
                .hero_bg = lv_color_hex(0xff1f0f),
                .chip_bg = lv_color_hex(0xffb8b0),
                .chip_text = lv_color_hex(0x650700),
                .hero_text = lv_color_white(),
                .hero_muted_text = lv_color_hex(0xffd9d4),
                .footer_bg = lv_color_hex(0xc51206),
                .pulse = true,
            };
        default:
            return get_primary_palette(&(app_state_t){0});
    }
}

static primary_palette_t get_primary_palette(const app_state_t *state)
{
    if (state->tariff_has_data && state->tariff_current_block_valid) {
        return get_primary_palette_for_band(state->tariff_current_band);
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

static void apply_primary_pulse_phase(ui_router_view_t *view)
{
    lv_opa_t opacity = LV_OPA_80;

    if (view == NULL || view->primary_pulse_dot == NULL) {
        return;
    }

    if (view->primary_pulse_enabled) {
        opacity = s_primary_pulse_lookup[view->primary_pulse_phase_index];
    }

    lv_obj_set_style_bg_opa(view->primary_pulse_dot, opacity, 0);
    lv_obj_set_style_outline_opa(view->primary_pulse_dot, (lv_opa_t)(opacity / 2), 0);

    if (view->primary_pulse_icon_label != NULL) {
        lv_obj_set_style_text_opa(
            view->primary_pulse_icon_label,
            view->primary_pulse_enabled ? opacity : LV_OPA_COVER,
            0
        );
    }
}

static void primary_pulse_timer_cb(lv_timer_t *timer)
{
    ui_router_view_t *view = timer != NULL ? (ui_router_view_t *)lv_timer_get_user_data(timer) : NULL;

    if (view == NULL || !view->primary_pulse_enabled) {
        return;
    }

    view->primary_pulse_phase_index = (uint8_t)((view->primary_pulse_phase_index + 1U) % PRIMARY_PULSE_PHASE_COUNT);
    apply_primary_pulse_phase(view);
}

static void set_primary_pulse_enabled(ui_router_view_t *view, bool enabled)
{
    if (view == NULL || view->primary_pulse_dot == NULL) {
        return;
    }

    if (view->primary_pulse_timer == NULL) {
        view->primary_pulse_timer = lv_timer_create(primary_pulse_timer_cb, PRIMARY_PULSE_FRAME_MS, view);
    }

    if (view->primary_pulse_enabled == enabled) {
        apply_primary_pulse_phase(view);
        return;
    }

    view->primary_pulse_enabled = enabled;
    if (enabled) {
        view->primary_pulse_phase_index = 0;
    }

    apply_primary_pulse_phase(view);
}

static void style_preview_card(lv_obj_t *card, lv_obj_t *time_label, lv_obj_t *band_label, tariff_band_t band, bool active)
{
    primary_palette_t palette = {0};

    if (card == NULL || time_label == NULL || band_label == NULL) {
        return;
    }

    palette = active ? get_primary_palette_for_band(band) : get_primary_palette(&(app_state_t){0});

    lv_obj_set_style_bg_color(card, active ? palette.hero_bg : lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_text_color(time_label, active ? palette.hero_muted_text : lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_text_color(band_label, active ? palette.hero_text : lv_color_white(), 0);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(band_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(band_label, &lv_font_montserrat_20_numeric, 0);
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
        style_preview_card(
            view->primary_preview_cards[index],
            view->primary_preview_time_labels[index],
            view->primary_preview_band_labels[index],
            preview->band,
            true
        );
        return;
    }
}

void ui_primary_update(const app_state_t *state, ui_router_view_t *view)
{
    primary_palette_t palette = get_primary_palette(state);
    char clock_text[12] = {0};
    uint8_t valid_preview_count = 0;

    lv_obj_set_style_bg_color(view->tiles[APP_SCREEN_PRIMARY], palette.tile_bg, 0);
    ui_router_format_clock_label(clock_text, sizeof(clock_text), state->local_time_text);

    if (view->primary_clock_label != NULL) {
        lv_label_set_text(view->primary_clock_label, clock_text);
    }

    if (view->primary_title_label != NULL) {
        lv_label_set_text(view->primary_title_label, "Current Price");
    }

    ui_router_update_wifi_status(
        view->primary_ota_label,
        view->primary_wifi_label,
        view->primary_wifi_strike,
        state->wifi_status,
        state->firmware_update_available,
        lv_color_white(),
        lv_color_hex(0xdc2626)
    );

    if (view->primary_hero_card != NULL) {
        lv_obj_set_style_bg_color(view->primary_hero_card, palette.hero_bg, 0);
        lv_obj_set_style_shadow_width(view->primary_hero_card, 0, 0);
    }

    if (view->primary_band_label != NULL) {
        lv_obj_set_style_text_color(view->primary_band_label, palette.hero_text, 0);
        lv_obj_set_style_text_align(view->primary_band_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (view->primary_hero_center_col != NULL && view->primary_band_label != NULL) {
        lv_obj_update_layout(view->primary_band_label);
        lv_coord_t band_label_height = lv_obj_get_height(view->primary_band_label);
        lv_obj_set_style_pad_top(view->primary_hero_center_col, band_label_height + 4, 0);
    }

    if (view->primary_pulse_dot != NULL) {
        lv_obj_set_style_bg_color(view->primary_pulse_dot, palette.chip_bg, 0);
        lv_obj_set_style_outline_color(view->primary_pulse_dot, palette.hero_text, 0);
    }

    if (view->primary_pulse_icon_label != NULL) {
        lv_obj_set_style_text_color(view->primary_pulse_icon_label, palette.hero_text, 0);
    }

    apply_primary_pulse_phase(view);

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
        if (view->primary_pulse_icon_label != NULL) {
            lv_label_set_text(view->primary_pulse_icon_label, get_primary_band_indicator_symbol(state->tariff_current_band));
        }
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
        if (view->primary_pulse_icon_label != NULL) {
            lv_label_set_text(view->primary_pulse_icon_label, "?");
        }
        lv_obj_set_style_text_font(view->primary_price_label, &lv_font_montserrat_14, 0);
        lv_label_set_text(view->primary_price_label, state->tariff_current_text);
        if (view->primary_price_unit_label != NULL) {
            lv_label_set_text(view->primary_price_unit_label, "");
        }
        lv_label_set_text(view->primary_remaining_label, state->tariff_next_text);
        lv_label_set_text(view->primary_change_label, state->tariff_updated_text);
        set_primary_pulse_enabled(view, false);
    }

    for (uint8_t index = 0; index < state->tariff_preview_count && index < APP_TARIFF_PREVIEW_MAX; index++) {
        if (state->tariff_previews[index].valid) {
            valid_preview_count++;
        }
    }

    if (view->primary_section_label != NULL) {
        if (valid_preview_count > 0) {
            lv_obj_clear_flag(view->primary_section_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(view->primary_section_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (view->primary_preview_row != NULL) {
        if (valid_preview_count > 0) {
            lv_obj_clear_flag(view->primary_preview_row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(view->primary_preview_row, LV_OBJ_FLAG_HIDDEN);
        }
    }

    for (uint8_t index = 0; index < APP_TARIFF_PREVIEW_MAX; index++) {
        const app_tariff_preview_t *preview = index < state->tariff_preview_count ? &state->tariff_previews[index] : NULL;
        bool show_preview = preview != NULL && preview->valid;

        if (view->primary_preview_cards[index] == NULL) {
            continue;
        }

        if (show_preview) {
            lv_obj_clear_flag(view->primary_preview_cards[index], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_flex_grow(view->primary_preview_cards[index], 1);
            update_primary_preview(view, index, preview);
        } else {
            lv_obj_add_flag(view->primary_preview_cards[index], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ui_primary_create(lv_obj_t *tile, ui_router_view_t *view)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x050816), 0);
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

    ui_router_create_wifi_status(
        view->primary_top_bar,
        &view->primary_ota_label,
        &view->primary_wifi_label,
        &view->primary_wifi_strike
    );

    view->primary_hero_card = lv_obj_create(tile);
    lv_obj_set_width(view->primary_hero_card, lv_pct(100));
    lv_obj_set_height(view->primary_hero_card, get_primary_hero_card_height());
    lv_obj_set_style_radius(view->primary_hero_card, 16, 0);
    lv_obj_set_style_border_width(view->primary_hero_card, 0, 0);
    lv_obj_set_style_pad_all(view->primary_hero_card, 8, 0);
    lv_obj_set_style_pad_row(view->primary_hero_card, 4, 0);
    lv_obj_set_layout(view->primary_hero_card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(view->primary_hero_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(view->primary_hero_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(view->primary_hero_card, LV_OBJ_FLAG_SCROLLABLE);

    view->primary_band_label = lv_label_create(view->primary_hero_card);
    lv_obj_set_width(view->primary_band_label, lv_pct(100));
    lv_label_set_long_mode(view->primary_band_label, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(view->primary_band_label, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(view->primary_band_label, LV_ALIGN_TOP_MID, 0, 0);

    view->primary_hero_content_row = lv_obj_create(view->primary_hero_card);
    lv_obj_set_width(view->primary_hero_content_row, lv_pct(100));
    lv_obj_set_flex_grow(view->primary_hero_content_row, 1);
    lv_obj_set_style_bg_opa(view->primary_hero_content_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->primary_hero_content_row, 0, 0);
    lv_obj_set_style_pad_all(view->primary_hero_content_row, 0, 0);
    lv_obj_set_style_pad_column(view->primary_hero_content_row, 8, 0);
    lv_obj_set_layout(view->primary_hero_content_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(view->primary_hero_content_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(view->primary_hero_content_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(view->primary_hero_content_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hero_left_col = lv_obj_create(view->primary_hero_content_row);
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

    view->primary_hero_center_col = lv_obj_create(view->primary_hero_content_row);
    lv_obj_set_width(view->primary_hero_center_col, 72);
    lv_obj_set_height(view->primary_hero_center_col, lv_pct(100));
    lv_obj_set_style_bg_opa(view->primary_hero_center_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->primary_hero_center_col, 0, 0);
    lv_obj_set_style_pad_all(view->primary_hero_center_col, 0, 0);
    lv_obj_set_style_pad_row(view->primary_hero_center_col, 6, 0);
    lv_obj_set_layout(view->primary_hero_center_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(view->primary_hero_center_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(view->primary_hero_center_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(view->primary_hero_center_col, LV_OBJ_FLAG_SCROLLABLE);

    view->primary_pulse_dot = lv_obj_create(view->primary_hero_center_col);
    lv_obj_set_size(view->primary_pulse_dot, 48, 48);
    lv_obj_set_style_radius(view->primary_pulse_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(view->primary_pulse_dot, 0, 0);
    lv_obj_set_style_outline_width(view->primary_pulse_dot, 4, 0);
    lv_obj_remove_flag(view->primary_pulse_dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    view->primary_pulse_icon_label = lv_label_create(view->primary_pulse_dot);
    lv_label_set_text(view->primary_pulse_icon_label, LV_SYMBOL_OK);
    lv_obj_center(view->primary_pulse_icon_label);

    view->primary_pulse_phase_index = 0;
    view->primary_pulse_enabled = false;
    apply_primary_pulse_phase(view);

    lv_obj_t *hero_right_col = lv_obj_create(view->primary_hero_content_row);
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

    view->primary_preview_row = lv_obj_create(tile);
    lv_obj_set_width(view->primary_preview_row, lv_pct(100));
    lv_obj_set_height(view->primary_preview_row, 60);
    lv_obj_set_style_bg_opa(view->primary_preview_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->primary_preview_row, 0, 0);
    lv_obj_set_style_pad_all(view->primary_preview_row, 0, 0);
    lv_obj_set_style_pad_column(view->primary_preview_row, 6, 0);
    lv_obj_set_layout(view->primary_preview_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(view->primary_preview_row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(view->primary_preview_row, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t index = 0; index < APP_TARIFF_PREVIEW_MAX; index++) {
        view->primary_preview_cards[index] = lv_obj_create(view->primary_preview_row);
        lv_obj_set_height(view->primary_preview_cards[index], lv_pct(100));
        lv_obj_set_flex_grow(view->primary_preview_cards[index], 1);
        lv_obj_set_style_radius(view->primary_preview_cards[index], 12, 0);
        lv_obj_set_style_border_width(view->primary_preview_cards[index], 0, 0);
        lv_obj_set_style_pad_all(view->primary_preview_cards[index], 5, 0);
        lv_obj_set_style_pad_row(view->primary_preview_cards[index], 3, 0);
        lv_obj_set_layout(view->primary_preview_cards[index], LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(view->primary_preview_cards[index], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(view->primary_preview_cards[index], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(view->primary_preview_cards[index], LV_OBJ_FLAG_SCROLLABLE);

        view->primary_preview_time_labels[index] = lv_label_create(view->primary_preview_cards[index]);
        lv_obj_set_width(view->primary_preview_time_labels[index], lv_pct(100));

        view->primary_preview_value_containers[index] = lv_obj_create(view->primary_preview_cards[index]);
        lv_obj_set_width(view->primary_preview_value_containers[index], lv_pct(100));
        lv_obj_set_flex_grow(view->primary_preview_value_containers[index], 1);
        lv_obj_set_style_bg_opa(view->primary_preview_value_containers[index], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(view->primary_preview_value_containers[index], 0, 0);
        lv_obj_set_style_pad_all(view->primary_preview_value_containers[index], 0, 0);
        lv_obj_set_layout(view->primary_preview_value_containers[index], LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(view->primary_preview_value_containers[index], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(view->primary_preview_value_containers[index], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(view->primary_preview_value_containers[index], LV_OBJ_FLAG_SCROLLABLE);

        view->primary_preview_band_labels[index] = lv_label_create(view->primary_preview_value_containers[index]);
        lv_obj_set_width(view->primary_preview_band_labels[index], lv_pct(100));
    }
}