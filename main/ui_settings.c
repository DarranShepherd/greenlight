#include "ui_router_internal.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "board_profile.h"
#include "numeric_fonts.h"
#include "ota_manager.h"
#include "sync_controller.h"
#include "touch.h"
#include "wifi_manager.h"

#define TOUCH_CALIBRATION_POINT_COUNT 5
#define TOUCH_CALIBRATION_TARGET_SIZE 36
#define TOUCH_CALIBRATION_CROSSHAIR_LENGTH 24
#define TOUCH_CALIBRATION_CROSSHAIR_THICKNESS 2
#define TOUCH_CALIBRATION_CENTER_DOT_SIZE 6
#define TOUCH_CALIBRATION_TARGET_IDLE_COLOR 0xf59e0b
#define TOUCH_CALIBRATION_TARGET_ACTIVE_COLOR 0x16a34a
#define TOUCH_CALIBRATION_PRESS_SAMPLE_CAPACITY 16
#define TOUCH_CALIBRATION_MIN_PRESS_SAMPLES 6
#define TOUCH_CALIBRATION_MAX_COMPONENT_ERROR_PX 18
#define TOUCH_CALIBRATION_MAX_AVERAGE_ERROR_PX 14

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

static const char *s_region_dropdown_options =
    "Select a region\n"
    "A  Eastern England\n"
    "B  East Midlands\n"
    "C  London\n"
    "D  Merseyside and North Wales\n"
    "E  West Midlands\n"
    "F  North East England\n"
    "G  North West England\n"
    "H  South England\n"
    "J  South East England\n"
    "K  South Wales\n"
    "L  South West England\n"
    "M  Yorkshire\n"
    "N  South Scotland\n"
    "P  North Scotland";

static const char *s_touch_calibration_prompts[TOUCH_CALIBRATION_POINT_COUNT] = {
    "Press and hold the upper-left target",
    "Press and hold the upper-right target",
    "Press and hold the center target",
    "Press and hold the lower-left target",
    "Press and hold the lower-right target",
};

static lv_point_t convert_display_point_to_touch_space(lv_point_t display_point);

static size_t get_region_option_index(const char *region_code)
{
    char normalized_code = '\0';

    if (region_code != NULL && region_code[0] != '\0') {
        normalized_code = (char)toupper((unsigned char)region_code[0]);
    }

    for (size_t index = 0; index < sizeof(s_region_options) / sizeof(s_region_options[0]); index++) {
        if (s_region_options[index].code == normalized_code) {
            return index;
        }
    }

    return 0;
}

static const char *get_region_name(const char *region_code)
{
    return s_region_options[get_region_option_index(region_code)].name;
}

static const char *get_selected_region_code(const app_settings_t *settings)
{
    if (settings != NULL && settings->region_code[0] != '\0') {
        return settings->region_code;
    }

    return "";
}

static const char *get_board_display_name(void)
{
    const greenlight_board_profile_t *board_profile = greenlight_board_profile_get();

    if (board_profile == NULL || board_profile->display_name == NULL || board_profile->display_name[0] == '\0') {
        return "Unknown board";
    }

    return board_profile->display_name;
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

static void update_region_dropdown(const app_state_t *state, ui_router_view_t *view)
{
    if (view->region_dropdown == NULL || state == NULL) {
        return;
    }

    if (!app_settings_region_is_configured(&state->settings)) {
        return;
    }

    lv_dropdown_set_selected(view->region_dropdown, get_region_option_index(get_selected_region_code(&state->settings)) + 1);
}

static bool apply_region_index(ui_router_view_t *view, size_t region_index)
{
    app_settings_t current_settings = {0};
    app_settings_t next_settings = {0};

    if (view->state == NULL || region_index >= (sizeof(s_region_options) / sizeof(s_region_options[0]))) {
        return false;
    }

    if (!ui_router_copy_settings(view, &current_settings)) {
        return false;
    }

    next_settings = current_settings;
    snprintf(next_settings.region_code, sizeof(next_settings.region_code), "%c", s_region_options[region_index].code);

    if (strcmp(next_settings.region_code, current_settings.region_code) == 0) {
        return true;
    }

    if (app_settings_save(&next_settings) != ESP_OK) {
        return false;
    }

    app_state_set_settings(view->state, &next_settings);
    if (view->region_label != NULL) {
        lv_label_set_text_fmt(view->region_label, "%s  %s", next_settings.region_code, get_region_name(next_settings.region_code));
    }
    sync_controller_request_refresh();
    return true;
}

static void sort_i32_values(int32_t *values, size_t count)
{
    if (values == NULL) {
        return;
    }

    for (size_t index = 1; index < count; index++) {
        int32_t value = values[index];
        size_t cursor = index;

        while (cursor > 0 && values[cursor - 1] > value) {
            values[cursor] = values[cursor - 1];
            cursor--;
        }

        values[cursor] = value;
    }
}

static bool solve_linear_system_3x3(double matrix[3][4], double solution[3])
{
    if (matrix == NULL || solution == NULL) {
        return false;
    }

    for (size_t pivot = 0; pivot < 3; pivot++) {
        size_t best_row = pivot;
        double best_abs = matrix[pivot][pivot] >= 0.0 ? matrix[pivot][pivot] : -matrix[pivot][pivot];

        for (size_t candidate = pivot + 1; candidate < 3; candidate++) {
            double candidate_abs = matrix[candidate][pivot] >= 0.0 ? matrix[candidate][pivot] : -matrix[candidate][pivot];

            if (candidate_abs > best_abs) {
                best_abs = candidate_abs;
                best_row = candidate;
            }
        }

        if (best_abs < 1e-6) {
            return false;
        }

        if (best_row != pivot) {
            for (size_t column = pivot; column < 4; column++) {
                double tmp = matrix[pivot][column];
                matrix[pivot][column] = matrix[best_row][column];
                matrix[best_row][column] = tmp;
            }
        }

        {
            double pivot_value = matrix[pivot][pivot];

            for (size_t column = pivot; column < 4; column++) {
                matrix[pivot][column] /= pivot_value;
            }
        }

        for (size_t row = 0; row < 3; row++) {
            if (row == pivot) {
                continue;
            }

            double factor = matrix[row][pivot];

            if (factor == 0.0) {
                continue;
            }

            for (size_t column = pivot; column < 4; column++) {
                matrix[row][column] -= factor * matrix[pivot][column];
            }
        }
    }

    solution[0] = matrix[0][3];
    solution[1] = matrix[1][3];
    solution[2] = matrix[2][3];
    return true;
}

static bool fit_touch_calibration_axis(
    const lv_point_t samples[TOUCH_CALIBRATION_POINT_COUNT],
    const int32_t targets[TOUCH_CALIBRATION_POINT_COUNT],
    double coefficients[3]
)
{
    double sx = 0.0;
    double sy = 0.0;
    double sxx = 0.0;
    double syy = 0.0;
    double sxy = 0.0;
    double st = 0.0;
    double sxt = 0.0;
    double syt = 0.0;
    double matrix[3][4] = {{0}};

    if (samples == NULL || targets == NULL || coefficients == NULL) {
        return false;
    }

    for (size_t index = 0; index < TOUCH_CALIBRATION_POINT_COUNT; index++) {
        double sample_x = samples[index].x;
        double sample_y = samples[index].y;
        double target = targets[index];

        sx += sample_x;
        sy += sample_y;
        sxx += sample_x * sample_x;
        syy += sample_y * sample_y;
        sxy += sample_x * sample_y;
        st += target;
        sxt += sample_x * target;
        syt += sample_y * target;
    }

    matrix[0][0] = sxx;
    matrix[0][1] = sxy;
    matrix[0][2] = sx;
    matrix[0][3] = sxt;
    matrix[1][0] = sxy;
    matrix[1][1] = syy;
    matrix[1][2] = sy;
    matrix[1][3] = syt;
    matrix[2][0] = sx;
    matrix[2][1] = sy;
    matrix[2][2] = TOUCH_CALIBRATION_POINT_COUNT;
    matrix[2][3] = st;
    return solve_linear_system_3x3(matrix, coefficients);
}

static void reset_touch_calibration_press_samples(ui_router_view_t *view)
{
    if (view == NULL) {
        return;
    }

    view->touch_calibration_press_sample_count = 0;
    memset(view->touch_calibration_press_samples, 0, sizeof(view->touch_calibration_press_samples));
}

static void capture_touch_calibration_press_sample(ui_router_view_t *view)
{
    int32_t raw_x = 0;
    int32_t raw_y = 0;

    if (view == NULL || view->touch_calibration_press_sample_count >= TOUCH_CALIBRATION_PRESS_SAMPLE_CAPACITY) {
        return;
    }

    if (!touch_get_latest_raw_point(&raw_x, &raw_y)) {
        return;
    }

    view->touch_calibration_press_samples[view->touch_calibration_press_sample_count].x = raw_x;
    view->touch_calibration_press_samples[view->touch_calibration_press_sample_count].y = raw_y;
    view->touch_calibration_press_sample_count++;
}

static bool finalize_touch_calibration_press_sample(ui_router_view_t *view, lv_point_t *sample)
{
    int32_t x_values[TOUCH_CALIBRATION_PRESS_SAMPLE_CAPACITY] = {0};
    int32_t y_values[TOUCH_CALIBRATION_PRESS_SAMPLE_CAPACITY] = {0};
    uint8_t count = 0;

    if (view == NULL || sample == NULL) {
        return false;
    }

    count = view->touch_calibration_press_sample_count;
    if (count < TOUCH_CALIBRATION_MIN_PRESS_SAMPLES) {
        return false;
    }

    for (uint8_t index = 0; index < count; index++) {
        x_values[index] = view->touch_calibration_press_samples[index].x;
        y_values[index] = view->touch_calibration_press_samples[index].y;
    }

    sort_i32_values(x_values, count);
    sort_i32_values(y_values, count);
    sample->x = x_values[count / 2];
    sample->y = y_values[count / 2];
    return true;
}

static void set_touch_calibration_target_active(ui_router_view_t *view, bool active)
{
    if (view == NULL || view->touch_calibration_target == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(
        view->touch_calibration_target,
        lv_color_hex(active ? TOUCH_CALIBRATION_TARGET_ACTIVE_COLOR : TOUCH_CALIBRATION_TARGET_IDLE_COLOR),
        0
    );
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
            point.x = overlay_coords.x1 + (width / 2);
            point.y = overlay_coords.y1 + (height / 2);
            break;
        case 3:
            point.x = overlay_coords.x1 + 36;
            point.y = overlay_coords.y1 + height - 56;
            break;
        case 4:
        default:
            point.x = overlay_coords.x1 + width - 36;
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
    int32_t x_targets[TOUCH_CALIBRATION_POINT_COUNT] = {0};
    int32_t y_targets[TOUCH_CALIBRATION_POINT_COUNT] = {0};
    double x_coefficients[3] = {0};
    double y_coefficients[3] = {0};
    int32_t total_error = 0;
    int32_t max_component_error = 0;

    if (samples == NULL || calibration == NULL) {
        return false;
    }

    for (uint8_t index = 0; index < TOUCH_CALIBRATION_POINT_COUNT; index++) {
        targets[index] = convert_display_point_to_touch_space(get_touch_calibration_target_point(view, index));
        x_targets[index] = targets[index].x;
        y_targets[index] = targets[index].y;
    }

    if (!fit_touch_calibration_axis(samples, x_targets, x_coefficients) ||
        !fit_touch_calibration_axis(samples, y_targets, y_coefficients)) {
        return false;
    }

    calibration->xx = (int32_t)(x_coefficients[0] * APP_TOUCH_CALIBRATION_SCALE);
    calibration->xy = (int32_t)(x_coefficients[1] * APP_TOUCH_CALIBRATION_SCALE);
    calibration->x_offset = (int32_t)(x_coefficients[2] * APP_TOUCH_CALIBRATION_SCALE);
    calibration->yx = (int32_t)(y_coefficients[0] * APP_TOUCH_CALIBRATION_SCALE);
    calibration->yy = (int32_t)(y_coefficients[1] * APP_TOUCH_CALIBRATION_SCALE);
    calibration->y_offset = (int32_t)(y_coefficients[2] * APP_TOUCH_CALIBRATION_SCALE);

    for (uint8_t index = 0; index < TOUCH_CALIBRATION_POINT_COUNT; index++) {
        int32_t predicted_x = (int32_t)(
            ((int64_t)calibration->xx * samples[index].x +
             (int64_t)calibration->xy * samples[index].y +
             (int64_t)calibration->x_offset) /
            APP_TOUCH_CALIBRATION_SCALE
        );
        int32_t predicted_y = (int32_t)(
            ((int64_t)calibration->yx * samples[index].x +
             (int64_t)calibration->yy * samples[index].y +
             (int64_t)calibration->y_offset) /
            APP_TOUCH_CALIBRATION_SCALE
        );
        int32_t x_error = predicted_x - targets[index].x;
        int32_t y_error = predicted_y - targets[index].y;

        if (x_error < 0) {
            x_error = -x_error;
        }

        if (y_error < 0) {
            y_error = -y_error;
        }

        if (x_error > max_component_error) {
            max_component_error = x_error;
        }

        if (y_error > max_component_error) {
            max_component_error = y_error;
        }

        total_error += x_error + y_error;
    }

    if (max_component_error > TOUCH_CALIBRATION_MAX_COMPONENT_ERROR_PX ||
        total_error > (TOUCH_CALIBRATION_POINT_COUNT * TOUCH_CALIBRATION_MAX_AVERAGE_ERROR_PX)) {
        return false;
    }

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
    set_touch_calibration_target_active(view, false);
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

static void region_next_event_cb(lv_event_t *event)
{
    uint16_t selected_index = 0;
    ui_router_view_t *view = (ui_router_view_t *)lv_event_get_user_data(event);

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (view == NULL || view->region_dropdown == NULL) {
        return;
    }

    selected_index = lv_dropdown_get_selected(view->region_dropdown);
    if (selected_index == 0) {
        return;
    }

    if (!apply_region_index(view, selected_index - 1)) {
        return;
    }

    if (view->wifi_card != NULL) {
        lv_obj_scroll_to_view(view->wifi_card, LV_ANIM_ON);
    }
}

static void region_continue_event_cb(lv_event_t *event)
{
    ui_router_view_t *view = (ui_router_view_t *)lv_event_get_user_data(event);

    if (lv_event_get_code(event) != LV_EVENT_CLICKED || view == NULL) {
        return;
    }

    region_next_event_cb(event);
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

static void maybe_request_onboarding_wifi_scan(const app_state_t *state, ui_router_view_t *view, bool wifi_step_visible)
{
    if (view == NULL || state == NULL) {
        return;
    }

    if (!wifi_step_visible || state->startup_stage != APP_STARTUP_STAGE_ONBOARDING) {
        view->onboarding_wifi_autoscan_requested = false;
        return;
    }

    if (state->wifi_status == APP_WIFI_STATUS_SCANNING || state->wifi_scan_result_count > 0) {
        view->onboarding_wifi_autoscan_requested = true;
        return;
    }

    if (!view->onboarding_wifi_autoscan_requested) {
        (void)wifi_manager_request_scan();
        view->onboarding_wifi_autoscan_requested = true;
    }
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
    view->touch_calibration_step = 0;
    memset(view->touch_calibration_samples, 0, sizeof(view->touch_calibration_samples));
    reset_touch_calibration_press_samples(view);
    lv_obj_clear_flag(view->touch_calibration_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(view->touch_calibration_overlay);
    update_touch_calibration_overlay(view);
}

static void touch_calibration_overlay_event_cb(lv_event_t *event)
{
    app_settings_t settings = {0};
    lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point = {0};
    app_touch_calibration_t calibration = {0};
    ui_router_view_t *view = (ui_router_view_t *)lv_event_get_user_data(event);

    if (view == NULL || view->touch_calibration_step >= TOUCH_CALIBRATION_POINT_COUNT) {
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        reset_touch_calibration_press_samples(view);
        set_touch_calibration_target_active(view, true);
        capture_touch_calibration_press_sample(view);
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        capture_touch_calibration_press_sample(view);
        return;
    }

    if (code != LV_EVENT_RELEASED) {
        return;
    }

    if (!finalize_touch_calibration_press_sample(view, &point)) {
        reset_touch_calibration_press_samples(view);
        set_touch_calibration_target_active(view, false);
        app_state_set_wifi_status(view->state, APP_WIFI_STATUS_FAILED, "Hold the target steady, then try again");
        update_touch_calibration_overlay(view);
        return;
    }

    view->touch_calibration_samples[view->touch_calibration_step] = point;
    view->touch_calibration_step++;
    reset_touch_calibration_press_samples(view);
    set_touch_calibration_target_active(view, false);

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
    const char *board_display_name = get_board_display_name();
    char clock_text[12] = {0};
    bool onboarding_active = state->startup_stage == APP_STARTUP_STAGE_ONBOARDING;
    bool region_configured = app_settings_region_is_configured(&state->settings);
    bool wifi_step_visible = onboarding_active && region_configured;

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
        lv_label_set_text(view->settings_title_label, onboarding_active ? "Setup" : "Settings");
    }

    if (view->settings_top_bar != NULL) {
        if (onboarding_active) {
            lv_obj_add_flag(view->settings_top_bar, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(view->settings_top_bar, LV_OBJ_FLAG_HIDDEN);
        }
    }

    ui_router_update_wifi_status(
        view->settings_wifi_label,
        view->settings_wifi_strike_label,
        state->wifi_status,
        lv_color_white(),
        lv_color_hex(0xdc2626)
    );

    update_region_dropdown(state, view);
    update_touch_calibration_status(state, view);
    update_wifi_dropdown(state, view);
    maybe_request_onboarding_wifi_scan(state, view, wifi_step_visible);

    if (view->onboarding_card != NULL) {
        lv_obj_add_flag(view->onboarding_card, LV_OBJ_FLAG_HIDDEN);
    }

    if (view->region_continue_button != NULL) {
        if (onboarding_active && !region_configured) {
            lv_obj_clear_flag(view->region_continue_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(view->region_continue_button, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (view->region_card != NULL) {
        if (onboarding_active && region_configured) {
            lv_obj_add_flag(view->region_card, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(view->region_card, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (view->onboarding_retry_button != NULL) {
        lv_obj_add_flag(view->onboarding_retry_button, LV_OBJ_FLAG_HIDDEN);
    }

    if (view->wifi_card != NULL) {
        if (onboarding_active && !region_configured) {
            lv_obj_add_flag(view->wifi_card, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(view->wifi_card, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (view->brightness_card != NULL) {
        if (onboarding_active) {
            lv_obj_add_flag(view->brightness_card, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(view->brightness_card, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (view->touch_card != NULL) {
        if (onboarding_active) {
            lv_obj_add_flag(view->touch_card, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(view->touch_card, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (view->time_card != NULL) {
        if (onboarding_active) {
            lv_obj_add_flag(view->time_card, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(view->time_card, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (view->firmware_card != NULL) {
        if (onboarding_active) {
            lv_obj_add_flag(view->firmware_card, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(view->firmware_card, LV_OBJ_FLAG_HIDDEN);
        }
    }

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
            "Board: %s\nInstalled: %s",
            board_display_name,
            state->firmware_current_version[0] != '\0' ? state->firmware_current_version : "dev"
        );
    }

    if (view->firmware_available_label != NULL) {
        if (state->firmware_available_version[0] != '\0') {
            lv_label_set_text_fmt(view->firmware_available_label, "Compatible release: %s", state->firmware_available_version);
        } else if (state->firmware_update_status == APP_FIRMWARE_UPDATE_STATUS_CHECKING) {
            lv_label_set_text(view->firmware_available_label, "Compatible release: checking GitHub Releases");
        } else if (state->firmware_update_status == APP_FIRMWARE_UPDATE_STATUS_IDLE) {
            lv_label_set_text(view->firmware_available_label, "Compatible release: not checked yet");
        } else {
            lv_label_set_text(view->firmware_available_label, "Compatible release: unavailable");
        }
    }

    if (view->firmware_status_label != NULL) {
        lv_label_set_text_fmt(
            view->firmware_status_label,
            "Status: %s",
            state->firmware_status_text[0] != '\0' ? state->firmware_status_text : app_state_get_firmware_update_status_name(state->firmware_update_status)
        );
    }

    if (view->firmware_update_progress_bar != NULL) {
        bool should_show_progress = state->firmware_update_status == APP_FIRMWARE_UPDATE_STATUS_DOWNLOADING ||
                                    state->firmware_update_status == APP_FIRMWARE_UPDATE_STATUS_APPLYING ||
                                    state->firmware_update_status == APP_FIRMWARE_UPDATE_STATUS_REBOOTING;

        if (should_show_progress) {
            lv_obj_clear_flag(view->firmware_update_progress_bar, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(view->firmware_update_progress_bar, state->firmware_update_progress_percent, LV_ANIM_OFF);
        } else {
            lv_obj_add_flag(view->firmware_update_progress_bar, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (view->firmware_update_button != NULL) {
        bool should_show_button = state->firmware_update_status == APP_FIRMWARE_UPDATE_STATUS_AVAILABLE;

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

    view->onboarding_card = ui_router_create_section_card(view->settings_content, lv_color_hex(0x0b1220));
    lv_obj_add_flag(view->onboarding_card, LV_OBJ_FLAG_HIDDEN);

    view->brightness_card = ui_router_create_section_card(view->settings_content, lv_color_hex(0x111827));
    lv_obj_set_style_radius(view->brightness_card, 16, 0);
    lv_obj_set_style_border_width(view->brightness_card, 1, 0);
    lv_obj_set_style_border_color(view->brightness_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_pad_all(view->brightness_card, 14, 0);
    lv_obj_set_style_pad_row(view->brightness_card, 10, 0);

    lv_obj_t *brightness_title = lv_label_create(view->brightness_card);
    lv_label_set_text(brightness_title, "Brightness");
    lv_obj_set_style_text_color(brightness_title, lv_color_white(), 0);

    view->brightness_label = lv_label_create(view->brightness_card);
    lv_obj_set_style_text_color(view->brightness_label, lv_color_hex(0xfbbf24), 0);
    lv_obj_set_style_text_font(view->brightness_label, &lv_font_montserrat_20_numeric, 0);

    lv_obj_t *brightness_row = lv_obj_create(view->brightness_card);
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

    view->region_card = ui_router_create_section_card(view->settings_content, lv_color_hex(0x111827));
    lv_obj_set_width(view->region_card, lv_pct(100));
    lv_obj_set_style_radius(view->region_card, 16, 0);
    lv_obj_set_style_border_width(view->region_card, 1, 0);
    lv_obj_set_style_border_color(view->region_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_bg_color(view->region_card, lv_color_hex(0x020617), 0);
    lv_obj_set_style_pad_all(view->region_card, 18, 0);
    lv_obj_set_style_pad_row(view->region_card, 10, 0);

    lv_obj_t *region_title = lv_label_create(view->region_card);
    lv_label_set_text(region_title, "Select your electricity region");
    lv_obj_set_style_text_color(region_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(region_title, &lv_font_montserrat_14, 0);

    view->region_dropdown = lv_dropdown_create(view->region_card);
    lv_obj_set_width(view->region_dropdown, lv_pct(100));
    lv_dropdown_set_options(view->region_dropdown, s_region_dropdown_options);
    lv_dropdown_set_selected(view->region_dropdown, 0);
    lv_obj_set_style_bg_color(view->region_dropdown, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_text_color(view->region_dropdown, lv_color_white(), 0);
    lv_obj_set_style_border_color(view->region_dropdown, lv_color_hex(0x334155), 0);

    view->region_continue_button = ui_router_create_dark_button(view->region_card, "Continue to Wi-Fi");
    lv_obj_set_width(view->region_continue_button, lv_pct(100));
    lv_obj_set_style_bg_color(view->region_continue_button, lv_color_hex(0x047857), 0);
    lv_obj_add_event_cb(view->region_continue_button, region_continue_event_cb, LV_EVENT_CLICKED, view);
    lv_obj_add_flag(view->region_continue_button, LV_OBJ_FLAG_HIDDEN);

    view->wifi_card = ui_router_create_section_card(view->settings_content, lv_color_hex(0x111827));
    lv_obj_set_style_radius(view->wifi_card, 16, 0);
    lv_obj_set_style_border_width(view->wifi_card, 1, 0);
    lv_obj_set_style_border_color(view->wifi_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_pad_all(view->wifi_card, 14, 0);
    lv_obj_set_style_pad_row(view->wifi_card, 10, 0);

    lv_obj_t *wifi_title = lv_label_create(view->wifi_card);
    lv_label_set_text(wifi_title, "Wi-Fi");
    lv_obj_set_style_text_color(wifi_title, lv_color_white(), 0);

    view->wifi_status_label = lv_label_create(view->wifi_card);
    lv_obj_set_width(view->wifi_status_label, lv_pct(100));
    lv_label_set_long_mode(view->wifi_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(view->wifi_status_label, lv_color_hex(0xe5e7eb), 0);

    view->wifi_scan_summary_label = lv_label_create(view->wifi_card);
    lv_obj_set_style_text_color(view->wifi_scan_summary_label, lv_color_hex(0x9ca3af), 0);

    view->wifi_dropdown = lv_dropdown_create(view->wifi_card);
    lv_obj_set_width(view->wifi_dropdown, lv_pct(100));
    lv_dropdown_set_options(view->wifi_dropdown, "Scan for Wi-Fi");
    lv_obj_set_style_bg_color(view->wifi_dropdown, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_text_color(view->wifi_dropdown, lv_color_white(), 0);
    lv_obj_set_style_border_color(view->wifi_dropdown, lv_color_hex(0x334155), 0);
    lv_obj_add_event_cb(view->wifi_dropdown, wifi_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, view);

    view->wifi_psk_textarea = lv_textarea_create(view->wifi_card);
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

    lv_obj_t *button_row = lv_obj_create(view->wifi_card);
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

    view->touch_card = ui_router_create_section_card(view->settings_content, lv_color_hex(0x111827));
    lv_obj_set_style_radius(view->touch_card, 16, 0);
    lv_obj_set_style_border_width(view->touch_card, 1, 0);
    lv_obj_set_style_border_color(view->touch_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_pad_all(view->touch_card, 14, 0);
    lv_obj_set_style_pad_row(view->touch_card, 10, 0);

    lv_obj_t *touch_title = lv_label_create(view->touch_card);
    lv_label_set_text(touch_title, "Touch Calibration");
    lv_obj_set_style_text_color(touch_title, lv_color_white(), 0);

    view->touch_calibration_label = lv_label_create(view->touch_card);
    lv_obj_set_style_text_color(view->touch_calibration_label, lv_color_hex(0x9ca3af), 0);
    lv_obj_set_width(view->touch_calibration_label, lv_pct(100));
    lv_label_set_long_mode(view->touch_calibration_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *calibrate_button = ui_router_create_dark_button(view->touch_card, "Calibrate touch");
    lv_obj_set_width(calibrate_button, lv_pct(100));
    lv_obj_set_style_bg_color(calibrate_button, lv_color_hex(0x1e293b), 0);
    lv_obj_add_event_cb(calibrate_button, touch_calibration_start_event_cb, LV_EVENT_CLICKED, view);

    view->time_card = ui_router_create_section_card(view->settings_content, lv_color_hex(0x111827));
    lv_obj_set_style_radius(view->time_card, 16, 0);
    lv_obj_set_style_border_width(view->time_card, 1, 0);
    lv_obj_set_style_border_color(view->time_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_pad_all(view->time_card, 14, 0);
    lv_obj_set_style_pad_row(view->time_card, 10, 0);

    lv_obj_t *time_title = lv_label_create(view->time_card);
    lv_label_set_text(time_title, "Time Sync");
    lv_obj_set_style_text_color(time_title, lv_color_white(), 0);

    view->time_status_label = lv_label_create(view->time_card);
    lv_obj_set_width(view->time_status_label, lv_pct(100));
    lv_label_set_long_mode(view->time_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(view->time_status_label, lv_color_hex(0xe5e7eb), 0);

    view->local_time_label = lv_label_create(view->time_card);
    lv_obj_set_style_text_color(view->local_time_label, lv_color_hex(0x9ca3af), 0);

    view->firmware_card = ui_router_create_section_card(view->settings_content, lv_color_hex(0x111827));
    lv_obj_set_style_radius(view->firmware_card, 16, 0);
    lv_obj_set_style_border_width(view->firmware_card, 1, 0);
    lv_obj_set_style_border_color(view->firmware_card, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_pad_all(view->firmware_card, 14, 0);
    lv_obj_set_style_pad_row(view->firmware_card, 10, 0);

    lv_obj_t *firmware_title = lv_label_create(view->firmware_card);
    lv_label_set_text(firmware_title, "Firmware Updates");
    lv_obj_set_style_text_color(firmware_title, lv_color_white(), 0);

    view->firmware_version_label = lv_label_create(view->firmware_card);
    lv_obj_set_width(view->firmware_version_label, lv_pct(100));
    lv_label_set_long_mode(view->firmware_version_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(view->firmware_version_label, lv_color_hex(0xe5e7eb), 0);

    view->firmware_available_label = lv_label_create(view->firmware_card);
    lv_obj_set_width(view->firmware_available_label, lv_pct(100));
    lv_label_set_long_mode(view->firmware_available_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(view->firmware_available_label, lv_color_hex(0x93c5fd), 0);

    view->firmware_status_label = lv_label_create(view->firmware_card);
    lv_obj_set_width(view->firmware_status_label, lv_pct(100));
    lv_label_set_long_mode(view->firmware_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(view->firmware_status_label, lv_color_hex(0x9ca3af), 0);

    view->firmware_update_button = ui_router_create_dark_button(view->firmware_card, "Update Firmware");
    lv_obj_set_width(view->firmware_update_button, lv_pct(100));
    lv_obj_set_style_bg_color(view->firmware_update_button, lv_color_hex(0x1d4ed8), 0);
    lv_obj_add_event_cb(view->firmware_update_button, firmware_update_button_event_cb, LV_EVENT_CLICKED, view);
    lv_obj_add_flag(view->firmware_update_button, LV_OBJ_FLAG_HIDDEN);
    view->firmware_update_button_label = lv_obj_get_child(view->firmware_update_button, 0);

    view->firmware_update_progress_bar = lv_bar_create(view->firmware_card);
    lv_obj_set_width(view->firmware_update_progress_bar, lv_pct(100));
    lv_obj_set_height(view->firmware_update_progress_bar, 12);
    lv_bar_set_range(view->firmware_update_progress_bar, 0, 100);
    lv_obj_set_style_bg_color(view->firmware_update_progress_bar, lv_color_hex(0x374151), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(view->firmware_update_progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(view->firmware_update_progress_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(view->firmware_update_progress_bar, lv_color_hex(0x2563eb), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(view->firmware_update_progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(view->firmware_update_progress_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_add_flag(view->firmware_update_progress_bar, LV_OBJ_FLAG_HIDDEN);

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
    lv_obj_add_event_cb(view->touch_calibration_overlay, touch_calibration_overlay_event_cb, LV_EVENT_PRESSED, view);
    lv_obj_add_event_cb(view->touch_calibration_overlay, touch_calibration_overlay_event_cb, LV_EVENT_PRESSING, view);
    lv_obj_add_event_cb(view->touch_calibration_overlay, touch_calibration_overlay_event_cb, LV_EVENT_RELEASED, view);
    lv_obj_add_flag(view->touch_calibration_overlay, LV_OBJ_FLAG_HIDDEN);

    view->touch_calibration_prompt = lv_label_create(view->touch_calibration_overlay);
    lv_obj_set_width(view->touch_calibration_prompt, lv_pct(100));
    lv_obj_set_style_text_align(view->touch_calibration_prompt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(view->touch_calibration_prompt, lv_color_white(), 0);
    lv_obj_align(view->touch_calibration_prompt, LV_ALIGN_TOP_MID, 0, 12);

    view->touch_calibration_target = lv_obj_create(view->touch_calibration_overlay);
    lv_obj_set_size(view->touch_calibration_target, TOUCH_CALIBRATION_TARGET_SIZE, TOUCH_CALIBRATION_TARGET_SIZE);
    lv_obj_set_style_radius(view->touch_calibration_target, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(view->touch_calibration_target, lv_color_hex(TOUCH_CALIBRATION_TARGET_IDLE_COLOR), 0);
    lv_obj_set_style_bg_opa(view->touch_calibration_target, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view->touch_calibration_target, 3, 0);
    lv_obj_set_style_border_color(view->touch_calibration_target, lv_color_white(), 0);
    lv_obj_remove_flag(view->touch_calibration_target, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    view->touch_calibration_target_crosshair_h = lv_obj_create(view->touch_calibration_target);
    lv_obj_set_size(
        view->touch_calibration_target_crosshair_h,
        TOUCH_CALIBRATION_CROSSHAIR_LENGTH,
        TOUCH_CALIBRATION_CROSSHAIR_THICKNESS
    );
    lv_obj_set_style_radius(view->touch_calibration_target_crosshair_h, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(view->touch_calibration_target_crosshair_h, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(view->touch_calibration_target_crosshair_h, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view->touch_calibration_target_crosshair_h, 0, 0);
    lv_obj_remove_flag(view->touch_calibration_target_crosshair_h, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(view->touch_calibration_target_crosshair_h);

    view->touch_calibration_target_crosshair_v = lv_obj_create(view->touch_calibration_target);
    lv_obj_set_size(
        view->touch_calibration_target_crosshair_v,
        TOUCH_CALIBRATION_CROSSHAIR_THICKNESS,
        TOUCH_CALIBRATION_CROSSHAIR_LENGTH
    );
    lv_obj_set_style_radius(view->touch_calibration_target_crosshair_v, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(view->touch_calibration_target_crosshair_v, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(view->touch_calibration_target_crosshair_v, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view->touch_calibration_target_crosshair_v, 0, 0);
    lv_obj_remove_flag(view->touch_calibration_target_crosshair_v, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(view->touch_calibration_target_crosshair_v);

    view->touch_calibration_target_center_dot = lv_obj_create(view->touch_calibration_target);
    lv_obj_set_size(
        view->touch_calibration_target_center_dot,
        TOUCH_CALIBRATION_CENTER_DOT_SIZE,
        TOUCH_CALIBRATION_CENTER_DOT_SIZE
    );
    lv_obj_set_style_radius(view->touch_calibration_target_center_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(view->touch_calibration_target_center_dot, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(view->touch_calibration_target_center_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view->touch_calibration_target_center_dot, 1, 0);
    lv_obj_set_style_border_color(view->touch_calibration_target_center_dot, lv_color_white(), 0);
    lv_obj_remove_flag(view->touch_calibration_target_center_dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(view->touch_calibration_target_center_dot);
}