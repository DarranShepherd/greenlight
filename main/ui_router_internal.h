#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <esp_err.h>
#include <esp_lvgl_port.h>

#include "app_settings.h"
#include "app_state.h"

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
    lv_obj_t *primary_wifi_strike;
    lv_obj_t *primary_hero_card;
    lv_obj_t *primary_band_chip;
    lv_obj_t *primary_band_label;
    lv_obj_t *primary_hero_content_row;
    lv_obj_t *primary_hero_center_col;
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
    lv_obj_t *detail_wifi_strike;
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
    lv_obj_t *firmware_version_label;
    lv_obj_t *firmware_available_label;
    lv_obj_t *firmware_status_label;
    lv_obj_t *firmware_update_button;
    lv_obj_t *firmware_update_button_label;
    lv_obj_t *wifi_dropdown;
    lv_obj_t *wifi_psk_textarea;
    lv_obj_t *wifi_keyboard;
    lv_obj_t *wifi_scan_summary_label;
    lv_obj_t *touch_calibration_label;
    lv_obj_t *touch_calibration_overlay;
    lv_obj_t *touch_calibration_prompt;
    lv_obj_t *touch_calibration_target;
    lv_point_t touch_calibration_samples[3];
    app_touch_calibration_t previous_touch_calibration;
    uint8_t touch_calibration_step;
    char wifi_dropdown_cache[APP_WIFI_SCAN_MAX_RESULTS * (APP_SETTINGS_WIFI_SSID_MAX_LEN + 1) + 32];
    app_state_t *state;
    app_state_t state_snapshot;
    app_screen_t last_active_screen;
} ui_router_view_t;

uint8_t ui_router_clamp_brightness_value(int32_t brightness_percent);
bool ui_router_copy_state_snapshot(const ui_router_view_t *view, app_state_t *snapshot);
bool ui_router_copy_settings(const ui_router_view_t *view, app_settings_t *settings);
void ui_router_apply_brightness(ui_router_view_t *view, uint8_t brightness_percent);
void ui_router_format_clock_label(char *buffer, size_t buffer_size, const char *local_time_text);
lv_obj_t *ui_router_create_section_card(lv_obj_t *parent, lv_color_t bg_color);
lv_obj_t *ui_router_create_dark_button(lv_obj_t *parent, const char *label_text);
void ui_router_create_wifi_status(lv_obj_t *parent, lv_obj_t **wifi_label, lv_obj_t **wifi_strike);
void ui_router_update_wifi_status(
    lv_obj_t *wifi_label,
    lv_obj_t *wifi_strike,
    app_wifi_status_t wifi_status,
    lv_color_t connected_color,
    lv_color_t disconnected_color
);
void ui_router_set_keyboard_target(ui_router_view_t *view, lv_obj_t *textarea);
void ui_router_hide_keyboard(ui_router_view_t *view);

lv_color_t ui_primary_get_band_fill_color(tariff_band_t band);
void ui_primary_create(lv_obj_t *tile, ui_router_view_t *view);
void ui_primary_update(const app_state_t *state, ui_router_view_t *view);

void ui_detail_create(lv_obj_t *tile, ui_router_view_t *view);
void ui_detail_update(const app_state_t *state, ui_router_view_t *view);

void ui_settings_create(lv_obj_t *screen, lv_obj_t *tile, ui_router_view_t *view);
void ui_settings_update(const app_state_t *state, ui_router_view_t *view);