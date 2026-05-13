#include "ui_router.h"

#include <stdbool.h>
#include <stdio.h>

#include <esp_check.h>
#include <esp_lvgl_port.h>

#include "app_settings.h"
#include "lcd.h"
#include "time_manager.h"
#include "touch.h"
#include "wifi_manager.h"

#define TOUCH_CALIBRATION_POINT_COUNT 3
#define TOUCH_CALIBRATION_TARGET_SIZE 36

typedef struct {
    lv_obj_t *tileview;
    lv_obj_t *tiles[APP_SCREEN_COUNT];
    lv_obj_t *settings_content;
    lv_obj_t *route_label;
    lv_obj_t *uptime_label;
    lv_obj_t *brightness_label;
    lv_obj_t *brightness_bar;
    lv_obj_t *region_label;
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
    bool wifi_psk_seeded_from_settings;
    uint8_t touch_calibration_step;
    char wifi_dropdown_cache[APP_WIFI_SCAN_MAX_RESULTS * (APP_SETTINGS_WIFI_SSID_MAX_LEN + 1) + 32];
    app_state_t *state;
} ui_router_view_t;

static ui_router_view_t s_view;

static const char *s_touch_calibration_prompts[TOUCH_CALIBRATION_POINT_COUNT] = {
    "Touch the upper-left target",
    "Touch the upper-right target",
    "Touch the lower-center target",
};

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

static void add_tile_header(lv_obj_t *parent, const char *eyebrow, const char *title, const char *body, lv_color_t text_color)
{
    lv_obj_t *eyebrow_label = lv_label_create(parent);
    lv_label_set_text(eyebrow_label, eyebrow);
    lv_obj_set_style_text_color(eyebrow_label, text_color, 0);
    lv_obj_set_style_text_opa(eyebrow_label, LV_OPA_70, 0);

    lv_obj_t *title_label = lv_label_create(parent);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(title_label, text_color, 0);

    lv_obj_t *body_label = lv_label_create(parent);
    lv_label_set_text(body_label, body);
    lv_obj_set_style_text_color(body_label, text_color, 0);
    lv_obj_set_width(body_label, lv_pct(100));
    lv_label_set_long_mode(body_label, LV_LABEL_LONG_WRAP);
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
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    int32_t delta = (int32_t)(intptr_t)lv_event_get_user_data(event);
    uint8_t next_brightness = clamp_brightness_value((int32_t)s_view.state->settings.brightness_percent + delta);
    apply_brightness_locked(next_brightness);
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
    const char *psk = NULL;
    char selected_ssid[APP_SETTINGS_WIFI_SSID_MAX_LEN + 1] = {0};
    const char *ssid = NULL;

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    psk = lv_textarea_get_text(s_view.wifi_psk_textarea);

    if (s_view.state->wifi_scan_result_count > 0) {
        lv_dropdown_get_selected_str(s_view.wifi_dropdown, selected_ssid, sizeof(selected_ssid));
        ssid = selected_ssid;
    } else if (s_view.state->settings.wifi_ssid[0] != '\0') {
        ssid = s_view.state->settings.wifi_ssid;
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
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_view.touch_calibration_overlay == NULL) {
        return;
    }

    hide_keyboard();
    s_view.previous_touch_calibration = s_view.state->settings.touch_calibration;
    touch_set_calibration(NULL);
    s_view.touch_calibration_step = 0;
    memset(s_view.touch_calibration_samples, 0, sizeof(s_view.touch_calibration_samples));
    lv_obj_clear_flag(s_view.touch_calibration_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_view.touch_calibration_overlay);
    update_touch_calibration_overlay_locked();
}

static void touch_calibration_overlay_event_cb(lv_event_t *event)
{
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

    if (solve_touch_calibration(s_view.touch_calibration_samples, &calibration)) {
        s_view.state->settings.touch_calibration = calibration;
        touch_set_calibration(&calibration);
        (void)app_settings_save_touch_calibration(&calibration);
        update_touch_calibration_status_locked(s_view.state);
        app_state_set_wifi_status(s_view.state, APP_WIFI_STATUS_IDLE, "Touch calibration saved");
    } else {
        s_view.state->settings.touch_calibration = s_view.previous_touch_calibration;
        touch_set_calibration(&s_view.previous_touch_calibration);
        app_state_set_wifi_status(s_view.state, APP_WIFI_STATUS_FAILED, "Touch calibration failed");
    }
}

static void sync_tile_locked(const app_state_t *state)
{
    if (state->active_screen >= APP_SCREEN_COUNT) {
        return;
    }

    if (lv_tileview_get_tile_active(s_view.tileview) != s_view.tiles[state->active_screen]) {
        lv_tileview_set_tile_by_index(s_view.tileview, state->active_screen, 0, LV_ANIM_OFF);
    }
}

static void apply_state_locked(const app_state_t *state)
{
    sync_tile_locked(state);

    if (s_view.route_label != NULL) {
        lv_label_set_text_fmt(s_view.route_label, "Screen: %s", app_state_get_screen_name(state->active_screen));
    }

    if (s_view.uptime_label != NULL) {
        lv_label_set_text_fmt(s_view.uptime_label, "Uptime %lus", (unsigned long)state->uptime_seconds);
    }

    if (s_view.brightness_label != NULL) {
        lv_label_set_text_fmt(s_view.brightness_label, "%u%%", (unsigned int)state->settings.brightness_percent);
    }

    if (s_view.brightness_bar != NULL) {
        lv_bar_set_value(s_view.brightness_bar, state->settings.brightness_percent, LV_ANIM_OFF);
    }

    if (s_view.region_label != NULL) {
        lv_label_set_text_fmt(s_view.region_label, "Region %s", state->settings.region_code);
    }

    update_touch_calibration_status_locked(state);

    update_wifi_dropdown_locked(state);

    if (s_view.wifi_status_label != NULL) {
        if (state->wifi_status == APP_WIFI_STATUS_CONNECTED && state->wifi_ip_address[0] != '\0') {
            lv_label_set_text_fmt(
                s_view.wifi_status_label,
                "Wi-Fi: %s\nSSID: %s\nIP: %s",
                state->wifi_status_text,
                state->wifi_connected_ssid,
                state->wifi_ip_address
            );
        } else {
            lv_label_set_text_fmt(s_view.wifi_status_label, "Wi-Fi: %s", state->wifi_status_text);
        }
    }

    if (s_view.wifi_scan_summary_label != NULL) {
        lv_label_set_text_fmt(s_view.wifi_scan_summary_label, "Nearby networks: %u", (unsigned int)state->wifi_scan_result_count);
    }

    if (s_view.time_status_label != NULL) {
        lv_label_set_text_fmt(s_view.time_status_label, "Clock: %s", state->time_status_text);
    }

    if (s_view.local_time_label != NULL) {
        lv_label_set_text_fmt(s_view.local_time_label, "Local time: %s", state->local_time_text);
    }

    if (!s_view.wifi_psk_seeded_from_settings && s_view.wifi_psk_textarea != NULL && state->settings.wifi_psk[0] != '\0') {
        lv_textarea_set_text(s_view.wifi_psk_textarea, state->settings.wifi_psk);
        s_view.wifi_psk_seeded_from_settings = true;
    }
}

static void create_primary_tile(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x0f3d2e), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tile, 18, 0);
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 14, 0);

    add_tile_header(
        tile,
        "Greenlight / Foundation",
        "Primary screen shell",
        "This replaces the bring-up demo. The live tariff card lands on this route in phase 4.",
        lv_color_white()
    );

    lv_obj_t *card = create_section_card(tile, lv_color_hex(0x14532d));
    lv_obj_t *status = lv_label_create(card);
    lv_label_set_text(status, "Phase 2 now brings up Wi-Fi onboarding and London time sync. The tariff card still lands here in phase 4.");
    lv_obj_set_style_text_color(status, lv_color_white(), 0);
    lv_obj_set_width(status, lv_pct(100));
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);

    s_view.route_label = lv_label_create(card);
    lv_obj_set_style_text_color(s_view.route_label, lv_color_hex(0xd1fae5), 0);

    s_view.uptime_label = lv_label_create(card);
    lv_obj_set_style_text_color(s_view.uptime_label, lv_color_hex(0xa7f3d0), 0);
}

static void create_detail_tile(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tile, 18, 0);
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 14, 0);

    add_tile_header(
        tile,
        "Greenlight / Detail",
        "Detail screen shell",
        "The histogram and daily summaries will attach here once the Octopus tariff pipeline exists.",
        lv_color_white()
    );

    lv_obj_t *card = create_section_card(tile, lv_color_hex(0x374151));
    lv_obj_t *status = lv_label_create(card);
    lv_label_set_text(status, "The detail route stays live while Wi-Fi and time come online, so tariff charts can plug in without reworking navigation.");
    lv_obj_set_style_text_color(status, lv_color_hex(0xe5e7eb), 0);
    lv_obj_set_width(status, lv_pct(100));
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
}

static void create_settings_tile(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0xf3efe5), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tile, 18, 0);
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 14, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    s_view.settings_content = lv_obj_create(tile);
    lv_obj_set_width(s_view.settings_content, lv_pct(100));
    lv_obj_set_flex_grow(s_view.settings_content, 1);
    lv_obj_set_style_bg_opa(s_view.settings_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_view.settings_content, 0, 0);
    lv_obj_set_style_pad_all(s_view.settings_content, 0, 0);
    lv_obj_set_style_pad_row(s_view.settings_content, 14, 0);
    lv_obj_set_layout(s_view.settings_content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_view.settings_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_view.settings_content, LV_DIR_VER);

    add_tile_header(
        s_view.settings_content,
        "Greenlight / Settings",
        "Onboarding and controls",
        "Scan for Wi-Fi, enter credentials on-device, and confirm the London clock before moving on to tariff work.",
        lv_color_hex(0x111827)
    );

    lv_obj_t *wifi_card = create_section_card(s_view.settings_content, lv_color_hex(0xfffbeb));

    lv_obj_t *wifi_title = lv_label_create(wifi_card);
    lv_label_set_text(wifi_title, "Wi-Fi onboarding");
    lv_obj_set_style_text_color(wifi_title, lv_color_hex(0x111827), 0);

    s_view.wifi_status_label = lv_label_create(wifi_card);
    lv_obj_set_width(s_view.wifi_status_label, lv_pct(100));
    lv_label_set_long_mode(s_view.wifi_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_view.wifi_status_label, lv_color_hex(0x374151), 0);

    s_view.wifi_scan_summary_label = lv_label_create(wifi_card);
    lv_obj_set_style_text_color(s_view.wifi_scan_summary_label, lv_color_hex(0x6b7280), 0);

    s_view.wifi_dropdown = lv_dropdown_create(wifi_card);
    lv_obj_set_width(s_view.wifi_dropdown, lv_pct(100));
    lv_dropdown_set_options(s_view.wifi_dropdown, "Scan for Wi-Fi");
    lv_obj_add_event_cb(s_view.wifi_dropdown, wifi_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_view.wifi_psk_textarea = lv_textarea_create(wifi_card);
    lv_obj_set_width(s_view.wifi_psk_textarea, lv_pct(100));
    lv_textarea_set_one_line(s_view.wifi_psk_textarea, true);
    lv_textarea_set_password_mode(s_view.wifi_psk_textarea, true);
    lv_textarea_set_placeholder_text(s_view.wifi_psk_textarea, "Wi-Fi password");
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
    lv_obj_add_event_cb(scan_button, wifi_scan_button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *connect_button = create_dark_button(button_row, "Join");
    lv_obj_set_flex_grow(connect_button, 1);
    lv_obj_add_event_cb(connect_button, wifi_connect_button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hint = lv_label_create(wifi_card);
    lv_label_set_text(hint, "Pick a scanned network, then tap the password field to open the keyboard.");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x6b7280), 0);
    lv_obj_set_width(hint, lv_pct(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);

    s_view.touch_calibration_label = lv_label_create(wifi_card);
    lv_obj_set_style_text_color(s_view.touch_calibration_label, lv_color_hex(0x6b7280), 0);

    lv_obj_t *calibrate_button = create_dark_button(wifi_card, "Calibrate touch");
    lv_obj_set_width(calibrate_button, lv_pct(100));
    lv_obj_add_event_cb(calibrate_button, touch_calibration_start_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *status_card = create_section_card(s_view.settings_content, lv_color_hex(0xf8fafc));

    lv_obj_t *clock_title = lv_label_create(status_card);
    lv_label_set_text(clock_title, "Clock and device settings");
    lv_obj_set_style_text_color(clock_title, lv_color_hex(0x111827), 0);

    s_view.time_status_label = lv_label_create(status_card);
    lv_obj_set_width(s_view.time_status_label, lv_pct(100));
    lv_label_set_long_mode(s_view.time_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_view.time_status_label, lv_color_hex(0x1f2937), 0);

    s_view.local_time_label = lv_label_create(status_card);
    lv_obj_set_style_text_color(s_view.local_time_label, lv_color_hex(0x374151), 0);

    lv_obj_t *brightness_title = lv_label_create(status_card);
    lv_label_set_text(brightness_title, "Brightness");
    lv_obj_set_style_text_color(brightness_title, lv_color_hex(0x111827), 0);

    s_view.brightness_label = lv_label_create(status_card);
    lv_obj_set_style_text_color(s_view.brightness_label, lv_color_hex(0x92400e), 0);

    lv_obj_t *brightness_row = lv_obj_create(status_card);
    lv_obj_set_width(brightness_row, lv_pct(100));
    lv_obj_set_height(brightness_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(brightness_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brightness_row, 0, 0);
    lv_obj_set_style_pad_all(brightness_row, 0, 0);
    lv_obj_set_style_pad_column(brightness_row, 8, 0);
    lv_obj_set_layout(brightness_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(brightness_row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(brightness_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *minus_button = lv_button_create(brightness_row);
    lv_obj_set_size(minus_button, 40, 36);
    lv_obj_set_style_bg_color(minus_button, lv_color_hex(0x111827), 0);
    lv_obj_add_event_cb(minus_button, brightness_button_event_cb, LV_EVENT_CLICKED, (void *)-10);
    lv_obj_t *minus_label = lv_label_create(minus_button);
    lv_label_set_text(minus_label, "-");
    lv_obj_center(minus_label);

    s_view.brightness_bar = lv_bar_create(brightness_row);
    lv_obj_set_width(s_view.brightness_bar, 120);
    lv_obj_set_height(s_view.brightness_bar, 18);
    lv_bar_set_range(s_view.brightness_bar, 10, 100);
    lv_obj_set_style_bg_color(s_view.brightness_bar, lv_color_hex(0xd1d5db), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_view.brightness_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_view.brightness_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_view.brightness_bar, lv_color_hex(0xf59e0b), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_view.brightness_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_view.brightness_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    lv_obj_t *plus_button = lv_button_create(brightness_row);
    lv_obj_set_size(plus_button, 40, 36);
    lv_obj_set_style_bg_color(plus_button, lv_color_hex(0x111827), 0);
    lv_obj_add_event_cb(plus_button, brightness_button_event_cb, LV_EVENT_CLICKED, (void *)10);
    lv_obj_t *plus_label = lv_label_create(plus_button);
    lv_label_set_text(plus_label, "+");
    lv_obj_center(plus_label);

    s_view.region_label = lv_label_create(status_card);
    lv_obj_set_style_text_color(s_view.region_label, lv_color_hex(0x374151), 0);

    s_view.wifi_keyboard = lv_keyboard_create(tile);
    lv_obj_set_width(s_view.wifi_keyboard, lv_pct(100));
    lv_obj_set_height(s_view.wifi_keyboard, 150);
    lv_obj_set_style_radius(s_view.wifi_keyboard, 16, 0);
    lv_obj_set_style_bg_color(s_view.wifi_keyboard, lv_color_hex(0xe5dcc8), 0);
    lv_obj_add_event_cb(s_view.wifi_keyboard, wifi_keyboard_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(s_view.wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
}

esp_err_t ui_router_init(app_state_t *state)
{
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

    lv_tileview_set_tile_by_index(s_view.tileview, 0, 0, LV_ANIM_OFF);
    apply_state_locked(state);

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_router_update(const app_state_t *state)
{
    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    apply_state_locked(state);

    lvgl_port_unlock();
    return ESP_OK;
}