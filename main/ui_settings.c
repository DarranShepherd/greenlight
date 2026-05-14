#include "ui_router_internal.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "numeric_fonts.h"
#include "ota_manager.h"
#include "sync_controller.h"
#include "touch.h"
#include "wifi_manager.h"

#define TOUCH_CALIBRATION_POINT_COUNT 3
#define TOUCH_CALIBRATION_TARGET_SIZE 36

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

static void update_touch_calibration_status(const app_state_t *state, ui_router_view_t *view)
{
    if (view->touch_calibration_label == NULL) {
        return;
    }

    if (state->settings.touch_calibration.valid) {
        lv_label_set_text(view->touch_calibration_label, "Touch calibration saved");
    } else {
        lv_label_set_text(view->touch_calibration_label, "Touch uses default mapping");
    }
}

static void update_region_label(const app_state_t *state, ui_router_view_t *view)
{
    if (view->region_label == NULL || state == NULL) {
        return;
    }

    lv_label_set_text_fmt(
        view->region_label,
        "%s  %s",
        state->settings.region_code,
        get_region_name(state->settings.region_code)
    );
}

static void apply_region_index(ui_router_view_t *view, size_t region_index)
{
    app_settings_t current_settings = {0};
    app_settings_t next_settings = {0};

    if (view->state == NULL || region_index >= (sizeof(s_region_options) / sizeof(s_region_options[0]))) {
        return;
    }

    if (!ui_router_copy_settings(view, &current_settings)) {
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

    app_state_set_settings(view->state, &next_settings);
    if (view->region_label != NULL) {
        lv_label_set_text_fmt(view->region_label, "%s  %s", next_settings.region_code, get_region_name(next_settings.region_code));
    }
    sync_controller_request_refresh();
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

static lv_point_t get_touch_calibration_target_point(ui_router_view_t *view, uint8_t step)
{
    lv_area_t overlay_coords = {0};
    int32_t width = 0;
    int32_t height = 0;
    lv_point_t point = {0};

    if (view->touch_calibration_overlay != NULL) {
        lv_obj_get_coords(view->touch_calibration_overlay, &overlay_coords);
        width = lv_obj_get_width(view->touch_calibration_overlay);
        height = lv_obj_get_height(view->touch_calibration_overlay);
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

static bool solve_touch_calibration(
    const lv_point_t samples[TOUCH_CALIBRATION_POINT_COUNT],
    ui_router_view_t *view,
    app_touch_calibration_t *calibration
)
{
    lv_point_t targets[TOUCH_CALIBRATION_POINT_COUNT] = {0};
    int64_t determinant = 0;

    if (samples == NULL || calibration == NULL) {
        return false;
    }

    for (uint8_t index = 0; index < TOUCH_CALIBRATION_POINT_COUNT; index++) {
        targets[index] = convert_display_point_to_touch_space(get_touch_calibration_target_point(view, index));
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

static void update_touch_calibration_overlay(ui_router_view_t *view)
{
    lv_area_t overlay_coords = {0};
    lv_point_t point = {0};

    if (view->touch_calibration_overlay == NULL || view->touch_calibration_prompt == NULL || view->touch_calibration_target == NULL) {
        return;
    }

    if (view->touch_calibration_step >= TOUCH_CALIBRATION_POINT_COUNT) {
        lv_obj_add_flag(view->touch_calibration_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    point = get_touch_calibration_target_point(view, view->touch_calibration_step);
    lv_obj_get_coords(view->touch_calibration_overlay, &overlay_coords);
    lv_label_set_text(view->touch_calibration_prompt, s_touch_calibration_prompts[view->touch_calibration_step]);
    lv_obj_set_pos(
        view->touch_calibration_target,
        point.x - overlay_coords.x1 - (TOUCH_CALIBRATION_TARGET_SIZE / 2),
        point.y - overlay_coords.y1 - (TOUCH_CALIBRATION_TARGET_SIZE / 2)
    );
    lv_obj_move_foreground(view->touch_calibration_target);
}

static void update_wifi_dropdown(const app_state_t *state, ui_router_view_t *view)
{
    char options[sizeof(view->wifi_dropdown_cache)] = {0};
    size_t offset = 0;

    if (view->wifi_dropdown == NULL) {
        return;
    }

    if (state->wifi_scan_result_count == 0) {
        const char *fallback_ssid = state->settings.wifi_ssid[0] != '\0' ? state->settings.wifi_ssid : "Scan for Wi-Fi";

        if (strcmp(view->wifi_dropdown_cache, fallback_ssid) != 0) {
            lv_dropdown_set_options(view->wifi_dropdown, fallback_ssid);
            strlcpy(view->wifi_dropdown_cache, fallback_ssid, sizeof(view->wifi_dropdown_cache));
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

    if (strcmp(options, view->wifi_dropdown_cache) != 0) {
        lv_dropdown_set_options(view->wifi_dropdown, options);
        strlcpy(view->wifi_dropdown_cache, options, sizeof(view->wifi_dropdown_cache));
    }
}

static void change_brightness(ui_router_view_t *view, int32_t delta)
{
    app_settings_t settings = {0};

    if (view == NULL || !ui_router_copy_settings(view, &settings)) {
        return;
    }

    uint8_t next_brightness = ui_router_clamp_brightness_value((int32_t)settings.brightness_percent + delta);
    ui_router_apply_brightness(view, next_brightness);
}

static void brightness_down_event_cb(lv_event_t *event)
{
    ui_router_view_t *view = (ui_router_view_t *)lv_event_get_user_data(event);

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    change_brightness(view, -10);
}

static void brightness_up_event_cb(lv_event_t *event)
{
    ui_router_view_t *view = (ui_router_view_t *)lv_event_get_user_data(event);

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    change_brightness(view, 10);
}

static void cycle_region(ui_router_view_t *view, int32_t delta)
{
    app_settings_t settings = {0};
    size_t region_count = sizeof(s_region_options) / sizeof(s_region_options[0]);
    size_t current_index = 0;
    size_t next_index = 0;

    if (view == NULL || view->state == NULL || region_count == 0) {
        return;
    }

    if (!ui_router_copy_settings(view, &settings)) {
        return;
    }

    current_index = get_region_option_index(settings.region_code);
    next_index = (current_index + region_count + (size_t)delta) % region_count;
    apply_region_index(view, next_index);
}

static void region_prev_event_cb(lv_event_t *event)
{
    ui_router_view_t *view = (ui_router_view_t *)lv_event_get_user_data(event);

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    cycle_region(view, -1);
}

static void region_next_event_cb(lv_event_t *event)
{
    ui_router_view_t *view = (ui_router_view_t *)lv_event_get_user_data(event);

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    cycle_region(view, 1);
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
    ui_router_view_t *view = (ui_router_view_t *)lv_event_get_user_data(event);

    if (lv_event_get_code(event) != LV_EVENT_CLICKED || view == NULL) {
        return;
    }

    if (!ui_router_copy_state_snapshot(view, &snapshot)) {
        return;
    }

    psk = lv_textarea_get_text(view->wifi_psk_textarea);

    if (snapshot.wifi_scan_result_count > 0) {
        lv_dropdown_get_selected_str(view->wifi_dropdown, selected_ssid, sizeof(selected_ssid));
        ssid = selected_ssid;
    } else if (snapshot.settings.wifi_ssid[0] != '\0') {
        ssid = snapshot.settings.wifi_ssid;
    }

    ui_router_hide_keyboard(view);
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
    ui_router_view_t *view = (ui_router_view_t *)lv_event_get_user_data(event);

    if (view == NULL) {
        return;
    }

    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        ui_router_set_keyboard_target(view, textarea);
    }
}

static void wifi_keyboard_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    ui_router_view_t *view = (ui_router_view_t *)lv_event_get_user_data(event);

    if (view == NULL) {
        return;
    }

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        ui_router_hide_keyboard(view);
    }
}

static void firmware_update_button_event_cb(lv_event_t *event)
{
    ui_router_view_t *view = (ui_router_view_t *)lv_event_get_user_data(event);

    if (lv_event_get_code(event) != LV_EVENT_CLICKED || view == NULL) {
        return;
    }

    ui_router_hide_keyboard(view);
    (void)ota_manager_request_update();
}

static void touch_calibration_start_event_cb(lv_event_t *event)
{
    app_settings_t settings = {0};
    ui_router_view_t *view = (ui_router_view_t *)lv_event_get_user_data(event);

    if (lv_event_get_code(event) != LV_EVENT_CLICKED || view == NULL || view->touch_calibration_overlay == NULL) {
        return;
    }

    if (!ui_router_copy_settings(view, &settings)) {
        return;
    }

    ui_router_hide_keyboard(view);
    view->previous_touch_calibration = settings.touch_calibration;
    touch_set_calibration(NULL);
    view->touch_calibration_step = 0;
    memset(view->touch_calibration_samples, 0, sizeof(view->touch_calibration_samples));
    lv_obj_clear_flag(view->touch_calibration_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(view->touch_calibration_overlay);
    update_touch_calibration_overlay(view);
}

static void touch_calibration_overlay_event_cb(lv_event_t *event)
{
    app_settings_t settings = {0};
    lv_indev_t *indev = NULL;
    lv_point_t point = {0};
    app_touch_calibration_t calibration = {0};
    ui_router_view_t *view = (ui_router_view_t *)lv_event_get_user_data(event);

    if (lv_event_get_code(event) != LV_EVENT_CLICKED || view == NULL || view->touch_calibration_step >= TOUCH_CALIBRATION_POINT_COUNT) {
        return;
    }

    indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }

    lv_indev_get_point(indev, &point);
    view->touch_calibration_samples[view->touch_calibration_step] = convert_display_point_to_touch_space(point);
    view->touch_calibration_step++;

    if (view->touch_calibration_step < TOUCH_CALIBRATION_POINT_COUNT) {
        update_touch_calibration_overlay(view);
        return;
    }

    lv_obj_add_flag(view->touch_calibration_overlay, LV_OBJ_FLAG_HIDDEN);

    if (!ui_router_copy_settings(view, &settings)) {
        return;
    }

    if (solve_touch_calibration(view->touch_calibration_samples, view, &calibration)) {
        settings.touch_calibration = calibration;
        app_state_set_settings(view->state, &settings);
        touch_set_calibration(&calibration);
        (void)app_settings_save_touch_calibration(&calibration);
        if (view->touch_calibration_label != NULL) {
            lv_label_set_text(view->touch_calibration_label, "Touch calibration saved");
        }
        app_state_set_wifi_status(view->state, APP_WIFI_STATUS_IDLE, "Touch calibration saved");
        return;
    }

    settings.touch_calibration = view->previous_touch_calibration;
    app_state_set_settings(view->state, &settings);
    touch_set_calibration(&view->previous_touch_calibration);
    if (view->touch_calibration_label != NULL) {
        lv_label_set_text(
            view->touch_calibration_label,
            view->previous_touch_calibration.valid ? "Touch calibration saved" : "Touch uses default mapping"
        );
    }
    app_state_set_wifi_status(view->state, APP_WIFI_STATUS_FAILED, "Touch calibration failed");
}

void ui_settings_update(const app_state_t *state, ui_router_view_t *view)
{
    char clock_text[12] = {0};

    if (view->brightness_label != NULL) {
        lv_label_set_text_fmt(view->brightness_label, "%u%%", (unsigned int)state->settings.brightness_percent);
    }

    if (view->brightness_bar != NULL) {
        lv_bar_set_value(view->brightness_bar, state->settings.brightness_percent, LV_ANIM_OFF);
    }

    ui_router_format_clock_label(clock_text, sizeof(clock_text), state->local_time_text);

    if (view->settings_clock_label != NULL) {
        lv_label_set_text(view->settings_clock_label, clock_text);
    }

    if (view->settings_title_label != NULL) {
        lv_label_set_text(view->settings_title_label, "Settings");
    }

    ui_router_update_wifi_status(
        view->settings_wifi_label,
        view->settings_wifi_strike_label,
        state->wifi_status,
        lv_color_white(),
        lv_color_hex(0xdc2626)
    );

    update_region_label(state, view);
    update_touch_calibration_status(state, view);
    update_wifi_dropdown(state, view);

    if (view->wifi_status_label != NULL) {
        if (state->wifi_status == APP_WIFI_STATUS_CONNECTED && state->wifi_ip_address[0] != '\0') {
            lv_label_set_text_fmt(
                view->wifi_status_label,
                "%s\nSSID %s\nIP %s",
                state->wifi_status_text,
                state->wifi_connected_ssid,
                state->wifi_ip_address
            );
        } else {
            lv_label_set_text(view->wifi_status_label, state->wifi_status_text);
        }
    }

    if (view->wifi_scan_summary_label != NULL) {
        lv_label_set_text_fmt(view->wifi_scan_summary_label, "%u networks nearby", (unsigned int)state->wifi_scan_result_count);
    }

    if (view->time_status_label != NULL) {
        lv_label_set_text(view->time_status_label, state->time_status_text);
    }

    if (view->local_time_label != NULL) {
        if (state->local_time_text[0] == '\0') {
            lv_label_set_text(view->local_time_label, "");
        } else {
            lv_label_set_text_fmt(view->local_time_label, "London %s", state->local_time_text);
        }
    }

    if (view->firmware_version_label != NULL) {
        lv_label_set_text_fmt(
            view->firmware_version_label,
            "Firmware Version: %s",
            state->firmware_current_version[0] != '\0' ? state->firmware_current_version : "dev"
        );
    }

    if (view->firmware_available_label != NULL) {
        if (state->firmware_update_available && state->firmware_available_version[0] != '\0') {
            lv_label_set_text_fmt(view->firmware_available_label, "New Version Available: %s", state->firmware_available_version);
        } else {
            lv_label_set_text(view->firmware_available_label, "");
        }
    }

    if (view->firmware_status_label != NULL) {
        lv_label_set_text_fmt(
            view->firmware_status_label,
            "Status: %s",
            state->firmware_status_text[0] != '\0' ? state->firmware_status_text : app_state_get_firmware_update_status_name(state->firmware_update_status)
        );
    }

    if (view->firmware_update_button != NULL) {
        bool should_show_button = state->firmware_update_available ||
                                  state->firmware_update_status == APP_FIRMWARE_UPDATE_STATUS_DOWNLOADING ||
                                  state->firmware_update_status == APP_FIRMWARE_UPDATE_STATUS_APPLYING ||
                                  state->firmware_update_status == APP_FIRMWARE_UPDATE_STATUS_REBOOTING;

        if (should_show_button) {
            lv_obj_clear_flag(view->firmware_update_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(view->firmware_update_button, LV_OBJ_FLAG_HIDDEN);
        }

        if (state->firmware_update_status == APP_FIRMWARE_UPDATE_STATUS_AVAILABLE) {
            lv_obj_clear_state(view->firmware_update_button, LV_STATE_DISABLED);
            if (view->firmware_update_button_label != NULL) {
                lv_label_set_text(view->firmware_update_button_label, "Update Firmware");
            }
        } else {
            lv_obj_add_state(view->firmware_update_button, LV_STATE_DISABLED);
            if (view->firmware_update_button_label != NULL) {
                lv_label_set_text(view->firmware_update_button_label, "Updating...");
            }
        }
    }
}

void ui_settings_create(lv_obj_t *screen, lv_obj_t *tile, ui_router_view_t *view)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x050816), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tile, 10, 0);
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 8, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    view->settings_top_bar = lv_obj_create(tile);
    lv_obj_set_width(view->settings_top_bar, lv_pct(100));
    lv_obj_set_height(view->settings_top_bar, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(view->settings_top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->settings_top_bar, 0, 0);
    lv_obj_set_style_pad_all(view->settings_top_bar, 0, 0);
    lv_obj_set_style_pad_column(view->settings_top_bar, 8, 0);
    lv_obj_set_layout(view->settings_top_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(view->settings_top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(view->settings_top_bar, LV_OBJ_FLAG_SCROLLABLE);

    view->settings_clock_label = lv_label_create(view->settings_top_bar);
    lv_obj_set_width(view->settings_clock_label, 52);
    lv_obj_set_style_text_color(view->settings_clock_label, lv_color_white(), 0);

    view->settings_title_label = lv_label_create(view->settings_top_bar);
    lv_obj_set_flex_grow(view->settings_title_label, 1);
    lv_obj_set_style_text_align(view->settings_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(view->settings_title_label, lv_color_white(), 0);

    ui_router_create_wifi_status(view->settings_top_bar, &view->settings_wifi_label, &view->settings_wifi_strike_label);

    view->settings_content = lv_obj_create(tile);
    lv_obj_set_width(view->settings_content, lv_pct(100));
    lv_obj_set_flex_grow(view->settings_content, 1);
    lv_obj_set_style_bg_opa(view->settings_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->settings_content, 0, 0);
    lv_obj_set_style_pad_all(view->settings_content, 0, 0);
    lv_obj_set_style_pad_row(view->settings_content, 8, 0);
    lv_obj_set_layout(view->settings_content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(view->settings_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(view->settings_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(view->settings_content, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *brightness_card = ui_router_create_section_card(view->settings_content, lv_color_hex(0x111827));
    lv_obj_set_style_radius(brightness_card, 16, 0);
    lv_obj_set_style_border_width(brightness_card, 1, 0);
    lv_obj_set_style_border_color(brightness_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_pad_all(brightness_card, 14, 0);
    lv_obj_set_style_pad_row(brightness_card, 10, 0);

    lv_obj_t *brightness_title = lv_label_create(brightness_card);
    lv_label_set_text(brightness_title, "Brightness");
    lv_obj_set_style_text_color(brightness_title, lv_color_white(), 0);

    view->brightness_label = lv_label_create(brightness_card);
    lv_obj_set_style_text_color(view->brightness_label, lv_color_hex(0xfbbf24), 0);
    lv_obj_set_style_text_font(view->brightness_label, &lv_font_montserrat_20_numeric, 0);

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
    lv_obj_add_event_cb(minus_button, brightness_down_event_cb, LV_EVENT_CLICKED, view);
    lv_obj_t *minus_label = lv_label_create(minus_button);
    lv_label_set_text(minus_label, "-");
    lv_obj_set_style_text_color(minus_label, lv_color_white(), 0);
    lv_obj_center(minus_label);

    view->brightness_bar = lv_bar_create(brightness_row);
    lv_obj_set_height(view->brightness_bar, 12);
    lv_obj_set_flex_grow(view->brightness_bar, 1);
    lv_bar_set_range(view->brightness_bar, 10, 100);
    lv_obj_set_style_bg_color(view->brightness_bar, lv_color_hex(0x374151), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(view->brightness_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(view->brightness_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(view->brightness_bar, lv_color_hex(0xf59e0b), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(view->brightness_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(view->brightness_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    lv_obj_t *plus_button = lv_button_create(brightness_row);
    lv_obj_set_size(plus_button, 40, 36);
    lv_obj_set_style_radius(plus_button, 12, 0);
    lv_obj_set_style_bg_color(plus_button, lv_color_hex(0x1e293b), 0);
    lv_obj_add_event_cb(plus_button, brightness_up_event_cb, LV_EVENT_CLICKED, view);
    lv_obj_t *plus_label = lv_label_create(plus_button);
    lv_label_set_text(plus_label, "+");
    lv_obj_set_style_text_color(plus_label, lv_color_white(), 0);
    lv_obj_center(plus_label);

    lv_obj_t *region_card = ui_router_create_section_card(view->settings_content, lv_color_hex(0x111827));
    lv_obj_set_style_radius(region_card, 16, 0);
    lv_obj_set_style_border_width(region_card, 1, 0);
    lv_obj_set_style_border_color(region_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_pad_all(region_card, 14, 0);
    lv_obj_set_style_pad_row(region_card, 10, 0);

    lv_obj_t *region_title = lv_label_create(region_card);
    lv_label_set_text(region_title, "Region");
    lv_obj_set_style_text_color(region_title, lv_color_white(), 0);

    view->region_label = lv_label_create(region_card);
    lv_obj_set_width(view->region_label, lv_pct(100));
    lv_obj_set_style_text_color(view->region_label, lv_color_hex(0x93c5fd), 0);
    lv_obj_set_style_text_font(view->region_label, &lv_font_montserrat_14, 0);

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

    lv_obj_t *region_prev_button = ui_router_create_dark_button(region_row, "Prev");
    lv_obj_set_flex_grow(region_prev_button, 1);
    lv_obj_set_style_bg_color(region_prev_button, lv_color_hex(0x1e293b), 0);
    lv_obj_add_event_cb(region_prev_button, region_prev_event_cb, LV_EVENT_CLICKED, view);

    lv_obj_t *region_next_button = ui_router_create_dark_button(region_row, "Next");
    lv_obj_set_flex_grow(region_next_button, 1);
    lv_obj_set_style_bg_color(region_next_button, lv_color_hex(0x1e293b), 0);
    lv_obj_add_event_cb(region_next_button, region_next_event_cb, LV_EVENT_CLICKED, view);

    lv_obj_t *wifi_card = ui_router_create_section_card(view->settings_content, lv_color_hex(0x111827));
    lv_obj_set_style_radius(wifi_card, 16, 0);
    lv_obj_set_style_border_width(wifi_card, 1, 0);
    lv_obj_set_style_border_color(wifi_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_pad_all(wifi_card, 14, 0);
    lv_obj_set_style_pad_row(wifi_card, 10, 0);

    lv_obj_t *wifi_title = lv_label_create(wifi_card);
    lv_label_set_text(wifi_title, "Wi-Fi");
    lv_obj_set_style_text_color(wifi_title, lv_color_white(), 0);

    view->wifi_status_label = lv_label_create(wifi_card);
    lv_obj_set_width(view->wifi_status_label, lv_pct(100));
    lv_label_set_long_mode(view->wifi_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(view->wifi_status_label, lv_color_hex(0xe5e7eb), 0);

    view->wifi_scan_summary_label = lv_label_create(wifi_card);
    lv_obj_set_style_text_color(view->wifi_scan_summary_label, lv_color_hex(0x9ca3af), 0);

    view->wifi_dropdown = lv_dropdown_create(wifi_card);
    lv_obj_set_width(view->wifi_dropdown, lv_pct(100));
    lv_dropdown_set_options(view->wifi_dropdown, "Scan for Wi-Fi");
    lv_obj_set_style_bg_color(view->wifi_dropdown, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_text_color(view->wifi_dropdown, lv_color_white(), 0);
    lv_obj_set_style_border_color(view->wifi_dropdown, lv_color_hex(0x334155), 0);
    lv_obj_add_event_cb(view->wifi_dropdown, wifi_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, view);

    view->wifi_psk_textarea = lv_textarea_create(wifi_card);
    lv_obj_set_width(view->wifi_psk_textarea, lv_pct(100));
    lv_textarea_set_one_line(view->wifi_psk_textarea, true);
    lv_textarea_set_password_mode(view->wifi_psk_textarea, true);
    lv_textarea_set_placeholder_text(view->wifi_psk_textarea, "Wi-Fi password");
    lv_obj_set_style_bg_color(view->wifi_psk_textarea, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_text_color(view->wifi_psk_textarea, lv_color_white(), 0);
    lv_obj_set_style_text_color(view->wifi_psk_textarea, lv_color_hex(0x64748b), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(view->wifi_psk_textarea, lv_color_hex(0x334155), 0);
    lv_obj_add_event_cb(view->wifi_psk_textarea, wifi_textarea_event_cb, LV_EVENT_FOCUSED, view);
    lv_obj_add_event_cb(view->wifi_psk_textarea, wifi_textarea_event_cb, LV_EVENT_CLICKED, view);

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

    lv_obj_t *scan_button = ui_router_create_dark_button(button_row, "Scan");
    lv_obj_set_flex_grow(scan_button, 1);
    lv_obj_set_style_bg_color(scan_button, lv_color_hex(0x1d4ed8), 0);
    lv_obj_add_event_cb(scan_button, wifi_scan_button_event_cb, LV_EVENT_CLICKED, view);

    lv_obj_t *connect_button = ui_router_create_dark_button(button_row, "Join");
    lv_obj_set_flex_grow(connect_button, 1);
    lv_obj_set_style_bg_color(connect_button, lv_color_hex(0x047857), 0);
    lv_obj_add_event_cb(connect_button, wifi_connect_button_event_cb, LV_EVENT_CLICKED, view);

    lv_obj_t *touch_card = ui_router_create_section_card(view->settings_content, lv_color_hex(0x111827));
    lv_obj_set_style_radius(touch_card, 16, 0);
    lv_obj_set_style_border_width(touch_card, 1, 0);
    lv_obj_set_style_border_color(touch_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_pad_all(touch_card, 14, 0);
    lv_obj_set_style_pad_row(touch_card, 10, 0);

    lv_obj_t *touch_title = lv_label_create(touch_card);
    lv_label_set_text(touch_title, "Touch Calibration");
    lv_obj_set_style_text_color(touch_title, lv_color_white(), 0);

    view->touch_calibration_label = lv_label_create(touch_card);
    lv_obj_set_style_text_color(view->touch_calibration_label, lv_color_hex(0x9ca3af), 0);
    lv_obj_set_width(view->touch_calibration_label, lv_pct(100));
    lv_label_set_long_mode(view->touch_calibration_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *calibrate_button = ui_router_create_dark_button(touch_card, "Calibrate touch");
    lv_obj_set_width(calibrate_button, lv_pct(100));
    lv_obj_set_style_bg_color(calibrate_button, lv_color_hex(0x1e293b), 0);
    lv_obj_add_event_cb(calibrate_button, touch_calibration_start_event_cb, LV_EVENT_CLICKED, view);

    lv_obj_t *time_card = ui_router_create_section_card(view->settings_content, lv_color_hex(0x111827));
    lv_obj_set_style_radius(time_card, 16, 0);
    lv_obj_set_style_border_width(time_card, 1, 0);
    lv_obj_set_style_border_color(time_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_pad_all(time_card, 14, 0);
    lv_obj_set_style_pad_row(time_card, 10, 0);

    lv_obj_t *time_title = lv_label_create(time_card);
    lv_label_set_text(time_title, "Time Sync");
    lv_obj_set_style_text_color(time_title, lv_color_white(), 0);

    view->time_status_label = lv_label_create(time_card);
    lv_obj_set_width(view->time_status_label, lv_pct(100));
    lv_label_set_long_mode(view->time_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(view->time_status_label, lv_color_hex(0xe5e7eb), 0);

    view->local_time_label = lv_label_create(time_card);
    lv_obj_set_style_text_color(view->local_time_label, lv_color_hex(0x9ca3af), 0);

    lv_obj_t *firmware_card = ui_router_create_section_card(view->settings_content, lv_color_hex(0x111827));
    lv_obj_set_style_radius(firmware_card, 16, 0);
    lv_obj_set_style_border_width(firmware_card, 1, 0);
    lv_obj_set_style_border_color(firmware_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_pad_all(firmware_card, 14, 0);
    lv_obj_set_style_pad_row(firmware_card, 10, 0);

    lv_obj_t *firmware_title = lv_label_create(firmware_card);
    lv_label_set_text(firmware_title, "Firmware Updates");
    lv_obj_set_style_text_color(firmware_title, lv_color_white(), 0);

    view->firmware_version_label = lv_label_create(firmware_card);
    lv_obj_set_width(view->firmware_version_label, lv_pct(100));
    lv_label_set_long_mode(view->firmware_version_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(view->firmware_version_label, lv_color_hex(0xe5e7eb), 0);

    view->firmware_available_label = lv_label_create(firmware_card);
    lv_obj_set_width(view->firmware_available_label, lv_pct(100));
    lv_label_set_long_mode(view->firmware_available_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(view->firmware_available_label, lv_color_hex(0x93c5fd), 0);

    view->firmware_status_label = lv_label_create(firmware_card);
    lv_obj_set_width(view->firmware_status_label, lv_pct(100));
    lv_label_set_long_mode(view->firmware_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(view->firmware_status_label, lv_color_hex(0x9ca3af), 0);

    view->firmware_update_button = ui_router_create_dark_button(firmware_card, "Update Firmware");
    lv_obj_set_width(view->firmware_update_button, lv_pct(100));
    lv_obj_set_style_bg_color(view->firmware_update_button, lv_color_hex(0x1d4ed8), 0);
    lv_obj_add_event_cb(view->firmware_update_button, firmware_update_button_event_cb, LV_EVENT_CLICKED, view);
    lv_obj_add_flag(view->firmware_update_button, LV_OBJ_FLAG_HIDDEN);
    view->firmware_update_button_label = lv_obj_get_child(view->firmware_update_button, 0);

    view->wifi_keyboard = lv_keyboard_create(tile);
    lv_obj_set_width(view->wifi_keyboard, lv_pct(100));
    lv_obj_set_height(view->wifi_keyboard, 150);
    lv_obj_set_style_radius(view->wifi_keyboard, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(view->wifi_keyboard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(view->wifi_keyboard, lv_color_hex(0x020617), LV_PART_MAIN);
    lv_obj_set_style_border_width(view->wifi_keyboard, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(view->wifi_keyboard, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_pad_all(view->wifi_keyboard, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(view->wifi_keyboard, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(view->wifi_keyboard, 10, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(view->wifi_keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(view->wifi_keyboard, lv_color_hex(0x1e293b), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(view->wifi_keyboard, lv_color_hex(0x334155), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(view->wifi_keyboard, lv_color_hex(0x2563eb), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(view->wifi_keyboard, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_border_width(view->wifi_keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(view->wifi_keyboard, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(view->wifi_keyboard, wifi_keyboard_event_cb, LV_EVENT_ALL, view);
    lv_obj_add_flag(view->wifi_keyboard, LV_OBJ_FLAG_HIDDEN);

    view->touch_calibration_overlay = lv_obj_create(screen);
    lv_obj_set_size(view->touch_calibration_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(view->touch_calibration_overlay, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(view->touch_calibration_overlay, LV_OPA_80, 0);
    lv_obj_set_style_border_width(view->touch_calibration_overlay, 0, 0);
    lv_obj_set_style_pad_all(view->touch_calibration_overlay, 0, 0);
    lv_obj_add_event_cb(view->touch_calibration_overlay, touch_calibration_overlay_event_cb, LV_EVENT_CLICKED, view);
    lv_obj_add_flag(view->touch_calibration_overlay, LV_OBJ_FLAG_HIDDEN);

    view->touch_calibration_prompt = lv_label_create(view->touch_calibration_overlay);
    lv_obj_set_width(view->touch_calibration_prompt, lv_pct(100));
    lv_obj_set_style_text_align(view->touch_calibration_prompt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(view->touch_calibration_prompt, lv_color_white(), 0);
    lv_obj_align(view->touch_calibration_prompt, LV_ALIGN_TOP_MID, 0, 12);

    view->touch_calibration_target = lv_obj_create(view->touch_calibration_overlay);
    lv_obj_set_size(view->touch_calibration_target, TOUCH_CALIBRATION_TARGET_SIZE, TOUCH_CALIBRATION_TARGET_SIZE);
    lv_obj_set_style_radius(view->touch_calibration_target, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(view->touch_calibration_target, lv_color_hex(0xf59e0b), 0);
    lv_obj_set_style_bg_opa(view->touch_calibration_target, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view->touch_calibration_target, 3, 0);
    lv_obj_set_style_border_color(view->touch_calibration_target, lv_color_white(), 0);
    lv_obj_remove_flag(view->touch_calibration_target, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}