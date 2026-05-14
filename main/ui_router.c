#include "ui_router.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include <esp_check.h>
#include <esp_lvgl_port.h>

#include "app_settings.h"
#include "lcd.h"
#include "numeric_fonts.h"
#include "splash_logo.h"
#include "sync_controller.h"
#include "time_manager.h"
#include "touch.h"
#include "wifi_manager.h"

#define TOUCH_CALIBRATION_POINT_COUNT 3
#define TOUCH_CALIBRATION_TARGET_SIZE 36

typedef struct {
    lv_obj_t *tileview;
    lv_obj_t *tiles[APP_SCREEN_COUNT];
    lv_obj_t *startup_overlay;
    lv_obj_t *startup_logo_image;
    lv_obj_t *startup_title_label;
    lv_obj_t *startup_status_label;
    lv_obj_t *settings_content;
    lv_obj_t *primary_top_bar;
    lv_obj_t *primary_clock_label;
    lv_obj_t *primary_title_label;
    lv_obj_t *primary_wifi_label;
    lv_obj_t *primary_hero_card;
    lv_obj_t *primary_band_chip;
    lv_obj_t *primary_band_label;
    lv_obj_t *primary_pulse_dot;
    lv_obj_t *primary_pulse_icon_label;
    lv_obj_t *primary_price_label;
    lv_obj_t *primary_price_unit_label;
    lv_obj_t *primary_remaining_label;
    lv_obj_t *primary_change_label;
    lv_obj_t *primary_section_label;
    lv_obj_t *primary_preview_cards[APP_TARIFF_PREVIEW_MAX];
    lv_obj_t *primary_preview_time_labels[APP_TARIFF_PREVIEW_MAX];
    lv_obj_t *primary_preview_band_labels[APP_TARIFF_PREVIEW_MAX];
    lv_obj_t *primary_preview_price_labels[APP_TARIFF_PREVIEW_MAX];
    lv_obj_t *detail_status_label;
    lv_obj_t *detail_updated_label;
    lv_obj_t *detail_top_bar;
    lv_obj_t *detail_clock_label;
    lv_obj_t *detail_title_label;
    lv_obj_t *detail_wifi_label;
    lv_obj_t *detail_day_panels[2];
    lv_obj_t *detail_day_titles[2];
    lv_obj_t *detail_day_bar_rows[2];
    lv_obj_t *detail_day_bars[2][APP_TARIFF_DAY_SLOT_MAX];
    lv_obj_t *detail_day_time_markers[2];
    lv_obj_t *detail_day_min_labels[2];
    lv_obj_t *detail_day_avg_labels[2];
    lv_obj_t *detail_day_max_labels[2];
    lv_obj_t *brightness_label;
    lv_obj_t *brightness_bar;
    lv_obj_t *region_label;
    lv_obj_t *settings_top_bar;
    lv_obj_t *settings_clock_label;
    lv_obj_t *settings_title_label;
    lv_obj_t *settings_wifi_label;
    lv_obj_t *settings_wifi_strike_label;
    lv_obj_t *wifi_status_label;
    lv_obj_t *time_status_label;
    lv_obj_t *local_time_label;
    lv_obj_t *wifi_dropdown;
    lv_obj_t *wifi_psk_textarea;
    lv_obj_t *wifi_keyboard;
    lv_obj_t *wifi_scan_summary_label;
    lv_obj_t *touch_calibration_label;
    lv_obj_t *touch_calibration_overlay;
    lv_obj_t *touch_calibration_prompt;
    lv_obj_t *touch_calibration_target;
    lv_point_t touch_calibration_samples[TOUCH_CALIBRATION_POINT_COUNT];
    app_touch_calibration_t previous_touch_calibration;
    uint8_t touch_calibration_step;
    char wifi_dropdown_cache[APP_WIFI_SCAN_MAX_RESULTS * (APP_SETTINGS_WIFI_SSID_MAX_LEN + 1) + 32];
    app_state_t *state;
    app_state_t state_snapshot;
} ui_router_view_t;

static ui_router_view_t s_view;

typedef struct {
    char code;
    const char *name;
} region_option_t;

static const region_option_t s_region_options[] = {
    {'A', "Eastern England"},
    {'B', "East Midlands"},
    {'C', "London"},
    {'D', "Merseyside and North Wales"},
    {'E', "West Midlands"},
    {'F', "North East England"},
    {'G', "North West England"},
    {'H', "South England"},
    {'J', "South East England"},
    {'K', "South Wales"},
    {'L', "South West England"},
    {'M', "Yorkshire"},
    {'N', "South Scotland"},
    {'P', "North Scotland"},
};

enum {
    DETAIL_DAY_INDEX_TODAY = 0,
    DETAIL_DAY_INDEX_TOMORROW = 1,
    DETAIL_DAY_COUNT = 2,
};

static const char *s_touch_calibration_prompts[TOUCH_CALIBRATION_POINT_COUNT] = {
    "Touch the upper-left target",
    "Touch the upper-right target",
    "Touch the lower-center target",
};

static size_t get_region_option_index(const char *region_code)
{
    char normalized_code = 'B';

    if (region_code != NULL && region_code[0] != '\0') {
        normalized_code = (char)toupper((unsigned char)region_code[0]);
    }

    for (size_t index = 0; index < sizeof(s_region_options) / sizeof(s_region_options[0]); index++) {
        if (s_region_options[index].code == normalized_code) {
            return index;
        }
    }

    return 1;
}

static const char *get_region_name(const char *region_code)
{
    return s_region_options[get_region_option_index(region_code)].name;
}

static uint8_t clamp_brightness_value(int32_t brightness_percent)
{
    if (brightness_percent < 10) {
        return 10;
    }

    if (brightness_percent > 100) {
        return 100;
    }

    return (uint8_t)brightness_percent;
}

static bool copy_view_state_snapshot(app_state_t *snapshot)
{
    if (s_view.state == NULL || snapshot == NULL) {
        return false;
    }

    app_state_get_snapshot(s_view.state, snapshot);
    return true;
}

static bool copy_view_settings(app_settings_t *settings)
{
    if (s_view.state == NULL || settings == NULL) {
        return false;
    }

    app_state_get_settings(s_view.state, settings);
    return true;
}

static void apply_brightness_locked(uint8_t brightness_percent)
{
    app_state_set_brightness(s_view.state, brightness_percent);
    (void)lcd_set_brightness(brightness_percent);
    (void)app_settings_save_brightness(brightness_percent);

    if (s_view.brightness_label != NULL) {
        lv_label_set_text_fmt(s_view.brightness_label, "%u%%", (unsigned int)brightness_percent);
    }

    if (s_view.brightness_bar != NULL) {
        lv_bar_set_value(s_view.brightness_bar, brightness_percent, LV_ANIM_OFF);
    }
}

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

static void format_clock_label(char *buffer, size_t buffer_size, const char *local_time_text)
{
    size_t source_length = 0;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (local_time_text == NULL) {
        strlcpy(buffer, "--:--", buffer_size);
        return;
    }

    source_length = strlen(local_time_text);
    if (source_length >= 5 && local_time_text[source_length - 3] == ':') {
        strlcpy(buffer, &local_time_text[source_length - 5], buffer_size);
        return;
    }

    strlcpy(buffer, local_time_text, buffer_size);
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

static void set_primary_pulse_enabled(bool enabled)
{
    lv_anim_t animation;

    if (s_view.primary_pulse_dot == NULL) {
        return;
    }

    lv_anim_del(s_view.primary_pulse_dot, primary_pulse_anim_cb);

    if (!enabled) {
        lv_obj_set_style_bg_opa(s_view.primary_pulse_dot, LV_OPA_80, 0);
        lv_obj_set_style_outline_opa(s_view.primary_pulse_dot, LV_OPA_30, 0);
        return;
    }

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, s_view.primary_pulse_dot);
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

    if (active) {
        palette = get_primary_palette_for_band(band);
    } else {
        palette = get_primary_palette(&(app_state_t){0});
    }

    lv_obj_set_style_bg_color(card, active ? palette.hero_bg : lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_text_color(time_label, active ? palette.hero_muted_text : lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_text_color(band_label, active ? palette.hero_text : lv_color_white(), 0);
    lv_obj_set_style_text_color(price_label, active ? palette.hero_muted_text : lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(band_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(band_label, &lv_font_montserrat_14, 0);
}

static lv_color_t get_band_fill_color(tariff_band_t band)
{
    return get_primary_palette_for_band(band).hero_bg;
}

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

static void update_detail_time_marker_locked(const app_state_t *state)
{
    struct tm local_tm = {0};
    int minutes_of_day = 0;
    lv_coord_t row_width = 0;
    lv_coord_t marker_x = 0;
    lv_obj_t *today_row = s_view.detail_day_bar_rows[DETAIL_DAY_INDEX_TODAY];
    lv_obj_t *today_marker = s_view.detail_day_time_markers[DETAIL_DAY_INDEX_TODAY];

    if (today_row == NULL || today_marker == NULL) {
        return;
    }

    lv_obj_add_flag(s_view.detail_day_time_markers[DETAIL_DAY_INDEX_TOMORROW], LV_OBJ_FLAG_HIDDEN);

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

static void update_detail_day_panel_locked(uint8_t index, const char *title, const app_tariff_day_view_t *day_view, bool two_day_layout)
{
    lv_obj_t *panel = NULL;
    lv_coord_t chart_height = 72;
    float max_price = 0.0f;

    if (index >= DETAIL_DAY_COUNT) {
        return;
    }

    panel = s_view.detail_day_panels[index];
    if (panel == NULL) {
        return;
    }

    lv_obj_set_width(panel, two_day_layout ? lv_pct(48) : lv_pct(100));
    lv_obj_set_flex_grow(panel, two_day_layout ? 0 : 1);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);

    if (s_view.detail_day_titles[index] != NULL) {
        lv_label_set_text(s_view.detail_day_titles[index], title);
    }

    if (day_view == NULL || !day_view->available || day_view->slot_count == 0) {
        if (s_view.detail_day_titles[index] != NULL) {
            lv_label_set_text_fmt(s_view.detail_day_titles[index], "%s\nAwaiting prices", title);
        }

        for (uint8_t slot_index = 0; slot_index < APP_TARIFF_DAY_SLOT_MAX; slot_index++) {
            if (s_view.detail_day_bars[index][slot_index] != NULL) {
                lv_obj_add_flag(s_view.detail_day_bars[index][slot_index], LV_OBJ_FLAG_HIDDEN);
            }
        }

        style_detail_stat_label(s_view.detail_day_min_labels[index], "Min", 0.0f, get_band_fill_color(TARIFF_BAND_CHEAP));
        style_detail_stat_label(s_view.detail_day_avg_labels[index], "Avg", 0.0f, get_band_fill_color(TARIFF_BAND_NORMAL));
        style_detail_stat_label(s_view.detail_day_max_labels[index], "Max", 0.0f, get_band_fill_color(TARIFF_BAND_EXPENSIVE));
        return;
    }

    max_price = day_view->max_price > 0.0f ? day_view->max_price : 1.0f;
    if (max_price < 5.0f) {
        max_price = 5.0f;
    }

    for (uint8_t slot_index = 0; slot_index < APP_TARIFF_DAY_SLOT_MAX; slot_index++) {
        lv_obj_t *bar = s_view.detail_day_bars[index][slot_index];

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
        lv_obj_set_style_bg_color(bar, get_band_fill_color(day_view->slots[slot_index].band), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
    }

    style_detail_stat_label(s_view.detail_day_min_labels[index], "Min", day_view->min_price, get_band_fill_color(TARIFF_BAND_CHEAP));
    style_detail_stat_label(s_view.detail_day_avg_labels[index], "Avg", day_view->avg_price, get_band_fill_color(TARIFF_BAND_NORMAL));
    style_detail_stat_label(s_view.detail_day_max_labels[index], "Max", day_view->max_price, get_band_fill_color(TARIFF_BAND_EXPENSIVE));
}

static lv_obj_t *create_section_card(lv_obj_t *parent, lv_color_t bg_color)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_bg_color(card, bg_color, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *create_dark_button(lv_obj_t *parent, const char *label_text)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_height(button, 42);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x111827), 0);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return button;
}

static int64_t det3(
    int32_t a11, int32_t a12, int32_t a13,
    int32_t a21, int32_t a22, int32_t a23,
    int32_t a31, int32_t a32, int32_t a33
)
{
    return (int64_t)a11 * ((int64_t)a22 * a33 - (int64_t)a23 * a32) -
           (int64_t)a12 * ((int64_t)a21 * a33 - (int64_t)a23 * a31) +
           (int64_t)a13 * ((int64_t)a21 * a32 - (int64_t)a22 * a31);
}

static lv_point_t convert_display_point_to_touch_space(lv_point_t display_point)
{
    lv_display_t *display = lv_display_get_default();
    lv_display_rotation_t rotation = LV_DISPLAY_ROTATION_0;
    int32_t original_width = 0;
    int32_t original_height = 0;
    lv_point_t touch_point = display_point;

    if (display == NULL) {
        return touch_point;
    }

    rotation = lv_display_get_rotation(display);
    original_width = lv_display_get_original_horizontal_resolution(display);
    original_height = lv_display_get_original_vertical_resolution(display);

    switch (rotation) {
        case LV_DISPLAY_ROTATION_90:
            touch_point.x = display_point.y;
            touch_point.y = original_height - display_point.x - 1;
            break;
        case LV_DISPLAY_ROTATION_180:
            touch_point.x = original_width - display_point.x - 1;
            touch_point.y = original_height - display_point.y - 1;
            break;
        case LV_DISPLAY_ROTATION_270:
            touch_point.x = original_width - display_point.y - 1;
            touch_point.y = display_point.x;
            break;
        case LV_DISPLAY_ROTATION_0:
        default:
            break;
    }

    return touch_point;
}

static lv_point_t get_touch_calibration_target_point(uint8_t step)
{
    lv_area_t overlay_coords = {0};
    int32_t width = 0;
    int32_t height = 0;
    lv_point_t point = {0};

    if (s_view.touch_calibration_overlay != NULL) {
        lv_obj_get_coords(s_view.touch_calibration_overlay, &overlay_coords);
        width = lv_obj_get_width(s_view.touch_calibration_overlay);
        height = lv_obj_get_height(s_view.touch_calibration_overlay);
    }

    if (width <= 0 || height <= 0) {
        lv_display_t *display = lv_display_get_default();
        width = display != NULL ? (int32_t)lv_display_get_horizontal_resolution(display) : 320;
        height = display != NULL ? (int32_t)lv_display_get_vertical_resolution(display) : 240;
        overlay_coords.x1 = 0;
        overlay_coords.y1 = 0;
    }

    switch (step) {
        case 0:
            point.x = overlay_coords.x1 + 36;
            point.y = overlay_coords.y1 + 36;
            break;
        case 1:
            point.x = overlay_coords.x1 + width - 36;
            point.y = overlay_coords.y1 + 36;
            break;
        case 2:
        default:
            point.x = overlay_coords.x1 + (width / 2);
            point.y = overlay_coords.y1 + height - 56;
            break;
    }

    return point;
}

static void update_touch_calibration_status_locked(const app_state_t *state)
{
    if (s_view.touch_calibration_label == NULL) {
        return;
    }

    if (state->settings.touch_calibration.valid) {
        lv_label_set_text(s_view.touch_calibration_label, "Touch calibration saved");
    } else {
        lv_label_set_text(s_view.touch_calibration_label, "Touch uses default mapping");
    }
}

static void update_region_label_locked(const app_state_t *state)
{
    if (s_view.region_label == NULL || state == NULL) {
        return;
    }

    lv_label_set_text_fmt(
        s_view.region_label,
        "%s  %s",
        state->settings.region_code,
        get_region_name(state->settings.region_code)
    );
}

static void apply_region_index_locked(size_t region_index)
{
    app_settings_t current_settings = {0};
    app_settings_t next_settings = {0};

    if (s_view.state == NULL || region_index >= (sizeof(s_region_options) / sizeof(s_region_options[0]))) {
        return;
    }

    if (!copy_view_settings(&current_settings)) {
        return;
    }

    next_settings = current_settings;
    snprintf(next_settings.region_code, sizeof(next_settings.region_code), "%c", s_region_options[region_index].code);

    if (strcmp(next_settings.region_code, current_settings.region_code) == 0) {
        return;
    }

    if (app_settings_save(&next_settings) != ESP_OK) {
        return;
    }

    app_state_set_settings(s_view.state, &next_settings);
    if (s_view.region_label != NULL) {
        lv_label_set_text_fmt(s_view.region_label, "%s  %s", next_settings.region_code, get_region_name(next_settings.region_code));
    }
    sync_controller_request_refresh();
}

static bool solve_touch_calibration(
    const lv_point_t samples[TOUCH_CALIBRATION_POINT_COUNT],
    app_touch_calibration_t *calibration
)
{
    lv_point_t targets[TOUCH_CALIBRATION_POINT_COUNT] = {0};
    int64_t determinant = 0;

    if (samples == NULL || calibration == NULL) {
        return false;
    }

    for (uint8_t index = 0; index < TOUCH_CALIBRATION_POINT_COUNT; index++) {
        targets[index] = convert_display_point_to_touch_space(get_touch_calibration_target_point(index));
    }

    determinant = det3(
        samples[0].x, samples[0].y, 1,
        samples[1].x, samples[1].y, 1,
        samples[2].x, samples[2].y, 1
    );

    if (determinant == 0) {
        return false;
    }

    calibration->xx = (int32_t)((APP_TOUCH_CALIBRATION_SCALE * det3(
        targets[0].x, samples[0].y, 1,
        targets[1].x, samples[1].y, 1,
        targets[2].x, samples[2].y, 1
    )) / determinant);
    calibration->xy = (int32_t)((APP_TOUCH_CALIBRATION_SCALE * det3(
        samples[0].x, targets[0].x, 1,
        samples[1].x, targets[1].x, 1,
        samples[2].x, targets[2].x, 1
    )) / determinant);
    calibration->x_offset = (int32_t)((APP_TOUCH_CALIBRATION_SCALE * det3(
        samples[0].x, samples[0].y, targets[0].x,
        samples[1].x, samples[1].y, targets[1].x,
        samples[2].x, samples[2].y, targets[2].x
    )) / determinant);
    calibration->yx = (int32_t)((APP_TOUCH_CALIBRATION_SCALE * det3(
        targets[0].y, samples[0].y, 1,
        targets[1].y, samples[1].y, 1,
        targets[2].y, samples[2].y, 1
    )) / determinant);
    calibration->yy = (int32_t)((APP_TOUCH_CALIBRATION_SCALE * det3(
        samples[0].x, targets[0].y, 1,
        samples[1].x, targets[1].y, 1,
        samples[2].x, targets[2].y, 1
    )) / determinant);
    calibration->y_offset = (int32_t)((APP_TOUCH_CALIBRATION_SCALE * det3(
        samples[0].x, samples[0].y, targets[0].y,
        samples[1].x, samples[1].y, targets[1].y,
        samples[2].x, samples[2].y, targets[2].y
    )) / determinant);
    calibration->valid = true;
    return true;
}

static void update_touch_calibration_overlay_locked(void)
{
    lv_area_t overlay_coords = {0};
    lv_point_t point = {0};

    if (s_view.touch_calibration_overlay == NULL || s_view.touch_calibration_prompt == NULL || s_view.touch_calibration_target == NULL) {
        return;
    }

    if (s_view.touch_calibration_step >= TOUCH_CALIBRATION_POINT_COUNT) {
        lv_obj_add_flag(s_view.touch_calibration_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    point = get_touch_calibration_target_point(s_view.touch_calibration_step);
    lv_obj_get_coords(s_view.touch_calibration_overlay, &overlay_coords);
    lv_label_set_text(s_view.touch_calibration_prompt, s_touch_calibration_prompts[s_view.touch_calibration_step]);
    lv_obj_set_pos(
        s_view.touch_calibration_target,
        point.x - overlay_coords.x1 - (TOUCH_CALIBRATION_TARGET_SIZE / 2),
        point.y - overlay_coords.y1 - (TOUCH_CALIBRATION_TARGET_SIZE / 2)
    );
    lv_obj_move_foreground(s_view.touch_calibration_target);
}

static void set_keyboard_target(lv_obj_t *textarea)
{
    if (s_view.wifi_keyboard == NULL) {
        return;
    }

    lv_keyboard_set_textarea(s_view.wifi_keyboard, textarea);
    lv_obj_clear_flag(s_view.wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_mode(s_view.wifi_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);

    lv_obj_scroll_to_view_recursive(textarea, LV_ANIM_ON);
}

static void hide_keyboard(void)
{
    if (s_view.wifi_keyboard == NULL) {
        return;
    }

    lv_keyboard_set_textarea(s_view.wifi_keyboard, NULL);
    lv_obj_add_flag(s_view.wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void update_wifi_dropdown_locked(const app_state_t *state)
{
    char options[sizeof(s_view.wifi_dropdown_cache)] = {0};
    size_t offset = 0;

    if (s_view.wifi_dropdown == NULL) {
        return;
    }

    if (state->wifi_scan_result_count == 0) {
        const char *fallback_ssid = state->settings.wifi_ssid[0] != '\0' ? state->settings.wifi_ssid : "Scan for Wi-Fi";

        if (strcmp(s_view.wifi_dropdown_cache, fallback_ssid) != 0) {
            lv_dropdown_set_options(s_view.wifi_dropdown, fallback_ssid);
            strlcpy(s_view.wifi_dropdown_cache, fallback_ssid, sizeof(s_view.wifi_dropdown_cache));
        }
        return;
    }

    for (uint8_t index = 0; index < state->wifi_scan_result_count; index++) {
        int written = snprintf(
            &options[offset],
            sizeof(options) - offset,
            "%s%s",
            index == 0 ? "" : "\n",
            state->wifi_scan_results[index].ssid[0] != '\0' ? state->wifi_scan_results[index].ssid : "Hidden SSID"
        );

        if (written < 0 || (size_t)written >= sizeof(options) - offset) {
            break;
        }

        offset += (size_t)written;
    }

    if (strcmp(options, s_view.wifi_dropdown_cache) != 0) {
        lv_dropdown_set_options(s_view.wifi_dropdown, options);
        strlcpy(s_view.wifi_dropdown_cache, options, sizeof(s_view.wifi_dropdown_cache));
    }
}

static void tileview_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_SCROLL_END) {
        return;
    }

    lv_obj_t *active_tile = lv_tileview_get_tile_active(s_view.tileview);
    for (uint32_t index = 0; index < APP_SCREEN_COUNT; index++) {
        if (s_view.tiles[index] == active_tile) {
            app_state_set_active_screen(s_view.state, (app_screen_t)index);
            break;
        }
    }
}

static void brightness_button_event_cb(lv_event_t *event)
{
    app_settings_t settings = {0};

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (!copy_view_settings(&settings)) {
        return;
    }

    int32_t delta = (int32_t)(intptr_t)lv_event_get_user_data(event);
    uint8_t next_brightness = clamp_brightness_value((int32_t)settings.brightness_percent + delta);
    apply_brightness_locked(next_brightness);
}

static void region_button_event_cb(lv_event_t *event)
{
    app_settings_t settings = {0};
    int32_t delta = 0;
    size_t region_count = sizeof(s_region_options) / sizeof(s_region_options[0]);
    size_t current_index = 0;
    size_t next_index = 0;

    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_view.state == NULL || region_count == 0) {
        return;
    }

    if (!copy_view_settings(&settings)) {
        return;
    }

    delta = (int32_t)(intptr_t)lv_event_get_user_data(event);
    current_index = get_region_option_index(settings.region_code);
    next_index = (current_index + region_count + (size_t)delta) % region_count;
    apply_region_index_locked(next_index);
}

static void wifi_scan_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    (void)wifi_manager_request_scan();
}

static void wifi_connect_button_event_cb(lv_event_t *event)
{
    app_state_t snapshot = {0};
    const char *psk = NULL;
    char selected_ssid[APP_SETTINGS_WIFI_SSID_MAX_LEN + 1] = {0};
    const char *ssid = NULL;

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (!copy_view_state_snapshot(&snapshot)) {
        return;
    }

    psk = lv_textarea_get_text(s_view.wifi_psk_textarea);

    if (snapshot.wifi_scan_result_count > 0) {
        lv_dropdown_get_selected_str(s_view.wifi_dropdown, selected_ssid, sizeof(selected_ssid));
        ssid = selected_ssid;
    } else if (snapshot.settings.wifi_ssid[0] != '\0') {
        ssid = snapshot.settings.wifi_ssid;
    }

    hide_keyboard();
    (void)wifi_manager_request_connect(ssid, psk);
}

static void wifi_dropdown_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
}

static void wifi_textarea_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *textarea = lv_event_get_target_obj(event);

    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        set_keyboard_target(textarea);
    }
}

static void wifi_keyboard_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        hide_keyboard();
    }
}

static void touch_calibration_start_event_cb(lv_event_t *event)
{
    app_settings_t settings = {0};

    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_view.touch_calibration_overlay == NULL) {
        return;
    }

    if (!copy_view_settings(&settings)) {
        return;
    }

    hide_keyboard();
    s_view.previous_touch_calibration = settings.touch_calibration;
    touch_set_calibration(NULL);
    s_view.touch_calibration_step = 0;
    memset(s_view.touch_calibration_samples, 0, sizeof(s_view.touch_calibration_samples));
    lv_obj_clear_flag(s_view.touch_calibration_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_view.touch_calibration_overlay);
    update_touch_calibration_overlay_locked();
}

static void touch_calibration_overlay_event_cb(lv_event_t *event)
{
    app_settings_t settings = {0};
    lv_indev_t *indev = NULL;
    lv_point_t point = {0};
    app_touch_calibration_t calibration = {0};

    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_view.touch_calibration_step >= TOUCH_CALIBRATION_POINT_COUNT) {
        return;
    }

    indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }

    lv_indev_get_point(indev, &point);
    s_view.touch_calibration_samples[s_view.touch_calibration_step] = convert_display_point_to_touch_space(point);
    s_view.touch_calibration_step++;

    if (s_view.touch_calibration_step < TOUCH_CALIBRATION_POINT_COUNT) {
        update_touch_calibration_overlay_locked();
        return;
    }

    lv_obj_add_flag(s_view.touch_calibration_overlay, LV_OBJ_FLAG_HIDDEN);

    if (!copy_view_settings(&settings)) {
        return;
    }

    if (solve_touch_calibration(s_view.touch_calibration_samples, &calibration)) {
        settings.touch_calibration = calibration;
        app_state_set_settings(s_view.state, &settings);
        touch_set_calibration(&calibration);
        (void)app_settings_save_touch_calibration(&calibration);
        if (s_view.touch_calibration_label != NULL) {
            lv_label_set_text(s_view.touch_calibration_label, "Touch calibration saved");
        }
        app_state_set_wifi_status(s_view.state, APP_WIFI_STATUS_IDLE, "Touch calibration saved");
    } else {
        settings.touch_calibration = s_view.previous_touch_calibration;
        app_state_set_settings(s_view.state, &settings);
        touch_set_calibration(&s_view.previous_touch_calibration);
        if (s_view.touch_calibration_label != NULL) {
            lv_label_set_text(
                s_view.touch_calibration_label,
                s_view.previous_touch_calibration.valid ? "Touch calibration saved" : "Touch uses default mapping"
            );
        }
        app_state_set_wifi_status(s_view.state, APP_WIFI_STATUS_FAILED, "Touch calibration failed");
    }
}

static void sync_tile_locked(const app_state_t *state)
{
    if (state->startup_stage != APP_STARTUP_STAGE_COMPLETE) {
        return;
    }

    if (state->active_screen >= APP_SCREEN_COUNT) {
        return;
    }

    if (lv_tileview_get_tile_active(s_view.tileview) != s_view.tiles[state->active_screen]) {
        lv_tileview_set_tile_by_index(s_view.tileview, state->active_screen, 0, LV_ANIM_OFF);
    }
}

static void update_primary_preview_locked(uint8_t index, const app_tariff_preview_t *preview)
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
        lv_label_set_text(s_view.primary_preview_time_labels[index], time_text);
        lv_label_set_text(s_view.primary_preview_band_labels[index], numeric_text);
        lv_label_set_text(s_view.primary_preview_price_labels[index], "");
        style_preview_card(
            s_view.primary_preview_cards[index],
            s_view.primary_preview_time_labels[index],
            s_view.primary_preview_band_labels[index],
            s_view.primary_preview_price_labels[index],
            preview->band,
            true
        );
    } else {
        lv_label_set_text(s_view.primary_preview_time_labels[index], "Later");
        lv_label_set_text(s_view.primary_preview_band_labels[index], "--");
        lv_label_set_text(s_view.primary_preview_price_labels[index], "");
        style_preview_card(
            s_view.primary_preview_cards[index],
            s_view.primary_preview_time_labels[index],
            s_view.primary_preview_band_labels[index],
            s_view.primary_preview_price_labels[index],
            TARIFF_BAND_NORMAL,
            false
        );
    }
}

static void update_primary_tile_locked(const app_state_t *state)
{
    primary_palette_t palette = get_primary_palette(state);
    char clock_text[12] = {0};

    lv_obj_set_style_bg_color(s_view.tiles[APP_SCREEN_PRIMARY], palette.tile_bg, 0);

    format_clock_label(clock_text, sizeof(clock_text), state->local_time_text);

    if (s_view.primary_clock_label != NULL) {
        lv_label_set_text(s_view.primary_clock_label, clock_text);
    }

    if (s_view.primary_title_label != NULL) {
        lv_label_set_text(s_view.primary_title_label, "Current Price");
    }

    if (s_view.primary_wifi_label != NULL) {
        lv_label_set_text(s_view.primary_wifi_label, state->wifi_status == APP_WIFI_STATUS_CONNECTED ? LV_SYMBOL_WIFI : "");
    }

    if (s_view.primary_hero_card != NULL) {
        lv_obj_set_style_bg_color(s_view.primary_hero_card, palette.hero_bg, 0);
        lv_obj_set_style_shadow_color(s_view.primary_hero_card, lv_color_black(), 0);
        lv_obj_set_style_shadow_width(s_view.primary_hero_card, 16, 0);
        lv_obj_set_style_shadow_opa(s_view.primary_hero_card, LV_OPA_20, 0);
    }

    if (s_view.primary_band_label != NULL) {
        lv_obj_set_style_text_color(s_view.primary_band_label, palette.hero_text, 0);
        lv_obj_set_style_text_align(s_view.primary_band_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (s_view.primary_pulse_dot != NULL) {
        lv_obj_set_style_bg_color(s_view.primary_pulse_dot, palette.chip_bg, 0);
        lv_obj_set_style_outline_color(s_view.primary_pulse_dot, palette.chip_bg, 0);
    }

    if (s_view.primary_pulse_icon_label != NULL) {
        lv_obj_set_style_text_color(s_view.primary_pulse_icon_label, palette.hero_bg, 0);
    }

    if (s_view.primary_price_label != NULL) {
        lv_obj_set_style_text_color(s_view.primary_price_label, palette.hero_text, 0);
        lv_obj_set_style_text_align(s_view.primary_price_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (s_view.primary_price_unit_label != NULL) {
        lv_obj_set_style_text_color(s_view.primary_price_unit_label, palette.hero_muted_text, 0);
        lv_obj_set_style_text_align(s_view.primary_price_unit_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (s_view.primary_remaining_label != NULL) {
        lv_obj_set_style_text_color(s_view.primary_remaining_label, palette.hero_text, 0);
        lv_obj_set_style_text_align(s_view.primary_remaining_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (s_view.primary_change_label != NULL) {
        lv_obj_set_style_text_color(s_view.primary_change_label, palette.hero_muted_text, 0);
        lv_obj_set_style_text_align(s_view.primary_change_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (s_view.primary_section_label != NULL) {
        lv_obj_set_style_text_color(s_view.primary_section_label, lv_color_hex(0xe5e7eb), 0);
    }

    if (state->tariff_has_data && state->tariff_current_block_valid) {
        char price_text[24] = {0};
        char remaining_text[32] = {0};
        char until_text[24] = {0};
        time_t now_local = time(NULL);

        lv_label_set_text(s_view.primary_band_label, tariff_model_get_band_name(state->tariff_current_band));
        lv_obj_set_style_text_font(s_view.primary_price_label, &lv_font_montserrat_28_numeric, 0);
        snprintf(price_text, sizeof(price_text), "%.1f", (double)state->tariff_current_price);
        lv_label_set_text(s_view.primary_price_label, price_text);
        if (s_view.primary_price_unit_label != NULL) {
            lv_label_set_text(s_view.primary_price_unit_label, "p/kWh");
        }
        format_remaining_compact(remaining_text, sizeof(remaining_text), state->tariff_current_block_end_local - now_local);
        lv_label_set_text(s_view.primary_remaining_label, remaining_text);

        format_until_time(until_text, sizeof(until_text), state->tariff_current_block_end_local);
        lv_label_set_text(s_view.primary_change_label, until_text);

        set_primary_pulse_enabled(tariff_band_is_extreme(state->tariff_current_band) && palette.pulse);
    } else {
        lv_label_set_text(s_view.primary_band_label, app_state_get_tariff_status_name(state->tariff_status));
        lv_obj_set_style_text_font(s_view.primary_price_label, &lv_font_montserrat_14, 0);
        lv_label_set_text(s_view.primary_price_label, state->tariff_current_text);
        if (s_view.primary_price_unit_label != NULL) {
            lv_label_set_text(s_view.primary_price_unit_label, "");
        }
        lv_label_set_text(s_view.primary_remaining_label, state->tariff_next_text);
        lv_label_set_text(s_view.primary_change_label, state->tariff_updated_text);
        set_primary_pulse_enabled(false);
    }

    for (uint8_t index = 0; index < APP_TARIFF_PREVIEW_MAX; index++) {
        const app_tariff_preview_t *preview = index < state->tariff_preview_count ? &state->tariff_previews[index] : NULL;
        update_primary_preview_locked(index, preview);
    }
}

static void apply_state_locked(const app_state_t *state)
{
    char clock_text[12] = {0};

    if (s_view.startup_overlay != NULL) {
        if (state->startup_stage == APP_STARTUP_STAGE_COMPLETE) {
            lv_obj_add_flag(s_view.startup_overlay, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_view.startup_overlay, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_view.startup_overlay);
        }
    }

    if (s_view.startup_title_label != NULL) {
        lv_label_set_text(s_view.startup_title_label, "Greenlight");
    }

    if (s_view.startup_status_label != NULL) {
        lv_label_set_text(
            s_view.startup_status_label,
            state->startup_status_text[0] != '\0' ? state->startup_status_text : app_state_get_startup_stage_name(state->startup_stage)
        );
    }

    sync_tile_locked(state);

    update_primary_tile_locked(state);

    lv_obj_set_style_bg_color(s_view.tiles[APP_SCREEN_DETAIL], lv_color_hex(0x0f172a), 0);
    format_clock_label(clock_text, sizeof(clock_text), state->local_time_text);

    if (s_view.detail_clock_label != NULL) {
        lv_label_set_text(s_view.detail_clock_label, clock_text);
    }

    if (s_view.detail_title_label != NULL) {
        lv_label_set_text(s_view.detail_title_label, "Daily Prices");
    }

    if (s_view.detail_wifi_label != NULL) {
        lv_label_set_text(s_view.detail_wifi_label, state->wifi_status == APP_WIFI_STATUS_CONNECTED ? LV_SYMBOL_WIFI : "");
    }

    if (s_view.detail_status_label != NULL) {
        lv_label_set_text(s_view.detail_status_label, state->tariff_status_text);
    }

    update_detail_day_panel_locked(DETAIL_DAY_INDEX_TODAY, "Today", &state->tariff_today, state->tariff_tomorrow.available);
    if (state->tariff_tomorrow.available) {
        update_detail_day_panel_locked(DETAIL_DAY_INDEX_TOMORROW, "Tomorrow", &state->tariff_tomorrow, true);
    } else if (s_view.detail_day_panels[DETAIL_DAY_INDEX_TOMORROW] != NULL) {
        lv_obj_add_flag(s_view.detail_day_panels[DETAIL_DAY_INDEX_TOMORROW], LV_OBJ_FLAG_HIDDEN);
    }
    update_detail_time_marker_locked(state);

    if (s_view.brightness_label != NULL) {
        lv_label_set_text_fmt(s_view.brightness_label, "%u%%", (unsigned int)state->settings.brightness_percent);
    }

    if (s_view.brightness_bar != NULL) {
        lv_bar_set_value(s_view.brightness_bar, state->settings.brightness_percent, LV_ANIM_OFF);
    }

    if (s_view.settings_clock_label != NULL) {
        lv_label_set_text(s_view.settings_clock_label, clock_text);
    }

    if (s_view.settings_title_label != NULL) {
        lv_label_set_text(s_view.settings_title_label, "Settings");
    }

    if (s_view.settings_wifi_label != NULL) {
        lv_label_set_text(s_view.settings_wifi_label, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(
            s_view.settings_wifi_label,
            state->wifi_status == APP_WIFI_STATUS_CONNECTED ? lv_color_white() : lv_color_hex(0x9ca3af),
            0
        );
    }

    if (s_view.settings_wifi_strike_label != NULL) {
        if (state->wifi_status == APP_WIFI_STATUS_CONNECTED) {
            lv_obj_add_flag(s_view.settings_wifi_strike_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_view.settings_wifi_strike_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    update_region_label_locked(state);

    update_touch_calibration_status_locked(state);

    update_wifi_dropdown_locked(state);

    if (s_view.wifi_status_label != NULL) {
        if (state->wifi_status == APP_WIFI_STATUS_CONNECTED && state->wifi_ip_address[0] != '\0') {
            lv_label_set_text_fmt(
                s_view.wifi_status_label,
                "%s\nSSID %s\nIP %s",
                state->wifi_status_text,
                state->wifi_connected_ssid,
                state->wifi_ip_address
            );
        } else {
            lv_label_set_text(s_view.wifi_status_label, state->wifi_status_text);
        }
    }

    if (s_view.wifi_scan_summary_label != NULL) {
        lv_label_set_text_fmt(s_view.wifi_scan_summary_label, "%u networks nearby", (unsigned int)state->wifi_scan_result_count);
    }

    if (s_view.time_status_label != NULL) {
        lv_label_set_text(s_view.time_status_label, state->time_status_text);
    }

    if (s_view.local_time_label != NULL) {
        lv_label_set_text_fmt(s_view.local_time_label, "London %s", state->local_time_text);
    }
}

static void create_primary_tile(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tile, 10, 0);
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 6, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    s_view.primary_top_bar = lv_obj_create(tile);
    lv_obj_set_width(s_view.primary_top_bar, lv_pct(100));
    lv_obj_set_height(s_view.primary_top_bar, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_view.primary_top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_view.primary_top_bar, 0, 0);
    lv_obj_set_style_pad_all(s_view.primary_top_bar, 0, 0);
    lv_obj_set_style_pad_column(s_view.primary_top_bar, 8, 0);
    lv_obj_set_layout(s_view.primary_top_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_view.primary_top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(s_view.primary_top_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_view.primary_clock_label = lv_label_create(s_view.primary_top_bar);
    lv_obj_set_width(s_view.primary_clock_label, 52);
    lv_obj_set_style_text_color(s_view.primary_clock_label, lv_color_white(), 0);

    s_view.primary_title_label = lv_label_create(s_view.primary_top_bar);
    lv_obj_set_flex_grow(s_view.primary_title_label, 1);
    lv_obj_set_style_text_align(s_view.primary_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_view.primary_title_label, lv_color_white(), 0);

    s_view.primary_wifi_label = lv_label_create(s_view.primary_top_bar);
    lv_obj_set_width(s_view.primary_wifi_label, 52);
    lv_obj_set_style_text_align(s_view.primary_wifi_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_view.primary_wifi_label, lv_color_white(), 0);

    s_view.primary_hero_card = lv_obj_create(tile);
    lv_obj_set_width(s_view.primary_hero_card, lv_pct(100));
    lv_obj_set_height(s_view.primary_hero_card, 100);
    lv_obj_set_style_radius(s_view.primary_hero_card, 16, 0);
    lv_obj_set_style_border_width(s_view.primary_hero_card, 0, 0);
    lv_obj_set_style_pad_all(s_view.primary_hero_card, 8, 0);
    lv_obj_set_style_pad_column(s_view.primary_hero_card, 8, 0);
    lv_obj_set_layout(s_view.primary_hero_card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_view.primary_hero_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_view.primary_hero_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_view.primary_hero_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hero_left_col = lv_obj_create(s_view.primary_hero_card);
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

    s_view.primary_price_label = lv_label_create(hero_left_col);
    lv_obj_set_width(s_view.primary_price_label, lv_pct(100));
    lv_label_set_long_mode(s_view.primary_price_label, LV_LABEL_LONG_WRAP);

    s_view.primary_price_unit_label = lv_label_create(hero_left_col);
    lv_obj_set_width(s_view.primary_price_unit_label, lv_pct(100));

    lv_obj_t *hero_center_col = lv_obj_create(s_view.primary_hero_card);
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

    s_view.primary_band_label = lv_label_create(hero_center_col);
    lv_obj_set_width(s_view.primary_band_label, lv_pct(100));
    lv_label_set_long_mode(s_view.primary_band_label, LV_LABEL_LONG_WRAP);

    s_view.primary_pulse_dot = lv_obj_create(hero_center_col);
    lv_obj_set_size(s_view.primary_pulse_dot, 48, 48);
    lv_obj_set_style_radius(s_view.primary_pulse_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_view.primary_pulse_dot, 0, 0);
    lv_obj_set_style_outline_width(s_view.primary_pulse_dot, 4, 0);
    lv_obj_remove_flag(s_view.primary_pulse_dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_view.primary_pulse_icon_label = lv_label_create(s_view.primary_pulse_dot);
    lv_label_set_text(s_view.primary_pulse_icon_label, LV_SYMBOL_OK);
    lv_obj_center(s_view.primary_pulse_icon_label);

    lv_obj_t *hero_right_col = lv_obj_create(s_view.primary_hero_card);
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

    s_view.primary_change_label = lv_label_create(hero_right_col);
    lv_obj_set_width(s_view.primary_change_label, lv_pct(100));
    lv_label_set_long_mode(s_view.primary_change_label, LV_LABEL_LONG_WRAP);

    s_view.primary_remaining_label = lv_label_create(hero_right_col);
    lv_obj_set_width(s_view.primary_remaining_label, lv_pct(100));
    lv_label_set_long_mode(s_view.primary_remaining_label, LV_LABEL_LONG_WRAP);

    s_view.primary_section_label = lv_label_create(tile);
    lv_label_set_text(s_view.primary_section_label, "Next periods");

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
        s_view.primary_preview_cards[index] = lv_obj_create(preview_row);
        lv_obj_set_height(s_view.primary_preview_cards[index], lv_pct(100));
        lv_obj_set_flex_grow(s_view.primary_preview_cards[index], 1);
        lv_obj_set_style_radius(s_view.primary_preview_cards[index], 12, 0);
        lv_obj_set_style_border_width(s_view.primary_preview_cards[index], 0, 0);
        lv_obj_set_style_pad_all(s_view.primary_preview_cards[index], 5, 0);
        lv_obj_set_style_pad_row(s_view.primary_preview_cards[index], 0, 0);
        lv_obj_set_layout(s_view.primary_preview_cards[index], LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(s_view.primary_preview_cards[index], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(s_view.primary_preview_cards[index], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(s_view.primary_preview_cards[index], LV_OBJ_FLAG_SCROLLABLE);

        s_view.primary_preview_time_labels[index] = lv_label_create(s_view.primary_preview_cards[index]);
        lv_obj_set_width(s_view.primary_preview_time_labels[index], lv_pct(100));
        s_view.primary_preview_band_labels[index] = lv_label_create(s_view.primary_preview_cards[index]);
        lv_obj_set_width(s_view.primary_preview_band_labels[index], lv_pct(100));
        s_view.primary_preview_price_labels[index] = lv_label_create(s_view.primary_preview_cards[index]);
        lv_obj_set_width(s_view.primary_preview_price_labels[index], lv_pct(100));
    }

}

static void create_detail_tile(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tile, 10, 0);
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 6, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    s_view.detail_top_bar = lv_obj_create(tile);
    lv_obj_set_width(s_view.detail_top_bar, lv_pct(100));
    lv_obj_set_height(s_view.detail_top_bar, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_view.detail_top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_view.detail_top_bar, 0, 0);
    lv_obj_set_style_pad_all(s_view.detail_top_bar, 0, 0);
    lv_obj_set_style_pad_column(s_view.detail_top_bar, 8, 0);
    lv_obj_set_layout(s_view.detail_top_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_view.detail_top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(s_view.detail_top_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_view.detail_clock_label = lv_label_create(s_view.detail_top_bar);
    lv_obj_set_width(s_view.detail_clock_label, 52);
    lv_obj_set_style_text_color(s_view.detail_clock_label, lv_color_white(), 0);

    s_view.detail_title_label = lv_label_create(s_view.detail_top_bar);
    lv_obj_set_flex_grow(s_view.detail_title_label, 1);
    lv_obj_set_style_text_align(s_view.detail_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_view.detail_title_label, lv_color_white(), 0);

    s_view.detail_wifi_label = lv_label_create(s_view.detail_top_bar);
    lv_obj_set_width(s_view.detail_wifi_label, 52);
    lv_obj_set_style_text_align(s_view.detail_wifi_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_view.detail_wifi_label, lv_color_white(), 0);

    s_view.detail_status_label = lv_label_create(tile);
    lv_obj_set_width(s_view.detail_status_label, lv_pct(100));
    lv_obj_set_style_text_align(s_view.detail_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_view.detail_status_label, lv_color_hex(0x64748b), 0);
    lv_label_set_long_mode(s_view.detail_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(s_view.detail_status_label, LV_OBJ_FLAG_HIDDEN);

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
        s_view.detail_day_panels[day_index] = panel;

        s_view.detail_day_titles[day_index] = lv_label_create(panel);
        lv_obj_set_style_text_color(s_view.detail_day_titles[day_index], lv_color_white(), 0);
        lv_obj_set_style_text_font(s_view.detail_day_titles[day_index], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(s_view.detail_day_titles[day_index], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(s_view.detail_day_titles[day_index], lv_pct(100));

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

        s_view.detail_day_bar_rows[day_index] = lv_obj_create(chart_shell);
        lv_obj_set_width(s_view.detail_day_bar_rows[day_index], lv_pct(100));
        lv_obj_set_height(s_view.detail_day_bar_rows[day_index], 70);
        lv_obj_set_style_bg_opa(s_view.detail_day_bar_rows[day_index], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_view.detail_day_bar_rows[day_index], 0, 0);
        lv_obj_set_style_pad_top(s_view.detail_day_bar_rows[day_index], 0, 0);
        lv_obj_set_style_pad_bottom(s_view.detail_day_bar_rows[day_index], 0, 0);
        lv_obj_set_style_pad_left(s_view.detail_day_bar_rows[day_index], 0, 0);
        lv_obj_set_style_pad_right(s_view.detail_day_bar_rows[day_index], 0, 0);
        lv_obj_set_style_pad_column(s_view.detail_day_bar_rows[day_index], 0, 0);
        lv_obj_set_style_border_side(s_view.detail_day_bar_rows[day_index], LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(s_view.detail_day_bar_rows[day_index], lv_color_hex(0x334155), 0);
        lv_obj_set_style_border_width(s_view.detail_day_bar_rows[day_index], 1, 0);
        lv_obj_set_layout(s_view.detail_day_bar_rows[day_index], LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(s_view.detail_day_bar_rows[day_index], LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(s_view.detail_day_bar_rows[day_index], LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lv_obj_clear_flag(s_view.detail_day_bar_rows[day_index], LV_OBJ_FLAG_SCROLLABLE);

        for (uint8_t slot_index = 0; slot_index < APP_TARIFF_DAY_SLOT_MAX; slot_index++) {
            s_view.detail_day_bars[day_index][slot_index] = lv_obj_create(s_view.detail_day_bar_rows[day_index]);
            lv_obj_set_width(s_view.detail_day_bars[day_index][slot_index], 4);
            lv_obj_set_height(s_view.detail_day_bars[day_index][slot_index], 6);
            lv_obj_set_style_radius(s_view.detail_day_bars[day_index][slot_index], 2, 0);
            lv_obj_set_style_bg_color(s_view.detail_day_bars[day_index][slot_index], lv_color_hex(0x374151), 0);
            lv_obj_set_style_bg_opa(s_view.detail_day_bars[day_index][slot_index], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(s_view.detail_day_bars[day_index][slot_index], 0, 0);
            lv_obj_clear_flag(s_view.detail_day_bars[day_index][slot_index], LV_OBJ_FLAG_SCROLLABLE);
        }

        s_view.detail_day_time_markers[day_index] = lv_obj_create(s_view.detail_day_bar_rows[day_index]);
        lv_obj_set_width(s_view.detail_day_time_markers[day_index], 2);
        lv_obj_set_height(s_view.detail_day_time_markers[day_index], lv_pct(100));
        lv_obj_set_style_radius(s_view.detail_day_time_markers[day_index], 0, 0);
        lv_obj_set_style_bg_color(s_view.detail_day_time_markers[day_index], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(s_view.detail_day_time_markers[day_index], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_view.detail_day_time_markers[day_index], 0, 0);
        lv_obj_add_flag(s_view.detail_day_time_markers[day_index], LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_view.detail_day_time_markers[day_index]);

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

        s_view.detail_day_min_labels[day_index] = lv_label_create(stats_row);
        lv_obj_set_width(s_view.detail_day_min_labels[day_index], 48);
        lv_obj_set_style_text_align(s_view.detail_day_min_labels[day_index], LV_TEXT_ALIGN_LEFT, 0);
        s_view.detail_day_avg_labels[day_index] = lv_label_create(stats_row);
        lv_obj_set_width(s_view.detail_day_avg_labels[day_index], 48);
        lv_obj_set_style_text_align(s_view.detail_day_avg_labels[day_index], LV_TEXT_ALIGN_CENTER, 0);
        s_view.detail_day_max_labels[day_index] = lv_label_create(stats_row);
        lv_obj_set_width(s_view.detail_day_max_labels[day_index], 48);
        lv_obj_set_style_text_align(s_view.detail_day_max_labels[day_index], LV_TEXT_ALIGN_RIGHT, 0);
    }
}

static void create_settings_tile(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x050816), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tile, 10, 0);
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 8, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    s_view.settings_top_bar = lv_obj_create(tile);
    lv_obj_set_width(s_view.settings_top_bar, lv_pct(100));
    lv_obj_set_height(s_view.settings_top_bar, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_view.settings_top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_view.settings_top_bar, 0, 0);
    lv_obj_set_style_pad_all(s_view.settings_top_bar, 0, 0);
    lv_obj_set_style_pad_column(s_view.settings_top_bar, 8, 0);
    lv_obj_set_layout(s_view.settings_top_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_view.settings_top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(s_view.settings_top_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_view.settings_clock_label = lv_label_create(s_view.settings_top_bar);
    lv_obj_set_width(s_view.settings_clock_label, 52);
    lv_obj_set_style_text_color(s_view.settings_clock_label, lv_color_white(), 0);

    s_view.settings_title_label = lv_label_create(s_view.settings_top_bar);
    lv_obj_set_flex_grow(s_view.settings_title_label, 1);
    lv_obj_set_style_text_align(s_view.settings_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_view.settings_title_label, lv_color_white(), 0);

    lv_obj_t *wifi_slot = lv_obj_create(s_view.settings_top_bar);
    lv_obj_set_size(wifi_slot, 52, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wifi_slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_slot, 0, 0);
    lv_obj_set_style_pad_all(wifi_slot, 0, 0);
    lv_obj_remove_flag(wifi_slot, LV_OBJ_FLAG_SCROLLABLE);

    s_view.settings_wifi_label = lv_label_create(wifi_slot);
    lv_label_set_text(s_view.settings_wifi_label, LV_SYMBOL_WIFI);
    lv_obj_align(s_view.settings_wifi_label, LV_ALIGN_RIGHT_MID, 0, 0);

    s_view.settings_wifi_strike_label = lv_label_create(wifi_slot);
    lv_label_set_text(s_view.settings_wifi_strike_label, "/");
    lv_obj_set_style_text_color(s_view.settings_wifi_strike_label, lv_color_hex(0xf87171), 0);
    lv_obj_align(s_view.settings_wifi_strike_label, LV_ALIGN_RIGHT_MID, -2, 0);

    s_view.settings_content = lv_obj_create(tile);
    lv_obj_set_width(s_view.settings_content, lv_pct(100));
    lv_obj_set_flex_grow(s_view.settings_content, 1);
    lv_obj_set_style_bg_opa(s_view.settings_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_view.settings_content, 0, 0);
    lv_obj_set_style_pad_all(s_view.settings_content, 0, 0);
    lv_obj_set_style_pad_row(s_view.settings_content, 8, 0);
    lv_obj_set_layout(s_view.settings_content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_view.settings_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_view.settings_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_view.settings_content, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *brightness_card = create_section_card(s_view.settings_content, lv_color_hex(0x111827));
    lv_obj_set_style_radius(brightness_card, 16, 0);
    lv_obj_set_style_border_width(brightness_card, 1, 0);
    lv_obj_set_style_border_color(brightness_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_pad_all(brightness_card, 14, 0);
    lv_obj_set_style_pad_row(brightness_card, 10, 0);

    lv_obj_t *brightness_title = lv_label_create(brightness_card);
    lv_label_set_text(brightness_title, "Brightness");
    lv_obj_set_style_text_color(brightness_title, lv_color_white(), 0);

    s_view.brightness_label = lv_label_create(brightness_card);
    lv_obj_set_style_text_color(s_view.brightness_label, lv_color_hex(0xfbbf24), 0);
    lv_obj_set_style_text_font(s_view.brightness_label, &lv_font_montserrat_20_numeric, 0);

    lv_obj_t *brightness_row = lv_obj_create(brightness_card);
    lv_obj_set_width(brightness_row, lv_pct(100));
    lv_obj_set_height(brightness_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(brightness_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brightness_row, 0, 0);
    lv_obj_set_style_pad_all(brightness_row, 0, 0);
    lv_obj_set_style_pad_column(brightness_row, 8, 0);
    lv_obj_set_layout(brightness_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(brightness_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brightness_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(brightness_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *minus_button = lv_button_create(brightness_row);
    lv_obj_set_size(minus_button, 40, 36);
    lv_obj_set_style_radius(minus_button, 12, 0);
    lv_obj_set_style_bg_color(minus_button, lv_color_hex(0x1e293b), 0);
    lv_obj_add_event_cb(minus_button, brightness_button_event_cb, LV_EVENT_CLICKED, (void *)-10);
    lv_obj_t *minus_label = lv_label_create(minus_button);
    lv_label_set_text(minus_label, "-");
    lv_obj_set_style_text_color(minus_label, lv_color_white(), 0);
    lv_obj_center(minus_label);

    s_view.brightness_bar = lv_bar_create(brightness_row);
    lv_obj_set_height(s_view.brightness_bar, 12);
    lv_obj_set_flex_grow(s_view.brightness_bar, 1);
    lv_bar_set_range(s_view.brightness_bar, 10, 100);
    lv_obj_set_style_bg_color(s_view.brightness_bar, lv_color_hex(0x374151), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_view.brightness_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_view.brightness_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_view.brightness_bar, lv_color_hex(0xf59e0b), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_view.brightness_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_view.brightness_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    lv_obj_t *plus_button = lv_button_create(brightness_row);
    lv_obj_set_size(plus_button, 40, 36);
    lv_obj_set_style_radius(plus_button, 12, 0);
    lv_obj_set_style_bg_color(plus_button, lv_color_hex(0x1e293b), 0);
    lv_obj_add_event_cb(plus_button, brightness_button_event_cb, LV_EVENT_CLICKED, (void *)10);
    lv_obj_t *plus_label = lv_label_create(plus_button);
    lv_label_set_text(plus_label, "+");
    lv_obj_set_style_text_color(plus_label, lv_color_white(), 0);
    lv_obj_center(plus_label);

    lv_obj_t *region_card = create_section_card(s_view.settings_content, lv_color_hex(0x111827));
    lv_obj_set_style_radius(region_card, 16, 0);
    lv_obj_set_style_border_width(region_card, 1, 0);
    lv_obj_set_style_border_color(region_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_pad_all(region_card, 14, 0);
    lv_obj_set_style_pad_row(region_card, 10, 0);

    lv_obj_t *region_title = lv_label_create(region_card);
    lv_label_set_text(region_title, "Region");
    lv_obj_set_style_text_color(region_title, lv_color_white(), 0);

    s_view.region_label = lv_label_create(region_card);
    lv_obj_set_width(s_view.region_label, lv_pct(100));
    lv_obj_set_style_text_color(s_view.region_label, lv_color_hex(0x93c5fd), 0);
    lv_obj_set_style_text_font(s_view.region_label, &lv_font_montserrat_14, 0);

    lv_obj_t *region_row = lv_obj_create(region_card);
    lv_obj_set_width(region_row, lv_pct(100));
    lv_obj_set_height(region_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(region_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(region_row, 0, 0);
    lv_obj_set_style_pad_all(region_row, 0, 0);
    lv_obj_set_style_pad_column(region_row, 8, 0);
    lv_obj_set_layout(region_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(region_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(region_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(region_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *region_prev_button = create_dark_button(region_row, "Prev");
    lv_obj_set_flex_grow(region_prev_button, 1);
    lv_obj_set_style_bg_color(region_prev_button, lv_color_hex(0x1e293b), 0);
    lv_obj_add_event_cb(region_prev_button, region_button_event_cb, LV_EVENT_CLICKED, (void *)-1);

    lv_obj_t *region_next_button = create_dark_button(region_row, "Next");
    lv_obj_set_flex_grow(region_next_button, 1);
    lv_obj_set_style_bg_color(region_next_button, lv_color_hex(0x1e293b), 0);
    lv_obj_add_event_cb(region_next_button, region_button_event_cb, LV_EVENT_CLICKED, (void *)1);

    lv_obj_t *wifi_card = create_section_card(s_view.settings_content, lv_color_hex(0x111827));
    lv_obj_set_style_radius(wifi_card, 16, 0);
    lv_obj_set_style_border_width(wifi_card, 1, 0);
    lv_obj_set_style_border_color(wifi_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_pad_all(wifi_card, 14, 0);
    lv_obj_set_style_pad_row(wifi_card, 10, 0);

    lv_obj_t *wifi_title = lv_label_create(wifi_card);
    lv_label_set_text(wifi_title, "Wi-Fi");
    lv_obj_set_style_text_color(wifi_title, lv_color_white(), 0);

    s_view.wifi_status_label = lv_label_create(wifi_card);
    lv_obj_set_width(s_view.wifi_status_label, lv_pct(100));
    lv_label_set_long_mode(s_view.wifi_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_view.wifi_status_label, lv_color_hex(0xe5e7eb), 0);

    s_view.wifi_scan_summary_label = lv_label_create(wifi_card);
    lv_obj_set_style_text_color(s_view.wifi_scan_summary_label, lv_color_hex(0x9ca3af), 0);

    s_view.wifi_dropdown = lv_dropdown_create(wifi_card);
    lv_obj_set_width(s_view.wifi_dropdown, lv_pct(100));
    lv_dropdown_set_options(s_view.wifi_dropdown, "Scan for Wi-Fi");
    lv_obj_set_style_bg_color(s_view.wifi_dropdown, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_text_color(s_view.wifi_dropdown, lv_color_white(), 0);
    lv_obj_set_style_border_color(s_view.wifi_dropdown, lv_color_hex(0x334155), 0);
    lv_obj_add_event_cb(s_view.wifi_dropdown, wifi_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_view.wifi_psk_textarea = lv_textarea_create(wifi_card);
    lv_obj_set_width(s_view.wifi_psk_textarea, lv_pct(100));
    lv_textarea_set_one_line(s_view.wifi_psk_textarea, true);
    lv_textarea_set_password_mode(s_view.wifi_psk_textarea, true);
    lv_textarea_set_placeholder_text(s_view.wifi_psk_textarea, "Wi-Fi password");
    lv_obj_set_style_bg_color(s_view.wifi_psk_textarea, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_text_color(s_view.wifi_psk_textarea, lv_color_white(), 0);
    lv_obj_set_style_text_color(s_view.wifi_psk_textarea, lv_color_hex(0x64748b), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(s_view.wifi_psk_textarea, lv_color_hex(0x334155), 0);
    lv_obj_add_event_cb(s_view.wifi_psk_textarea, wifi_textarea_event_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_view.wifi_psk_textarea, wifi_textarea_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *button_row = lv_obj_create(wifi_card);
    lv_obj_set_width(button_row, lv_pct(100));
    lv_obj_set_height(button_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(button_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button_row, 0, 0);
    lv_obj_set_style_pad_all(button_row, 0, 0);
    lv_obj_set_style_pad_column(button_row, 10, 0);
    lv_obj_set_layout(button_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(button_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *scan_button = create_dark_button(button_row, "Scan");
    lv_obj_set_flex_grow(scan_button, 1);
    lv_obj_set_style_bg_color(scan_button, lv_color_hex(0x1d4ed8), 0);
    lv_obj_add_event_cb(scan_button, wifi_scan_button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *connect_button = create_dark_button(button_row, "Join");
    lv_obj_set_flex_grow(connect_button, 1);
    lv_obj_set_style_bg_color(connect_button, lv_color_hex(0x047857), 0);
    lv_obj_add_event_cb(connect_button, wifi_connect_button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *touch_card = create_section_card(s_view.settings_content, lv_color_hex(0x111827));
    lv_obj_set_style_radius(touch_card, 16, 0);
    lv_obj_set_style_border_width(touch_card, 1, 0);
    lv_obj_set_style_border_color(touch_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_pad_all(touch_card, 14, 0);
    lv_obj_set_style_pad_row(touch_card, 10, 0);

    lv_obj_t *touch_title = lv_label_create(touch_card);
    lv_label_set_text(touch_title, "Touch Calibration");
    lv_obj_set_style_text_color(touch_title, lv_color_white(), 0);

    s_view.touch_calibration_label = lv_label_create(touch_card);
    lv_obj_set_style_text_color(s_view.touch_calibration_label, lv_color_hex(0x9ca3af), 0);
    lv_obj_set_width(s_view.touch_calibration_label, lv_pct(100));
    lv_label_set_long_mode(s_view.touch_calibration_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *calibrate_button = create_dark_button(touch_card, "Calibrate touch");
    lv_obj_set_width(calibrate_button, lv_pct(100));
    lv_obj_set_style_bg_color(calibrate_button, lv_color_hex(0x1e293b), 0);
    lv_obj_add_event_cb(calibrate_button, touch_calibration_start_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *time_card = create_section_card(s_view.settings_content, lv_color_hex(0x111827));
    lv_obj_set_style_radius(time_card, 16, 0);
    lv_obj_set_style_border_width(time_card, 1, 0);
    lv_obj_set_style_border_color(time_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_pad_all(time_card, 14, 0);
    lv_obj_set_style_pad_row(time_card, 10, 0);

    lv_obj_t *time_title = lv_label_create(time_card);
    lv_label_set_text(time_title, "Time Sync");
    lv_obj_set_style_text_color(time_title, lv_color_white(), 0);

    s_view.time_status_label = lv_label_create(time_card);
    lv_obj_set_width(s_view.time_status_label, lv_pct(100));
    lv_label_set_long_mode(s_view.time_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_view.time_status_label, lv_color_hex(0xe5e7eb), 0);

    s_view.local_time_label = lv_label_create(time_card);
    lv_obj_set_style_text_color(s_view.local_time_label, lv_color_hex(0x9ca3af), 0);

    s_view.wifi_keyboard = lv_keyboard_create(tile);
    lv_obj_set_width(s_view.wifi_keyboard, lv_pct(100));
    lv_obj_set_height(s_view.wifi_keyboard, 150);
    lv_obj_set_style_radius(s_view.wifi_keyboard, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_view.wifi_keyboard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_view.wifi_keyboard, lv_color_hex(0x020617), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_view.wifi_keyboard, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_view.wifi_keyboard, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_view.wifi_keyboard, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(s_view.wifi_keyboard, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(s_view.wifi_keyboard, 10, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_view.wifi_keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_view.wifi_keyboard, lv_color_hex(0x1e293b), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_view.wifi_keyboard, lv_color_hex(0x334155), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(s_view.wifi_keyboard, lv_color_hex(0x2563eb), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s_view.wifi_keyboard, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_view.wifi_keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(s_view.wifi_keyboard, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_view.wifi_keyboard, wifi_keyboard_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(s_view.wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
}

esp_err_t ui_router_init(app_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    app_state_get_snapshot(state, &s_view.state_snapshot);

    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    s_view = (ui_router_view_t){
        .state = state,
    };

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x081018), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    s_view.tileview = lv_tileview_create(screen);
    lv_obj_set_size(s_view.tileview, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_view.tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_view.tileview, 0, 0);
    lv_obj_add_event_cb(s_view.tileview, tileview_event_cb, LV_EVENT_SCROLL_END, NULL);

    s_view.tiles[APP_SCREEN_PRIMARY] = lv_tileview_add_tile(s_view.tileview, 0, 0, LV_DIR_HOR);
    s_view.tiles[APP_SCREEN_DETAIL] = lv_tileview_add_tile(s_view.tileview, 1, 0, LV_DIR_HOR);
    s_view.tiles[APP_SCREEN_SETTINGS] = lv_tileview_add_tile(s_view.tileview, 2, 0, LV_DIR_LEFT);

    create_primary_tile(s_view.tiles[APP_SCREEN_PRIMARY]);
    create_detail_tile(s_view.tiles[APP_SCREEN_DETAIL]);
    create_settings_tile(s_view.tiles[APP_SCREEN_SETTINGS]);

    s_view.touch_calibration_overlay = lv_obj_create(screen);
    lv_obj_set_size(s_view.touch_calibration_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_view.touch_calibration_overlay, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(s_view.touch_calibration_overlay, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_view.touch_calibration_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_view.touch_calibration_overlay, 0, 0);
    lv_obj_add_event_cb(s_view.touch_calibration_overlay, touch_calibration_overlay_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_view.touch_calibration_overlay, LV_OBJ_FLAG_HIDDEN);

    s_view.touch_calibration_prompt = lv_label_create(s_view.touch_calibration_overlay);
    lv_obj_set_width(s_view.touch_calibration_prompt, lv_pct(100));
    lv_obj_set_style_text_align(s_view.touch_calibration_prompt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_view.touch_calibration_prompt, lv_color_white(), 0);
    lv_obj_align(s_view.touch_calibration_prompt, LV_ALIGN_TOP_MID, 0, 12);

    s_view.touch_calibration_target = lv_obj_create(s_view.touch_calibration_overlay);
    lv_obj_set_size(s_view.touch_calibration_target, TOUCH_CALIBRATION_TARGET_SIZE, TOUCH_CALIBRATION_TARGET_SIZE);
    lv_obj_set_style_radius(s_view.touch_calibration_target, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_view.touch_calibration_target, lv_color_hex(0xf59e0b), 0);
    lv_obj_set_style_bg_opa(s_view.touch_calibration_target, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_view.touch_calibration_target, 3, 0);
    lv_obj_set_style_border_color(s_view.touch_calibration_target, lv_color_white(), 0);
    lv_obj_remove_flag(s_view.touch_calibration_target, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_view.startup_overlay = lv_obj_create(screen);
    lv_obj_set_size(s_view.startup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_view.startup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_view.startup_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_view.startup_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_view.startup_overlay, 24, 0);
    lv_obj_set_style_pad_row(s_view.startup_overlay, 10, 0);
    lv_obj_set_flex_flow(s_view.startup_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_view.startup_overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_layout(s_view.startup_overlay, LV_LAYOUT_FLEX);
    lv_obj_clear_flag(s_view.startup_overlay, LV_OBJ_FLAG_SCROLLABLE);

    s_view.startup_logo_image = lv_image_create(s_view.startup_overlay);
    lv_image_set_src(s_view.startup_logo_image, &splash_logo);

    s_view.startup_title_label = lv_label_create(s_view.startup_overlay);
    lv_obj_set_style_text_color(s_view.startup_title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_view.startup_title_label, &lv_font_montserrat_28_numeric, 0);
    lv_obj_set_style_text_align(s_view.startup_title_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *startup_rule = lv_obj_create(s_view.startup_overlay);
    lv_obj_set_size(startup_rule, 90, 4);
    lv_obj_set_style_radius(startup_rule, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(startup_rule, lv_color_hex(0x1d4ed8), 0);
    lv_obj_set_style_bg_opa(startup_rule, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(startup_rule, 0, 0);
    lv_obj_remove_flag(startup_rule, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_view.startup_status_label = lv_label_create(s_view.startup_overlay);
    lv_obj_set_width(s_view.startup_status_label, lv_pct(100));
    lv_obj_set_height(s_view.startup_status_label, 40);
    lv_obj_set_style_text_color(s_view.startup_status_label, lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_text_align(s_view.startup_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_view.startup_status_label, LV_LABEL_LONG_WRAP);

    lv_tileview_set_tile_by_index(s_view.tileview, 0, 0, LV_ANIM_OFF);
    apply_state_locked(&s_view.state_snapshot);

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_router_update(const app_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    app_state_get_snapshot(state, &s_view.state_snapshot);

    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    apply_state_locked(&s_view.state_snapshot);

    lvgl_port_unlock();
    return ESP_OK;
}