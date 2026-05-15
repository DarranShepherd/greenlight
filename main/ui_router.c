#include "ui_router.h"

#include <stdbool.h>
#include <string.h>

#include <esp_lvgl_port.h>

#include "lcd.h"
#include "numeric_fonts.h"
#include "ota_manager.h"
#include "splash_logo.h"
#include "ui_router_internal.h"

static ui_router_view_t s_view;

uint8_t ui_router_clamp_brightness_value(int32_t brightness_percent)
{
    if (brightness_percent < APP_SETTINGS_MIN_BRIGHTNESS_PERCENT) {
        return APP_SETTINGS_MIN_BRIGHTNESS_PERCENT;
    }

    if (brightness_percent > 100) {
        return 100;
    }

    return (uint8_t)brightness_percent;
}

bool ui_router_copy_state_snapshot(const ui_router_view_t *view, app_state_t *snapshot)
{
    if (view == NULL || view->state == NULL || snapshot == NULL) {
        return false;
    }

    app_state_get_snapshot(view->state, snapshot);
    return true;
}

bool ui_router_copy_settings(const ui_router_view_t *view, app_settings_t *settings)
{
    if (view == NULL || view->state == NULL || settings == NULL) {
        return false;
    }

    app_state_get_settings(view->state, settings);
    return true;
}

void ui_router_apply_brightness(ui_router_view_t *view, uint8_t brightness_percent)
{
    if (view == NULL || view->state == NULL) {
        return;
    }

    app_state_set_brightness(view->state, brightness_percent);
    (void)lcd_set_brightness(brightness_percent);
    (void)app_settings_save_brightness(brightness_percent);

    if (view->brightness_label != NULL) {
        lv_label_set_text_fmt(view->brightness_label, "%u%%", (unsigned int)brightness_percent);
    }

    if (view->brightness_bar != NULL) {
        lv_bar_set_value(view->brightness_bar, brightness_percent, LV_ANIM_OFF);
    }
}

void ui_router_format_clock_label(char *buffer, size_t buffer_size, const char *local_time_text)
{
    size_t source_length = 0;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (local_time_text == NULL || local_time_text[0] == '\0') {
        buffer[0] = '\0';
        return;
    }

    source_length = strlen(local_time_text);
    if (source_length >= 5 && local_time_text[source_length - 3] == ':') {
        strlcpy(buffer, &local_time_text[source_length - 5], buffer_size);
        return;
    }

    strlcpy(buffer, local_time_text, buffer_size);
}

void ui_router_create_wifi_status(lv_obj_t *parent, lv_obj_t **wifi_label, lv_obj_t **wifi_strike)
{
    lv_obj_t *wifi_slot = NULL;

    if (parent == NULL || wifi_label == NULL || wifi_strike == NULL) {
        return;
    }

    wifi_slot = lv_obj_create(parent);
    lv_obj_set_size(wifi_slot, 52, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wifi_slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_slot, 0, 0);
    lv_obj_set_style_pad_all(wifi_slot, 0, 0);
    lv_obj_remove_flag(wifi_slot, LV_OBJ_FLAG_SCROLLABLE);

    *wifi_label = lv_label_create(wifi_slot);
    lv_label_set_text(*wifi_label, LV_SYMBOL_WIFI);
    lv_obj_set_width(*wifi_label, 24);
    lv_obj_set_style_text_align(*wifi_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(*wifi_label, LV_ALIGN_RIGHT_MID, 0, 0);

    *wifi_strike = lv_obj_create(wifi_slot);
    lv_obj_set_size(*wifi_strike, 24, 24);
    lv_obj_set_style_bg_opa(*wifi_strike, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(*wifi_strike, 0, 0);
    lv_obj_set_style_pad_all(*wifi_strike, 0, 0);
    lv_obj_set_style_transform_pivot_x(*wifi_strike, 12, 0);
    lv_obj_set_style_transform_pivot_y(*wifi_strike, 12, 0);
    lv_obj_set_style_transform_rotation(*wifi_strike, 150, 0);
    lv_obj_remove_flag(*wifi_strike, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *strike_back = lv_label_create(*wifi_strike);
    lv_label_set_text(strike_back, "/");
    lv_obj_set_style_text_color(strike_back, lv_color_hex(0xdc2626), 0);
    lv_obj_align(strike_back, LV_ALIGN_CENTER, -1, 1);

    lv_obj_t *strike_middle = lv_label_create(*wifi_strike);
    lv_label_set_text(strike_middle, "/");
    lv_obj_set_style_text_color(strike_middle, lv_color_hex(0xdc2626), 0);
    lv_obj_align(strike_middle, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *strike_front = lv_label_create(*wifi_strike);
    lv_label_set_text(strike_front, "/");
    lv_obj_set_style_text_color(strike_front, lv_color_hex(0xdc2626), 0);
    lv_obj_align(strike_front, LV_ALIGN_CENTER, 1, -1);

    lv_obj_align(*wifi_strike, LV_ALIGN_RIGHT_MID, -1, 1);
    lv_obj_move_foreground(*wifi_strike);
}

void ui_router_update_wifi_status(lv_obj_t *wifi_label, lv_obj_t *wifi_strike, app_wifi_status_t wifi_status, lv_color_t connected_color, lv_color_t disconnected_color)
{
    if (wifi_label != NULL) {
        lv_label_set_text(wifi_label, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(
            wifi_label,
            wifi_status == APP_WIFI_STATUS_CONNECTED ? connected_color : disconnected_color,
            0
        );
    }

    if (wifi_strike != NULL) {
        lv_obj_add_flag(wifi_strike, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *ui_router_create_section_card(lv_obj_t *parent, lv_color_t bg_color)
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

lv_obj_t *ui_router_create_dark_button(lv_obj_t *parent, const char *label_text)
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

void ui_router_set_keyboard_target(ui_router_view_t *view, lv_obj_t *textarea)
{
    if (view == NULL || view->wifi_keyboard == NULL) {
        return;
    }

    lv_keyboard_set_textarea(view->wifi_keyboard, textarea);
    lv_obj_clear_flag(view->wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_mode(view->wifi_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_scroll_to_view_recursive(textarea, LV_ANIM_ON);
}

void ui_router_hide_keyboard(ui_router_view_t *view)
{
    if (view == NULL || view->wifi_keyboard == NULL) {
        return;
    }

    lv_keyboard_set_textarea(view->wifi_keyboard, NULL);
    lv_obj_add_flag(view->wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
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

static void apply_state_locked(const app_state_t *state)
{
    if (state->active_screen != s_view.last_active_screen) {
        s_view.last_active_screen = state->active_screen;
        if (state->active_screen == APP_SCREEN_SETTINGS) {
            (void)ota_manager_request_check();
        }
    }

    if (s_view.startup_overlay != NULL) {
        if (state->startup_stage == APP_STARTUP_STAGE_BOOTING) {
            lv_obj_clear_flag(s_view.startup_overlay, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_view.startup_overlay);
        } else {
            lv_obj_add_flag(s_view.startup_overlay, LV_OBJ_FLAG_HIDDEN);
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
    ui_primary_update(state, &s_view);
    ui_detail_update(state, &s_view);
    ui_settings_update(state, &s_view);
}

esp_err_t ui_router_init(app_state_t *state)
{
    app_state_t initial_snapshot = {0};

    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    app_state_get_snapshot(state, &initial_snapshot);

    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    s_view = (ui_router_view_t){
        .state = state,
        .state_snapshot = initial_snapshot,
        .last_active_screen = APP_SCREEN_COUNT,
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

    ui_primary_create(s_view.tiles[APP_SCREEN_PRIMARY], &s_view);
    ui_detail_create(s_view.tiles[APP_SCREEN_DETAIL], &s_view);
    ui_settings_create(screen, s_view.tiles[APP_SCREEN_SETTINGS], &s_view);

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

lv_obj_t *ui_router_get_screen_root(app_screen_t screen)
{
    if (screen >= APP_SCREEN_COUNT) {
        return NULL;
    }

    return s_view.tiles[screen];
}
